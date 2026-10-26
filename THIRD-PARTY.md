# Third-party components

The root MIT license does not license every third-party binary or model.

| Component | Source / terms | Distribution here |
|---|---|---|
| Modified Unishade host | https://github.com/OMouta/Unishade, MIT | Source and compiled host; original copyright retained |
| Modified OpenNR add-on | https://github.com/zmodelerlover/OpenNR-ReShade, MIT | Source and compiled add-ons; no neural runtime/model |
| Aeon SR | https://github.com/BarbatosAWLS/AeonSR, MIT | Vendored source subset; optional unmodified add-on and AMD helper in portable preview |
| ReShade 6.8.0 with full add-ons | https://reshade.me and https://github.com/crosire/reshade, BSD 3-Clause | Unmodified x64 `dxgi.dll`; BSD notice included |
| AMD FidelityFX signed binaries | https://github.com/GPUOpen-LibrariesAndSDKs/FidelityFX-SDK/tree/v2.3.0 | Unmodified AMD loader, upscaler and framegeneration DLLs; vendor license included |
| Dear ImGui and MinHook | Notices in the vendored SDK and source | Source notices retained; compiled into applicable components |
| AMD neural runtime and weights | https://github.com/danielblnc/DLSS-NR-on-AMD/blob/master/LICENSE | **Not distributed.** Import a compatible personally obtained copy locally |
| NVIDIA / Intel vendor runtimes | Their respective vendors | **Not distributed** in this portable package |
| Microsoft D3D12 system files and AMD HIP | Windows / AMD driver installation | **Not distributed.** The host resolves required system files locally |
| Community ReShade shader packs | Various authors and terms | **Not distributed.** The neural add-on works without these unrelated effects |

Aeon SR's original third-party notices are retained for attribution. Some components described there are absent from this package. Their presence in a notice is not a claim that they are bundled.

Additional shader-port notices are in `OpenNR-Multipass-source/docs/third-party/`; ReShade/ImGui SDK notices are in `OpenNR-Multipass-source/3rdparty/`.

No third-party author endorses this integration. DLSS, NVIDIA, Radeon, AMD and Roblox are the respective owners' names and marks.
