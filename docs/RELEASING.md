# Building and packaging a preview

Run `scripts/build.ps1` using Visual Studio C++ tools and CMake. Do not publish a copy of a user's working installation.

`scripts/package.ps1` takes a host build directory, a locally compiled OpenNR add-on directory, and a vendor directory holding reviewed unmodified dependencies. It uses a fixed allowlist and refuses to package neural runtime/model files, personal logs, screenshots, recovery folders, Microsoft system DLLs, or unrelated vendor libraries.

Reviewed vendor inputs for this preview:

- ReShade `dxgi.dll`: 6.8.0.2155 x64, full add-on support.
- Aeon SR 1.3.2 `AeonSR.addon64`, `AeonSR.addonfx`, `AeonSRHost.exe` from its official release.
- `runtime/amd_fidelityfx_loader_dx12.dll`, `amd_fidelityfx_upscaler_dx12.dll`, `amd_fidelityfx_framegeneration_dx12.dll`, unmodified signed AMD SDK files supplied with that release.
- Corresponding licenses copied from official notices and retained under `Licenses/`.

Obtain vendor releases from their official sources listed in `THIRD-PARTY.md`. No AMD neural runtime/model or extracted weights belong in the Git repository, build artifacts or release assets.

Example:

```powershell
scripts/package.ps1 -BuildDirectory build -AddonDirectory OpenNR-Multipass-source/build-nr -VendorDirectory path/to/reviewed/vendor-files
```

Before publishing:

1. Inspect the source diff and third-party notices; scan for personal paths, credentials and unwanted binary blobs.
2. Build the clean host and add-ons. Run the isolated `neural_detail_smoke` check on a GPU-capable Windows machine. This check needs no neural runtime.
3. Verify `Setup-Runtime.ps1 -SourceFolder <compatible installation> -VerifyOnly` against the tested pair and ensure incompatible files are rejected.
4. Inspect the ZIP and SHA-256 manifest. Test gameplay separately; do not represent shader fixture checks as gameplay FPS benchmarks.
5. Publish an explicitly marked prerelease, with the runtime import requirement prominent in its notes.

The first preview was compiled from the clean public source tree. Its detail-shader check ran locally on an RX 7900 XTX. The original working app has separate settings and is not overwritten by public release preparation.
