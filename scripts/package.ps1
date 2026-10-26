param(
    [Parameter(Mandatory)][string]$BuildDirectory,
    [Parameter(Mandatory)][string]$AddonDirectory,
    [Parameter(Mandatory)][string]$VendorDirectory,
    [string]$Version='0.1.0-preview.1'
)
$ErrorActionPreference='Stop'
$root=Split-Path -Parent $PSScriptRoot
$dist=Join-Path $root 'dist'
$stage=Join-Path $dist ('UnishadeNR-'+$Version+'-win-x64-portable')
if(Test-Path -LiteralPath $stage){throw 'Staging directory already exists; use a new version or inspect the previous build.'}
New-Item -ItemType Directory -Path $stage -Force | Out-Null
Copy-Item -Path (Join-Path $root 'package\*') -Destination $stage -Recurse
Copy-Item -LiteralPath (Join-Path $root 'Licenses') -Destination $stage -Recurse
Copy-Item -LiteralPath (Join-Path $root 'THIRD-PARTY.md'),(Join-Path $root 'LICENSE') -Destination $stage
foreach($file in @('UnishadeNR.exe','UnishadeUi.dll')) {
    Copy-Item -LiteralPath (Join-Path $BuildDirectory ('Release\'+$file)) -Destination $stage
}
foreach($file in @('amd-nr.addon64','amd-nr-panelinput.addon64')) {
    Copy-Item -LiteralPath (Join-Path $AddonDirectory $file) -Destination $stage
}
foreach($file in @('dxgi.dll','AeonSR.addon64','AeonSR.addonfx','AeonSRHost.exe')) {
    Copy-Item -LiteralPath (Join-Path $VendorDirectory $file) -Destination $stage
}
New-Item -ItemType Directory -Path (Join-Path $stage 'runtime'),(Join-Path $stage 'screenshots'),(Join-Path $stage 'reshade-shaders\Shaders'),(Join-Path $stage 'reshade-shaders\Textures') -Force | Out-Null
foreach($file in @('amd_fidelityfx_loader_dx12.dll','amd_fidelityfx_upscaler_dx12.dll','amd_fidelityfx_framegeneration_dx12.dll')) {
    $source=Join-Path $VendorDirectory ('runtime\'+$file)
    $signature=Get-AuthenticodeSignature -LiteralPath $source
    if($signature.Status -ne 'Valid' -or $signature.SignerCertificate.Subject -notmatch 'Advanced Micro Devices') {
        throw "Vendor signature check failed: $file"
    }
    Copy-Item -LiteralPath $source -Destination (Join-Path $stage 'runtime')
}
$forbidden=@('dlssnr_amd_pass1.dll','dlssnr_on_amd_weights.bin','nvngx_dlss.dll','nvngx_dlssg.dll','D3D12Core.dll','fsr12p.dll')
$files=@(Get-ChildItem -LiteralPath $stage -Recurse -File)
foreach($file in $files) {
    if($file.Name -in $forbidden -or $file.Extension -in @('.log','.pdb','.bin') -or $file.FullName -match '[\\/]Recovery[\\/]') {
        throw ('Forbidden public asset: '+$file.Name)
    }
}
$manifest=foreach($file in $files) {
    $relative=$file.FullName.Substring($stage.Length+1).Replace('\','/')
    (Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash.ToLowerInvariant()+'  '+$relative
}
[IO.File]::WriteAllLines((Join-Path $stage 'SHA256SUMS.txt'),$manifest,[Text.UTF8Encoding]::new($false))
$zip=$stage+'.zip'
Compress-Archive -LiteralPath $stage -DestinationPath $zip
$hash=(Get-FileHash -LiteralPath $zip -Algorithm SHA256).Hash.ToLowerInvariant()
[IO.File]::WriteAllText((Join-Path $dist 'SHA256SUMS.txt'),$hash+'  '+[IO.Path]::GetFileName($zip)+[Environment]::NewLine,[Text.UTF8Encoding]::new($false))
Write-Output $zip
