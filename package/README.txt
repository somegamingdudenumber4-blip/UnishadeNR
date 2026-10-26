UnishadeNR 0.1.0 preview - Windows x64 portable

This is an experimental external OpenNR/Unishade integration for AMD GPUs and Roblox.
It is not an official NVIDIA, AMD, Roblox or upstream Unishade/OpenNR release.

FIRST SETUP
1. Extract the entire ZIP to a writable folder. Keep its files together.
2. Run SETUP-RUNTIME.cmd. Select a folder containing your personally obtained compatible OpenNR 0.6.0 runtime/model pair.
   Both SHA-256 hashes are checked before anything is copied. No download or upload occurs.
3. Quit other Unishade copies. Open UnishadeNR.exe. Nothing is installed into Roblox.
4. Open Roblox windowed/borderless, then Home > OpenNR > Enabled.
   The desktop launcher also has an Open OpenNR controls button.

The neural runtime and model are NOT bundled because their author prohibits redistribution.
Official projects: https://github.com/zmodelerlover/OpenNR-ReShade
                   https://github.com/danielblnc/DLSS-NR-on-AMD
This preview needs the existing 0.6.0 pair. A newer NeuralHub runtime is not a drop-in replacement.
Expected SHA-256:
dlssnr_amd_pass1.dll
430be589020685d03f0ad92194457bafe1a186bf658c8cfe2fb7a0bdb7592cc4
dlssnr_on_amd_weights.bin
6bf8dc931ef3ccffe18c82de26ab374156e7f19539ffcf8eabaa25dca5cf15ab

Windows 11 x64, compatible AMD hardware, and the AMD HIP 7 runtime are required.
An RX 7900 XTX was used locally. Other GPU/driver combinations are not validated here.
Use the upstream projects' official setup guidance for AMD driver/HIP prerequisites.

SETTINGS
Default: model disabled, Scale 0.50, one pass, source-detail preservation on.
Tick Enabled to run it. Five passes can be extremely slow and do not mean 5x frame generation.
Network output under Experimental gives a stronger neural look.
Keep original source detail retains captured fine detail alongside neural changes at reduced model Scale.
It can temper the stylization; disable it to view the raw, softer model image.
Settings > Performance chooses host processing quality: 720p, 1080p, 1440p, 1800p, native.
For true 3840x2160 output, capture an actual 3840x2160 client area.
Lower host/model processing does not lower Roblox's own render workload.
External FSR lacks the engine inputs of a native game integration. 720p output reconstruction may look poor.
Optional ReShade > Aeon SR frame generation is experimental and starts off. No performance parity claim is made.

INPUT / MONITORING
Home closes the host editing menu.
To keep the native model panel visible while moving: Home > ReShade > OpenNR, then Ctrl+F11.
Press Ctrl+F11 again to edit that panel.
Home > Status and Show debug info display capture/submitted FPS and processing timing.
MONITOR.cmd writes a local GPU/VRAM CSV. It does not upload telemetry.

Current limitations: slow high-Scale/multi-pass inference, possible neural artifacts and temporal instability,
experimental frame pacing, no engine motion/depth/jitter integration, and limited hardware testing.
The latest source-detail change passed isolated 4K shader checks; that is not a gameplay benchmark.
Keep the upstream watchdogs enabled. Check the model's actual resolution if its automatic Scale cap engages.

Licenses are in Licenses/. Source repository:
https://github.com/somegamingdudenumber4-blip/UnishadeNR
This portable build does not include NVIDIA/Intel runtimes, Windows system DLLs, or community shader packs.
