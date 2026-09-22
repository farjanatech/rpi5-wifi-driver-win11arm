Set-StrictMode -Version Latest
$ErrorActionPreference='Stop'
$baseline='8b3d5d557150cfadcc3bdb289a528e40157096ff'
$proven='c0a22eb8a572ae6ee678fa6835e98c37ba750917'
$root=Split-Path -Parent $PSScriptRoot
function Get-ScopeSource {
    param([string]$Path,[string]$Revision)
    if ($Revision) {
        $lines = & git -C $root show ($Revision+':'+$Path)
        if ($LASTEXITCODE -ne 0) { throw "Cannot read baseline $Path" }
        return ($lines -join [Environment]::NewLine).TrimEnd()
    }
    return (Get-Content -LiteralPath (Join-Path $root $Path) -Raw).TrimEnd()
}
function ConvertTo-ScopeToken {
    param([string]$Text)
    return [regex]::Replace([regex]::Replace($Text,'/\*.*?\*/|//[^\r\n]*','',[Text.RegularExpressions.RegexOptions]::Singleline),'\s+','')
}
function ConvertFrom-TimingInstrumentation {
    param([string]$Text)
    return [regex]::Replace($Text,'/\* TIMING-BEGIN \*/.*?/\* TIMING-END \*/','',[Text.RegularExpressions.RegexOptions]::Singleline)
}
function Assert-SameSource {
    param([string]$Actual,[string]$Expected,[string]$Label)
    if((ConvertTo-ScopeToken $Actual) -cne (ConvertTo-ScopeToken $Expected)){throw "Unexpected change: $Label"}
}
# .20 retained .16's SDIO, queue and processing budgets; check both anchors.
foreach($file in @('src/sdio/sdio.c','src/cyw43455/tx_queue.h','src/cyw43455/tx_types.h','src/cyw43455/tx_dispatch.h','src/cyw43455/network_protocol.h','src/cyw43455/control.h','src/cyw43455/firmware.c')) {
    Assert-SameSource (Get-ScopeSource $file $baseline) (Get-ScopeSource $file $proven) "$file .16 anchor"
}
foreach($file in @('src/cyw43455/tx_queue.h','src/cyw43455/tx_types.h','src/cyw43455/tx_dispatch.h','src/cyw43455/network_protocol.h','src/cyw43455/control.h','src/cyw43455/connection.h','src/cyw43455/join_preference.h','src/cyw43455/radio.h','src/cyw43455/firmware.c','utility/Connect-RPi5-WiFi.ps1','utility/Get-RPi5-WiFi-Radio.ps1','utility/Measure-RPi5-WiFi-Load.ps1','utility/Set-RPi5-WiFi-Autoconnect.ps1','utility/WiFi.config.example.json','scripts/fetch-firmware.ps1')) {
    Assert-SameSource (Get-ScopeSource $file) (Get-ScopeSource $file $baseline) $file
}
$sdio=ConvertFrom-TimingInstrumentation (Get-ScopeSource 'src/sdio/sdio.c')
$sdio=$sdio.Replace('SdioSendCommandRaw(', 'SdioSendCommand(').Replace('SdioCmd53TransferRaw(', 'SdioCmd53Transfer(')
Assert-SameSource $sdio (Get-ScopeSource 'src/sdio/sdio.c' $proven) 'Actual SDIO engine'
$network=ConvertFrom-TimingInstrumentation (Get-ScopeSource 'src/cyw43455/network.c')
$network=$network.Replace('CywMeasuredTxPump(', 'CywTxPump(').Replace('CywMeasuredDiagnostics(', 'Rpi5CywWriteDiagnostics(')
$network=$network -replace '(?s)(if\(!i && !sentBefore && !sentAfter\))\s*\{\s*(KeWaitForSingleObject\([^;]+;)\s*\}', '$1 $2'
Assert-SameSource $network (Get-ScopeSource 'src/cyw43455/network.c' $baseline) 'Worker/receive/NDIS processing'
if(Test-Path (Join-Path $root 'src/cyw43455/rx_poll.h')){throw 'Retired read-ahead remains.'}
$header=Get-ScopeSource 'src/driver/driver.h'
if($header -notmatch '#define RPI5CYW_TX_LIMIT 64u'){throw 'Queue limit changed.'}
$driver=Get-ScopeSource 'src/driver/driver.c'
if($driver -notmatch 'SET_DWORD\(L"DiagVersion", 22\)'){throw 'Diagnostic version incorrect.'}
Write-Output 'PASS: .16 packet engine/queue/budgets unchanged; .20 radio/join policy retained; only timing surrounds runtime work.'
