#include "upscale.h"
#include "fsr4.h"
#include "config.h"
#include "detail.h"
#include "capture.h"
#include "log.h"
#include "resource.h"
#include "state.h"

#include <d3dcompiler.h>
#include <dcomp.h>
#include <wincodec.h>

#include <bit>
#include <chrono>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <string_view>

namespace
{
DetailRenderer detail;
// The legacy spatial fallback uses EASU/RCAS. FSR 4 and retained 4K detail use separate paths.
constexpr char kPrelude[] = R"(
#define A_GPU 1
#define A_HLSL 1
)";

constexpr char kCallbacks[] = R"(
cbuffer Constants : register(b0)
{
    uint4 Con0;
    uint4 Con1;
    uint4 Con2;
    uint4 Con3;
    int2 Offset; // where RCAS writes in the composition surface's texture
};
Texture2D<float4> Input : register(t0);
SamplerState Clamp : register(s0);

#define FSR_EASU_F 1
#define FSR_RCAS_F 1
AF4 FsrEasuRF(AF2 p) { return Input.GatherRed(Clamp, p); }
AF4 FsrEasuGF(AF2 p) { return Input.GatherGreen(Clamp, p); }
AF4 FsrEasuBF(AF2 p) { return Input.GatherBlue(Clamp, p); }
AF4 FsrRcasLoadF(ASU2 p) { return Input.Load(int3(p, 0)); }
void FsrRcasInputF(inout AF1 r, inout AF1 g, inout AF1 b) {}
)";

constexpr char kEntryPoints[] = R"(
float4 vs(uint id : SV_VertexID) : SV_Position
{
    float2 at = float2((id << 1) & 2, id & 2);
    return float4(at * float2(2, -2) + float2(-1, 1), 0, 1);
}

float4 easu(float4 position : SV_Position) : SV_Target
{
    AF3 color;
    FsrEasuF(color, AU2(position.xy), Con0, Con1, Con2, Con3);
    return float4(color, 1);
}

float4 rcas(float4 position : SV_Position) : SV_Target
{
    AF1 r, g, b;
    FsrRcasF(r, g, b, AU2(int2(position.xy) - Offset), Con0);
    return float4(r, g, b, 1);
}

float4 copyOutput(float4 position : SV_Position) : SV_Target
{
    return float4(Input.Load(int3(int2(position.xy)-Offset, 0)).rgb, 1);
}
)";

// RCAS sharpening, in stops below its strongest at 0.
constexpr float kSharpness = 0.2f;

struct Constants
{
    uint32_t con[4][4];
    int32_t offset[4];
};

struct Upscaler
{
    winrt::com_ptr<ID3D11VertexShader> vertexShader;
    winrt::com_ptr<ID3D11PixelShader> easu;
    winrt::com_ptr<ID3D11PixelShader> rcas;
    winrt::com_ptr<ID3D11PixelShader> copyOutput;
    winrt::com_ptr<ID3D11Buffer> constants;
    winrt::com_ptr<ID3D11SamplerState> sampler;
    // The finished frame, copied out of the back buffer so shaders can read it.
    winrt::com_ptr<ID3D11Texture2D> input;
    winrt::com_ptr<ID3D11ShaderResourceView> inputView;
    // EASU's output at the window's size, which RCAS reads.
    winrt::com_ptr<ID3D11Texture2D> upscaled;
    winrt::com_ptr<ID3D11RenderTargetView> upscaledTarget;
    winrt::com_ptr<ID3D11ShaderResourceView> upscaledView;
    UINT width = 0;
    UINT height = 0;

    winrt::com_ptr<IDCompositionDevice> composition;
    winrt::com_ptr<IDCompositionTarget> target;
    winrt::com_ptr<IDCompositionVisual> visual;
    winrt::com_ptr<IDCompositionSurface> surface;
    // The texture the surface last drew into, which usually stays the same.
    winrt::com_ptr<ID3D11Texture2D> surfaceTexture;
    winrt::com_ptr<ID3D11RenderTargetView> surfaceTarget;
    bool shown = false;
    int outputMode = -1;
    // Set after an error. The frame falls back to Windows' stretch instead of failing every frame.
    bool failed = false;
};
Upscaler u;

std::string_view Resource(int id)
{
    const HRSRC info = FindResourceW(nullptr, MAKEINTRESOURCEW(id), RT_RCDATA);
    const HGLOBAL resource = info ? LoadResource(nullptr, info) : nullptr;
    const void* data = resource ? LockResource(resource) : nullptr;
    if (!data)
        throw std::runtime_error("the FSR shader is missing from Unishade.exe");
    return { static_cast<const char*>(data), SizeofResource(nullptr, info) };
}

