#pragma once
#include <d3d11.h>
// Returns a GPU-only, full-output-resolution texture. Throws on unsupported or failed FSR 4.
ID3D11Texture2D* Fsr4Upscale(ID3D11Device* device, ID3D11DeviceContext* context,
                           ID3D11Texture2D* source, UINT width, UINT height);
void ReleaseFsr4();
