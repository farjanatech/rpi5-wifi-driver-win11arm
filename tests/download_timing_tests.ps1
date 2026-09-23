Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$path = Join-Path (Split-Path -Parent $PSScriptRoot) 'utility\RPi5-WiFi-DownloadTiming.ps1'
$tokens = $null; $errors = $null
$ast = [Management.Automation.Language.Parser]::ParseFile($path, [ref]$tokens, [ref]$errors)
if ($errors.Count) { throw ($errors | Out-String) }
if (@($ast.EndBlock.Statements | Where-Object {
    $_ -isnot [Management.Automation.Language.FunctionDefinitionAst]
}).Count) { throw 'Timing helper must contain function definitions only.' }
. $path

function Get-DownloadTimingFixture {
    param([string]$Phase = 'RPI5_PHASE|0.010000|0.030000|0.070000|0.080000|0.100000|0.500000',
        [string]$Outcome = 'Complete', [double]$Total = 0.5, [int]$ExitCode = 0,
        [bool]$TimedOut = $false)
    [pscustomobject]@{
        Sample = [pscustomobject][ordered]@{ Outcome=$Outcome; Http=200; Bytes=1048576L;
            TransferSeconds=$Total; ExitCode=$ExitCode; TimedOut=$TimedOut }
        Result = [pscustomobject]@{ Output="RPI5_METRIC|200|1048576|0.500000`n$Phase";
            ExitCode=$ExitCode; TimedOut=$TimedOut }
    }
}
function Add-CheckedDownloadTiming {
    param($Fixture)
    $original = $Fixture.Sample | ConvertTo-Json -Compress
    $names = @($Fixture.Sample.PSObject.Properties.Name)
    $emitted = @(Add-Rpi5DownloadTimingToSample -Sample $Fixture.Sample -Result $Fixture.Result)
    if ($emitted.Count) { throw 'Decorator emitted data into workload result pipeline.' }
    $preserved = [ordered]@{}
    foreach ($name in $names) { $preserved[$name] = $Fixture.Sample.$name }
    if (([pscustomobject]$preserved | ConvertTo-Json -Compress) -cne $original) {
        throw 'Timing changed an original outcome, byte count or deadline result.'
    }
}
function Assert-DownloadTimingUnknown {
    param($Fixture, [string]$Status)
    Add-CheckedDownloadTiming $Fixture
    if ($Fixture.Sample.TimingStatus -cne $Status) { throw "Wrong unknown timing status: $Status" }
    foreach ($name in @('CumulativeDnsSeconds','CumulativeConnectSeconds','CumulativeTlsSeconds',
        'CumulativePreTransferSeconds','CumulativeFirstByteSeconds','CumulativeTotalSeconds',
        'DnsSeconds','TcpSeconds','TlsSeconds','RequestPreparationSeconds','FirstByteWaitSeconds','BodySeconds')) {
        if ($null -ne $Fixture.Sample.$name) { throw "Unknown timing falsely measured: $name" }
    }
}

$good = Get-DownloadTimingFixture
$good.Result | Add-Member -NotePropertyMembers @{RequestStart100ns=[uint64]100000;
    RequestEnd100ns=[uint64]5100000; ClockKind='QueryInterruptTime100nsSinceBoot'}
Add-CheckedDownloadTiming $good
if ($good.Sample.TimingStatus -ne 'Valid' -or $good.Sample.ClockStatus -ne 'Valid' -or
    $good.Sample.RequestStart100ns -ne 100000 -or $good.Sample.RequestEnd100ns -ne 5100000 -or
    $good.Sample.ClockKind -cne 'QueryInterruptTime100nsSinceBoot') { throw 'Valid timing or clock rejected.' }
$expected = [ordered]@{DnsSeconds=0.01; TcpSeconds=0.02; TlsSeconds=0.04;
    RequestPreparationSeconds=0.01; FirstByteWaitSeconds=0.02; BodySeconds=0.4}
foreach ($name in $expected.Keys) {
    if ([math]::Abs($good.Sample.$name - $expected[$name]) -gt 0.00000001) { throw "Wrong interval: $name" }
}
if ($good.Sample.CumulativeConnectSeconds -ne 0.03 -or $good.Sample.CumulativeTlsSeconds -ne 0.07 -or
    $good.Sample.CumulativeTotalSeconds -ne 0.5) { throw 'Cumulative timings not retained.' }
