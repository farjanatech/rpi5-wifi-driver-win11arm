Set-StrictMode -Version Latest
$ErrorActionPreference='Stop'
$root=Split-Path -Parent $PSScriptRoot
$baseline='4f8f456b1b72d7f6b534531b02d83863ae9ed60c'
function Get-PerformanceSource([string]$Path,[switch]$Original) {
    if($Original) {
        $lines=@(& git -C $root show ($baseline+':'+$Path))
        if($LASTEXITCODE -ne 0){throw "Cannot read protected baseline: $Path"}
        return ($lines -join "`n").TrimEnd()
    }
    return (Get-Content -LiteralPath (Join-Path $root $Path) -Raw).Replace("`r`n","`n").TrimEnd()
}
function Assert-PerformanceEqual([string]$Actual,[string]$Expected,[string]$Label) {
    if($Actual -cne $Expected){throw "Protected baseline changed: $Label"}
}
$allowed=@('src/driver/driver.c','src/driver/driver.h','src/sdio/sdio.c','src/sdio/sdio.h',
    'src/cyw43455/network.c','src/cyw43455/transport_poll.h','src/cyw43455/transport_send.h')
$new=@('src/sdio/fifo_blocks.h','src/cyw43455/rx_performance.h','src/cyw43455/rx_config.h')
$paths=@(& git -C $root ls-tree -r --name-only $baseline -- src utility connector installer diagnostics scripts/fetch-firmware.ps1 rpi5-cyw43455.vcxproj)
if($LASTEXITCODE -ne 0 -or !$paths.Count){throw 'Cannot enumerate protected files.'}
foreach($path in $paths) {
    if($path -in $allowed){continue}
    if($path -eq 'installer/Install-RPi5-WiFi-Driver.ps1') {
        $actual=(Get-PerformanceSource $path).Replace("'0.7.0'","'0.6.29.1'")
        Assert-PerformanceEqual $actual (Get-PerformanceSource $path -Original) $path
    } else {
        & git -C $root diff --quiet $baseline -- $path
        if($LASTEXITCODE -ne 0){throw "Protected baseline changed: $path"}
    }
}
foreach($file in Get-ChildItem -LiteralPath (Join-Path $root 'src') -Recurse -File) {
    $relative=$file.FullName.Substring($root.Length+1).Replace('\','/')
    if($relative -notin $paths -and $relative -notin $new){throw "Unexpected driver source: $relative"}
}
# The worker itself is byte-identical outside the two explicit startup lines.
# No larger queues, altered TX/RX budgets, retry policy or authentication.
$network=Get-PerformanceSource 'src/cyw43455/network.c'
$original=Get-PerformanceSource 'src/cyw43455/network.c' -Original
$network=$network.Replace("    N->RxNextLength=0;RtlZeroMemory(&N->RxGlom,sizeof(N->RxGlom));`n",'')
$network=$network.Replace("    if(NT_SUCCESS(Status) && !N->Stop)Status=SdioPrepareRuntimeFifo(A);`n",'')
$start=$network.IndexOf('static VOID CywWorker(')
$end=$network.IndexOf('NTSTATUS CywNetworkInitialize(')
$oldStart=$original.IndexOf('static VOID CywWorker(')
$oldEnd=$original.IndexOf('NTSTATUS CywNetworkInitialize(')
if($start -lt 0 -or $end -le $start -or $oldStart -lt 0 -or $oldEnd -le $oldStart){throw 'Worker scope markers missing.'}
Assert-PerformanceEqual $network.Substring($start,$end-$start) $original.Substring($oldStart,$oldEnd-$oldStart) 'worker scheduling/ownership'
$inf=Get-PerformanceSource 'package/rpi5cyw.inf'
if($inf -notmatch '09/24/2026,0\.7\.0\.0'){throw 'Performance version missing.'}
Assert-PerformanceEqual ($inf.Replace('09/24/2026,0.7.0.0','09/23/2026,0.6.29.0')) (Get-PerformanceSource 'package/rpi5cyw.inf' -Original) 'INF except version'
Write-Output 'PASS: isolated transport changes; firmware, authentication, country, band policy, queues, scheduling, connector, utilities and installer safeguards preserved.'
