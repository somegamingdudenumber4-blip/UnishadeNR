param([int]$Samples = 0)
$ErrorActionPreference = 'Stop'
$csv = Join-Path $PSScriptRoot ('telemetry-' + (Get-Date -Format yyyyMMdd-HHmmss) + '.csv')
Write-Host 'Ctrl+C stops. GPU is the busiest engine across the system, not a sum of overlapping engines.'
Write-Host 'FPS/capture age: Home > Status. Neural latency/skips: ReShade > Add-ons > OpenNR.'
Write-Host "Writing telemetry to $csv"
$index=0
while ($Samples -eq 0 -or $index -lt $Samples) {
    $counters=Get-Counter '\GPU Engine(*)\Utilization Percentage','\GPU Process Memory(*)\Dedicated Usage' -SampleInterval 1 -MaxSamples 1 -ErrorAction SilentlyContinue
    $valid=@($counters.CounterSamples | Where-Object Status -eq 0)
    $engines=@($valid | Where-Object Path -like '*utilization percentage')
    $gpu=($engines | Measure-Object CookedValue -Maximum).Maximum
    $processIds=@(Get-Process -Name UnishadeNR,RobloxPlayerBeta -ErrorAction SilentlyContinue | Select-Object -ExpandProperty Id)
    $memory=@($valid | Where-Object { $_.Path -like '*dedicated usage' -and $_.InstanceName -match '^pid_(\d+)_' -and [int]$Matches[1] -in $processIds })
    $mib=($memory | Measure-Object CookedValue -Sum).Sum / 1MB
    $line=[pscustomobject]@{Time=(Get-Date -Format o);BusiestGpuEnginePercent=[math]::Round($gpu,1);HostAndRobloxDedicatedMiB=[math]::Round($mib,1)}
    $line | Export-Csv -LiteralPath $csv -NoTypeInformation -Append
    Write-Host ('{0}  GPU busiest engine {1,5:N1}%   Host+Roblox dedicated {2,8:N1} MiB' -f (Get-Date -Format HH:mm:ss),$gpu,$mib)
    foreach($leaf in @('Unishade.log','amd-nr.log')) {
        $path=Join-Path $PSScriptRoot $leaf
        if(Test-Path -LiteralPath $path) {
            Get-Content -LiteralPath $path -Tail 150 | Where-Object { $_ -match 'Frame timings:|^stats:|^raster:|^frame \d+ processed|^temporal:' } | Select-Object -Last 1 | ForEach-Object {Write-Host $_}
        }
    }
    $index++
}
