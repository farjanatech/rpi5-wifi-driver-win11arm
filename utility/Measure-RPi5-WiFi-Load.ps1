[CmdletBinding()]
param([switch]$LibraryOnly, [int]$InterfaceIndex, [string]$Gateway,
    [string]$OutputDirectory, [int]$OwnerPid, [long]$OwnerStartTicks)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'RPi5-WiFi-MeasurementClock.ps1')

function Get-Rpi5CounterRate {
    param($Previous, $Current, [double]$Seconds)
    if ($null -eq $Previous -or $null -eq $Current -or $Seconds -le 0 -or $Current -lt $Previous) { return $null }
    return ([double]$Current - [double]$Previous) * 8 / $Seconds / 1000000
}
if ($LibraryOnly) { return }
if ($env:PROCESSOR_ARCHITECTURE -ne 'ARM64') { throw 'Sampler is for the Raspberry Pi, not the development PC.' }
Initialize-Rpi5MeasurementClock
$targets = @(Get-NetAdapter | Where-Object ifIndex -eq $InterfaceIndex)
if ($targets.Count -ne 1 -or $targets[0].InterfaceDescription -notlike '*CYW43455*') { throw 'Not a CYW43455 adapter.' }
$target = $targets[0]
$address = [Net.IPAddress]::Parse($Gateway)
if ($address.AddressFamily -ne [Net.Sockets.AddressFamily]::InterNetwork) { throw 'Expected IPv4 gateway.' }
if (-not (Test-Path -LiteralPath $OutputDirectory -PathType Container)) { throw 'Output directory is missing.' }
$watch = [Diagnostics.Stopwatch]::StartNew()
$ping = [Net.NetworkInformation.Ping]::new()
$previousRx = $null; $previousTx = $null; $previousTime = 0.0
try {
    'ready' | Set-Content -LiteralPath (Join-Path $OutputDirectory 'sampling.ready')
    # Independent hard lifetime, and stop if the owner exits or its PID is reused.
    while ($watch.Elapsed.TotalSeconds -lt 110 -and -not (Test-Path -LiteralPath (Join-Path $OutputDirectory 'sampling.stop'))) {
        try { $owner = Get-Process -Id $OwnerPid -ErrorAction Stop } catch { break }
        if ($owner.StartTime.ToUniversalTime().Ticks -ne $OwnerStartTicks) { break }
        $row = [ordered]@{ SampleStart100ns=(Get-Rpi5MeasurementTimestamp); SampleEnd100ns=$null;
            ClockKind='QueryInterruptTime100nsSinceBoot';
            SampleStartUtc=[datetime]::UtcNow.ToString('o'); SampleEndUtc='';
            ElapsedSeconds=0.0; GatewayStatus='Unavailable'; GatewayRttMs=$null;
            ReceivedBytes=$null; SentBytes=$null; ReceiveMbps=$null; SendMbps=$null;
            StatisticsError=''; DriverSnapshotError='' }
        $pendingPing = $null
        try { $pendingPing = $ping.SendPingAsync($address,1000) }
        catch { $row.GatewayStatus=$_.Exception.GetType().Name }
        try {
            $stats = Get-NetAdapterStatistics -Name $target.Name
            $currentTime = $watch.Elapsed.TotalSeconds
            $row.ReceivedBytes=$stats.ReceivedBytes; $row.SentBytes=$stats.SentBytes
            $row.ReceiveMbps=Get-Rpi5CounterRate $previousRx $stats.ReceivedBytes ($currentTime-$previousTime)
            $row.SendMbps=Get-Rpi5CounterRate $previousTx $stats.SentBytes ($currentTime-$previousTime)
            $previousRx=$stats.ReceivedBytes; $previousTx=$stats.SentBytes; $previousTime=$currentTime
        } catch { $row.StatisticsError=$_.Exception.GetType().Name }
        # These are periodic (~30s), non-atomic snapshots, NOT live queue depths.
        $fields=@('SnapshotTimeUtc','NetworkPhase','NetworkStatus','TxPackets','RxPackets',
            'TxErrors','RxErrors','RxNoBuffer','TxQueueHighWater','TxQueueFull','TxQueueMaxDelayMs',
            'TxQueueLimit','TxQueueFrames','TxBurstAdmissions','TxOversizedNbl','TxInterleavedPackets',
            'TxCreditWaits','TxCreditSequence','TxCreditMaximum','TxFlowMask',
            'RuntimeCmd53CommandSleeps','RuntimeCmd53BufferSleeps','RuntimeCmd53CompleteSleeps',
            'RuntimeCmd53SleepMs','BpWindowCacheHits','BpWindowSelections')
        foreach ($field in $fields) { $row[$field]=$null }
        try {
            $snapshot=Get-ItemProperty -LiteralPath 'HKLM:\SOFTWARE\Rpi5CywDirectDiag'
            foreach ($field in $fields) {
                if ($snapshot.PSObject.Properties[$field]) { $row[$field]=$snapshot.$field }
            }
        } catch { $row.DriverSnapshotError=$_.Exception.GetType().Name }
        if ($null -ne $pendingPing) {
            try {
                $reply=$pendingPing.GetAwaiter().GetResult()
                $row.GatewayStatus=[string]$reply.Status
                if ($reply.Status -eq [Net.NetworkInformation.IPStatus]::Success) { $row.GatewayRttMs=$reply.RoundtripTime }
            } catch { $row.GatewayStatus=$_.Exception.GetType().Name }
        }
        $row.SampleEndUtc=[datetime]::UtcNow.ToString('o'); $row.ElapsedSeconds=$watch.Elapsed.TotalSeconds
        $row.SampleEnd100ns=Get-Rpi5MeasurementTimestamp
        [pscustomobject]$row | Export-Csv -LiteralPath (Join-Path $OutputDirectory 'load-timeline.csv') -NoTypeInformation -Append -Encoding UTF8
        Start-Sleep -Milliseconds 1000
    }
} finally { $ping.Dispose() }