winrt::com_ptr<ID3DBlob> Compile(const std::string& source, const char* entry, const char* target)
{
    winrt::com_ptr<ID3DBlob> code, errors;
    if (FAILED(D3DCompile(source.data(), source.size(), "fsr", nullptr, nullptr, entry, target, D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, code.put(),
                          errors.put())))
        throw std::runtime_error(errors ? static_cast<const char*>(errors->GetBufferPointer()) : "the FSR shader did not compile");
    return code;
}

void CreateShaders()
{
    const auto started = FrameStatistics::Clock::now();
    const std::string source = std::string(kPrelude) + std::string(Resource(IDR_FFX_A)) + kCallbacks + std::string(Resource(IDR_FFX_FSR1)) + kEntryPoints;
    const auto vertex = Compile(source, "vs", "vs_5_0");
    const auto easu = Compile(source, "easu", "ps_5_0");
    const auto rcas = Compile(source, "rcas", "ps_5_0");
    const auto copyOutput = Compile(source, "copyOutput", "ps_5_0");
    winrt::check_hresult(g.device->CreateVertexShader(vertex->GetBufferPointer(), vertex->GetBufferSize(), nullptr, u.vertexShader.put()));
    winrt::check_hresult(g.device->CreatePixelShader(easu->GetBufferPointer(), easu->GetBufferSize(), nullptr, u.easu.put()));
    winrt::check_hresult(g.device->CreatePixelShader(rcas->GetBufferPointer(), rcas->GetBufferSize(), nullptr, u.rcas.put()));
    winrt::check_hresult(g.device->CreatePixelShader(copyOutput->GetBufferPointer(), copyOutput->GetBufferSize(), nullptr, u.copyOutput.put()));

    D3D11_BUFFER_DESC constants{};
    constants.ByteWidth = sizeof(Constants);
    constants.Usage = D3D11_USAGE_DEFAULT;
    constants.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    winrt::check_hresult(g.device->CreateBuffer(&constants, nullptr, u.constants.put()));
    D3D11_SAMPLER_DESC sampler{};
    sampler.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    sampler.AddressU = sampler.AddressV = sampler.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampler.ComparisonFunc = D3D11_COMPARISON_NEVER;
    winrt::check_hresult(g.device->CreateSamplerState(&sampler, u.sampler.put()));
    Log(LogLevel::Info, L"FSR shaders compiled in %.0f ms.", std::chrono::duration<double, std::milli>(FrameStatistics::Clock::now() - started).count());
}

void CreateComposition()
{
    winrt::check_hresult(DCompositionCreateDevice(g.device.as<IDXGIDevice>().get(), __uuidof(IDCompositionDevice), u.composition.put_void()));
    // Topmost, so it covers the window's swapchain. The swapchain keeps presenting underneath because ReShade draws into
    // it and reads menu input through its window.
    winrt::check_hresult(u.composition->CreateTargetForHwnd(g.overlay, TRUE, u.target.put()));
    winrt::check_hresult(u.composition->CreateVisual(u.visual.put()));
}

// Makes what follows the window's size.
void Resize(UINT width, UINT height)
{
    u.upscaled = nullptr;
    u.upscaledTarget = nullptr;
    u.upscaledView = nullptr;
    u.surface = nullptr;
    u.surfaceTexture = nullptr;
    u.surfaceTarget = nullptr;
    u.width = u.height = 0;

    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = width;
    desc.Height = height;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    // More than 8 bits between the passes, so RCAS does not sharpen EASU's rounding.
    desc.Format = DXGI_FORMAT_R10G10B10A2_UNORM;
    desc.SampleDesc = { 1, 0 };
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
    winrt::check_hresult(g.device->CreateTexture2D(&desc, nullptr, u.upscaled.put()));
    winrt::check_hresult(g.device->CreateRenderTargetView(u.upscaled.get(), nullptr, u.upscaledTarget.put()));
    winrt::check_hresult(g.device->CreateShaderResourceView(u.upscaled.get(), nullptr, u.upscaledView.put()));

    winrt::check_hresult(u.composition->CreateSurface(width, height, DXGI_FORMAT_B8G8R8A8_UNORM, DXGI_ALPHA_MODE_IGNORE, u.surface.put()));
    winrt::check_hresult(u.visual->SetContent(u.surface.get()));
    u.width = width;
    u.height = height;
}