$columns = @($good.Sample.PSObject.Properties.Name) -join '|'

foreach ($phase in @(
    'RPI5_PHASE',
    'RPI5_PHASE 0.01|0.03|0.07|0.08|0.1|0.5',
    'RPI5_PHASE|NaN|0.03|0.07|0.08|0.1|0.5',
    'RPI5_PHASE|Infinity|0.03|0.07|0.08|0.1|0.5',
    'RPI5_PHASE|-0.01|0.03|0.07|0.08|0.1|0.5',
    'RPI5_PHASE|0,01|0.03|0.07|0.08|0.1|0.5',
    'RPI5_PHASE|1e-2|0.03|0.07|0.08|0.1|0.5',
    'RPI5_PHASE|0.01||0.07|0.08|0.1|0.5',
    'RPI5_PHASE|0.01|0.03|0.07|0.08|0.1',
    'RPI5_PHASE|0.01|0.03|0.07|0.08|0.1|0.5|extra',
    ('RPI5_PHASE|' + ('9' * 400) + '|0.03|0.07|0.08|0.1|0.5'))) {
    $bad = Get-DownloadTimingFixture -Phase $phase
    Assert-DownloadTimingUnknown $bad 'Malformed'
    if ((@($bad.Sample.PSObject.Properties.Name) -join '|') -cne $columns) { throw 'CSV columns changed on invalid data.' }
}
foreach ($phase in @('RPI5_PHASE|0.04|0.03|0.07|0.08|0.1|0.5',
    'RPI5_PHASE|0.01|0.03|0.07|0.08|0.6|0.5')) {
    Assert-DownloadTimingUnknown (Get-DownloadTimingFixture -Phase $phase) 'NonMonotonic'
}
Assert-DownloadTimingUnknown (Get-DownloadTimingFixture -Phase '') 'Missing'
Assert-DownloadTimingUnknown (Get-DownloadTimingFixture -Phase ($good.Result.Output + "`n" + $good.Result.Output)) 'Duplicate'
Assert-DownloadTimingUnknown (Get-DownloadTimingFixture -Phase ($good.Result.Output + "`nRPI5_PHASE")) 'Duplicate'
Assert-DownloadTimingUnknown (Get-DownloadTimingFixture -Total 0.6) 'TotalMismatch'
Assert-DownloadTimingUnknown (Get-DownloadTimingFixture -Total 0 -Phase 'RPI5_PHASE|0|0|0|0|0|0') 'TotalMismatch'
Assert-DownloadTimingUnknown (Get-DownloadTimingFixture -Outcome 'TransportFailed' -ExitCode 28 -TimedOut $true) 'IncompleteRequest'
Assert-DownloadTimingUnknown (Get-DownloadTimingFixture -Outcome 'InvalidResponse') 'IncompleteRequest'
Assert-DownloadTimingUnknown (Get-DownloadTimingFixture -Outcome 'ServerRejected' -ExitCode 22) 'IncompleteRequest'
Assert-DownloadTimingUnknown (Get-DownloadTimingFixture -Outcome 'Complete' -ExitCode 1) 'IncompleteRequest'
Assert-DownloadTimingUnknown (Get-DownloadTimingFixture -Outcome 'Complete' -TimedOut $true) 'IncompleteRequest'
$missing = Get-DownloadTimingFixture
$missing.Result.PSObject.Properties.Remove('Output')
Assert-DownloadTimingUnknown $missing 'Missing'

