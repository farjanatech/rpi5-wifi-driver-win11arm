[CmdletBinding()]
param([switch]$TimingLibraryOnly)
Set-StrictMode -Version Latest
$ErrorActionPreference='Stop'

function ConvertFrom-Rpi5Timing {
    param($Diagnostic)
    if ($null -eq $Diagnostic -or -not $Diagnostic.PSObject.Properties['DiagVersion'] -or
        $Diagnostic.DiagVersion -lt 22) {
        throw 'Runtime timing snapshot unavailable; exp0.6.22 must finish startup first.'
    }
    # Never reuse a stale .22 value after installing .23. The current driver
    # version selects the required snapshot schema; fallback would fake data.
    $expectedVersion=if($Diagnostic.DiagVersion -ge 23){2}else{1}
    $property=if($expectedVersion -eq 2){'TimingV2'}else{'TimingV1'}
    if(-not $Diagnostic.PSObject.Properties[$property]){throw 'Current runtime timing snapshot unavailable; wait for startup.'}
    [byte[]]$data=$Diagnostic.$property
    $headerBytes=if($expectedVersion -eq 2){48}else{40}
    if ($data.Length -ne $headerBytes+520) { throw 'Unexpected timing snapshot size.' }
    $version=[BitConverter]::ToUInt64($data,0)
    $count=[BitConverter]::ToUInt64($data,8)
    $frequency=[BitConverter]::ToUInt64($data,16)
    $session=[BitConverter]::ToUInt64($data,24)
    $stamp=[BitConverter]::ToUInt64($data,32)
    if ($version -ne $expectedVersion -or $count -ne 13 -or $frequency -eq 0 -or $stamp -lt $session) {
        throw 'Unsupported/invalid timing snapshot header.'
    }
    $activeMask=if($version -eq 2){[BitConverter]::ToUInt64($data,40)}else{[uint64]8191}
    if($activeMask -gt 8191){throw 'Unknown timing bucket mask.'}
    $names=@('TxPump','RxBatch','WorkerWork','IdleWait','WorkerInterval','Diagnostics',
        'Control','Cmd52','Cmd53F1','Cmd53Rx','Cmd53Tx','ReceiveIndication','CreditRecheck')
    $rows=for($i=0;$i -lt 13;$i++) {
        $offset=$headerBytes+40*$i
        $enabled=($activeMask -band (1L -shl $i)) -ne 0
        $n=[BitConverter]::ToUInt64($data,$offset)
        $total=[BitConverter]::ToUInt64($data,$offset+8)
        $max=[BitConverter]::ToUInt64($data,$offset+16)
        $slow=[BitConverter]::ToUInt64($data,$offset+24)
        $verySlow=[BitConverter]::ToUInt64($data,$offset+32)
        if ($max -gt $total -or $verySlow -gt $slow -or $slow -gt $n) {
            throw 'Inconsistent timing bucket.'
        }
        if(((-not $enabled) -or $n -eq 0) -and
            ($n -ne 0 -or $total -ne 0 -or $max -ne 0 -or $slow -ne 0 -or $verySlow -ne 0)){
            throw 'Disabled/empty timing bucket contains measurements.'
        }
        [pscustomobject]@{Name=$names[$i];Enabled=$enabled;Count=$n;TotalTicks=$total;MaxTicks=$max;
            AtLeast10ms=$slow;AtLeast100ms=$verySlow}
    }
    [pscustomobject]@{Version=$version;ActiveMask=$activeMask;Frequency=$frequency;
        SessionQpc=$session;SnapshotQpc=$stamp;Rows=@($rows)}
}
function Get-Rpi5TimingReport {
    param($Before,$After)
    $last=ConvertFrom-Rpi5Timing $After
    $first=$null
    try { $first=ConvertFrom-Rpi5Timing $Before } catch { $first=$null }
    $comparable=$null -ne $first -and $first.SessionQpc -eq $last.SessionQpc -and
        $first.Frequency -eq $last.Frequency -and $first.Version -eq $last.Version -and
        $first.ActiveMask -eq $last.ActiveMask -and $last.SnapshotQpc -gt $first.SnapshotQpc
    $rows=for($i=0;$i -lt 13;$i++) {
        $b=$last.Rows[$i];$deltaCount=$null;$deltaMs=$null;$deltaSlow=$null;$deltaVerySlow=$null;$mean=$null
        if ($comparable -and $b.Enabled) {
            $p=$first.Rows[$i]
            if($b.Count -ge $p.Count -and $b.TotalTicks -ge $p.TotalTicks -and
                $b.AtLeast10ms -ge $p.AtLeast10ms -and $b.AtLeast100ms -ge $p.AtLeast100ms -and
                $b.Count -ne [uint64]::MaxValue -and $b.TotalTicks -ne [uint64]::MaxValue) {
                $deltaCount=$b.Count-$p.Count
                $deltaMs=1000.0*($b.TotalTicks-$p.TotalTicks)/$last.Frequency
                $deltaSlow=$b.AtLeast10ms-$p.AtLeast10ms
                $deltaVerySlow=$b.AtLeast100ms-$p.AtLeast100ms
                if($deltaCount -gt 0){$mean=$deltaMs/$deltaCount}
            }
        }
        $n=$null;$total=$null;$max=$null;$slow=$null;$verySlow=$null
        if($b.Enabled){
            $n=$b.Count;$total=1000.0*$b.TotalTicks/$last.Frequency
            $max=1000.0*$b.MaxTicks/$last.Frequency;$slow=$b.AtLeast10ms;$verySlow=$b.AtLeast100ms
        }
        [pscustomobject]@{Name=$b.Name;Enabled=$b.Enabled;Count=$n;TotalMs=$total;
            CumulativeMaxMs=$max;AtLeast10ms=$slow;
            AtLeast100ms=$verySlow;DeltaCount=$deltaCount;DeltaTotalMs=$deltaMs;
            DeltaMeanMs=$mean;DeltaAtLeast10ms=$deltaSlow;DeltaAtLeast100ms=$deltaVerySlow}
    }
    [pscustomobject]@{
        Notice='Inclusive wall time, not CPU time; nested buckets must not be added. Disabled buckets have null metrics, not measured zero latency. Periodic snapshots include activity outside the download. Maxima are cumulative, not interval maxima. CreditRecheck measures service-loop intervals after credit-blocked TX, not exact firmware wait. WorkerInterval includes work/idle/control, not proof of scheduler starvation.'
        Version=$last.Version;ActiveMask=$last.ActiveMask;
        ComparableSnapshots=$comparable;Frequency=$last.Frequency;SessionQpc=$last.SessionQpc;
        SnapshotQpc=$last.SnapshotQpc;Rows=@($rows)
    }
}
if ($TimingLibraryOnly) { return }
Get-Rpi5TimingReport -After (Get-ItemProperty -LiteralPath 'HKLM:\SOFTWARE\Rpi5CywDirectDiag') |
    ConvertTo-Json -Depth 5
