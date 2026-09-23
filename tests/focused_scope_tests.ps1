Set-StrictMode -Version Latest
$ErrorActionPreference='Stop'
$baseline='a5b3748d6dd64b6f5bb1de31cad82a667dd4afeb'
$proven='c0a22eb8a572ae6ee678fa6835e98c37ba750917'
$fastest='e342399205e59513dc46b98b7bfd973e506c9bd9'
$immediate='e81e8193f6c5f9e763e385cbee6911cf107cac7f'
$utilityAnchor='d0f721e8e90c23dda04c51146c24c27e298ec188'
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
function ConvertTo-ScanScopeReplacement {
    param([string]$Text,[string]$Old,[string]$New,[int]$Count,[string]$Label)
    $spliceMatches=[regex]::Matches($Text,[regex]::Escape($Old))
    if($spliceMatches.Count -ne $Count){throw "Missing or ambiguous exact scan splice: $Label"}
    return $Text.Replace($Old,$New)
}
function ConvertFrom-ScanControlIntegration {
    param([string]$Text)
    $text=$Text.Replace("`r`n","`n")
    # These exact lines are the reviewed disconnected control-plane additions.
    # No wildcard/marker removal can hide a new traffic, credential or SDIO path.
    foreach($line in @(
        '#include "scan_protocol.h"',
        '#define CYW_IOCTL_SCAN_START CTL_CODE(FILE_DEVICE_NETWORK,0x805,METHOD_BUFFERED,FILE_WRITE_DATA)',
        '#define CYW_IOCTL_SCAN_STATUS CTL_CODE(FILE_DEVICE_NETWORK,0x806,METHOD_BUFFERED,FILE_READ_DATA)',
        '#define CYW_IOCTL_SCAN_CANCEL CTL_CODE(FILE_DEVICE_NETWORK,0x807,METHOD_BUFFERED,FILE_WRITE_DATA)',
        '    CYW_SCAN_REPORT ScanReport;',
        '    UCHAR ScanCountry[2];',
        '    BOOLEAN ScanBusy,ControlBusy,ScanComplete,ScanAcceptEvents;',
        '    USHORT ScanSyncId;',
        '    volatile LONG ScanCancel;',
        '    ULONG ScanEventStatus;',
        'static VOID CywScanEvent(PRPI5CYW_ADAPTER A,ULONG Status,PUCHAR Payload,ULONG Length);',
        '#include "scan_control.h"',
        '#include "scan_sequence.h"',
        '        N->ControlBusy=op!=0;',
        '        else if(op==4)CywScanRequest(A);',
        '        if(op) {KeAcquireSpinLock(&N->Lock,&irql);N->ControlBusy=FALSE;KeReleaseSpinLock(&N->Lock,irql);}',
        '    CywScanQuiesce(A);',
        '    N->ScanReport.Version=1;',
        '    if(Paused)CywScanQuiesce(A);',
        '        CywScanReset(A);',
        '    CywScanReset(A);'
    )) {
        $text=ConvertTo-ScanScopeReplacement $text ($line+"`n") '' 1 $line
    }
    $scanEventSplice=@'
    /* Scan events are not association events. Never publish an SSID scan as
     * a link transition or overwrite the existing connection diagnostics. */
    if(type==69) {CywScanEvent(A,status,eth+72,CywBe32(msg+20));return;}
    if(N->ScanBusy)return;
'@
    $text=ConvertTo-ScanScopeReplacement $text ($scanEventSplice.Replace("`r`n","`n")+"`n") '' 1 'escan event routing and scan-only link-event suppression'
    $dispatch=@'
        else if(code==CYW_IOCTL_SCAN_START || code==CYW_IOCTL_SCAN_STATUS || code==CYW_IOCTL_SCAN_CANCEL)
            Status=CywScanControl(A,code,Irp->AssociatedIrp.SystemBuffer,
                Stack->Parameters.DeviceIoControl.InputBufferLength,Stack->Parameters.DeviceIoControl.OutputBufferLength,&bytes);
'@
    $text=ConvertTo-ScanScopeReplacement $text ($dispatch.Replace("`r`n","`n")+"`n") '' 1 'memory-only scan IOCTL dispatch'
    $text=ConvertTo-ScanScopeReplacement $text 'else if(N->Request || N->RadioBusy || N->ScanBusy)Status=STATUS_DEVICE_BUSY;' 'else if(N->Request || N->RadioBusy)Status=STATUS_DEVICE_BUSY;' 2 'block connect/radio requests during an active scan'
    return $text
}
# .28 permits only the exact scan control-plane splices above. Every other
# .25 CYW/SDIO source byte stays protected, including the complete live worker,
# queue, receive dispatch, existing IOCTLs and authentication sequence.
$protectedFiles=@(& git -C $root ls-tree -r --name-only $immediate -- src/cyw43455 src/sdio)
$scanHeaders=@('src/cyw43455/scan_protocol.h','src/cyw43455/scan_sequence.h','src/cyw43455/scan_control.h')
if($LASTEXITCODE -ne 0 -or -not $protectedFiles.Count){throw 'Cannot enumerate .25 packet-path anchor.'}
$networkForScope=ConvertFrom-ScanControlIntegration (Get-ScopeSource 'src/cyw43455/network.c')
foreach($file in $protectedFiles) {
    $actual=if($file -eq 'src/cyw43455/network.c'){$networkForScope}else{Get-ScopeSource $file}
    Assert-ExactScopeSource $actual (Get-ScopeSource $file $immediate) "$file exact .25 traffic anchor outside explicit scan control plane"
}
foreach($directory in @('src/cyw43455','src/sdio')) {
    foreach($entry in Get-ChildItem -LiteralPath (Join-Path $root $directory) -File -Recurse) {
        $relative=$entry.FullName.Substring($root.Length+1).Replace('\','/')
        if($relative -notin $protectedFiles -and $relative -notin $scanHeaders){throw "Unexpected packet-path source: $relative"}
    }
}
# The scan candidate may advance only the two driver release labels here.
# All remaining driver source, build settings and connection behavior retain
# the exact .27 anchor; isolated scan control-plane allowances are separate.
$driverFiles=@(& git -C $root ls-tree -r --name-only $utilityAnchor -- src/driver)
if($LASTEXITCODE -ne 0 -or -not $driverFiles.Count){throw 'Cannot enumerate .27 driver anchor.'}
foreach($file in $driverFiles) {
    $actual=Get-ScopeSource $file
    if($file -eq 'src/driver/driver.c') {
        $actual=$actual.Replace('SET_DWORD(L"DiagVersion", 28);','SET_DWORD(L"DiagVersion", 27);')
        $actual=$actual.Replace('Data.Ulong = 0x0006001c;','Data.Ulong = 0x0006001b;')
    }
    Assert-ExactScopeSource $actual (Get-ScopeSource $file $utilityAnchor) "$file exact .27 anchor outside release labels"
}
foreach($entry in Get-ChildItem -LiteralPath (Join-Path $root 'src/driver') -File -Recurse) {
    $relative=$entry.FullName.Substring($root.Length+1).Replace('\','/')
    if($relative -notin $driverFiles){throw "Unexpected driver source: $relative"}
}
foreach($file in @('rpi5-cyw43455.vcxproj','utility/Connect-RPi5-WiFi.ps1','utility/RPi5-WiFi-Operations.ps1')) {
    Assert-ExactScopeSource (Get-ScopeSource $file) (Get-ScopeSource $file $utilityAnchor) "$file exact .27 utility-only anchor"
}
$inf=Get-ScopeSource 'package/rpi5cyw.inf'
if($inf -notmatch '(?m)^DriverVer\s*=\s*09/23/2026,0\.6\.28\.0\s*$'){throw 'Driver INF version incorrect.'}
Assert-ExactScopeSource ($inf.Replace('09/23/2026,0.6.28.0','09/23/2026,0.6.27.0')) (Get-ScopeSource 'package/rpi5cyw.inf' $utilityAnchor) 'INF unchanged outside .28 release version'
function ConvertFrom-TxRetryInstrumentation {
    param([string]$Text)
    $text=[regex]::Replace($Text,'/\* TX-RETRY-BEGIN \*/.*?/\* TX-RETRY-END \*/','',[Text.RegularExpressions.RegexOptions]::Singleline)
    return [regex]::Replace($text,'/\* TX-RETRY-WAIT-BEGIN \*/.*?/\* TX-RETRY-WAIT-END \*/',
        'KeWaitForSingleObject(&N->Wake,Executive,KernelMode,FALSE,&wait);',[Text.RegularExpressions.RegexOptions]::Singleline)
}
Assert-SameSource (ConvertFrom-TxRetryInstrumentation $networkForScope) (Get-ScopeSource 'src/cyw43455/network.c' $fastest) 'Entire .24 worker except bounded idle retry and exact disconnected scan dispatch'
# The complete .25 worker/retry files were checked above. This additional
# comparison proves that removing only that exact retry returns the .24 worker.
# Queue ownership, control framing and firmware upload remain the .16 anchor.
foreach($file in @('src/cyw43455/tx_queue.h','src/cyw43455/tx_types.h','src/cyw43455/tx_dispatch.h','src/cyw43455/control.h','src/cyw43455/firmware.c')) {
    Assert-SameSource (Get-ScopeSource $file $baseline) (Get-ScopeSource $file $proven) "$file .16 anchor"
}
foreach($file in @('src/cyw43455/tx_types.h','src/cyw43455/tx_dispatch.h','src/cyw43455/control.h','utility/WiFi.config.example.json','scripts/fetch-firmware.ps1')) {
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
# Four exact observation-only sampler splices. Normalize checkout line endings
# only; preserve every original ping, counter read, sleep and lifetime bound.
$sampler=(Get-ScopeSource 'utility/Measure-RPi5-WiFi-Load.ps1').Replace("`r`n","`n")
$clockImport=@'
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'RPi5-WiFi-MeasurementClock.ps1')
'@
$sampler=ConvertTo-ScopeReplacement $sampler ([regex]::Escape($clockImport.Replace("`r`n","`n"))) '$ErrorActionPreference = ''Stop''' 'sampler clock helper import after error policy'
$clockInitialize=@'
if ($env:PROCESSOR_ARCHITECTURE -ne 'ARM64') { throw 'Sampler is for the Raspberry Pi, not the development PC.' }
Initialize-Rpi5MeasurementClock
'@
$samplerGuard='if ($env:PROCESSOR_ARCHITECTURE -ne ''ARM64'') { throw ''Sampler is for the Raspberry Pi, not the development PC.'' }'
$sampler=ConvertTo-ScopeReplacement $sampler ([regex]::Escape($clockInitialize.Replace("`r`n","`n"))) $samplerGuard 'sampler native initialization only after target guard'
$rowStart=@'
        $row = [ordered]@{ SampleStart100ns=(Get-Rpi5MeasurementTimestamp); SampleEnd100ns=$null;
            ClockKind='QueryInterruptTime100nsSinceBoot';
            SampleStartUtc=[datetime]::UtcNow.ToString('o'); SampleEndUtc='';
'@
$oldRowStart='        $row = [ordered]@{ SampleStartUtc=[datetime]::UtcNow.ToString(''o''); SampleEndUtc='''';'
$sampler=ConvertTo-ScopeReplacement $sampler ([regex]::Escape($rowStart.Replace("`r`n","`n"))) $oldRowStart 'sampler adds only boot-clock row metadata'
$rowEnd=@'
        $row.SampleEndUtc=[datetime]::UtcNow.ToString('o'); $row.ElapsedSeconds=$watch.Elapsed.TotalSeconds
        $row.SampleEnd100ns=Get-Rpi5MeasurementTimestamp
'@
$oldRowEnd='        $row.SampleEndUtc=[datetime]::UtcNow.ToString(''o''); $row.ElapsedSeconds=$watch.Elapsed.TotalSeconds'
$sampler=ConvertTo-ScopeReplacement $sampler ([regex]::Escape($rowEnd.Replace("`r`n","`n"))) $oldRowEnd 'sampler observes end timestamp after original elapsed sample'
Assert-ExactScopeSource $sampler (Get-ScopeSource 'utility/Measure-RPi5-WiFi-Load.ps1' $utilityAnchor) 'Entire .27 sampler outside four exact clock observation splices'
Assert-ExactScopeSource (Get-ScopeSource 'utility/Measure-RPi5-WiFi-Load.ps1' $utilityAnchor) (Get-ScopeSource 'utility/Measure-RPi5-WiFi-Load.ps1' $baseline) '.27 sampler retains prior workload anchor'
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
$network=ConvertFrom-TimingInstrumentation (ConvertFrom-TxRetryInstrumentation $networkForScope)
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
# Keep the exact sustained workload. Utility 0.6.27.1 may attach observed curl
# timing to a sample only after the original request end time is captured.
$performance=Get-ScopeSource 'utility/Test-RPi5-WiFi-Performance.ps1'
$performanceBefore=Get-ScopeSource 'utility/Test-RPi5-WiFi-Performance.ps1' $baseline
foreach($name in @('Invoke-Rpi5RepeatedDownload','ConvertFrom-Rpi5DownloadResult')){
    $pattern='(?ms)^function '+[regex]::Escape($name)+' \{.*?^\}'
    $actual=[regex]::Match($performance,$pattern)
    $expected=[regex]::Match($performanceBefore,$pattern)
    if(-not $actual.Success -or -not $expected.Success){throw "Missing protected workload $name"}
    $actualWorkload=$actual.Value
    if($name -eq 'Invoke-Rpi5RepeatedDownload') {
        $endSampleLine='$sample | Add-Member -NotePropertyName EndSeconds -NotePropertyValue ((& $Now) - $start)'
        $observationLine='Add-Rpi5DownloadTimingToSample -Sample $sample -Result $result'
        $observationSplice='(?m)^[ \t]*'+[regex]::Escape($endSampleLine)+'\r?\n[ \t]*'+[regex]::Escape($observationLine)+'[ \t]*\r?$'
        $actualWorkload=ConvertTo-ScopeReplacement $actualWorkload $observationSplice $endSampleLine 'single timing observation after captured request end'
    }
    Assert-SameSource $actualWorkload $expected.Value $name
}
# Preserve subprocess behavior and curl request semantics. Only the caller's
# clock wrapper and write-out fields may differ; compare literal source here
# because comment-stripping must never hide a changed https:// destination.
$utilityPerformance=Get-ScopeSource 'utility/Test-RPi5-WiFi-Performance.ps1' $utilityAnchor
$boundedProcessPattern='(?ms)^function Invoke-Rpi5BoundedProcess \{.*?^\}'
Assert-ExactScopeSource (Get-ScopeRegion $performance $boundedProcessPattern 'bounded curl process') (Get-ScopeRegion $utilityPerformance $boundedProcessPattern 'old bounded curl process') 'Existing subprocess launch, timeout and capture behavior'
$commonPattern='(?m)^[ \t]*\$common = [^\r\n]+\r?$'
Assert-ExactScopeSource (Get-ScopeRegion $performance $commonPattern 'curl common arguments') (Get-ScopeRegion $utilityPerformance $commonPattern 'old curl common arguments') 'Curl common request settings'
$requestPattern='(?m)^[ \t]*\$request = \{\r?\n[ \t]*param\(\$seconds\)\r?\n[^\r\n]+\r?\n[ \t]*\}'
$request=Get-ScopeRegion $performance $requestPattern 'single sequential curl request'
$request=ConvertTo-ScopeReplacement $request 'Invoke-Rpi5TimedDownload' 'Invoke-Rpi5BoundedProcess' 'one timed wrapper around existing curl request'
$oldWriteOut='RPI5_METRIC|%{http_code}|%{size_download}|%{time_total}'
$phaseWriteOut=$oldWriteOut+'\nRPI5_PHASE|%{time_namelookup}|%{time_connect}|%{time_appconnect}|%{time_pretransfer}|%{time_starttransfer}|%{time_total}\n'
$request=ConvertTo-ScopeReplacement $request ([regex]::Escape($phaseWriteOut)) $oldWriteOut 'only append curl phase write-out values'
Assert-ExactScopeSource $request (Get-ScopeRegion $utilityPerformance $requestPattern 'old sequential curl request') 'Curl URL, byte limit, request arguments and timeout unchanged'
$expectedClockWrapper=@'
function Invoke-Rpi5TimedDownload {
    param([string]$File, [string[]]$Arguments, [int]$Seconds)
    # Observe the existing curl invocation, including process start/exit overhead.
    # This is not the exact instant curl starts DNS or receives its first byte.
    $start100ns=Get-Rpi5MeasurementTimestamp
    $result=Invoke-Rpi5BoundedProcess $File $Arguments $Seconds
    $end100ns=Get-Rpi5MeasurementTimestamp
    $result | Add-Member -NotePropertyMembers @{
        RequestStart100ns=$start100ns;RequestEnd100ns=$end100ns;
        ClockKind='QueryInterruptTime100nsSinceBoot'
    }
    return $result
}
'@
Assert-SameSource (Get-ScopeRegion $performance '(?ms)^function Invoke-Rpi5TimedDownload \{.*?^\}' 'read-only curl clock wrapper') $expectedClockWrapper 'Clock wrapper invokes unchanged process once and appends metadata only'
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
if($driver -notmatch 'SET_DWORD\(L"DiagVersion", 28\)'){throw 'Diagnostic version incorrect.'}
if($driver -notmatch 'case OID_GEN_VENDOR_DRIVER_VERSION:\s*Data.Ulong = 0x0006001c;'){throw 'NDIS vendor driver version incorrect.'}
$project=Get-ScopeSource 'rpi5-cyw43455.vcxproj'
$workflow=Get-ScopeSource '.github/workflows/build-arm64-driver.yml'
if($project -notmatch '<Optimization>MaxSpeed</Optimization>' -or
   $project -notmatch '<FavorSizeOrSpeed>Speed</FavorSizeOrSpeed>' -or
   $workflow -notmatch 'Configuration: Release'){
    throw 'Optimized Release build is not configured.'
}
Write-Output 'PASS: exact disconnected scan control-plane splices only; .27 driver/INF/build/connector preserved outside release labels and .25 immediate traffic path retained. Existing workload, limits, sampler, firmware and security remain protected.'
