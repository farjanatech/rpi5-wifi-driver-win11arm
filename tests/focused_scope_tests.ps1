Set-StrictMode -Version Latest
$ErrorActionPreference='Stop'
$baseline='e8273bfe092ab0493609b815d035bf42287721bb'
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
# Queue ownership, credentials, firmware and bus mode are outside .23's scope.
foreach($file in @('src/cyw43455/tx_queue.h','src/cyw43455/tx_types.h','src/cyw43455/tx_dispatch.h','src/cyw43455/control.h','src/cyw43455/firmware.c')) {
    Assert-SameSource (Get-ScopeSource $file $baseline) (Get-ScopeSource $file $proven) "$file .16 anchor"
}
foreach($file in @('src/cyw43455/tx_queue.h','src/cyw43455/tx_types.h','src/cyw43455/tx_dispatch.h','src/cyw43455/control.h','src/cyw43455/connection.h','src/cyw43455/join_preference.h','src/cyw43455/firmware.c','utility/Connect-RPi5-WiFi.ps1','utility/Measure-RPi5-WiFi-Load.ps1','utility/Set-RPi5-WiFi-Autoconnect.ps1','utility/WiFi.config.example.json','scripts/fetch-firmware.ps1')) {
    Assert-SameSource (Get-ScopeSource $file) (Get-ScopeSource $file $baseline) $file
}
$sdio=ConvertFrom-TimingInstrumentation (Get-ScopeSource 'src/sdio/sdio.c')
$sdio=$sdio.Replace('SdioSendCommandRaw(', 'SdioSendCommand(').Replace('SdioCmd53TransferRaw(', 'SdioCmd53Transfer(')
Assert-SameSource $sdio (Get-ScopeSource 'src/sdio/sdio.c' $proven) 'Actual SDIO engine'
function Get-ScopeFunction {
    param([string]$Text,[string]$Name)
    # Match complete C function bodies, not substrings of similarly named helpers.
    $match=[regex]::Match($Text,'(?m)^(?:static\s+)?(?:__inline\s+)?[\w*]+\s+'+[regex]::Escape($Name)+'\s*\([^;{]*\)\s*\{')
    if(-not $match.Success){throw "Missing protected function $Name"}
    $depth=1;$position=$match.Index+$match.Length
    while($position -lt $Text.Length -and $depth -gt 0){
        if($Text[$position] -eq '{'){$depth++}
        elseif($Text[$position] -eq '}'){$depth--}
        $position++
    }
    if($depth){throw "Unbalanced protected function $Name"}
    return $Text.Substring($match.Index,$position-$match.Index)
}
$network=ConvertFrom-TimingInstrumentation (Get-ScopeSource 'src/cyw43455/network.c')
$priorNetwork=ConvertFrom-TimingInstrumentation (Get-ScopeSource 'src/cyw43455/network.c' $baseline)
foreach($name in @('CywReceive','CywLink','CywConfigure')) {
    Assert-SameSource (Get-ScopeFunction $network $name) (Get-ScopeFunction $priorNetwork $name) $name
}
$protocol=Get-ScopeSource 'src/cyw43455/network_protocol.h'
foreach($name in @('CywReceiveBudget','CywEthernetBody','CywSdpcmHeader','CywAcceptEthernet','CywValidConnect')){
    Assert-SameSource (Get-ScopeFunction $protocol $name) (Get-ScopeFunction (Get-ScopeSource 'src/cyw43455/network_protocol.h' $proven) $name) $name
}
if($network -notmatch 'CywMeasuredTxPump\(A,&N->Sends,4,&sentBefore\)' -or
   $network -notmatch 'CywMeasuredTxPump\(A,&N->Sends,4,&sentAfter\)' -or
   $network -notmatch 'CywReceiveBudget\(i,KeQueryInterruptTime\(\)-rxStart\)'){
    throw 'Proven TX/RX processing budgets changed.'
}
# Keep the exact sustained workload; diagnostics may run only around it.
$performance=Get-ScopeSource 'utility/Test-RPi5-WiFi-Performance.ps1'
$performanceBefore=Get-ScopeSource 'utility/Test-RPi5-WiFi-Performance.ps1' $baseline
foreach($name in @('Invoke-Rpi5RepeatedDownload','ConvertFrom-Rpi5DownloadResult')){
    $pattern='(?ms)^function '+[regex]::Escape($name)+' \{.*?^\}'
    $actual=[regex]::Match($performance,$pattern)
    $expected=[regex]::Match($performanceBefore,$pattern)
    if(-not $actual.Success -or -not $expected.Success){throw "Missing protected workload $name"}
    Assert-SameSource $actual.Value $expected.Value $name
}
if(Test-Path (Join-Path $root 'src/cyw43455/rx_poll.h')){throw 'Retired read-ahead remains.'}
$header=Get-ScopeSource 'src/driver/driver.h'
if($header -notmatch '#define RPI5CYW_TX_LIMIT 64u'){throw 'Queue limit changed.'}
$driver=Get-ScopeSource 'src/driver/driver.c'
if($driver -notmatch 'SET_DWORD\(L"DiagVersion", 23\)'){throw 'Diagnostic version incorrect.'}
if($driver -notmatch 'case OID_GEN_VENDOR_DRIVER_VERSION:\s*Data.Ulong = 0x00060017;'){throw 'NDIS vendor driver version incorrect.'}
$project=Get-ScopeSource 'rpi5-cyw43455.vcxproj'
$workflow=Get-ScopeSource '.github/workflows/build-arm64-driver.yml'
if($project -notmatch '<Optimization>MaxSpeed</Optimization>' -or
   $project -notmatch '<FavorSizeOrSpeed>Speed</FavorSizeOrSpeed>' -or
   $workflow -notmatch 'Configuration: Release'){
    throw 'Optimized Release build is not configured.'
}
Write-Output 'PASS: proven SDIO/queue/firmware/NDIS receive/budgets and workload unchanged; scoped transport fixes, radio evidence, optimized build and masked timing.'
