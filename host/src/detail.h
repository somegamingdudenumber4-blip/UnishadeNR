#pragma once
#include <d3d11.h>
#include <wrl/client.h>

// Retain the real captured detail, transferring only the change made by low-resolution effects.
// All textures belong to one D3D11 device and are used on the same ordered GPU context.
class DetailRenderer
{
public:
    void Prepare(ID3D11Device* device, ID3D11DeviceContext* context,
                 ID3D11Texture2D* fullCapture, ID3D11Texture2D* beforeEffects);
    void BeforeOverlay(ID3D11DeviceContext* context, ID3D11Texture2D* afterEffects);
    ID3D11Texture2D* Render(ID3D11DeviceContext* context, ID3D11Texture2D* finished, UINT width, UINT height);
    bool Ready() const { return full_ && baseline_; }
    ID3D11Texture2D* Original() const { return full_.Get(); }
    void Release();
private:
    using Texture = Microsoft::WRL::ComPtr<ID3D11Texture2D>;
    using View = Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>;
    void EnsureCopy(ID3D11Texture2D* source, Texture& texture, View& view);
    Microsoft::WRL::ComPtr<ID3D11Device> device_;
    Microsoft::WRL::ComPtr<ID3D11VertexShader> vertex_;
    Microsoft::WRL::ComPtr<ID3D11PixelShader> pixel_;
    Microsoft::WRL::ComPtr<ID3D11SamplerState> sampler_;
    Microsoft::WRL::ComPtr<ID3D11Buffer> constants_;
    Texture full_, baseline_, processed_, finished_, output_;
    View fullView_, baselineView_, processedView_, finishedView_;
    Microsoft::WRL::ComPtr<ID3D11RenderTargetView> outputTarget_;
    bool beforeOverlay_ = false;
};
