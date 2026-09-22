Set-StrictMode -Version Latest
$ErrorActionPreference='Stop'
$baseline='8b3d5d557150cfadcc3bdb289a528e40157096ff'
$root=Split-Path -Parent $PSScriptRoot
function Get-ScopeSource {
    param([string]$Path,[switch]$BaselineSource)
    if ($BaselineSource) {
        $lines = & git -C $root show "${baseline}:$Path"
        if ($LASTEXITCODE -ne 0) { throw "Cannot read baseline $Path" }
        return ($lines -join "`n").TrimEnd()
    }
    return (Get-Content -LiteralPath (Join-Path $root $Path) -Raw).Replace("`r`n","`n").TrimEnd()
}
function ConvertTo-ScopeToken {
    param([string]$Text)
    return [regex]::Replace([regex]::Replace($Text,'/\*.*?\*/|//[^\r\n]*','',[Text.RegularExpressions.RegexOptions]::Singleline),'\s+','')
}
$changed=@(& git -C $root diff --name-only $baseline -- src)
if ($LASTEXITCODE -ne 0) { throw 'Cannot compare source scope.' }
$allowed=@('src/cyw43455/network.c','src/cyw43455/rx_poll.h','src/driver/driver.c','src/driver/driver.h')
foreach($file in $changed) { if($file -notin $allowed) { throw "Unexpected driver-source change versus exp0.6.20: $file" } }
foreach($file in @('src/sdio/sdio.c','tests/sdio_host_tests.c','src/cyw43455/network_protocol.h','src/cyw43455/tx_queue.h','src/cyw43455/tx_types.h','src/cyw43455/tx_dispatch.h','src/cyw43455/control.h','src/cyw43455/connection.h','src/cyw43455/join_preference.h','src/cyw43455/radio.h','src/cyw43455/firmware.c','utility/Connect-RPi5-WiFi.ps1','utility/Get-RPi5-WiFi-Radio.ps1','utility/Get-RPi5-WiFi-Radio.cmd','utility/Test-RPi5-WiFi-Performance.ps1','utility/Measure-RPi5-WiFi-Load.ps1','utility/Set-RPi5-WiFi-Autoconnect.ps1','utility/WiFi.config.example.json','scripts/fetch-firmware.ps1')) {
    if((Get-ScopeSource $file) -cne (Get-ScopeSource $file -BaselineSource)){throw "Preserved baseline changed: $file"}
}
$expected=Get-ScopeSource 'src/driver/driver.h' -BaselineSource
$actual=(Get-ScopeSource 'src/driver/driver.h') -replace '(?m)^\s*ULONG RxHeaderReads, RxReadAheadAttempts, RxReadAheadFrames;\n','' -replace '(?m)^\s*ULONG RxReadAheadSavedCommands, RxReadAheadMismatch, RxReadAheadHintIgnored;\n',''
if((ConvertTo-ScopeToken $actual) -cne (ConvertTo-ScopeToken $expected)){throw 'Unexpected adapter/cap change.'}
$expected=(Get-ScopeSource 'src/driver/driver.c' -BaselineSource).Replace('SET_DWORD(L"DiagVersion", 20);','SET_DWORD(L"DiagVersion", 21);').Replace('Data.Ulong = 0x00060014;','Data.Ulong = 0x00060015;')
$actual=(Get-ScopeSource 'src/driver/driver.c') -replace '(?m)^\s*SET_DWORD\(L"Rx(?:HeaderReads|ReadAhead\w+)", Adapter->Rx(?:HeaderReads|ReadAhead\w+)\);\n',''
if((ConvertTo-ScopeToken $actual) -cne (ConvertTo-ScopeToken $expected)){throw 'Unexpected driver-core behavior.'}
$expected=(Get-ScopeSource 'src/cyw43455/network.c' -BaselineSource) -replace '(?ms)^/\* STATUS_NO_MORE_ENTRIES.*?^}\n',('#include "rx_poll.h"'+"`n")
$actual=(Get-ScopeSource 'src/cyw43455/network.c') -replace '(?m)^\s*ULONG RxNextLength;[^\n]*\n','' -replace '(?m)^\s*N->RxNextLength=0;\n',''
$actual=$actual.Replace('CywPollFrame(A,&channel,&off,&len,TRUE)','CywPoll(A,&channel,&off,&len)')
if((ConvertTo-ScopeToken $actual) -cne (ConvertTo-ScopeToken $expected)){throw 'Unexpected worker, NDIS ownership, or TX scheduling change.'}
$poll=Get-ScopeSource 'src/cyw43455/rx_poll.h'
if($poll -match 'KeStall|KeDelay|CywSendFrame|CywTxPump|ExAllocate|NdisAllocate|while\s*\('){throw 'Receive poller must not add waits, TX work, allocations or loops.'}
Write-Output 'PASS: .20 transport/queue/scheduler/country/band policy preserved; only bounded RX read-ahead and diagnostics added.'