void CopyInput(ID3D11Texture2D* backBuffer)
{
    D3D11_TEXTURE2D_DESC desc{};
    backBuffer->GetDesc(&desc);
    D3D11_TEXTURE2D_DESC current{};
    if (u.input)
        u.input->GetDesc(&current);
    if (current.Width != desc.Width || current.Height != desc.Height || current.Format != desc.Format)
    {
        u.inputView = nullptr;
        u.input = nullptr;
        desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        desc.MiscFlags = 0;
        desc.CPUAccessFlags = 0;
        desc.Usage = D3D11_USAGE_DEFAULT;
        winrt::check_hresult(g.device->CreateTexture2D(&desc, nullptr, u.input.put()));
        winrt::check_hresult(g.device->CreateShaderResourceView(u.input.get(), nullptr, u.inputView.put()));
    }
    g.context->CopyResource(u.input.get(), backBuffer);
}

uint32_t Bits(float value)
{
    return std::bit_cast<uint32_t>(value);
}

// FsrEasuCon from ffx_fsr1.h, for an input that fills its texture.
Constants EasuConstants(float inputWidth, float inputHeight, float outputWidth, float outputHeight)
{
    const float x = 1 / inputWidth;
    const float y = 1 / inputHeight;
    return { { { Bits(inputWidth / outputWidth), Bits(inputHeight / outputHeight), Bits(0.5f * inputWidth / outputWidth - 0.5f),
                 Bits(0.5f * inputHeight / outputHeight - 0.5f) },
               { Bits(x), Bits(y), Bits(x), Bits(-y) },
               { Bits(-x), Bits(2 * y), Bits(x), Bits(2 * y) },
               { Bits(0), Bits(4 * y), 0, 0 } },
             {} };
}

// Runs a pass over viewport, reading view.
void Draw(ID3D11PixelShader* shader, ID3D11ShaderResourceView* view, ID3D11RenderTargetView* target, const D3D11_VIEWPORT& viewport,
          const Constants& constants)
{
    g.context->UpdateSubresource(u.constants.get(), 0, nullptr, &constants, 0, 0);
    ID3D11Buffer* buffers[] = { u.constants.get() };
    ID3D11SamplerState* samplers[] = { u.sampler.get() };
    g.context->IASetInputLayout(nullptr);
    g.context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    g.context->VSSetShader(u.vertexShader.get(), nullptr, 0);
    g.context->PSSetShader(shader, nullptr, 0);
    g.context->PSSetConstantBuffers(0, 1, buffers);
    g.context->PSSetSamplers(0, 1, samplers);
    g.context->PSSetShaderResources(0, 1, &view);
    g.context->RSSetState(nullptr);
    g.context->RSSetViewports(1, &viewport);
    g.context->OMSetBlendState(nullptr, nullptr, 0xFFFFFFFF);
    g.context->OMSetDepthStencilState(nullptr, 0);
    g.context->OMSetRenderTargets(1, &target, nullptr);
    g.context->Draw(3, 0);
    // The next pass reads this target, and the input is written again next frame.
    ID3D11ShaderResourceView* none = nullptr;
    g.context->PSSetShaderResources(0, 1, &none);
    g.context->OMSetRenderTargets(0, nullptr, nullptr);
}

void Hide()
{
    if (!u.shown)
        return;
    u.shown = false;
    if (u.target)
    {
        u.target->SetRoot(nullptr);
        u.composition->Commit();
    }
    Log(LogLevel::Info, L"FSR upscaling off.");
    u.outputMode = -1;
}