$oldCulture = [Threading.Thread]::CurrentThread.CurrentCulture
try {
    [Threading.Thread]::CurrentThread.CurrentCulture = [Globalization.CultureInfo]::GetCultureInfo('fr-FR')
    $localized = Get-DownloadTimingFixture
    Add-CheckedDownloadTiming $localized
    if ($localized.Sample.TimingStatus -ne 'Valid' -or $localized.Sample.DnsSeconds -ne 0.01) {
        throw 'Invariant decimal parser used the operating-system locale.'
    }
    $localizedRows = @($localized.Sample | ConvertTo-Csv -NoTypeInformation | ConvertFrom-Csv)
    $localizedSummary = Get-Rpi5DownloadTimingSummary -Samples $localizedRows
    if ($localizedSummary.ValidTimingCount -ne 1 -or
        [math]::Abs($localizedSummary.SumValidPhaseSeconds.BodySeconds - 0.4) -gt 0.00000001) {
        throw 'Active non-English locale changed CSV timing interpretation.'
    }
} finally { [Threading.Thread]::CurrentThread.CurrentCulture = $oldCulture }
$zero = Get-DownloadTimingFixture -Phase 'RPI5_PHASE|0|0|0|0|0.5|0.5'
Add-CheckedDownloadTiming $zero
if ($zero.Sample.TimingStatus -ne 'Valid' -or $zero.Sample.DnsSeconds -ne 0 -or
    $zero.Sample.BodySeconds -ne 0 -or $zero.Sample.FirstByteWaitSeconds -ne 0.5) { throw 'Valid equal/rounded-zero phases rejected.' }
$tolerance = Get-DownloadTimingFixture -Total 0.500001
Add-CheckedDownloadTiming $tolerance
if ($tolerance.Sample.TimingStatus -ne 'Valid') { throw 'Six-decimal tolerance rejected.' }

$noClock = Get-DownloadTimingFixture
$noClock.Result | Add-Member -NotePropertyMembers @{RequestStart100ns=$null;
    RequestEnd100ns=$null; ClockKind='QueryInterruptTime100nsSinceBoot'}
Add-CheckedDownloadTiming $noClock
if ($noClock.Sample.ClockStatus -ne 'Unavailable' -or $null -ne $noClock.Sample.ClockKind -or
    $null -ne $noClock.Sample.RequestStart100ns -or $null -ne $noClock.Sample.RequestEnd100ns) {
    throw 'Missing clock invented timestamp or clock domain.'
}
foreach ($clock in @(
    @{RequestStart100ns=10; RequestEnd100ns=9; ClockKind='QueryInterruptTime100nsSinceBoot'},
    @{RequestStart100ns=10; ClockKind='QueryInterruptTime100nsSinceBoot'},
    @{RequestStart100ns=10; RequestEnd100ns=11; ClockKind='UtcTicks'},
    @{RequestStart100ns='NaN'; RequestEnd100ns=11; ClockKind='QueryInterruptTime100nsSinceBoot'},
    @{RequestStart100ns=-1; RequestEnd100ns=11; ClockKind='QueryInterruptTime100nsSinceBoot'},
    @{RequestStart100ns=1.5; RequestEnd100ns=11; ClockKind='QueryInterruptTime100nsSinceBoot'},
    @{RequestStart100ns='18446744073709551616'; RequestEnd100ns=11; ClockKind='QueryInterruptTime100nsSinceBoot'})) {
    $badClock = Get-DownloadTimingFixture
    $badClock.Result | Add-Member -NotePropertyMembers $clock
    Add-CheckedDownloadTiming $badClock
    if ($badClock.Sample.ClockStatus -ne 'Invalid' -or $badClock.Sample.TimingStatus -ne 'Valid' -or
        $null -ne $badClock.Sample.ClockKind -or $null -ne $badClock.Sample.RequestStart100ns -or
        $null -ne $badClock.Sample.RequestEnd100ns) { throw 'Invalid clock contaminated timing or invented alignment.' }
}

$slow = Get-DownloadTimingFixture -Total 1 -Phase 'RPI5_PHASE|0.01|0.03|0.07|0.08|0.1|1'
Add-CheckedDownloadTiming $slow
$failed = Get-DownloadTimingFixture -Outcome 'TransportFailed' -ExitCode 28 -TimedOut $true -Total 15
Add-CheckedDownloadTiming $failed
$summary = Get-Rpi5DownloadTimingSummary -Samples @($good.Sample, $slow.Sample, $failed.Sample, $missing.Sample)
if ($summary.AttemptCount -ne 4 -or $summary.CompleteRequestCount -ne 3 -or
    $summary.FailedOrInvalidRequestCount -ne 1 -or $summary.ValidTimingCount -ne 2 -or
    $summary.UnknownTimingCount -ne 2 -or $summary.ValidClockCount -ne 1 -or
    $summary.UnknownClockCount -ne 3 -or $summary.SlowAttemptCount -ne 2 -or
    $summary.SlowValidTimingCount -ne 1 -or $summary.SumValidRequestTotalSeconds -ne 1.5 -or
    [math]::Abs($summary.SumValidPhaseSeconds.BodySeconds - 1.3) -gt 0.00000001 -or
    $summary.Attribution -ne 'NotDetermined') { throw 'Summary counts/sums misstate measured attempts or causality.' }
