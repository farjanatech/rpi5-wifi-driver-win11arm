Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$path = Join-Path (Split-Path -Parent $PSScriptRoot) 'utility\Test-RPi5-WiFi-Performance.ps1'
$tokens=$null; $errors=$null
[void][Management.Automation.Language.Parser]::ParseFile($path,[ref]$tokens,[ref]$errors)
if ($errors.Count) { throw ($errors | Out-String) }
. $path -LibraryOnly
$d=[pscustomobject]@{ DiagVersion=10; BusModeStage=6; BusWidth=4; BusActualKhz=25000; BusVerifyReads=16; BusUpgradeStatus=0; BusVerifyStatus=0 }
if ((Get-Rpi5BusAssessment $d) -notlike 'BUS VERIFIED:*') { throw 'Valid bus rejected.' }
$d.BusWidth=1
if ((Get-Rpi5BusAssessment $d) -notlike 'BUS NOT VERIFIED:*') { throw 'Slow fallback misreported as performance success.' }
if ((Get-Rpi5BusAssessment $null) -notlike '*Restart*') { throw 'Missing runtime not diagnosed.' }
$fast=[pscustomobject]@{DiagVersion=24;BusModeStage=6;BusWidth=4;BusActualKhz=50000;
    BusVerifyReads=16;BusUpgradeStatus=0;BusVerifyStatus=0;BusHighSpeedActive=1;BusHighSpeedStatus=0}
if ((Get-Rpi5BusAssessment $fast) -notlike 'BUS VERIFIED:*') { throw 'Verified high-speed mode rejected.' }
$fast.BusVerifyReads=15
if ((Get-Rpi5BusAssessment $fast) -notlike 'BUS NOT VERIFIED:*') { throw 'Partial high-speed verification accepted.' }
$fast.BusVerifyReads=16;$fast.BusHighSpeedStatus=3221225861L
if ((Get-Rpi5BusAssessment $fast) -notlike 'BUS NOT VERIFIED:*') { throw 'Failed high-speed verification accepted.' }
$fast.BusActualKhz=25000;$fast.BusHighSpeedActive=0
if ((Get-Rpi5BusAssessment $fast) -notlike 'BUS VERIFIED:*') { throw 'Reverified default-timing fallback rejected.' }
$fast.BusActualKhz=50000;$fast.PSObject.Properties.Remove('BusHighSpeedStatus')
if ((Get-Rpi5BusAssessment $fast) -notlike 'BUS NOT VERIFIED:*') { throw 'Unknown high-speed evidence accepted.' }
if (-not (Test-Rpi5ExclusiveRoute @([pscustomobject]@{InterfaceIndex=6}) 6)) { throw 'Correct route rejected.' }
if (Test-Rpi5ExclusiveRoute @([pscustomobject]@{InterfaceIndex=6},[pscustomobject]@{InterfaceIndex=15}) 6) { throw 'Competing route accepted.' }
if (Test-Rpi5ExclusiveRoute @() 6) { throw 'Absent route accepted.' }
$stamp=134344762239466275L
if (Test-Rpi5NewerSnapshot $null $stamp) { throw 'Absent snapshot accepted.' }
if (Test-Rpi5NewerSnapshot ([pscustomobject]@{}) $stamp) { throw 'Missing timestamp accepted.' }
if (Test-Rpi5NewerSnapshot ([pscustomobject]@{SnapshotTimeUtc=$stamp}) $stamp) { throw 'Same end-of-test snapshot accepted.' }
if (Test-Rpi5NewerSnapshot ([pscustomobject]@{SnapshotTimeUtc=$stamp-1}) $stamp) { throw 'Older/mid-test snapshot accepted.' }
if (-not (Test-Rpi5NewerSnapshot ([pscustomobject]@{SnapshotTimeUtc=$stamp+1}) $stamp)) { throw 'New post-test snapshot rejected.' }
$r=Invoke-Rpi5BoundedProcess (Join-Path $PSHOME 'powershell.exe') @('-NoProfile','-Command','Write-Output 123; exit 7') 10
if ($r.ExitCode -ne 7 -or $r.TimedOut -or $r.Output -notmatch '123') { throw 'Native output/exit capture failed.' }
$r=Invoke-Rpi5BoundedProcess (Join-Path $PSHOME 'powershell.exe') @('-NoProfile','-Command','Start-Sleep -Seconds 20') 1
if (-not $r.TimedOut) { throw 'Process deadline failed.' }
$source=Get-Content -LiteralPath $path -Raw
if ($source -match '(?i)Start-Transcript|Set-DnsClient|Disable-NetAdapter|--insecure|bcdedit') { throw 'Unexpected mutation or credential capture.' }
Write-Output 'PASS: performance assessment, competing routes, native output, deadline, no unsafe network edits.'

