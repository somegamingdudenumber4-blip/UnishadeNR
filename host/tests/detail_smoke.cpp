#include "../src/detail.h"
#include <d3dcompiler.h>
#include <algorithm>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <vector>
using Microsoft::WRL::ComPtr;
void Check(HRESULT h) { if(FAILED(h))throw std::runtime_error("D3D failure "+std::to_string(unsigned(h))); }
ComPtr<ID3D11Texture2D> Texture(ID3D11Device* d,UINT w,UINT h,const std::vector<unsigned char>& pixels) {
    D3D11_TEXTURE2D_DESC td{};td.Width=w;td.Height=h;td.MipLevels=td.ArraySize=1;
    td.Format=DXGI_FORMAT_B8G8R8A8_UNORM;td.SampleDesc.Count=1;td.Usage=D3D11_USAGE_DEFAULT;
    td.BindFlags=D3D11_BIND_SHADER_RESOURCE|D3D11_BIND_RENDER_TARGET;
    D3D11_SUBRESOURCE_DATA data{pixels.data(),w*4,0};ComPtr<ID3D11Texture2D> t;
    Check(d->CreateTexture2D(&td,pixels.empty()?nullptr:&data,&t));return t;
}
std::vector<unsigned char> Read(ID3D11Device* d,ID3D11DeviceContext* c,ID3D11Texture2D* t) {
    D3D11_TEXTURE2D_DESC td{};t->GetDesc(&td);td.Usage=D3D11_USAGE_STAGING;
    td.BindFlags=td.MiscFlags=0;td.CPUAccessFlags=D3D11_CPU_ACCESS_READ;ComPtr<ID3D11Texture2D> stage;
    Check(d->CreateTexture2D(&td,nullptr,&stage));c->CopyResource(stage.Get(),t);
    D3D11_MAPPED_SUBRESOURCE m{};Check(c->Map(stage.Get(),0,D3D11_MAP_READ,0,&m));
    std::vector<unsigned char> result(size_t(td.Width)*td.Height*4);
    for(UINT y=0;y<td.Height;++y)memcpy(result.data()+size_t(y)*td.Width*4,
        static_cast<unsigned char*>(m.pData)+size_t(y)*m.RowPitch,td.Width*4);
    c->Unmap(stage.Get(),0);return result;
}
int main(int argc,char** argv) {
    try {
        if(argc!=2)throw std::runtime_error("pass capture.cpp path");
        ComPtr<ID3D11Device> device;ComPtr<ID3D11DeviceContext> context;
        Check(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_HARDWARE,nullptr,D3D11_CREATE_DEVICE_BGRA_SUPPORT,
            nullptr,0,D3D11_SDK_VERSION,&device,nullptr,&context));
        constexpr UINT w=3840,h=2160,lw=1280,lh=720;
        std::vector<unsigned char> high(size_t(w)*h*4),low(size_t(lw)*lh*4);
        for(UINT y=0;y<h;++y)for(UINT x=0;x<w;++x) {
            auto* p=high.data()+(size_t(y)*w+x)*4;
            p[0]=40+((x+y)%2)*160;p[1]=40+(y%5)*30;p[2]=40+(x%5)*30;p[3]=255;
        }
        for(UINT y=0;y<lh;++y)for(UINT x=0;x<lw;++x)for(UINT c=0;c<4;++c) {
            unsigned sum=0;for(UINT yy=0;yy<3;++yy)for(UINT xx=0;xx<3;++xx)
                sum+=high[(size_t(y*3+yy)*w+x*3+xx)*4+c];
            low[(size_t(y)*lw+x)*4+c]=static_cast<unsigned char>((sum+4)/9);
        }
        auto source=Texture(device.Get(),w,h,high),baseline=Texture(device.Get(),lw,lh,low);
        DetailRenderer detail;detail.Prepare(device.Get(),context.Get(),source.Get(),baseline.Get());
        auto identity=Read(device.Get(),context.Get(),detail.Render(context.Get(),baseline.Get(),w,h));
        unsigned maxError=0;unsigned long long total=0;
        for(size_t i=0;i<high.size();++i) { unsigned e=std::abs(int(high[i])-int(identity[i]));maxError=std::max(maxError,e);total+=e; }
        printf("IDENTITY: 720p processing -> 4K output, max_channel_error=%u/255, mean_channel_error=%.6f/255\n",maxError,double(total)/high.size());
        if(maxError>1)throw std::runtime_error("native detail was not preserved");
        auto changed=low;for(size_t i=0;i<changed.size();i+=4) {changed[i]+=12;changed[i+1]+=8;changed[i+2]+=16;}
        auto effect=Texture(device.Get(),lw,lh,changed);
        detail.Prepare(device.Get(),context.Get(),source.Get(),baseline.Get());detail.BeforeOverlay(context.Get(),effect.Get());
        auto result=Read(device.Get(),context.Get(),detail.Render(context.Get(),effect.Get(),w,h));
        unsigned correctionError=0;const unsigned delta[]={12,8,16,0};
        for(size_t i=0;i<high.size();++i)correctionError=std::max(correctionError,
            unsigned(std::abs(int(result[i])-int(high[i])-int(delta[i%4]))));
        printf("EFFECT TRANSFER: high-frequency 4K detail plus low-resolution color change, max_error=%u/255\n",correctionError);
        if(correctionError>1)throw std::runtime_error("effect transfer changed native texture detail");
        auto menu=changed;for(UINT y=40;y<140;++y)for(UINT x=40;x<140;++x) {
            auto* p=menu.data()+(size_t(y)*lw+x)*4;p[0]=20;p[1]=180;p[2]=30;p[3]=255;
        }
        auto ui=Texture(device.Get(),lw,lh,menu);
        auto withUi=Read(device.Get(),context.Get(),detail.Render(context.Get(),ui.Get(),w,h));
        const auto* p=withUi.data()+(size_t(250)*w+250)*4;
        printf("OVERLAY: inside opaque panel BGR=%u,%u,%u (expected 20,180,30)\n",p[0],p[1],p[2]);
        if(p[0]!=20||p[1]!=180||p[2]!=30)throw std::runtime_error("overlay leaked original game detail");

        // Compile and execute the exact area-filter shader embedded in capture.cpp.
        std::ifstream input(argv[1]);std::string file((std::istreambuf_iterator<char>(input)),{});
        const std::string marker="constexpr char kScaleShader[] = R\"(";
        auto begin=file.find(marker);if(begin==std::string::npos)throw std::runtime_error("scale shader not found");
        begin+=marker.size();auto end=file.find(")\";",begin);std::string shader=file.substr(begin,end-begin);
        ComPtr<ID3DBlob> vs,ps,err;
        Check(D3DCompile(shader.data(),shader.size(),"capture.cpp",nullptr,nullptr,"vs","vs_4_0",0,0,&vs,&err));
        HRESULT hlsl=D3DCompile(shader.data(),shader.size(),"capture.cpp",nullptr,nullptr,"ps","ps_4_0",0,0,&ps,&err);
        if(FAILED(hlsl))throw std::runtime_error(err?static_cast<char*>(err->GetBufferPointer()):"capture shader failed");
        ComPtr<ID3D11VertexShader> v;ComPtr<ID3D11PixelShader> s;ComPtr<ID3D11ShaderResourceView> view;
        Check(device->CreateVertexShader(vs->GetBufferPointer(),vs->GetBufferSize(),nullptr,&v));
        Check(device->CreatePixelShader(ps->GetBufferPointer(),ps->GetBufferSize(),nullptr,&s));
        Check(device->CreateShaderResourceView(source.Get(),nullptr,&view));
        auto filtered=Texture(device.Get(),lw,lh,{});ComPtr<ID3D11RenderTargetView> target;
        Check(device->CreateRenderTargetView(filtered.Get(),nullptr,&target));
        const float constants[]={1,0,float(w),float(h),3,3,0,0};
        D3D11_BUFFER_DESC bd{};bd.ByteWidth=32;bd.Usage=D3D11_USAGE_DEFAULT;bd.BindFlags=D3D11_BIND_CONSTANT_BUFFER;
        D3D11_SUBRESOURCE_DATA init{constants,0,0};ComPtr<ID3D11Buffer> cb;Check(device->CreateBuffer(&bd,&init,&cb));
        ID3D11Buffer* buffers[]={cb.Get()};ID3D11RenderTargetView* targets[]={target.Get()};
        ID3D11ShaderResourceView* views[]={view.Get()};D3D11_VIEWPORT viewport{0,0,float(lw),float(lh),0,1};
        D3D11_SAMPLER_DESC sd{};sd.Filter=D3D11_FILTER_MIN_MAG_LINEAR_MIP_POINT;
        sd.AddressU=sd.AddressV=sd.AddressW=D3D11_TEXTURE_ADDRESS_CLAMP;ComPtr<ID3D11SamplerState> sampler;
        Check(device->CreateSamplerState(&sd,&sampler));ID3D11SamplerState* samplers[]={sampler.Get()};
        context->VSSetShader(v.Get(),nullptr,0);context->PSSetShader(s.Get(),nullptr,0);
        context->PSSetShaderResources(0,1,views);context->PSSetConstantBuffers(0,1,buffers);context->PSSetSamplers(0,1,samplers);
        context->RSSetViewports(1,&viewport);context->OMSetRenderTargets(1,targets,nullptr);context->Draw(3,0);
        ID3D11ShaderResourceView* none[]={nullptr};context->PSSetShaderResources(0,1,none);context->OMSetRenderTargets(0,nullptr,nullptr);
        auto downsampled=Read(device.Get(),context.Get(),filtered.Get());unsigned filterError=0;
        for(size_t i=0;i<low.size();++i)filterError=std::max(filterError,unsigned(std::abs(int(low[i])-int(downsampled[i]))));
        printf("AREA FILTER: GPU 4K->720p versus exact 3x3 pixel integration, max_error=%u/255\n",filterError);
        if(filterError>1)throw std::runtime_error("area filter differs from reference");
        printf("PASS: original detail, effect transfer, overlay isolation, anti-aliased downsampling. No Roblox gameplay benchmark.\n");
        return 0;
    } catch(const std::exception& e) {printf("FAIL: %s\n",e.what());return 1;}
}
