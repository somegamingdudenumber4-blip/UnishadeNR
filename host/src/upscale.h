#pragma once
#include <d3d11.h>

void PrepareDetailFrame(ID3D11Texture2D* fullCapture, ID3D11Texture2D* beforeEffects);
void CaptureDetailBeforeOverlay();
void ResetDetailFrame();
// Saves the actual full-resolution composition in detail-preserving mode; false asks ReShade to handle it.
bool SaveDetailScreenshot(const char* label, bool beforeEffects = false);

// Composes retained native detail, FSR 4, or the legacy spatial fallback when effects run below the window's size.
// Shows the result through DirectComposition after effects/menu and before Present.
// Never throws. Present reports a lost device, and other errors fall back to Windows' stretch.
void UpscaleFrame() noexcept;
// Call before releasing the graphics device.
void ReleaseUpscaler();