function Get-DownloadFixture {
    param([string]$Metric='RPI5_METRIC|200|1048576|0.5', [int]$Code=0, [bool]$Timeout=$false)
    [pscustomobject]@{ Output=$Metric; ExitCode=$Code; TimedOut=$Timeout }
}
if ((ConvertFrom-Rpi5DownloadResult (Get-DownloadFixture)).Outcome -ne 'Complete') { throw 'Valid chunk rejected.' }
if ((ConvertFrom-Rpi5DownloadResult (Get-DownloadFixture 'RPI5_METRIC|403|0|0.07' 22)).Outcome -ne 'ServerRejected') { throw '403 misclassified as a driver speed failure.' }
if ((ConvertFrom-Rpi5DownloadResult (Get-DownloadFixture 'RPI5_METRIC|429|0|0.07' 22)).Outcome -ne 'ServerRejected') { throw '429 must not be retried.' }
if ((ConvertFrom-Rpi5DownloadResult (Get-DownloadFixture 'RPI5_METRIC|200|512|15' 28)).Outcome -ne 'TransportFailed') { throw 'Partial timeout accepted.' }
if ((ConvertFrom-Rpi5DownloadResult (Get-DownloadFixture 'RPI5_METRIC|200|512|0.1')).Outcome -ne 'InvalidResponse') { throw 'Short body accepted.' }
if ((ConvertFrom-Rpi5DownloadResult (Get-DownloadFixture 'RPI5_METRIC|302|0|0.1')).Outcome -ne 'InvalidResponse') { throw 'Redirect accepted.' }
if ((ConvertFrom-Rpi5DownloadResult (Get-DownloadFixture 'no metrics')).Outcome -ne 'InvalidResponse') { throw 'Missing metadata accepted.' }

# Simulate the real loop without network, wall-clock delays or driver installation.
$state=@{ Time=0.0; Calls=0; Rows=[Collections.Generic.List[object]]::new(); Outcome='ok' }
$now={ $state.Time }
$request={
    param($seconds)
    $state.Calls++; $state.Time += [math]::Min(2,$seconds)
    if ($state.Outcome -eq '403') { return (Get-DownloadFixture 'RPI5_METRIC|403|0|0.1' 22) }
    if ($state.Outcome -eq 'timeout') { return (Get-DownloadFixture 'RPI5_METRIC|000|0|2' 28 $true) }
    return (Get-DownloadFixture)
}
$observe={ param($sample) $state.Rows.Add($sample) }
$r=Invoke-Rpi5RepeatedDownload -Request $request -Now $now -OnSample $observe -DurationSeconds 90 -MaxRequests 3
if ($r.StopReason -ne 'RequestByteCap' -or $r.Completed -ne 3 -or $r.VerifiedBytes -ne 3145728 -or $r.ElapsedSeconds -ne 6 -or $state.Rows.Count -ne 3 -or [math]::Abs($r.EffectiveMbps-4.194304) -gt 0.00001) { throw 'Chunk cap, timestamp or effective Mbps failed.' }
$state.Time=0; $state.Calls=0
$r=Invoke-Rpi5RepeatedDownload -Request $request -Now $now -DurationSeconds 5
if ($r.StopReason -ne 'TimeLimit' -or $r.ElapsedSeconds -ne 5 -or $state.Calls -ne 3) { throw 'Wall-time budget failed.' }
$state.Time=0; $state.Calls=0; $state.Outcome='403'
$r=Invoke-Rpi5RepeatedDownload -Request $request -Now $now
if ($r.StopReason -ne 'ServerRejected' -or $state.Calls -ne 1 -or $null -ne $r.EffectiveMbps) { throw 'HTTP rejection retried or invented speed.' }
$state.Time=0; $state.Calls=0; $state.Outcome='timeout'
$r=Invoke-Rpi5RepeatedDownload -Request $request -Now $now
if ($r.StopReason -ne 'RepeatedTransportFailure' -or $state.Calls -ne 3 -or $null -ne $r.EffectiveMbps) { throw 'Transport failures unbounded or invented speed.' }

