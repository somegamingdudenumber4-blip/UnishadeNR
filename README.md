# UnishadeNR

An experimental Windows app that runs OpenNR neural rendering over captured Roblox frames on AMD GPUs, using a modified Unishade external host.

**Preview:** this is a community integration, not an official NVIDIA, AMD, Roblox, Unishade, or OpenNR release. Neural rendering can be very slow. No RTX 5070 Ti performance claim is made.

## Download and setup

Download the **Windows x64 portable ZIP** from this repository's Releases, extract the entire folder, and read `README.txt` inside it.

**Bring your own compatible runtime/model.** The download does not contain `dlssnr_amd_pass1.dll` or `dlssnr_on_amd_weights.bin`. The AMD runtime author prohibits redistribution; its terms are separate from this repository's MIT source license. This preview currently requires the tested **OpenNR-compatible 0.6.0 pair**, not the newer NeuralHub runtime.

1. Obtain the runtime/model through an existing compatible [OpenNR installation](https://github.com/zmodelerlover/OpenNR-ReShade). Consult the [official AMD runtime project and its terms](https://github.com/danielblnc/DLSS-NR-on-AMD). We do not host or automatically download these files.
2. Run `SETUP-RUNTIME.cmd`, select the folder holding your existing pair, and let it verify the SHA-256 hashes before copying them into this portable app folder.
3. Quit any running stock Unishade copy, then open `UnishadeNR.exe`. Do not put these files in Roblox's installation folder.
4. Open Roblox in windowed or borderless mode. Press **Home → OpenNR → Enabled**. The desktop launcher also has **Open OpenNR controls**.

Windows 11 x64, a compatible AMD GPU, and AMD HIP 7 are required. The local integration was used on an RX 7900 XTX. Other hardware has not been validated by this project. NVIDIA and Intel neural backends are not supported by this preview.

## Included changes

- Permanent OpenNR controls in the host menu and a direct desktop launcher button.
- Native OpenNR add-on with one to five serial neural passes. Starts disabled with one pass and 0.50 Scale.
- 720p, 1080p, 1440p, 1800p and native host processing choices; output follows the captured window size.
- Source-detail preservation for reduced model Scale, including Network output mode. It transfers the neural edit onto captured fine detail rather than showing only a stretched low-resolution model image.
- Optional external AMD FSR reconstruction and experimental Aeon SR frame generation controls. Aeon upscaling and frame generation start off.
- Capture/output FPS and timing display, plus an optional local GPU/VRAM logging script.
- Ctrl+F11 panel input adapter for leaving the native ReShade OpenNR panel visible while playing.

## Settings and limits

For 3840×2160 output, the captured game client must actually be 3840×2160. A maximized window with taskbar or title-bar space may capture fewer pixels.

Start with **Scale 0.50**, **one pass**, and **Keep original source detail on**. Scale 0.50 uses half the width and height: one quarter of the model pixels. Five passes can produce an exaggerated look at a substantial cost; they are not 5× frame generation or a guaranteed FPS increase.

**Network output** under Experimental changes the strength/look. Source detail preservation may temper stylization or expose original edges around strongly transformed objects. Turn preservation off for the original raw model display; reduced Scale can then look soft. This does not reproduce full-resolution inference at reduced cost.

Reducing host processing resolution reduces post-processing work. Roblox still renders at its own resolution. This external capture integration lacks Roblox engine motion vectors, jitter, depth and separated UI, so it does not match native engine FSR quality or performance. 720p reconstruction can look poor. Frame generation remains experimental and may have artifacts or uneven pacing.

To move while the native panel stays visible: **Home → ReShade → OpenNR**, then **Ctrl+F11**. Press Ctrl+F11 again to edit it. The normal host editing menu closes with Home.

## Verification

The integration has been used locally in Roblox. The latest composition change passed isolated 4K GPU checks for identity/detail retention, neural correction transfer, linear-light output and unchanged raw/debug output. See [verification evidence](docs/detail-shader-check.txt).

Those checks are not a public gameplay benchmark. The latest detail-preserving look still needs broader testing. No claim is made that a source build, GPU or driver combination is universally compatible, or that Roblox approves third-party capture overlays.

## Build

Install Visual Studio 2022 C++ tools, Windows SDK, CMake and Git. Run:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/build.ps1
```

The host build downloads pinned, hash-verified headers and libraries. The small Aeon source subset and OpenNR add-on source are vendored with notices. Public builds never terminate another application. Your private dependencies are not needed to compile.

See [release packaging](docs/RELEASING.md) for binary dependencies, checks and release instructions.

## Credits and licensing

This project combines work from:

- [Unishade](https://github.com/OMouta/Unishade), MIT, upstream base `9955868450ee4be353cdd021b85a11ceb2307b29`.
- [OpenNR-ReShade](https://github.com/zmodelerlover/OpenNR-ReShade), MIT, upstream base `8a8144d6bc838f2a6c50d48b4922b3af40b24c6a`.
- [DLSS-NR-on-AMD](https://github.com/danielblnc/DLSS-NR-on-AMD), separately licensed runtime, **not redistributed**.
- Aeon SR 1.3.2, MIT, upstream base `1c4bbc79cf530a818969b608f914b9f7ff4756f8`; its AMD runtime binaries retain their vendor terms.
- ReShade by Patrick Mours and contributors, BSD 3-Clause, plus Dear ImGui, MinHook and the notices retained in the source tree.

See [third-party notices](THIRD-PARTY.md) and `Licenses/`. The MIT license covers this source integration, not every external model or vendor DLL. No affiliation or endorsement is implied.
