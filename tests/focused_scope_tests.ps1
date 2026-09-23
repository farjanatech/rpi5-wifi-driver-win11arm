Set-StrictMode -Version Latest
$ErrorActionPreference='Stop'
$baseline='a5b3748d6dd64b6f5bb1de31cad82a667dd4afeb'
$proven='c0a22eb8a572ae6ee678fa6835e98c37ba750917'
$fastest='e342399205e59513dc46b98b7bfd973e506c9bd9'
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
# .26 additionally batches already-transferred send completions. Protect all
# SDIO/transport/band/security files against tested .24; the worker and .25 idle
# retry remain identical. Queue admission/lifecycle functions are checked below.
$protectedFiles=@(& git -C $root ls-tree -r --name-only $fastest -- src/cyw43455 src/sdio)
if($LASTEXITCODE -ne 0 -or -not $protectedFiles.Count){throw 'Cannot enumerate .24 hardware anchor.'}
foreach($file in $protectedFiles) {
    if($file -notin @('src/cyw43455/network.c','src/cyw43455/tx_queue.h')) {
        Assert-SameSource (Get-ScopeSource $file) (Get-ScopeSource $file $fastest) "$file .24 hardware anchor"
    }
}
function ConvertFrom-TxRetryInstrumentation {
    param([string]$Text)
    $text=[regex]::Replace($Text,'/\* TX-RETRY-BEGIN \*/.*?/\* TX-RETRY-END \*/','',[Text.RegularExpressions.RegexOptions]::Singleline)
    return [regex]::Replace($text,'/\* TX-RETRY-WAIT-BEGIN \*/.*?/\* TX-RETRY-WAIT-END \*/',
        'KeWaitForSingleObject(&N->Wake,Executive,KernelMode,FALSE,&wait);',[Text.RegularExpressions.RegexOptions]::Singleline)
}
Assert-SameSource (ConvertFrom-TxRetryInstrumentation (Get-ScopeSource 'src/cyw43455/network.c')) (Get-ScopeSource 'src/cyw43455/network.c' $fastest) 'Entire .24 worker except bounded idle retry'
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
$queueBefore=Get-ScopeSource 'src/cyw43455/tx_queue.h' $fastest
foreach($name in @('CywTxSetGate','CywTxOutstanding','CywTxSubmit','CywTxCancel','CywTxComplete','CywTxAbortStatus','CywTxFlush')) {
    Assert-SameSource (Get-ScopeFunction $queue $name) (Get-ScopeFunction $queueBefore $name) "$name .24 queue ownership/admission anchor"
}
function ConvertTo-ScopeReplacement {
    param([string]$Text,[string]$Pattern,[string]$Replacement,[string]$Label)
    $region=Get-ScopeRegion $Text $Pattern $Label
    return $Text.Replace($region,$Replacement)
}
function ConvertTo-ScopeLiteralReplacement {
    param([string]$Text,[string]$Before,[string]$After,[string]$Label)
    # Input is already comment/whitespace-normalized. Every approved splice
    # must occur exactly once; no broad regex may hide packet-path changes.
    return ConvertTo-ScopeReplacement $Text ([regex]::Escape((ConvertTo-ScopeToken $Before))) (ConvertTo-ScopeToken $After) $Label
}
# The .26 queue exception is narrowly bounded: optional charge retention,
# worker-local SUCCESS staging, checked age/size flushes and a common flush
# exit. Prove the complete remaining TX pump is still the .24 implementation.
$detachFunction=Get-ScopeFunction $queue 'CywTxDetach'
$removeWrapper=Get-ScopeFunction $queue 'CywTxRemove'
$oldRemove=Get-ScopeFunction $queueBefore 'CywTxRemove'
$detach=ConvertTo-ScopeToken $detachFunction
$detach=ConvertTo-ScopeLiteralReplacement $detach `
    'CywTxDetach(PRPI5CYW_ADAPTER A,CYW_TX_STATE *Q,ULONG Index,NDIS_STATUS Status,BOOLEAN HoldCharge)' `
    'CywTxRemove(PRPI5CYW_ADAPTER A,CYW_TX_STATE *Q,ULONG Index,NDIS_STATUS Status)' 'retained-charge signature'
$detach=ConvertTo-ScopeLiteralReplacement $detach `
    'if(!HoldCharge){Q->Frames-=Q->Entries[Index].HeldFrames;Q->Bytes-=Q->Entries[Index].Bytes;}' `
    'Q->Frames-=Q->Entries[Index].HeldFrames;Q->Bytes-=Q->Entries[Index].Bytes;' 'retained-charge branch'
