#include "detail.h"
#include <d3dcompiler.h>
#include <stdexcept>
#include <string>

using Microsoft::WRL::ComPtr;
namespace {
void Check(HRESULT value, const char* step) {
    if (FAILED(value)) throw std::runtime_error(std::string("4K detail: ")+step+" failed ("+std::to_string(unsigned(value))+")");
}
constexpr char shader[] = R"(
Texture2D<float4> Full : register(t0);
Texture2D<float4> Before : register(t1);
Texture2D<float4> Processed : register(t2);
Texture2D<float4> Finished : register(t3);
SamplerState Smooth : register(s0);
cbuffer Size : register(b0) { float2 OutputSize; uint HaveBeforeOverlay; uint Padding; };
float4 vs(uint id : SV_VertexID) : SV_Position {
    float2 p=float2((id<<1)&2,id&2);
    return float4(p*float2(2,-2)+float2(-1,1),0,1);
}
float4 ps(float4 at : SV_Position) : SV_Target {
    float2 uv=at.xy/OutputSize;
    float3 original=Full.SampleLevel(Smooth,uv,0).rgb;
    float3 before=Before.SampleLevel(Smooth,uv,0).rgb;
    float3 processed=Processed.SampleLevel(Smooth,uv,0).rgb;
    float3 finished=Finished.SampleLevel(Smooth,uv,0).rgb;
    // If effects do nothing, this cancels exactly and the original pixels survive at any processing size.
    float3 result=saturate(original+processed-before);
    // ReShade/Unishade menus are drawn after effects. Keep their own pixels rather than transferring
    // a dark panel as a lighting correction over the game's high-frequency textures.
    float3 overlayDifference=abs(finished-processed);
    if (HaveBeforeOverlay && max(overlayDifference.r,max(overlayDifference.g,overlayDifference.b))>0.001)
        result=finished;
    return float4(result,1);
}
)";
ComPtr<ID3DBlob> Compile(const char* entry,const char* profile) {
    ComPtr<ID3DBlob> code,errors;
    HRESULT h=D3DCompile(shader,sizeof(shader)-1,"4K detail",nullptr,nullptr,entry,profile,
                        D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&code,&errors);
    if (FAILED(h)) throw std::runtime_error(errors ? static_cast<const char*>(errors->GetBufferPointer()) : "4K detail shader failed");
    return code;
}
}

void DetailRenderer::EnsureCopy(ID3D11Texture2D* source, Texture& texture, View& view) {
    D3D11_TEXTURE2D_DESC wanted{},current{};source->GetDesc(&wanted);
    if (texture) texture->GetDesc(&current);
    if (current.Width==wanted.Width && current.Height==wanted.Height && current.Format==wanted.Format) return;
    view.Reset();texture.Reset();wanted.MipLevels=wanted.ArraySize=1;wanted.SampleDesc={1,0};
    wanted.Usage=D3D11_USAGE_DEFAULT;wanted.BindFlags=D3D11_BIND_SHADER_RESOURCE;
    wanted.CPUAccessFlags=wanted.MiscFlags=0;
    Check(device_->CreateTexture2D(&wanted,nullptr,&texture),"copy texture");
    Check(device_->CreateShaderResourceView(texture.Get(),nullptr,&view),"copy view");
}

void DetailRenderer::Prepare(ID3D11Device* device, ID3D11DeviceContext* context,
                             ID3D11Texture2D* fullCapture, ID3D11Texture2D* beforeEffects) {
    if (device_.Get()!=device) { Release();device_=device; }
    beforeOverlay_=false;
    if (full_.Get()!=fullCapture) {
        fullView_.Reset();full_=fullCapture;
        Check(device_->CreateShaderResourceView(full_.Get(),nullptr,&fullView_),"full-resolution capture view");
    }
    EnsureCopy(beforeEffects,baseline_,baselineView_);
    context->CopyResource(baseline_.Get(),beforeEffects);
}

