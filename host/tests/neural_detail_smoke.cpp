#include "../../OpenNR-Multipass-source/core/shaders/compose.h"
#include <d3d11.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <vector>
using Microsoft::WRL::ComPtr;
void Check(HRESULT hr) { if (FAILED(hr)) throw std::runtime_error("D3D11 shader test failed"); }
float Linear(float x) { return x <= .04045f ? x/12.92f : std::pow((x+.055f)/1.055f,2.4f); }
float Srgb(float x) { return x <= .0031308f ? x*12.92f : 1.055f*std::pow(x,1/2.4f)-.055f; }
int main() {
 try {
    ComPtr<ID3D11Device> device; ComPtr<ID3D11DeviceContext> context;
    Check(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_HARDWARE,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&device,nullptr,&context));
    ComPtr<ID3DBlob> code,error;
    const HRESULT hr=D3DCompile(shaders::kComposeShader,sizeof(shaders::kComposeShader)-1,"production compose",nullptr,nullptr,
        "main","cs_5_0",D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&code,&error);
    if (FAILED(hr)) throw std::runtime_error(error?static_cast<char*>(error->GetBufferPointer()):"shader compile failed");
    ComPtr<ID3D11ComputeShader> shader;
    Check(device->CreateComputeShader(code->GetBufferPointer(),code->GetBufferSize(),nullptr,&shader));
    constexpr UINT w=3840,h=2160,sw=1920,sh=1080;
    auto texture=[&](UINT width,UINT height,const std::vector<float>& pixels,UINT bind) {
        D3D11_TEXTURE2D_DESC d{};d.Width=width;d.Height=height;d.MipLevels=d.ArraySize=1;d.Format=DXGI_FORMAT_R32G32B32A32_FLOAT;
        d.SampleDesc.Count=1;d.BindFlags=bind;
        D3D11_SUBRESOURCE_DATA data{pixels.data(),width*16,0};ComPtr<ID3D11Texture2D> result;
        Check(device->CreateTexture2D(&d,pixels.empty()?nullptr:&data,&result));return result;
    };
    std::vector<float> original(size_t(w)*h*4),low(size_t(sw)*sh*4);
    for(UINT y=0;y<h;++y)for(UINT x=0;x<w;++x)for(UINT c=0;c<4;++c)
        original[(size_t(y)*w+x)*4+c]=c==3?1.f:.5f+((x+y)%2?.12f:-.12f);
    auto full=texture(w,h,original,D3D11_BIND_SHADER_RESOURCE);
    auto output=texture(w,h,{},D3D11_BIND_UNORDERED_ACCESS);
    ComPtr<ID3D11ShaderResourceView> fullView;Check(device->CreateShaderResourceView(full.Get(),nullptr,&fullView));
    ComPtr<ID3D11UnorderedAccessView> outView;Check(device->CreateUnorderedAccessView(output.Get(),nullptr,&outView));
    D3D11_TEXTURE2D_DESC sd{};output->GetDesc(&sd);sd.BindFlags=0;sd.Usage=D3D11_USAGE_STAGING;sd.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
    ComPtr<ID3D11Texture2D> staging;Check(device->CreateTexture2D(&sd,nullptr,&staging));
    struct Constants {UINT dw,dh,sw,sh,mode;float k,intensity;UINT pad;float limit,fade,colour,guard;};
    static_assert(sizeof(Constants)==48);
    D3D11_BUFFER_DESC bd{};bd.ByteWidth=48;bd.BindFlags=D3D11_BIND_CONSTANT_BUFFER;
    ComPtr<ID3D11Buffer> cb;Check(device->CreateBuffer(&bd,nullptr,&cb));
    D3D11_SAMPLER_DESC samp{};samp.Filter=D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    samp.AddressU=samp.AddressV=samp.AddressW=D3D11_TEXTURE_ADDRESS_CLAMP;
    ComPtr<ID3D11SamplerState> sampler;Check(device->CreateSamplerState(&samp,&sampler));
    for (int test=0;test<5;++test) {
        const bool keep=test!=2;const bool linear=test==3;const bool debug=test==4;
        const float delta=test==0?0.f:.08f;
        for(size_t i=0;i<low.size();++i)low[i]=i%4==3?1.f:delta;
        auto residual=texture(sw,sh,low,D3D11_BIND_SHADER_RESOURCE);
        for(size_t i=0;i<low.size();++i)low[i]=i%4==3?1.f:.5f+delta;
        auto neural=texture(sw,sh,low,D3D11_BIND_SHADER_RESOURCE);
        ComPtr<ID3D11ShaderResourceView> residualView,neuralView;
        Check(device->CreateShaderResourceView(residual.Get(),nullptr,&residualView));
        Check(device->CreateShaderResourceView(neural.Get(),nullptr,&neuralView));
        Constants data{w,h,sw,sh,linear?1u:0u,1.f,1.f,1u|((debug?2u:6u)<<1)|(keep?256u:0u),.01f,0,1,2};
        context->UpdateSubresource(cb.Get(),0,nullptr,&data,0,0);
        ID3D11ShaderResourceView* views[]={fullView.Get(),residualView.Get(),neuralView.Get()};
        ID3D11UnorderedAccessView* uavs[]={outView.Get()};ID3D11Buffer* buffers[]={cb.Get()};ID3D11SamplerState* samplers[]={sampler.Get()};
        context->CSSetShader(shader.Get(),nullptr,0);context->CSSetShaderResources(0,3,views);
        context->CSSetUnorderedAccessViews(0,1,uavs,nullptr);context->CSSetConstantBuffers(0,1,buffers);context->CSSetSamplers(0,1,samplers);
        context->Dispatch((w+7)/8,(h+7)/8,1);
        ID3D11UnorderedAccessView* none[]={nullptr};context->CSSetUnorderedAccessViews(0,1,none,nullptr);
        context->CopyResource(staging.Get(),output.Get());D3D11_MAPPED_SUBRESOURCE map{};
        Check(context->Map(staging.Get(),0,D3D11_MAP_READ,0,&map));float maxError=0;
        for(UINT y=0;y<h;++y)for(UINT x=0;x<w;++x)for(UINT c=0;c<4;++c) {
            const float source=original[(size_t(y)*w+x)*4+c];
            const float expected=c==3?1.f:(!keep||debug)?.5f+delta:linear?Srgb(Linear(source)+delta):source+delta;
            const float actual=reinterpret_cast<const float*>(static_cast<const char*>(map.pData)+size_t(y)*map.RowPitch)[x*4+c];
            if(!std::isfinite(actual))throw std::runtime_error("nonfinite shader output");
            maxError=std::max(maxError,std::abs(actual-expected));
        }
        context->Unmap(staging.Get(),0);
        const char* names[]={"identity retains 4K checker at 50% model scale","neural edit retains 4K checker","raw output compatibility","linear-light detail transfer","debug view compatibility"};
        printf("PASS %s: max_error=%.8f\n",names[test],maxError);
        if(maxError>0.0001f)throw std::runtime_error("pixel verification failed");
    }
    return 0;
 } catch(const std::exception& e) {fprintf(stderr,"FAIL: %s\n",e.what());return 1;}
}
