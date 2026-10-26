# UnishadeNR 0.1.0 preview 1

First public preview of the modified Unishade external host with OpenNR controls integrated directly into its menu.

**Download `UnishadeNR-0.1.0-preview.1-win-x64-portable.zip` below, extract it, and read `README.txt`.** GitHub's automatically generated source archives are for developers.

**Required separately:** a personally obtained compatible OpenNR 0.6.0 runtime/model pair and AMD HIP 7. Run the included `SETUP-RUNTIME.cmd` to import and hash-check your existing files. The separately licensed neural runtime and model are not included and are not automatically downloaded. Newer NeuralHub runtimes are not accepted by this preview.

Included: permanent OpenNR controls, one-to-five neural passes, reduced-Scale source-detail preservation, host processing quality choices, optional AMD FSR/Aeon controls, input adapter and local monitoring. Neural rendering starts disabled at 0.50 Scale and one pass. Experimental frame generation starts off.

This preview was built from the published source. All five isolated 4K detail-shader checks passed; valid dependency hashes were accepted and missing/incompatible dependencies were rejected. The integration has been used locally in Roblox on an RX 7900 XTX. The latest detail-preserving look and other systems still need wider gameplay testing. No public gameplay performance benchmark or RTX performance parity is claimed.

Known limits: expensive multi-pass inference, possible neural artifacts, imperfect external FSR reconstruction, experimental frame generation/pacing and no Roblox engine depth/motion/jitter access. A lower processing resolution does not lower Roblox's own render workload.

Public builds leave other Unishade installations untouched and ask you to close a running stock host. The release excludes personal logs/screenshots/configuration, the neural runtime/model, NVIDIA/Intel libraries, Microsoft system DLLs and community shader packs.

Credits and licenses: see the README and `Licenses/`. This is an unofficial community integration; upstream developers do not endorse it.
