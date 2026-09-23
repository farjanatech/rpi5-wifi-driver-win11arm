Set-StrictMode -Version Latest
$ErrorActionPreference='Stop'
$baseline='a5b3748d6dd64b6f5bb1de31cad82a667dd4afeb'
$proven='c0a22eb8a572ae6ee678fa6835e98c37ba750917'
$fastest='e342399205e59513dc46b98b7bfd973e506c9bd9'
$immediate='e81e8193f6c5f9e763e385cbee6911cf107cac7f'
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
function Assert-ExactScopeSource {
    param([string]$Actual,[string]$Expected,[string]$Label)
    # Source loading trims end-of-file whitespace; otherwise permit checkout
    # line endings only, not comments, includes, or code hidden in markers.
    if($Actual.Replace("`r`n","`n") -cne $Expected.Replace("`r`n","`n")) {
        throw "Unexpected exact-source change: $Label"
    }
}
# .27 restores every .25 CYW/SDIO source file, without a batching exemption or
# an unverified edit hidden inside instrumentation markers. .26 utility-only
# readiness changes remain permitted by the credential/workload guards below.
$protectedFiles=@(& git -C $root ls-tree -r --name-only $immediate -- src/cyw43455 src/sdio)
if($LASTEXITCODE -ne 0 -or -not $protectedFiles.Count){throw 'Cannot enumerate .25 packet-path anchor.'}
foreach($file in $protectedFiles) {
    Assert-ExactScopeSource (Get-ScopeSource $file) (Get-ScopeSource $file $immediate) "$file exact .25 packet-path anchor"
}
foreach($directory in @('src/cyw43455','src/sdio')) {
    foreach($entry in Get-ChildItem -LiteralPath (Join-Path $root $directory) -File -Recurse) {
        $relative=$entry.FullName.Substring($root.Length+1).Replace('\','/')
        if($relative -notin $protectedFiles){throw "Unexpected packet-path source: $relative"}
    }
}
function ConvertFrom-TxRetryInstrumentation {
    param([string]$Text)
    $text=[regex]::Replace($Text,'/\* TX-RETRY-BEGIN \*/.*?/\* TX-RETRY-END \*/','',[Text.RegularExpressions.RegexOptions]::Singleline)
    return [regex]::Replace($text,'/\* TX-RETRY-WAIT-BEGIN \*/.*?/\* TX-RETRY-WAIT-END \*/',
        'KeWaitForSingleObject(&N->Wake,Executive,KernelMode,FALSE,&wait);',[Text.RegularExpressions.RegexOptions]::Singleline)
}
Assert-SameSource (ConvertFrom-TxRetryInstrumentation (Get-ScopeSource 'src/cyw43455/network.c')) (Get-ScopeSource 'src/cyw43455/network.c' $fastest) 'Entire .24 worker except bounded idle retry'
# The complete .25 worker/retry files were checked above. This additional
# comparison proves that removing only that exact retry returns the .24 worker.
# Queue ownership, control framing and firmware upload remain the .16 anchor.
foreach($file in @('src/cyw43455/tx_queue.h','src/cyw43455/tx_types.h','src/cyw43455/tx_dispatch.h','src/cyw43455/control.h','src/cyw43455/firmware.c')) {
    Assert-SameSource (Get-ScopeSource $file $baseline) (Get-ScopeSource $file $proven) "$file .16 anchor"
}
foreach($file in @('src/cyw43455/tx_types.h','src/cyw43455/tx_dispatch.h','src/cyw43455/control.h','utility/Measure-RPi5-WiFi-Load.ps1','utility/WiFi.config.example.json','scripts/fetch-firmware.ps1')) {
    Assert-SameSource (Get-ScopeSource $file) (Get-ScopeSource $file $baseline) $file
}
function Get-ScopeFunction {
    param([string]$Text,[string]$Name)
    # Match complete C function bodies, not substrings of similarly named helpers.
    $match=[regex]::Match($Text,'(?m)^(?:static\s+)?(?:__(?:force)?inline\s+)?[\w*]+\s+'+[regex]::Escape($Name)+'\s*\([^;{]*\)\s*\{')
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
function Get-ScopeRegion {
    param([string]$Text,[string]$Pattern,[string]$Label)
    $regions=[regex]::Matches($Text,$Pattern,[Text.RegularExpressions.RegexOptions]::Singleline)
    if($regions.Count -ne 1){throw "Missing or ambiguous protected region: $Label"}
    return $regions[0].Value
}
$queue=Get-ScopeSource 'src/cyw43455/tx_queue.h'
$queueBefore=Get-ScopeSource 'src/cyw43455/tx_queue.h' $immediate
# Compare the entire queue, not selected functions or normalized splices. This
# fixes completion placement inside the pump, whole-NBL retained accounting,
# reentrant admission, cancellation, credit/busy exits and error cleanup to .25.
Assert-SameSource $queue $queueBefore 'Entire .25 immediate-completion TX queue'
Assert-SameSource $queueBefore (Get-ScopeSource 'src/cyw43455/tx_queue.h' $fastest) '.25 queue matches .24 packet-path anchor'
if(Test-Path (Join-Path $root 'src/cyw43455/tx_completion_batch.h')){throw 'Retired completion-batching helper remains.'}
function ConvertTo-ScopeReplacement {
    param([string]$Text,[string]$Pattern,[string]$Replacement,[string]$Label)
    $region=Get-ScopeRegion $Text $Pattern $Label
    return $Text.Replace($region,$Replacement)
}
# .24 may extend the verified-mode guard, but not the byte-transfer engine,
# dividers, phase waits, lengths, reset handling, or existing timing probes.
$sdio=Get-ScopeSource 'src/sdio/sdio.c'
$sdioBefore=Get-ScopeSource 'src/sdio/sdio.c' $baseline
$operatingGuard='OperatingBus =.*?;(?=\s*FastLimit =)'
$sdio=ConvertTo-ScopeReplacement $sdio $operatingGuard (Get-ScopeRegion $sdioBefore $operatingGuard 'old operating guard') 'operating guard'
Assert-SameSource $sdio $sdioBefore 'SDIO engine outside verified-mode guard'
$sdioAnchor=ConvertFrom-TimingInstrumentation $sdioBefore
$sdioAnchor=$sdioAnchor.Replace('SdioSendCommandRaw(', 'SdioSendCommand(').Replace('SdioCmd53TransferRaw(', 'SdioCmd53Transfer(')
Assert-SameSource $sdioAnchor (Get-ScopeSource 'src/sdio/sdio.c' $proven) 'Actual SDIO .16 engine anchor'
# Retain every previous protocol helper verbatim. Only the new eligibility
# helper/rejection constants may be appended; no CMD52/CMD53 argument rewrite.
$protocolBefore=ConvertTo-ScopeToken (Get-ScopeSource 'src/sdio/sdio_protocol.h' $baseline)
$sdioProtocol=ConvertTo-ScopeToken (Get-ScopeSource 'src/sdio/sdio_protocol.h')
if(-not $sdioProtocol.StartsWith($protocolBefore,[StringComparison]::Ordinal)){throw 'SDIO wire/divider helpers changed.'}
$sdioHeader=Get-ScopeSource 'src/sdio/sdio.h'
foreach($pattern in @(
    '#define CYW_SDIO_SPEED_SUPPORTS_HS\s+0x01',
    '#define CYW_SDIO_SPEED_ENABLE_HS\s+0x02',
    '#define CYW_SDIO_HIGH_SPEED_CLOCK_KHZ\s+50000UL',
    '#define SDHCI_CAP_HIGH_SPEED\s+0x00200000UL',
    'NTSTATUS SdioRestoreDefaultOperatingBus\(PRPI5CYW_ADAPTER Adapter\);')){
    $sdioHeader=ConvertTo-ScopeReplacement $sdioHeader $pattern '' 'high-speed header extension'
}
Assert-SameSource $sdioHeader (Get-ScopeSource 'src/sdio/sdio.h' $baseline) 'SDIO existing definitions'
$bus=Get-ScopeSource 'src/sdio/bus_mode.h'
$busBefore=Get-ScopeSource 'src/sdio/bus_mode.h' $baseline
foreach($name in @('SdioRestoreDefaultOperatingBus','SdioSetHighSpeedBus')){
    $bus=$bus.Replace((Get-ScopeFunction $bus $name),'')
}
$defaultBus=Get-ScopeFunction $bus 'SdioSetDefaultBus'
$bus=$bus.Replace($defaultBus,$defaultBus.Replace('A->BusHighSpeedActive=0;',''))
foreach($pattern in @(
    'A->BusHighSpeedEligible=A->BusHighSpeedAttempted=A->BusHighSpeedActive=0;',
    'A->BusHighSpeedRejectMask=0;',
    'A->BusHighSpeedStatus=\(NTSTATUS\)0xc00000bbL;')){
    $bus=ConvertTo-ScopeReplacement $bus $pattern '' 'high-speed state reset'
}
$upgradeTail='A->BusUpgradeStatus = STATUS_SUCCESS;.*?(?=Failed:)'
$bus=ConvertTo-ScopeReplacement $bus $upgradeTail (Get-ScopeRegion $busBefore $upgradeTail 'old upgrade tail') 'high-speed negotiation extension'
Assert-SameSource $bus $busBefore 'Existing default/identification bus and recovery'
# Firmware upload, RAM verification, security material and stop/cleanup must
# stay unchanged. Only the cache eligibility and post-upload speed verification
# may gain a single conservative high-speed-to-default retry.
$firmware=Get-ScopeSource 'src/cyw43455/firmware.c'
$firmwareBefore=Get-ScopeSource 'src/cyw43455/firmware.c' $baseline
$cacheGuard='BOOLEAN cache=.*?;'
$firmware=ConvertTo-ScopeReplacement $firmware $cacheGuard (Get-ScopeRegion $firmwareBefore $cacheGuard 'old window-cache guard') 'window-cache guard'
$firmware=ConvertTo-ScopeReplacement $firmware 'BOOLEAN busRetried=FALSE;' '' 'single bus retry declaration'
$verification='TRY\(SdioNegotiateOperatingSpeed\(A\)\);.*?A->BusVerifyStatus=STATUS_SUCCESS;A->BusModeStage=6;'
$firmware=ConvertTo-ScopeReplacement $firmware $verification (Get-ScopeRegion $firmwareBefore $verification 'old bus verification') 'bus verification/fallback'
Assert-SameSource $firmware $firmwareBefore 'Firmware outside verified-mode fallback'
# Preserve the entire credential/country/PMK sequence, including error paths
# and zeroization. Normalize only the explicitly added band-selection hold,
# bounded join tail and failed-join publication hold.
$connection=Get-ScopeSource 'src/cyw43455/connection.h'
$connectionBefore=Get-ScopeSource 'src/cyw43455/connection.h' $baseline
$connection=$connection.Replace('#include "band_selection.h"','')
$connection=ConvertTo-ScopeReplacement $connection 'RtlZeroMemory\(A->BandSelection,sizeof\(A->BandSelection\)\);A->BandSelection\[0\]=CYW_BAND_SELECTION_VERSION;\s*A->Network->SelectingBand=TRUE;' '' 'band selection start'
$joinTail='if\(A->JoinPreferenceAccepted\).*?(?=Exit:)'
$oldJoinTail=Get-ScopeRegion $connectionBefore 'STEP\(13,CywFirmwareCommand\(A,26,TRUE,ssid,sizeof\(ssid\)\)\);\s*A->NetworkPhase=520;\s*(?=Exit:)' 'old join tail'
$connection=ConvertTo-ScopeReplacement $connection $joinTail $oldJoinTail 'bounded join tail'
$failedHold='Exit:\s*if\(!NT_SUCCESS\(Status\)\).*?(?=RtlSecureZeroMemory\(pmk,)'
$connection=ConvertTo-ScopeReplacement $connection $failedHold "Exit:`n    " 'failed band publication hold'
Assert-SameSource $connection $connectionBefore 'Country, credentials, PMK and connection cleanup'
$preference=Get-ScopeSource 'src/cyw43455/join_preference.h'
$setter=Get-ScopeFunction $preference 'CywSetJoinPreference'
$setter=$setter.Replace('CywSetJoinPreference(PRPI5CYW_ADAPTER A,BOOLEAN Prefer5)','CywApplyJoinPreference(PRPI5CYW_ADAPTER A)')
$setter=$setter.Replace('UCHAR preference[8];ULONG length=CywBuildJoinPreference(Prefer5,preference);','UCHAR preference[8]={4,2,8,1, 1,2,0,0};')
$setter=$setter.Replace('preference,length);','preference,sizeof(preference));')
Assert-SameSource $setter (Get-ScopeFunction (Get-ScopeSource 'src/cyw43455/join_preference.h' $baseline) 'CywApplyJoinPreference') 'Join preference transport/error handling'
Assert-SameSource (Get-ScopeFunction $preference 'CywApplyJoinPreference') 'static NTSTATUS CywApplyJoinPreference(PRPI5CYW_ADAPTER A){return CywSetJoinPreference(A,TRUE);}' 'Bounded initial preference wrapper'
$network=ConvertFrom-TimingInstrumentation (ConvertFrom-TxRetryInstrumentation (Get-ScopeSource 'src/cyw43455/network.c'))
$priorNetwork=ConvertFrom-TimingInstrumentation (Get-ScopeSource 'src/cyw43455/network.c' $baseline)
$receive=(Get-ScopeFunction $network 'CywReceive').Replace('N->SelectingBand || ','')
$link=(Get-ScopeFunction $network 'CywLink').Replace('if(Up && A->Network->SelectingBand)return;','')
Assert-SameSource $receive (Get-ScopeFunction $priorNetwork 'CywReceive') 'NDIS receive apart from band publication hold'
Assert-SameSource $link (Get-ScopeFunction $priorNetwork 'CywLink') 'NDIS link apart from band publication hold'
Assert-SameSource (Get-ScopeFunction $network 'CywConfigure') (Get-ScopeFunction $priorNetwork 'CywConfigure') 'Firmware configuration'
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
# Connect display summaries may change, never credential validation, key
# derivation/native request serialization, country choice, or secure cleanup.
$connect=Get-ScopeSource 'utility/Connect-RPi5-WiFi.ps1'
$connect=$connect.Replace('Invoke-Rpi5FreshConnectionBoundary -Initial (Get-Rpi5ConnectionReadiness)','')
$connectBefore=Get-ScopeSource 'utility/Connect-RPi5-WiFi.ps1' $baseline
foreach($name in @('Test-Rpi5ConnectionInput','Read-Rpi5WifiConfig','Resolve-Rpi5Country')){
    $pattern='(?m)^function '+[regex]::Escape($name)+' \{.*?^\}'
    Assert-SameSource (Get-ScopeRegion $connect $pattern $name) (Get-ScopeRegion $connectBefore $pattern $name) $name
}
foreach($pattern in @("Add-Type -TypeDefinition @'.*?'@",'\$countryKey = .*?\$secure\.Dispose\(\)')){
    Assert-SameSource (Get-ScopeRegion $connect $pattern 'credential processing') (Get-ScopeRegion $connectBefore $pattern 'old credential processing') 'Connect credential processing'
}
if(Test-Path (Join-Path $root 'src/cyw43455/rx_poll.h')){throw 'Retired read-ahead remains.'}
$header=Get-ScopeSource 'src/driver/driver.h'
if($header -notmatch '#define RPI5CYW_TX_LIMIT 64u'){throw 'Queue limit changed.'}
$driver=Get-ScopeSource 'src/driver/driver.c'
if($driver -notmatch 'SET_DWORD\(L"DiagVersion", 27\)'){throw 'Diagnostic version incorrect.'}
if($driver -notmatch 'case OID_GEN_VENDOR_DRIVER_VERSION:\s*Data.Ulong = 0x0006001b;'){throw 'NDIS vendor driver version incorrect.'}
$project=Get-ScopeSource 'rpi5-cyw43455.vcxproj'
$workflow=Get-ScopeSource '.github/workflows/build-arm64-driver.yml'
if($project -notmatch '<Optimization>MaxSpeed</Optimization>' -or
   $project -notmatch '<FavorSizeOrSpeed>Speed</FavorSizeOrSpeed>' -or
   $workflow -notmatch 'Configuration: Release'){
    throw 'Optimized Release build is not configured.'
}
Write-Output 'PASS: exact .25 immediate-completion queue/worker/retry restored; .24 hardware path and .26 utility readiness scope preserved. Band/HS50/firmware/security/workload unchanged; no completion batching.'