Assert-SameSource $detach $oldRemove 'Detach metadata, drops and charge arithmetic outside optional retention'
Assert-SameSource $removeWrapper 'static PNET_BUFFER_LIST CywTxRemove(PRPI5CYW_ADAPTER A,CYW_TX_STATE *Q,ULONG Index,NDIS_STATUS Status){return CywTxDetach(A,Q,Index,Status,FALSE);}' 'Immediate removal always releases its charge'

$pumpFunction=Get-ScopeFunction $queue 'CywTxPump'
$oldPump=Get-ScopeFunction $queueBefore 'CywTxPump'
$pump=ConvertTo-ScopeToken $pumpFunction
$pump=ConvertTo-ScopeLiteralReplacement $pump 'NTSTATUS status,result=STATUS_SUCCESS;' 'NTSTATUS status;' 'common flush status local'
$pump=ConvertTo-ScopeLiteralReplacement $pump 'CYW_TX_COMPLETION_BATCH batch={0};' '' 'worker-local zeroed batch'
$pump=ConvertTo-ScopeLiteralReplacement $pump 'while(*Sent<Budget){CywTxBatchFlushIfDue(A,Q,&batch);' 'while(*Sent<Budget){' 'pre-transfer batch deadline boundary'
$successStaging=@'
if(completion!=NDIS_STATUS_SUCCESS)nbl=CywTxRemove(A,Q,0,completion);
else if(!Q->Entries[0].Frames) {
    CywTxBatchAppend(&batch,Q->Entries[0].Nbl,Q->Entries[0].HeldFrames,
        Q->Entries[0].Bytes,KeQueryInterruptTime());
    (void)CywTxDetach(A,Q,0,NDIS_STATUS_SUCCESS,TRUE);
}
'@
$pump=ConvertTo-ScopeLiteralReplacement $pump $successStaging `
    'if(completion!=NDIS_STATUS_SUCCESS || !Q->Entries[0].Frames)nbl=CywTxRemove(A,Q,0,completion);' 'only fully completed SUCCESS NBLs are staged'
$pump=ConvertTo-ScopeLiteralReplacement $pump `
    'if(nbl)CywTxComplete(A,Q,nbl,completion);CywTxBatchFlushIfDue(A,Q,&batch);' `
    'if(nbl)CywTxComplete(A,Q,nbl,completion);' 'failed ownership handoff precedes success callback'
$pump=ConvertTo-ScopeLiteralReplacement $pump 'if(!NT_SUCCESS(status) && data){result=status;break;}' `
    'if(!NT_SUCCESS(status) && data)return status;' 'bus fault routed through common flush'
$pump=ConvertTo-ScopeLiteralReplacement $pump 'CywTxBatchFlush(A,Q,&batch);return result;' 'return STATUS_SUCCESS;' 'all pump exits flush local completions'
Assert-SameSource $pump $oldPump 'Entire TX pump outside exact approved completion-batching splices'

# Also reject unrelated helpers, includes, global state or code outside the
# compared functions. The new completion-only helper has production C tests.
$normalizedQueue=$queue.Replace($detachFunction,$oldRemove).Replace($removeWrapper,'').Replace($pumpFunction,$oldPump)
$normalizedQueue=ConvertTo-ScopeReplacement $normalizedQueue '#include "tx_completion_batch\.h"' '' 'completion-only helper include'
Assert-SameSource $normalizedQueue $queueBefore 'Entire .24 TX queue outside verified completion-batching extension'
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
if($driver -notmatch 'SET_DWORD\(L"DiagVersion", 26\)'){throw 'Diagnostic version incorrect.'}
if($driver -notmatch 'case OID_GEN_VENDOR_DRIVER_VERSION:\s*Data.Ulong = 0x0006001a;'){throw 'NDIS vendor driver version incorrect.'}
$project=Get-ScopeSource 'rpi5-cyw43455.vcxproj'
$workflow=Get-ScopeSource '.github/workflows/build-arm64-driver.yml'
if($project -notmatch '<Optimization>MaxSpeed</Optimization>' -or
   $project -notmatch '<FavorSizeOrSpeed>Speed</FavorSizeOrSpeed>' -or
   $workflow -notmatch 'Configuration: Release'){
    throw 'Optimized Release build is not configured.'
}
Write-Output 'PASS: .24 hardware path, queue admission/lifecycle and worker budgets preserved; bounded completion batching and utility readiness are the .26 scope. Band/HS50/firmware/security/workload unchanged.'
