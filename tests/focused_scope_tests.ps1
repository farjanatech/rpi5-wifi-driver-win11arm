Set-StrictMode -Version Latest
$ErrorActionPreference='Stop'
$baseline='c0a22eb8a572ae6ee678fa6835e98c37ba750917'
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
$changed=@(& git -C $root diff --name-only $baseline HEAD -- src)
if ($LASTEXITCODE -ne 0) { throw 'Cannot compare source scope.' }
$allowed=@('src/cyw43455/network.c','src/cyw43455/radio.h','src/cyw43455/tx_types.h','src/driver/driver.c','src/driver/driver.h')
foreach($file in $changed) { if($file -notin $allowed) { throw "Unexpected driver-source change versus exp0.6.16: $file" } }
foreach($file in @('src/cyw43455/tx_queue.h','src/cyw43455/tx_dispatch.h','src/cyw43455/control.h','src/cyw43455/connection.h')) {
    if((Get-ScopeSource $file) -cne (Get-ScopeSource $file -BaselineSource)){throw "Ownership/transport/connection changed: $file"}
}
if((ConvertTo-ScopeToken (Get-ScopeSource 'src/cyw43455/tx_types.h')) -cne (ConvertTo-ScopeToken (Get-ScopeSource 'src/cyw43455/tx_types.h' -BaselineSource))){throw 'Queue layout/expiry changed.'}
$network=Get-ScopeSource 'src/cyw43455/network.c'
$oldNetwork=Get-ScopeSource 'src/cyw43455/network.c' -BaselineSource
foreach($name in @('CywPoll','CywSendFrame','CywReceive','CywConfigure','CywNetworkSend','CywWorker')) {
    $pattern='(?ms)^(?:static )?(?:NTSTATUS|VOID|NDIS_STATUS) '+[regex]::Escape($name)+'\([^\n]*\)\n\{.*?^\}'
    $old=[regex]::Match($oldNetwork,$pattern).Value
    $current=[regex]::Match($network,$pattern).Value
    if(!$old -or !$current){throw "Missing function: $name"}
    if($name -eq 'CywWorker'){$current=$current.Replace('if(op==3)CywRadioRequest(A);'+"`n"+'        else if(op) {','if(op) {')}
    if($current -cne $old){throw "Scheduling/transport changed: $name"}
}
$expected=Get-ScopeSource 'src/driver/driver.h' -BaselineSource
$actual=(Get-ScopeSource 'src/driver/driver.h') -replace '(?m)^\s*ULONG RadioReport\[20\];[^\n]*\n',''
if((ConvertTo-ScopeToken $actual) -cne (ConvertTo-ScopeToken $expected)){throw 'Unexpected adapter/cap change.'}
$expected=(Get-ScopeSource 'src/driver/driver.c' -BaselineSource).Replace('SET_DWORD(L"DiagVersion", 16);','SET_DWORD(L"DiagVersion", 18);').Replace('Data.Ulong = 0x00060010;','Data.Ulong = 0x00060012;')
$actual=(Get-ScopeSource 'src/driver/driver.c') -replace '(?m)^\s*SET_DWORD\(L"Radio\w+", Adapter->RadioReport\[\d+\]\);\n',''
if((ConvertTo-ScopeToken $actual) -cne (ConvertTo-ScopeToken $expected)){throw 'Unexpected driver-core behavior.'}
$radio=Get-ScopeSource 'src/cyw43455/radio.h'
if($radio -match '\bTRUE\b|CywCmdInt|CywSendFrame|CywTxPump'){throw 'Radio report must only issue GETs.'}
if(([regex]::Matches($network,'CywRadioRequest\(A\)')).Count -ne 1 -or $network -notmatch 'if\(op==3\)CywRadioRequest\(A\)'){throw 'Radio query must be explicit request only.'}
# Also prove this is a cap-only rollback from .17, not another scheduling,
# connection, radio or measurement experiment.
$baseline='df03fc1587d086b886836737398ce7113e6612fb'
$changed=@(& git -C $root diff --name-only $baseline HEAD -- src)
if ($LASTEXITCODE -ne 0) { throw 'Cannot compare .17 rollback scope.' }
foreach($file in $changed) {
    if($file -notin @('src/driver/driver.h','src/driver/driver.c','src/cyw43455/tx_types.h')) { throw "Unexpected runtime change versus .17: $file" }
}
foreach($file in @('src/cyw43455/network.c','src/cyw43455/radio.h','utility/Get-RPi5-WiFi-Radio.ps1','utility/Get-RPi5-WiFi-Radio.cmd','utility/Test-RPi5-WiFi-Performance.ps1','utility/Measure-RPi5-WiFi-Load.ps1','utility/Connect-RPi5-WiFi.ps1','utility/Set-RPi5-WiFi-Autoconnect.ps1')) {
    if((Get-ScopeSource $file) -cne (Get-ScopeSource $file -BaselineSource)){throw "Retained .17 code changed: $file"}
}
Write-Output 'PASS: exp0.6.16 64-frame cap/scheduling/ownership restored; .17 radio/connection/performance code unchanged.'