void Upscale(UINT width, UINT height)
{
    if (!u.easu)
        CreateShaders();
    if (!u.composition)
        CreateComposition();
    if (u.width != width || u.height != height)
        Resize(width, height);

    winrt::com_ptr<ID3D11Texture2D> backBuffer;
    winrt::check_hresult(g.swapchain->GetBuffer(0, __uuidof(ID3D11Texture2D), backBuffer.put_void()));
    const bool keepDetail = Fsr4OutputEnabled() && Preserve4KDetail() && detail.Ready();
    const bool fsr4 = Fsr4OutputEnabled() && !keepDetail;
    const int outputMode = keepDetail ? 2 : fsr4 ? 1 : 0;
    if (keepDetail && u.outputMode != outputMode)
        ReleaseFsr4();
    CopyInput(keepDetail ? detail.Render(g.context.get(), backBuffer.get(), width, height) :
              fsr4 ? Fsr4Upscale(g.device.get(), g.context.get(), backBuffer.get(), width, height) : backBuffer.get());
    D3D11_TEXTURE2D_DESC input{};
    u.input->GetDesc(&input);

    const float outputWidth = static_cast<float>(width);
    const float outputHeight = static_cast<float>(height);
    Draw((keepDetail || fsr4) ? u.copyOutput.get() : u.easu.get(), u.inputView.get(), u.upscaledTarget.get(), { 0, 0, outputWidth, outputHeight, 0, 1 },
         EasuConstants(static_cast<float>(input.Width), static_cast<float>(input.Height), outputWidth, outputHeight));

    winrt::com_ptr<ID3D11Texture2D> texture;
    POINT offset{};
    winrt::check_hresult(u.surface->BeginDraw(nullptr, __uuidof(ID3D11Texture2D), texture.put_void(), &offset));
    if (texture != u.surfaceTexture)
    {
        u.surfaceTarget = nullptr;
        winrt::check_hresult(g.device->CreateRenderTargetView(texture.get(), nullptr, u.surfaceTarget.put()));
        u.surfaceTexture = texture;
    }
    // FsrRcasCon turns sharpness in stops into a linear scale.
    const Constants rcas{ { { Bits(std::exp2(-kSharpness)) } }, { offset.x, offset.y } };
    // Captured native detail is already sharp. Do not amplify its texture grain with another RCAS pass.
    Draw(keepDetail ? u.copyOutput.get() : u.rcas.get(), u.upscaledView.get(), u.surfaceTarget.get(),
         { static_cast<float>(offset.x), static_cast<float>(offset.y), outputWidth, outputHeight, 0, 1 }, rcas);
    winrt::check_hresult(u.surface->EndDraw());

    if (!u.shown)
    {
        winrt::check_hresult(u.target->SetRoot(u.visual.get()));
        u.shown = true;
    }
    if (u.outputMode != outputMode)
    {
        Log(LogLevel::Info, keepDetail ? L"4K DETAIL PRESERVATION ON: %ux%u real capture + low-resolution effect correction; FSR reconstruction bypassed." :
            fsr4 ? L"FSR 4 final output composition on: %ux%u." : L"FSR 1 output composition on: %ux%u.", width, height);
        u.outputMode = outputMode;
    }
    winrt::check_hresult(u.composition->Commit());
}
} // namespace

void PrepareDetailFrame(ID3D11Texture2D* fullCapture, ID3D11Texture2D* beforeEffects)
{
    detail.Prepare(g.device.get(), g.context.get(), fullCapture, beforeEffects);
}

void CaptureDetailBeforeOverlay()
{
    if (!Fsr4OutputEnabled() || !Preserve4KDetail() || !detail.Ready() || !g.swapchain)
        return;
    try
    {
        winrt::com_ptr<ID3D11Texture2D> frame;
        winrt::check_hresult(g.swapchain->GetBuffer(0, __uuidof(ID3D11Texture2D), frame.put_void()));
        detail.BeforeOverlay(g.context.get(), frame.get());
    }
    catch (const std::exception& e) { Log(LogLevel::Warning, L"4K detail UI capture unavailable: %hs", e.what()); }
    catch (const winrt::hresult_error&) { /* Present handles a lost device. */ }
}

void ResetDetailFrame() { detail.Release(); }