void DetailRenderer::BeforeOverlay(ID3D11DeviceContext* context,ID3D11Texture2D* afterEffects) {
    if (!Ready()) return;
    EnsureCopy(afterEffects,processed_,processedView_);
    context->CopyResource(processed_.Get(),afterEffects);beforeOverlay_=true;
}

ID3D11Texture2D* DetailRenderer::Render(ID3D11DeviceContext* context, ID3D11Texture2D* finished,UINT width,UINT height) {
    if (!Ready()) throw std::runtime_error("4K detail source not ready");
    if (!pixel_) {
        const auto vs=Compile("vs","vs_5_0"),ps=Compile("ps","ps_5_0");
        Check(device_->CreateVertexShader(vs->GetBufferPointer(),vs->GetBufferSize(),nullptr,&vertex_),"vertex shader");
        Check(device_->CreatePixelShader(ps->GetBufferPointer(),ps->GetBufferSize(),nullptr,&pixel_),"pixel shader");
        D3D11_SAMPLER_DESC sd{};sd.Filter=D3D11_FILTER_MIN_MAG_MIP_LINEAR;
        sd.AddressU=sd.AddressV=sd.AddressW=D3D11_TEXTURE_ADDRESS_CLAMP;sd.ComparisonFunc=D3D11_COMPARISON_NEVER;
        Check(device_->CreateSamplerState(&sd,&sampler_),"sampler");
        D3D11_BUFFER_DESC bd{};bd.ByteWidth=16;bd.Usage=D3D11_USAGE_DEFAULT;bd.BindFlags=D3D11_BIND_CONSTANT_BUFFER;
        Check(device_->CreateBuffer(&bd,nullptr,&constants_),"constants");
    }
    EnsureCopy(finished,finished_,finishedView_);context->CopyResource(finished_.Get(),finished);
    D3D11_TEXTURE2D_DESC od{};if(output_)output_->GetDesc(&od);
    if (od.Width!=width||od.Height!=height) {
        outputTarget_.Reset();output_.Reset();od={};od.Width=width;od.Height=height;
        od.MipLevels=od.ArraySize=1;od.Format=DXGI_FORMAT_B8G8R8A8_UNORM;od.SampleDesc.Count=1;
        od.Usage=D3D11_USAGE_DEFAULT;od.BindFlags=D3D11_BIND_RENDER_TARGET|D3D11_BIND_SHADER_RESOURCE;
        Check(device_->CreateTexture2D(&od,nullptr,&output_),"output texture");
        Check(device_->CreateRenderTargetView(output_.Get(),nullptr,&outputTarget_),"output target");
    }
    struct { float size[2];UINT ui,padding; } data{{float(width),float(height)},beforeOverlay_?1u:0u,0};
    context->UpdateSubresource(constants_.Get(),0,nullptr,&data,0,0);
    ID3D11ShaderResourceView* views[]={fullView_.Get(),baselineView_.Get(),
        beforeOverlay_?processedView_.Get():finishedView_.Get(),finishedView_.Get()};
    ID3D11Buffer* buffers[]={constants_.Get()};ID3D11SamplerState* samplers[]={sampler_.Get()};
    ID3D11RenderTargetView* targets[]={outputTarget_.Get()};
    D3D11_VIEWPORT viewport{0,0,float(width),float(height),0,1};
    context->IASetInputLayout(nullptr);context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    context->VSSetShader(vertex_.Get(),nullptr,0);context->PSSetShader(pixel_.Get(),nullptr,0);
    context->PSSetShaderResources(0,4,views);context->PSSetSamplers(0,1,samplers);context->PSSetConstantBuffers(0,1,buffers);
    context->RSSetState(nullptr);context->RSSetViewports(1,&viewport);context->OMSetBlendState(nullptr,nullptr,0xffffffff);
    context->OMSetDepthStencilState(nullptr,0);context->OMSetRenderTargets(1,targets,nullptr);context->Draw(3,0);
    ID3D11ShaderResourceView* none[4]={};context->PSSetShaderResources(0,4,none);context->OMSetRenderTargets(0,nullptr,nullptr);
    return output_.Get();
}

void DetailRenderer::Release() { *this=DetailRenderer{}; }
