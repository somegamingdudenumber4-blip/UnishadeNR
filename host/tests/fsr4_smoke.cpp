#include "../src/fsr4.h"
#include "../src/log.h"
#include <windows.h>
#include <d3d11.h>
#include <wrl/client.h>
#include <algorithm>
#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <filesystem>
#include <stdexcept>
#include <vector>
using Microsoft::WRL::ComPtr;
std::wstring root;
std::wstring ExeDirectory() { return root; }
void Log(LogLevel, const wchar_t* fmt, ...) {
    va_list args; va_start(args,fmt); vwprintf(fmt,args); va_end(args); wprintf(L"\n"); fflush(stdout);
}
void Check(HRESULT h) { if(FAILED(h)) throw std::runtime_error("D3D11 failure "+std::to_string(unsigned(h))); }
int wmain(int argc,wchar_t** argv) {
    if(argc!=2) return 2;
    root=std::filesystem::absolute(argv[1]).wstring()+L"\\";
    try {
        ComPtr<ID3D11Device> dev; ComPtr<ID3D11DeviceContext> ctx;
        Check(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_HARDWARE,nullptr,D3D11_CREATE_DEVICE_BGRA_SUPPORT,
            nullptr,0,D3D11_SDK_VERSION,&dev,nullptr,&ctx));
        constexpr UINT w=2560,h=1440,ow=3840,oh=2160;
        std::vector<unsigned char> data(size_t(w)*h*4);
        for(UINT y=0;y<h;++y) for(UINT x=0;x<w;++x) {
            auto* p=data.data()+(size_t(y)*w+x)*4;
            p[0]=static_cast<unsigned char>(x*255/w); p[1]=static_cast<unsigned char>(y*255/h);
            p[2]=((x/32+y/32)&1)?210:30;p[3]=255;
        }
        D3D11_TEXTURE2D_DESC d{}; d.Width=w;d.Height=h;d.MipLevels=d.ArraySize=1;d.Format=DXGI_FORMAT_B8G8R8A8_UNORM;
        d.SampleDesc.Count=1; d.Usage=D3D11_USAGE_DEFAULT;d.BindFlags=D3D11_BIND_SHADER_RESOURCE;
        D3D11_SUBRESOURCE_DATA initial{data.data(),w*4,0};ComPtr<ID3D11Texture2D> input;
        Check(dev->CreateTexture2D(&d,&initial,&input));
        std::vector<double> times; ID3D11Texture2D* output=nullptr;
        for(int i=0;i<40;++i) {
            const auto start=std::chrono::steady_clock::now();
            output=Fsr4Upscale(dev.Get(),ctx.Get(),input.Get(),ow,oh);
            if(i>=5) times.push_back(std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count());
        }
        D3D11_TEXTURE2D_DESC od{};output->GetDesc(&od);
        if(od.Width!=ow||od.Height!=oh) throw std::runtime_error("Output dimensions wrong");
        od.Usage=D3D11_USAGE_STAGING;od.CPUAccessFlags=D3D11_CPU_ACCESS_READ;od.BindFlags=od.MiscFlags=0;
        ComPtr<ID3D11Texture2D> readback;Check(dev->CreateTexture2D(&od,nullptr,&readback));
        ctx->CopyResource(readback.Get(),output); D3D11_MAPPED_SUBRESOURCE mapped{};
        Check(ctx->Map(readback.Get(),0,D3D11_MAP_READ,0,&mapped));
        unsigned long long sum[3]={}; unsigned lo=255,hi=0;
        for(UINT y=0;y<oh;++y) for(UINT x=0;x<ow;++x) {
            const auto* p=static_cast<unsigned char*>(mapped.pData)+size_t(y)*mapped.RowPitch+x*4;
            for(int c=0;c<3;++c) { sum[c]+=p[c];lo=std::min(lo,unsigned(p[c]));hi=std::max(hi,unsigned(p[c])); }
        }
        ctx->Unmap(readback.Get(),0);
        std::sort(times.begin(),times.end());double total=0;for(auto t:times)total+=t;
        wprintf(L"SMOKE RESULT frames=40 output=%ux%u format=%u mean_ms=%.3f median_ms=%.3f rgb_mean=%.2f,%.2f,%.2f range=%u..%u\n",
            od.Width,od.Height,od.Format,total/times.size(),times[times.size()/2],
            double(sum[0])/(ow*oh),double(sum[1])/(ow*oh),double(sum[2])/(ow*oh),lo,hi);
        ReleaseFsr4();
        if(hi-lo<100 || sum[0]==0 || sum[1]==0 || sum[2]==0) return 3;
        return 0;
    } catch(const std::exception& e) { printf("SMOKE FAILED: %s\n",e.what()); fflush(stdout); ReleaseFsr4();return 1; }
}