bool SaveDetailScreenshot(const char* label, bool beforeEffects)
{
    if (!Fsr4OutputEnabled() || !Preserve4KDetail() || !detail.Ready() || !g.swapchain)
        return false;
    try
    {
        winrt::com_ptr<ID3D11Texture2D> back;
        winrt::check_hresult(g.swapchain->GetBuffer(0, __uuidof(ID3D11Texture2D), back.put_void()));
        RECT bounds{}; GetClientRect(g.overlay, &bounds);
        ID3D11Texture2D* image = beforeEffects ? detail.Original() :
            detail.Render(g.context.get(), back.get(), bounds.right, bounds.bottom);
        D3D11_TEXTURE2D_DESC desc{}; image->GetDesc(&desc);
        desc.BindFlags = desc.MiscFlags = 0;
        desc.Usage = D3D11_USAGE_STAGING; desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        winrt::com_ptr<ID3D11Texture2D> staging;
        winrt::check_hresult(g.device->CreateTexture2D(&desc, nullptr, staging.put()));
        g.context->CopyResource(staging.get(), image);
        wchar_t folder[32768]{};
        GetPrivateProfileStringW(L"SCREENSHOT", L"SavePath", L".\\screenshots", folder,
                                static_cast<DWORD>(std::size(folder)), (ExeDirectory()+L"ReShade.ini").c_str());
        auto directory = std::filesystem::path(folder);
        if (directory.is_relative()) directory = std::filesystem::path(ExeDirectory()) / directory;
        std::filesystem::create_directories(directory);
        SYSTEMTIME now{}; GetLocalTime(&now);
        wchar_t name[160]{};
        swprintf_s(name, L"Unishade4K %04u-%02u-%02u %02u-%02u-%02u-%03u%ls.png", now.wYear, now.wMonth,
            now.wDay, now.wHour, now.wMinute, now.wSecond, now.wMilliseconds,
            label && std::strcmp(label,"Before")==0 ? L" Before" : label ? L" After" : L"");
        const auto path = directory/name;
        winrt::com_ptr<IWICImagingFactory> factory;
        winrt::com_ptr<IWICStream> stream;
        winrt::com_ptr<IWICBitmapEncoder> encoder;
        winrt::com_ptr<IWICBitmapFrameEncode> frame;
        winrt::check_hresult(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                                             __uuidof(IWICImagingFactory), factory.put_void()));
        winrt::check_hresult(factory->CreateStream(stream.put()));
        winrt::check_hresult(stream->InitializeFromFilename(path.c_str(), GENERIC_WRITE));
        winrt::check_hresult(factory->CreateEncoder(GUID_ContainerFormatPng, nullptr, encoder.put()));
        winrt::check_hresult(encoder->Initialize(stream.get(), WICBitmapEncoderNoCache));
        winrt::check_hresult(encoder->CreateNewFrame(frame.put(), nullptr));
        winrt::check_hresult(frame->Initialize(nullptr));
        winrt::check_hresult(frame->SetSize(desc.Width, desc.Height));
        WICPixelFormatGUID format = GUID_WICPixelFormat32bppBGRA;
        winrt::check_hresult(frame->SetPixelFormat(&format));
        if (format != GUID_WICPixelFormat32bppBGRA)
            throw std::runtime_error("PNG encoder did not accept BGRA8");
        D3D11_MAPPED_SUBRESOURCE mapped{};
        winrt::check_hresult(g.context->Map(staging.get(), 0, D3D11_MAP_READ, 0, &mapped));
        const HRESULT written = frame->WritePixels(desc.Height, mapped.RowPitch,
            mapped.RowPitch*desc.Height, static_cast<BYTE*>(mapped.pData));
        g.context->Unmap(staging.get(), 0);
        winrt::check_hresult(written);
        winrt::check_hresult(frame->Commit());winrt::check_hresult(encoder->Commit());
        Report(LogLevel::Ok, L"Screenshot saved: %ls (%ux%u).", path.c_str(), desc.Width, desc.Height);
        return true;
    }
    catch (const winrt::hresult_error& e) {
        Log(LogLevel::Warning, L"4K screenshot failed: %ls. ReShade will save its processing frame instead.", e.message().c_str());
    }
    catch (const std::exception& e) {
        Log(LogLevel::Warning, L"4K screenshot failed: %hs. ReShade will save its processing frame instead.", e.what());
    }
    return false;
}

void UpscaleFrame() noexcept
{
    try
    {
        if (!g.swapchain || u.failed)
            return;
        DXGI_SWAP_CHAIN_DESC1 desc{};
        winrt::check_hresult(g.swapchain->GetDesc1(&desc));
        RECT client{};
        GetClientRect(g.overlay, &client);
        const UINT width = static_cast<UINT>(client.right);
        const UINT height = static_cast<UINT>(client.bottom);
        // FSR only scales up. At full effect resolution the frame can be a few pixels off the window's size, which
        // Windows' stretch handles as before.
        if (desc.Width < width && desc.Height < height)
            Upscale(width, height);
        else
            Hide();
    }
    catch (const winrt::hresult_error& e)
    {
        // Present reports a lost device and the host starts again on a new one.
        if (DeviceLost(e.code()))
            return;
        Log(LogLevel::Error, L"FSR upscaling stopped: %ls (0x%08X). Effects below 100%% are stretched instead.", e.message().c_str(),
            static_cast<unsigned>(e.code()));
        u.failed = true;
        Hide();
    }
    catch (const std::exception& e)
    {
        Log(LogLevel::Error, L"FSR upscaling stopped: %hs Effects below 100%% are stretched instead.", e.what());
        u.failed = true;
        Hide();
    }
}

void ReleaseUpscaler()
{
    detail.Release();
    ReleaseFsr4();
    // Releasing the target also takes the window out of composition.
    u = {};
}
