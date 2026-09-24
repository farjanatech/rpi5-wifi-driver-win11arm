Set-StrictMode -Version Latest
$ErrorActionPreference='Stop'
$root=Split-Path -Parent $PSScriptRoot
$baseline='6e652fb86aef595cf6f6fbf6f05c1769d5673055'
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
$allowed=@('src/driver/driver.c','src/driver/driver.h','src/cyw43455/network.c')
$new=@('src/cyw43455/tx_pressure_gate.h','src/cyw43455/tx_pressure_pump.h')
$paths=@(& git -C $root ls-tree -r --name-only $baseline -- src utility connector installer diagnostics scripts/fetch-firmware.ps1 rpi5-cyw43455.vcxproj)
if($LASTEXITCODE -ne 0 -or !$paths.Count){throw 'Cannot enumerate protected files.'}
foreach($path in $paths) {
    if($path -in $allowed){continue}
    if($path -eq 'installer/Install-RPi5-WiFi-Driver.ps1') {
        $actual=(Get-PerformanceSource $path).Replace("'0.7.0.1'","'0.7.0'")
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
# Alpha.1 is the user-reported best build. Its entire network implementation
# remains identical outside the two includes and ONE post-RX pump call.
$network=Get-PerformanceSource 'src/cyw43455/network.c'
$original=Get-PerformanceSource 'src/cyw43455/network.c' -Original
$network=$network.Replace("#include `"tx_pressure_gate.h`"`n#include `"tx_pressure_pump.h`"`n`n",'')
$network=$network.Replace('CywTxPostReceivePump(A,&N->Sends,&sentAfter)',
    'CywMeasuredTxPump(A,&N->Sends,4,&sentAfter)')
Assert-PerformanceEqual $network $original 'network except bounded post-RX pressure pump'
$header=Get-PerformanceSource 'src/driver/driver.h'
$header=$header.Replace("    ULONG TxPressurePasses, TxPressureFrames, TxPressureDeadlineYields;`n",'')
Assert-PerformanceEqual $header (Get-PerformanceSource 'src/driver/driver.h' -Original) 'adapter except pressure counters'
$driver=(Get-PerformanceSource 'src/driver/driver.c').Replace('SET_DWORD(L"DiagVersion", 31);','SET_DWORD(L"DiagVersion", 30);')
foreach($counter in @('TxPressurePasses','TxPressureFrames','TxPressureDeadlineYields')) {
    $driver=$driver.Replace("    SET_DWORD(L`"$counter`", Adapter->$counter);`n",'')
}
Assert-PerformanceEqual $driver (Get-PerformanceSource 'src/driver/driver.c' -Original) 'driver except diagnostic publication'
$inf=Get-PerformanceSource 'package/rpi5cyw.inf'
if($inf -notmatch '09/24/2026,0\.7\.0\.1'){throw 'Performance version missing.'}
Assert-PerformanceEqual ($inf.Replace('09/24/2026,0.7.0.1','09/24/2026,0.7.0.0')) (Get-PerformanceSource 'package/rpi5cyw.inf' -Original) 'INF except version'
Write-Output 'PASS: alpha.1 protected; only bounded post-RX pressure scheduling and counters added. SDIO, aggregation, queue ownership/cap, retry, firmware, authentication, country, band, connector, utilities and installer safeguards unchanged.'
