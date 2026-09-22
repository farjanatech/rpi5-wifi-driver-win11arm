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
$expected=(Get-ScopeSource 'src/driver/driver.h' -BaselineSource).Replace('#define RPI5CYW_TX_LIMIT 64u','#define RPI5CYW_TX_LIMIT 128u')
$actual=(Get-ScopeSource 'src/driver/driver.h') -replace '(?m)^\s*ULONG RadioReport\[20\];[^\n]*\n',''
if((ConvertTo-ScopeToken $actual) -cne (ConvertTo-ScopeToken $expected)){throw 'Unexpected adapter/cap change.'}
$expected=(Get-ScopeSource 'src/driver/driver.c' -BaselineSource).Replace('SET_DWORD(L"DiagVersion", 16);','SET_DWORD(L"DiagVersion", 17);').Replace('Data.Ulong = 0x00060010;','Data.Ulong = 0x00060011;')
$actual=(Get-ScopeSource 'src/driver/driver.c') -replace '(?m)^\s*SET_DWORD\(L"Radio\w+", Adapter->RadioReport\[\d+\]\);\n',''
if((ConvertTo-ScopeToken $actual) -cne (ConvertTo-ScopeToken $expected)){throw 'Unexpected driver-core behavior.'}
$radio=Get-ScopeSource 'src/cyw43455/radio.h'
if($radio -match '\bTRUE\b|CywCmdInt|CywSendFrame|CywTxPump'){throw 'Radio report must only issue GETs.'}
if(([regex]::Matches($network,'CywRadioRequest\(A\)')).Count -ne 1 -or $network -notmatch 'if\(op==3\)CywRadioRequest\(A\)'){throw 'Radio query must be explicit request only.'}
Write-Output 'PASS: exp0.6.16 worker budgets/transport/ownership preserved; only 128-frame cap plus explicit radio GET hooks and metadata.'