if ($summary.TimingStatusCounts.Valid -ne 2 -or $summary.TimingStatusCounts.Missing -ne 1) {
    throw 'Summary status coverage incorrect.'
}
$empty = Get-Rpi5DownloadTimingSummary -Samples @()
if ($empty.AttemptCount -ne 0 -or $empty.ValidTimingCount -ne 0 -or
    $null -ne $empty.SumValidRequestTotalSeconds -or $null -ne $empty.SumValidPhaseSeconds.BodySeconds) {
    throw 'Empty phase aggregate falsely reported as measured zero.'
}
foreach ($sample in @($failed.Sample, $missing.Sample)) {
    $empty = Get-Rpi5DownloadTimingSummary -Samples @($sample)
    if ($empty.ValidTimingCount -ne 0 -or $null -ne $empty.SumValidRequestTotalSeconds -or
        $null -ne $empty.SumValidPhaseSeconds.BodySeconds) { throw 'Unknown phase aggregate falsely reported as measured zero.' }
}
$roundTrip = @(@($good.Sample, $slow.Sample, $failed.Sample, $missing.Sample) | ConvertTo-Csv -NoTypeInformation | ConvertFrom-Csv)
$csvSummary = Get-Rpi5DownloadTimingSummary -Samples $roundTrip
foreach ($name in @('AttemptCount','CompleteRequestCount','FailedOrInvalidRequestCount','ValidTimingCount',
    'UnknownTimingCount','ValidClockCount','UnknownClockCount','SlowAttemptCount','SlowValidTimingCount','Attribution')) {
    if ($csvSummary.$name -cne $summary.$name) { throw "CSV string round-trip changed summary field: $name" }
}
if ([math]::Abs($csvSummary.SumValidRequestTotalSeconds - $summary.SumValidRequestTotalSeconds) -gt 0.00000001 -or
    ($csvSummary.TimingStatusCounts | ConvertTo-Json -Compress) -cne ($summary.TimingStatusCounts | ConvertTo-Json -Compress)) {
    throw 'CSV string round-trip changed timing coverage or total.'
}
foreach ($name in $expected.Keys) {
    if ([math]::Abs($csvSummary.SumValidPhaseSeconds.$name - $summary.SumValidPhaseSeconds.$name) -gt 0.00000001) {
        throw "CSV string round-trip changed phase sum: $name"
    }
}
$tiny = Get-DownloadTimingFixture -Phase 'RPI5_PHASE|0.000001|0.000002|0.000003|0.000004|0.000005|0.500000'
Add-CheckedDownloadTiming $tiny
$tinyRows = @($tiny.Sample | ConvertTo-Csv -NoTypeInformation | ConvertFrom-Csv)
$tinySummary = Get-Rpi5DownloadTimingSummary -Samples $tinyRows
if ($tinySummary.ValidTimingCount -ne 1 -or
    [math]::Abs($tinySummary.SumValidPhaseSeconds.TcpSeconds - 0.000001) -gt 0.000000001) {
    throw 'Serialized exponent-sized phase interval rejected.'
}
$roundTrip[0].BodySeconds = ''
$invalidSummary = Get-Rpi5DownloadTimingSummary -Samples @($roundTrip[0])
if ($invalidSummary.ValidTimingCount -ne 0 -or $invalidSummary.UnknownTimingCount -ne 1 -or
    $null -ne $invalidSummary.SumValidPhaseSeconds.BodySeconds) { throw 'Empty imported phase falsely counted as measured zero.' }
$roundTrip[0].BodySeconds = '999'
$invalidSummary = Get-Rpi5DownloadTimingSummary -Samples @($roundTrip[0])
if ($invalidSummary.ValidTimingCount -ne 0) { throw 'Inconsistent imported phase sum accepted.' }
Write-Output 'PASS: invariant download phase parsing, ordered complete-only intervals, stable CSV columns, optional same-boot clock, unchanged outcomes and cautious summary.'
