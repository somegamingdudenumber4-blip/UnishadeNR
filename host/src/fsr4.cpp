#include "fsr4.h"
#include "config.h"
#include "log.h"
#include <windows.h>
#include <d3d11_4.h>
#include <d3d12.h>
#include <dxgi1_2.h>
#include <wrl/client.h>
#include <ffx_api.h>
#include <ffx_upscale.h>
#include <dx12/ffx_api_dx12.h>
#include "aeon_sr/motion/optical_flow.hpp"
#include <chrono>
#include <filesystem>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

using Microsoft::WRL::ComPtr;
namespace {
void Hr(HRESULT h, const char* step) {
    if (FAILED(h)) { char s[160]; sprintf_s(s, "%s failed: 0x%08X", step, unsigned(h)); throw std::runtime_error(s); }
}
void Ffx(ffxReturnCode_t r, const char* step) {
    if (r != FFX_API_RETURN_OK) throw std::runtime_error(std::string(step)+" failed: "+std::to_string(r));
}
void Barrier(ID3D12GraphicsCommandList* list, ID3D12Resource* res,
             D3D12_RESOURCE_STATES from, D3D12_RESOURCE_STATES to) {
    if (from == to) return;
    D3D12_RESOURCE_BARRIER b{}; b.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    b.Transition={res,D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,from,to}; list->ResourceBarrier(1,&b);
}
struct Processor {
    ID3D11Device* owner=nullptr;
    ComPtr<ID3D12Device> device;
    ComPtr<ID3D12CommandQueue> queue;
    ComPtr<ID3D12CommandAllocator> allocator;
    ComPtr<ID3D12GraphicsCommandList> list;
    ComPtr<ID3D12Fence> fence;
    ComPtr<ID3D11Fence> outputFence11,inputFence11;
    ComPtr<ID3D12Fence> inputFence12;
    ComPtr<ID3D11DeviceContext4> context4;
    ComPtr<ID3D12QueryHeap> timing;
    ComPtr<ID3D12Resource> timingReadback;
    UINT64 timestampFrequency=0;
    HANDLE eventHandle=nullptr;
    UINT64 serial=0;
    HMODULE graphics=nullptr, runtime=nullptr;
    PfnFfxCreateContext create=nullptr;
    PfnFfxDestroyContext destroy=nullptr;
    PfnFfxQuery query=nullptr;
    PfnFfxDispatch dispatch=nullptr;
    ffxContext model=nullptr;
    std::string provider;
    UINT iw=0,ih=0,ow=0,oh=0;
    ComPtr<ID3D11Texture2D> input11,output11;
    ComPtr<ID3D12Resource> input12,output12;
    aeon_sr::OpticalFlowD3D12 flow;
    bool first=true;
    UINT64 frames=0;
    std::chrono::steady_clock::time_point last=std::chrono::steady_clock::now(),stats=last;
    double totalMs=0;
    UINT statsFrames=0;
    ~Processor() {
        if (fence && serial && fence->GetCompletedValue()<serial && eventHandle) {
            if (SUCCEEDED(fence->SetEventOnCompletion(serial,eventHandle))) WaitForSingleObject(eventHandle,2000);
        }
        if (model && destroy) destroy(&model,nullptr);
        if (eventHandle) CloseHandle(eventHandle);
        // Keep vendor/graphics modules mapped until process exit; flow releases its PSOs afterwards.
    }
    void Initialize(ID3D11Device* native) {
        owner=native;
        const std::filesystem::path dir=std::filesystem::path(ExeDirectory())/L"fsr4-runtime";
        std::filesystem::create_directories(dir/L"D3D12");
        wchar_t sys[MAX_PATH]; GetSystemDirectoryW(sys,MAX_PATH);
        const auto copy=[&](const wchar_t* name,const std::filesystem::path& to) {
            const auto from=std::filesystem::path(sys)/name;
            if (!std::filesystem::exists(to) || std::filesystem::file_size(from)!=std::filesystem::file_size(to))
                std::filesystem::copy_file(from,to,std::filesystem::copy_options::overwrite_existing);
        };
        copy(L"d3d12.dll",dir/L"fsr12p.dll");
        copy(L"D3D12Core.dll",dir/L"D3D12Core.dll");
        copy(L"D3D12Core.dll",dir/L"D3D12"/L"D3D12Core.dll");
        graphics=LoadLibraryExW((dir/L"fsr12p.dll").c_str(),nullptr,LOAD_WITH_ALTERED_SEARCH_PATH);
        if (!graphics) throw std::runtime_error("Private D3D12 could not load");
        auto make=reinterpret_cast<decltype(&D3D12CreateDevice)>(GetProcAddress(graphics,"D3D12CreateDevice"));
        ComPtr<IDXGIDevice> dxgi; ComPtr<IDXGIAdapter> adapter;
        Hr(native->QueryInterface(IID_PPV_ARGS(&dxgi)),"DXGI device");
        Hr(dxgi->GetAdapter(&adapter),"adapter");
        if (!make) throw std::runtime_error("D3D12CreateDevice missing");
        Hr(make(adapter.Get(),D3D_FEATURE_LEVEL_12_0,IID_PPV_ARGS(&device)),"FSR D3D12 device");
        DXGI_ADAPTER_DESC ad{}; adapter->GetDesc(&ad);
        if (ad.VendorId!=0x1002) throw std::runtime_error("FSR 4 output requires an AMD adapter");
        D3D12_COMMAND_QUEUE_DESC q{}; q.Type=D3D12_COMMAND_LIST_TYPE_DIRECT;
        Hr(device->CreateCommandQueue(&q,IID_PPV_ARGS(&queue)),"compute queue");
        Hr(device->CreateCommandAllocator(q.Type,IID_PPV_ARGS(&allocator)),"allocator");
        Hr(device->CreateCommandList(0,q.Type,allocator.Get(),nullptr,IID_PPV_ARGS(&list)),"command list");
        Hr(list->Close(),"close initial list");
        Hr(device->CreateFence(0,D3D12_FENCE_FLAG_SHARED,IID_PPV_ARGS(&fence)),"completion fence");
        eventHandle=CreateEventW(nullptr,FALSE,FALSE,nullptr);
        if (!eventHandle) throw std::runtime_error("FSR completion event failed");
        ComPtr<ID3D11Device5> device5;ComPtr<ID3D11DeviceContext> context;
        Hr(native->QueryInterface(IID_PPV_ARGS(&device5)),"D3D11 shared fence device");
        native->GetImmediateContext(&context);Hr(context.As(&context4),"D3D11 GPU fence context");
        HANDLE shared=nullptr;
        Hr(device->CreateSharedHandle(fence.Get(),nullptr,GENERIC_ALL,nullptr,&shared),"output fence handle");
        HRESULT opened=device5->OpenSharedFence(shared,IID_PPV_ARGS(&outputFence11));CloseHandle(shared);
        Hr(opened,"open output GPU fence");
        Hr(device5->CreateFence(0,D3D11_FENCE_FLAG_SHARED,IID_PPV_ARGS(&inputFence11)),"input GPU fence");
        Hr(inputFence11->CreateSharedHandle(nullptr,GENERIC_ALL,nullptr,&shared),"input fence handle");
        opened=device->OpenSharedHandle(shared,IID_PPV_ARGS(&inputFence12));CloseHandle(shared);
        Hr(opened,"open input GPU fence");
        D3D12_QUERY_HEAP_DESC td{};td.Type=D3D12_QUERY_HEAP_TYPE_TIMESTAMP;td.Count=2;
        Hr(device->CreateQueryHeap(&td,IID_PPV_ARGS(&timing)),"GPU timer");
        Hr(queue->GetTimestampFrequency(&timestampFrequency),"GPU timestamp frequency");
        D3D12_HEAP_PROPERTIES hp{};hp.Type=D3D12_HEAP_TYPE_READBACK;
        D3D12_RESOURCE_DESC rd{};rd.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;rd.Width=16;
        rd.Height=rd.DepthOrArraySize=rd.MipLevels=1;rd.SampleDesc.Count=1;rd.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        Hr(device->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&rd,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,
            IID_PPV_ARGS(&timingReadback)),"GPU timing readback");
        const auto path=std::filesystem::path(ExeDirectory())/L"runtime"/L"amd_fidelityfx_upscaler_dx12.dll";
        runtime=LoadLibraryExW(path.c_str(),nullptr,LOAD_WITH_ALTERED_SEARCH_PATH);
        if (!runtime) throw std::runtime_error("AMD FSR upscaler runtime could not load");
        create=reinterpret_cast<PfnFfxCreateContext>(GetProcAddress(runtime,"ffxCreateContext"));
        destroy=reinterpret_cast<PfnFfxDestroyContext>(GetProcAddress(runtime,"ffxDestroyContext"));
        query=reinterpret_cast<PfnFfxQuery>(GetProcAddress(runtime,"ffxQuery"));
        dispatch=reinterpret_cast<PfnFfxDispatch>(GetProcAddress(runtime,"ffxDispatch"));
        if (!create || !destroy || !query || !dispatch) throw std::runtime_error("FSR API exports missing");
        Log(LogLevel::Info,L"FSR 4 output: native compute device on %ls; GPU shared-fence handoff; color remains on GPU.",ad.Description);
    }
    void Shared(ID3D11Device* native,UINT w,UINT h,DXGI_FORMAT format,UINT bind,
                ComPtr<ID3D11Texture2D>& a,ComPtr<ID3D12Resource>& b) {
        D3D11_TEXTURE2D_DESC d{}; d.Width=w; d.Height=h; d.MipLevels=d.ArraySize=1;
        d.Format=format; d.SampleDesc.Count=1; d.Usage=D3D11_USAGE_DEFAULT; d.BindFlags=bind;
        d.MiscFlags=D3D11_RESOURCE_MISC_SHARED|D3D11_RESOURCE_MISC_SHARED_NTHANDLE;
        Hr(native->CreateTexture2D(&d,nullptr,&a),"shared texture");
        ComPtr<IDXGIResource1> resource; Hr(a.As(&resource),"shared resource");
        HANDLE shared=nullptr;
        Hr(resource->CreateSharedHandle(nullptr,GENERIC_ALL,nullptr,&shared),"shared texture handle");
        const HRESULT result=device->OpenSharedHandle(shared,IID_PPV_ARGS(&b)); CloseHandle(shared);
        Hr(result,"open shared texture");
    }
    void Ensure(ID3D11Device* native,ID3D11Texture2D* input,UINT w,UINT h) {
        D3D11_TEXTURE2D_DESC sd{}; input->GetDesc(&sd);
        if (model && iw==sd.Width && ih==sd.Height && ow==w && oh==h) return;
        if (sd.SampleDesc.Count!=1 || sd.Format!=DXGI_FORMAT_B8G8R8A8_UNORM)
            throw std::runtime_error("FSR 4 output currently requires SDR BGRA8 capture");
        if (model) Ffx(destroy(&model,nullptr),"destroy old FSR context");
        flow.release(); input11.Reset();output11.Reset();input12.Reset();output12.Reset();
        iw=sd.Width; ih=sd.Height; ow=w;oh=h;
        Shared(native,iw,ih,sd.Format,D3D11_BIND_SHADER_RESOURCE,input11,input12);
        Shared(native,ow,oh,DXGI_FORMAT_R8G8B8A8_UNORM,
               D3D11_BIND_SHADER_RESOURCE|D3D11_BIND_UNORDERED_ACCESS,output11,output12);
        uint64_t count=0; ffxQueryDescGetVersions versions{};
        versions.header.type=FFX_API_QUERY_DESC_TYPE_GET_VERSIONS;
        versions.createDescType=FFX_API_CREATE_CONTEXT_DESC_TYPE_UPSCALE;
        versions.device=device.Get();versions.outputCount=&count;
        Ffx(query(nullptr,&versions.header),"enumerate FSR providers");
        if (count==0 || count>32) throw std::runtime_error("No FSR providers available");
        std::vector<uint64_t> ids(count);std::vector<const char*> names(count);
        versions.versionIds=ids.data();versions.versionNames=names.data();
        Ffx(query(nullptr,&versions.header),"read FSR providers");
        uint64_t id=0;provider.clear();
        for (size_t i=0;i<count;++i) {
            if (names[i]) Log(LogLevel::Info,L"FSR output provider offered: %hs",names[i]);
            if (names[i] && names[i][0]=='4' && (provider.empty() || provider<names[i])) {
                id=ids[i];provider=names[i];
            }
        }
        if (provider.empty()) throw std::runtime_error("No FSR 4 provider supports this GPU/driver");
        ffxOverrideVersion version{};version.header.type=FFX_API_DESC_TYPE_OVERRIDE_VERSION;version.versionId=id;
        ffxCreateBackendDX12Desc backend{};backend.header.type=FFX_API_CREATE_CONTEXT_DESC_TYPE_BACKEND_DX12;
        backend.header.pNext=&version.header;backend.device=device.Get();
        ffxCreateContextDescUpscale desc{};desc.header.type=FFX_API_CREATE_CONTEXT_DESC_TYPE_UPSCALE;
        desc.header.pNext=&backend.header;
        desc.flags=FFX_UPSCALE_ENABLE_AUTO_EXPOSURE|FFX_UPSCALE_ENABLE_NON_LINEAR_COLORSPACE;
        desc.maxRenderSize={iw,ih};desc.maxUpscaleSize={ow,oh};
        Ffx(create(&model,&desc.header,nullptr),"create FSR 4 upscaler");
        std::wstring error;
        if (!flow.ensure(device.Get(),iw,ih,&error))
            throw std::runtime_error("Optical flow initialization failed");
        flow.set_colour_space(0);
        first=true;last=stats=std::chrono::steady_clock::now();totalMs=0;statsFrames=0;
        Log(LogLevel::Info,L"FSR 4 OUTPUT READY: %ux%u -> %ux%u, provider %hs; optical-flow motion, no engine depth/jitter.",iw,ih,ow,oh,provider.c_str());
    }
    ID3D11Texture2D* Run(ID3D11Device* native,ID3D11DeviceContext* ctx,ID3D11Texture2D* input,UINT w,UINT h) {
        if (!device) Initialize(native);
        // One in-flight command allocator: wait only when CPU reuse overtakes the previous GPU dispatch.
        // Color handoffs themselves use GPU queue waits, rather than CPU polling or color readback.
        if (serial) {
            if (fence->GetCompletedValue()<serial) {
                Hr(fence->SetEventOnCompletion(serial,eventHandle),"allocator reuse event");
                if (WaitForSingleObject(eventHandle,2000)!=WAIT_OBJECT_0)
                    throw std::runtime_error("FSR GPU work did not complete within 2 seconds");
            }
            Hr(device->GetDeviceRemovedReason(),"FSR device");
            void* data=nullptr;D3D12_RANGE read{0,16};
            Hr(timingReadback->Map(0,&read,&data),"read GPU timing");
            const auto* ticks=static_cast<const UINT64*>(data);
            totalMs+=double(ticks[1]-ticks[0])*1000.0/double(timestampFrequency);++statsFrames;
            D3D12_RANGE written{0,0};timingReadback->Unmap(0,&written);
        }
        Ensure(native,input,w,h);
        const auto begin=std::chrono::steady_clock::now();
        ctx->CopyResource(input11.Get(),input);
        const UINT64 next=serial+1;
        Hr(context4->Signal(inputFence11.Get(),next),"signal source GPU completion");ctx->Flush();
        Hr(queue->Wait(inputFence12.Get(),next),"wait for source GPU copy");
        Hr(allocator->Reset(),"reset allocator");Hr(list->Reset(allocator.Get(),nullptr),"reset list");
        list->EndQuery(timing.Get(),D3D12_QUERY_TYPE_TIMESTAMP,0);
        std::wstring error;
        if (!flow.record(list.Get(),input12.Get(),D3D12_RESOURCE_STATE_COMMON,nullptr,D3D12_RESOURCE_STATE_COMMON,
                         aeon_sr::OpticalFlowD3D12::Quality::Balanced,false,&error,false))
            throw std::runtime_error("Optical flow dispatch failed");
        Barrier(list.Get(),input12.Get(),D3D12_RESOURCE_STATE_COMMON,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE|D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        Barrier(list.Get(),output12.Get(),D3D12_RESOURCE_STATE_COMMON,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        ffxDispatchDescUpscale work{};work.header.type=FFX_API_DISPATCH_DESC_TYPE_UPSCALE;
        work.commandList=list.Get();
        work.color=ffxApiGetResourceDX12(input12.Get(),FFX_API_RESOURCE_STATE_PIXEL_COMPUTE_READ);
        work.motionVectors=ffxApiGetResourceDX12(flow.motion(),FFX_API_RESOURCE_STATE_PIXEL_COMPUTE_READ);
        work.output=ffxApiGetResourceDX12(output12.Get(),FFX_API_RESOURCE_STATE_UNORDERED_ACCESS);
        work.motionVectorScale={float(iw),float(ih)};
        work.renderSize={iw,ih};work.upscaleSize={ow,oh};
        work.preExposure=1;work.reset=first;work.viewSpaceToMetersFactor=1;
        work.frameTimeDelta=float(std::chrono::duration<double,std::milli>(begin-last).count());
        if (work.frameTimeDelta<1 || work.frameTimeDelta>250) work.frameTimeDelta=16.67f;
        work.flags=FFX_UPSCALE_FLAG_NON_LINEAR_COLOR_SRGB;
        const auto result=dispatch(&model,&work.header);
        Barrier(list.Get(),input12.Get(),D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE|D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_COMMON);
        Barrier(list.Get(),output12.Get(),D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_COMMON);
        list->EndQuery(timing.Get(),D3D12_QUERY_TYPE_TIMESTAMP,1);
        list->ResolveQueryData(timing.Get(),D3D12_QUERY_TYPE_TIMESTAMP,0,2,timingReadback.Get(),0);
        Hr(list->Close(),"close FSR list");
        ID3D12CommandList* commands[]={list.Get()};queue->ExecuteCommandLists(1,commands);
        Hr(queue->Signal(fence.Get(),++serial),"signal FSR completion");
        Hr(context4->Wait(outputFence11.Get(),serial),"wait for FSR on presentation GPU queue");
        Ffx(result,"FSR 4 dispatch");
        first=false;last=begin;++frames;
        if (statsFrames && std::chrono::steady_clock::now()-stats>std::chrono::seconds(3)) {
            Log(LogLevel::Info,L"FSR 4 output: frames=%llu, input=%ux%u, output=%ux%u, mean GPU flow+FSR=%.2f ms, provider=%hs.",
                frames,iw,ih,ow,oh,totalMs/statsFrames,provider.c_str());
            totalMs=0;statsFrames=0;stats=std::chrono::steady_clock::now();
        }
        return output11.Get();
    }
};
std::unique_ptr<Processor> processor;
}
ID3D11Texture2D* Fsr4Upscale(ID3D11Device* device,ID3D11DeviceContext* context,
                           ID3D11Texture2D* source,UINT width,UINT height) {
    if (!processor) processor=std::make_unique<Processor>();
    return processor->Run(device,context,source,width,height);
}
void ReleaseFsr4() { processor.reset(); }
