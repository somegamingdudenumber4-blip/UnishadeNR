param([string]$BuildDirectory = '')
$ErrorActionPreference='Stop'
$root=Split-Path -Parent $PSScriptRoot
if (!$BuildDirectory) { $BuildDirectory=Join-Path $root 'build' }
& cmake -S (Join-Path $root 'host') -B $BuildDirectory -A x64 -DBUILD_TESTING=OFF
if ($LASTEXITCODE -ne 0) { throw 'CMake configure failed' }
& cmake --build $BuildDirectory --config Release --target Unishade --parallel 6
if ($LASTEXITCODE -ne 0) { throw 'Host build failed' }
foreach ($target in @('neural','panelinput')) {
    & powershell.exe -NoProfile -ExecutionPolicy Bypass -File (Join-Path $root 'OpenNR-Multipass-source\build.ps1') -Target $target
    if ($LASTEXITCODE -ne 0) { throw "OpenNR $target build failed" }
}
Write-Output "Built host in $BuildDirectory\Release and add-ons in OpenNR-Multipass-source\build-nr."
