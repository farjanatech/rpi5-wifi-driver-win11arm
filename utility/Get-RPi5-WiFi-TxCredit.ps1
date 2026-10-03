[CmdletBinding()]
param(
    [switch]$LibraryOnly,
    [string]$ReadSnapshot,
    [string]$BeforeSnapshot,
    [string]$SaveSnapshot
)
Set-StrictMode -Version Latest
$ErrorActionPreference='Stop'
$script:CywTxCreditFields=@(
    'Version','Bytes','SessionQpc','SnapshotQpc','Frequency','WorkerStart',
    'SchedulingEnabled','DetailedTimingEnabled',
    'PumpCalls','PumpFrames','PumpRequestedFrames','PumpEmptyStarts',
    'PumpNoProgress','PumpBudgetHits','PumpPendingEnds','PumpMaxFrames',
    'QueueEntriesSampleMax','QueueRetainedSampleMax','PumpTicks','PumpMaxTicks',
    'CreditSamples','CreditZero','CreditInvalid','Credit1To4','Credit5To16','Credit17To64',
    'GateLifecycleBlocked','GateCreditZero','GateCreditInvalid','GatePriorityBlocked',
    'F1Calls','F1Errors','F1FlowBusy','F1StatusReads','F1StatusAcks','F1MailboxReads',
    'F1Ticks','F1MaxTicks','F2Calls','F2Errors','F2PayloadBytes','F2PaddedBytes',
    'F2Cmd53Writes','F2Ticks','F2MaxTicks','ClockRegressions',
    'RxBatches','RxWindowChanges','RxCreditReopens','RxWindowGrows',
    'PostRxCalls','PostRxFrames','PostRxMaxFrames','ExtraPasses','ExtraFrames','ExtraDeadlineYields',
    'RxEndToPostPumpTicks','RxEndToPostPumpMaxTicks'
)
function ConvertFrom-CywTxCreditSnapshot {
    param([Parameter(Mandatory=$true)][object]$Registry)
    foreach($name in @('TxCreditV1','TimingV2','WorkerStartCount')) {
        if($null -eq $Registry.PSObject.Properties[$name]){throw "Missing $name; this is not a current experimental snapshot."}
    }
    [byte[]]$bytes=$Registry.TxCreditV1
    [byte[]]$timing=$Registry.TimingV2
    if($bytes.Length -ne 464 -or $timing.Length -ne 568){throw 'Invalid or inactive snapshot sizes.'}
    $values=[ordered]@{}
    for($i=0;$i -lt $script:CywTxCreditFields.Count;$i++) {
        $values[$script:CywTxCreditFields[$i]]=[BitConverter]::ToUInt64($bytes,8*$i)
    }
    $v=[pscustomobject]$values
    if($v.Version -ne 1 -or $v.Bytes -ne 464 -or $v.Frequency -eq 0 -or
       $v.SessionQpc -eq 0 -or $v.SnapshotQpc -lt $v.SessionQpc -or
       $v.SchedulingEnabled -gt 1 -or $v.DetailedTimingEnabled -gt 1){throw 'Invalid TX snapshot header.'}
    if([BitConverter]::ToUInt64($timing,0) -ne 2 -or [BitConverter]::ToUInt64($timing,8) -ne 13 -or
       [BitConverter]::ToUInt64($timing,16) -ne $v.Frequency -or
       [BitConverter]::ToUInt64($timing,24) -ne $v.SessionQpc -or
       [BitConverter]::ToUInt64($timing,32) -gt $v.SnapshotQpc -or
       [uint64]$Registry.WorkerStartCount -ne $v.WorkerStart) {
        throw 'Stale/mixed snapshots (restart, rollback or exporter race). Collect again; do not compare these values.'
    }
    return $v
}
function Get-CywTxCreditReport {
    param([Parameter(Mandatory=$true)][object]$After,[object]$Before)
    $last=ConvertFrom-CywTxCreditSnapshot $After
    $first=$null
    if($null -ne $Before){$first=ConvertFrom-CywTxCreditSnapshot $Before}
    $delta=[ordered]@{};$maxima=[ordered]@{}
    foreach($name in $script:CywTxCreditFields[8..57]) {
        if($name -match 'Max'){$maxima[$name]=$last.$name}
    }
    $reason='A second snapshot is required for interval results.'
    $seconds=$null;$ratio=$null;$f1Mean=$null;$f2Mean=$null;$pumpMean=$null;$gapMean=$null
    if($null -ne $first) {
        foreach($name in @('Version','Frequency','SessionQpc','WorkerStart','SchedulingEnabled','DetailedTimingEnabled')) {
            if($first.$name -ne $last.$name){throw "Incomparable snapshots: $name changed."}
        }
        if($last.SnapshotQpc -le $first.SnapshotQpc){throw 'Snapshot did not advance; periodic exporter data is unchanged.'}
        foreach($name in $script:CywTxCreditFields[8..57]) {
            if($last.$name -lt $first.$name -or $last.$name -eq [uint64]::MaxValue){throw "Counter reset, regression or saturation: $name."}
            if($name -notmatch 'Max'){$delta[$name]=[uint64]($last.$name-$first.$name)}
        }
        $seconds=([double]($last.SnapshotQpc-$first.SnapshotQpc))/$last.Frequency
        if($delta.PumpCalls -gt 0){$ratio=[double]$delta.PumpFrames/$delta.PumpCalls;$pumpMean=1000000.0*$delta.PumpTicks/$last.Frequency/$delta.PumpCalls}
        if($delta.PostRxCalls -gt 0){$gapMean=1000000.0*$delta.RxEndToPostPumpTicks/$last.Frequency/$delta.PostRxCalls}
        if($last.DetailedTimingEnabled) {
            if($delta.F1Calls -gt 0){$f1Mean=1000000.0*$delta.F1Ticks/$last.Frequency/$delta.F1Calls}
            if($delta.F2Calls -gt 0){$f2Mean=1000000.0*$delta.F2Ticks/$last.Frequency/$delta.F2Calls}
        }
        $reason='Interval counters use driver QPC snapshot boundaries, not benchmark start/stop timestamps.'
    }
    [pscustomobject]@{
        Mode=$last.SchedulingEnabled;DetailedTiming=[bool]$last.DetailedTimingEnabled
        IntervalSeconds=$seconds;FramesPerPump=$ratio;PumpMeanUs=$pumpMean
        F1MeanUs=$f1Mean;F2MeanUs=$f2Mean;RxEndToPostPumpMeanUs=$gapMean
        Delta=[pscustomobject]$delta;CumulativeMaxima=[pscustomobject]$maxima
        Cumulative=$last;Notes=$reason
    }
}
if(-not $LibraryOnly) {
    if($ReadSnapshot){$raw=Get-Content -LiteralPath $ReadSnapshot -Raw | ConvertFrom-Json}
    else {$raw=Get-ItemProperty -LiteralPath 'HKLM:\SOFTWARE\Rpi5CywDirectDiag'}
    $before=$null
    if($BeforeSnapshot){$before=Get-Content -LiteralPath $BeforeSnapshot -Raw | ConvertFrom-Json}
    $report=Get-CywTxCreditReport -After $raw -Before $before
    if($SaveSnapshot) {
        # Explicit allowlist: never export credentials or arbitrary registry values.
        [pscustomobject]@{TxCreditV1=$raw.TxCreditV1;TimingV2=$raw.TimingV2;
            WorkerStartCount=$raw.WorkerStartCount;CapturedUtc=[DateTime]::UtcNow.ToString('o')} |
            ConvertTo-Json -Depth 4 | Set-Content -LiteralPath $SaveSnapshot -Encoding UTF8
    }
    $report | ConvertTo-Json -Depth 5
}