$samplerPath=Join-Path (Split-Path -Parent $PSScriptRoot) 'utility\Measure-RPi5-WiFi-Load.ps1'
. $samplerPath -LibraryOnly
if ((Get-Rpi5CounterRate 0 1048576 1) -ne 8.388608) { throw 'Traffic rate units wrong.' }
if ($null -ne (Get-Rpi5CounterRate $null 10 1) -or $null -ne (Get-Rpi5CounterRate 20 10 1) -or $null -ne (Get-Rpi5CounterRate 0 10 0)) { throw 'Counter reset/first sample invented rate.' }
$samplerSource=Get-Content -LiteralPath $samplerPath -Raw
if ($samplerSource -match '(?i)Start-Transcript|Set-DnsClient|Disable-NetAdapter|bcdedit|Set-ItemProperty') { throw 'Sampler contains unexpected mutation.' }
if ($samplerSource -notmatch 'OwnerStartTicks' -or $samplerSource -notmatch 'TotalSeconds -lt 110' -or $samplerSource -notmatch 'sampling.stop') { throw 'Sampler lifetime guards missing.' }
Write-Output 'PASS: repeated workload byte/time bounds, server-denial stop, partial failures, effective rate and sampler counter reset.'

# Exercise the real wrapper and loop with mocked process/clock calls. No network,
# registry, driver or native clock is touched by this integration fixture.
& {
    $fixture=@{ClockCalls=0;ProcessCalls=0;Time=0.0;Rows=[Collections.Generic.List[object]]::new()}
    function Get-Rpi5MeasurementTimestamp {
        $fixture.ClockCalls++
        return [long](10000000+$fixture.ClockCalls*10000000)
    }
    function Invoke-Rpi5BoundedProcess {
        param([string]$File,[string[]]$Arguments,[int]$Seconds)
        $fixture.ProcessCalls++
        if($File -ne 'mock-curl' -or $Arguments.Count -ne 2 -or $Arguments[1] -ne 'unchanged' -or $Seconds -ne 16) {
            throw 'Timed wrapper changed process arguments or deadline.'
        }
        $fixture.Time+=0.5
        return (Get-DownloadFixture "RPI5_METRIC|200|1048576|0.500000`nRPI5_PHASE|0.010000|0.030000|0.050000|0.060000|0.100000|0.500000`n")
    }
    $request={param($seconds) Invoke-Rpi5TimedDownload 'mock-curl' @('--test','unchanged') ($seconds+1)}
    $observe={param($sample) $fixture.Rows.Add($sample)}
    $result=@(Invoke-Rpi5RepeatedDownload -Request $request -OnSample $observe -Now {$fixture.Time} -MaxRequests 3)
    if($result.Count -ne 1 -or $result[0].Completed -ne 3 -or $result[0].ElapsedSeconds -ne 1.5 -or
        $result[0].VerifiedBytes -ne 3145728 -or $fixture.ClockCalls -ne 6 -or $fixture.ProcessCalls -ne 3) {
        throw 'Timing decoration changed loop output, byte/time bounds or request count.'
    }
    foreach($sample in $fixture.Rows) {
        if($sample.Outcome -ne 'Complete' -or $sample.TimingStatus -ne 'Valid' -or
            [math]::Abs($sample.BodySeconds-0.4) -gt 0.000001 -or
            $sample.RequestEnd100ns-$sample.RequestStart100ns -ne 10000000) {
            throw 'Timing metadata did not survive the actual request/loop integration.'
        }
    }
}
Write-Output 'PASS: phase decoration and clock wrapper preserve actual workload return, requests, bytes, deadlines and outcomes.'
