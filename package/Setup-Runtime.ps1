param([string]$SourceFolder = '', [switch]$VerifyOnly)
$ErrorActionPreference = 'Stop'
$expected = @{
    'dlssnr_amd_pass1.dll'='430be589020685d03f0ad92194457bafe1a186bf658c8cfe2fb7a0bdb7592cc4'
    'dlssnr_on_amd_weights.bin'='6bf8dc931ef3ccffe18c82de26ab374156e7f19539ffcf8eabaa25dca5cf15ab'
}
if (!$SourceFolder) {
    Add-Type -AssemblyName System.Windows.Forms
    $dialog = New-Object System.Windows.Forms.FolderBrowserDialog
    $dialog.Description = 'Select your existing OpenNR 0.6.0 runtime/model folder. Files are verified before importing.'
    if ($dialog.ShowDialog() -ne [System.Windows.Forms.DialogResult]::OK) { exit 0 }
    $SourceFolder = $dialog.SelectedPath
    $dialog.Dispose()
}
$source = (Resolve-Path -LiteralPath $SourceFolder).Path
foreach ($name in $expected.Keys) {
    $file = Join-Path $source $name
    if (!(Test-Path -LiteralPath $file -PathType Leaf) -or (Get-FileHash -LiteralPath $file -Algorithm SHA256).Hash -ne $expected[$name]) {
        throw "$name is missing or incompatible. No files were imported. This preview requires the tested OpenNR 0.6.0 pair; see README.txt."
    }
}
if ($VerifyOnly) { Write-Output 'Both runtime/model SHA-256 hashes match. No files copied.'; exit 0 }
foreach ($name in $expected.Keys) {
    $target = Join-Path $PSScriptRoot $name
    if (Test-Path -LiteralPath $target) {
        if ((Get-FileHash -LiteralPath $target -Algorithm SHA256).Hash -ne $expected[$name]) {
            throw "An incompatible $name already exists in the destination. Extract a fresh portable folder first."
        }
    }
}
foreach ($name in $expected.Keys) {
    $target = Join-Path $PSScriptRoot $name
    if (!(Test-Path -LiteralPath $target)) { Copy-Item -LiteralPath (Join-Path $source $name) -Destination $target }
    if ((Get-FileHash -LiteralPath $target -Algorithm SHA256).Hash -ne $expected[$name]) { throw "Import verification failed: $name" }
}
Write-Output 'Runtime/model imported for local use. Open UnishadeNR.exe. Nothing was uploaded or installed into Roblox.'
