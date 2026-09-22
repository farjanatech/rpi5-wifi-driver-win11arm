[CmdletBinding()]
param([switch]$TimingLibraryOnly)
Set-StrictMode -Version Latest
$ErrorActionPreference='Stop'

function ConvertFrom-Rpi5Timing {
    param($Diagnostic)
    if ($null -eq $Diagnostic -or -not $Diagnostic.PSObject.Properties['DiagVersion'] -or
        $Diagnostic.DiagVersion -lt 22 -or -not $Diagnostic.PSObject.Properties['TimingV1']) {
        throw 'Runtime timing snapshot unavailable; exp0.6.22 must finish startup first.'
    }
    [byte[]]$data=$Diagnostic.TimingV1
    if ($data.Length -ne 560) { throw 'Unexpected timing snapshot size.' }
    $version=[BitConverter]::ToUInt64($data,0)
    $count=[BitConverter]::ToUInt64($data,8)
    $frequency=[BitConverter]::ToUInt64($data,16)
    $session=[BitConverter]::ToUInt64($data,24)
    $stamp=[BitConverter]::ToUInt64($data,32)
    if ($version -ne 1 -or $count -ne 13 -or $frequency -eq 0 -or $stamp -lt $session) {
        throw 'Unsupported/invalid timing snapshot header.'
    }
    $names=@('TxPump','RxBatch','WorkerWork','IdleWait','WorkerInterval','Diagnostics',
        'Control','Cmd52','Cmd53F1','Cmd53Rx','Cmd53Tx','ReceiveIndication','CreditRecheck')
    $rows=for($i=0;$i -lt 13;$i++) {
        $offset=40+40*$i
        $n=[BitConverter]::ToUInt64($data,$offset)
        $total=[BitConverter]::ToUInt64($data,$offset+8)
        $max=[BitConverter]::ToUInt64($data,$offset+16)
        $slow=[BitConverter]::ToUInt64($data,$offset+24)
        $verySlow=[BitConverter]::ToUInt64($data,$offset+32)
        if ($max -gt $total -or $verySlow -gt $slow -or $slow -gt $n) {
            throw 'Inconsistent timing bucket.'
        }
        [pscustomobject]@{Name=$names[$i];Count=$n;TotalTicks=$total;MaxTicks=$max;
            AtLeast10ms=$slow;AtLeast100ms=$verySlow}
    }
    [pscustomobject]@{Version=$version;Frequency=$frequency;SessionQpc=$session;SnapshotQpc=$stamp;Rows=@($rows)}
}
function Get-Rpi5TimingReport {
    param($Before,$After)
    $last=ConvertFrom-Rpi5Timing $After
    $first=$null
    try { $first=ConvertFrom-Rpi5Timing $Before } catch { $first=$null }
    $comparable=$null -ne $first -and $first.SessionQpc -eq $last.SessionQpc -and
        $first.Frequency -eq $last.Frequency -and $last.SnapshotQpc -gt $first.SnapshotQpc
    $rows=for($i=0;$i -lt 13;$i++) {
        $b=$last.Rows[$i];$deltaCount=$null;$deltaMs=$null;$deltaSlow=$null;$deltaVerySlow=$null;$mean=$null
        if ($comparable) {
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
        [pscustomobject]@{Name=$b.Name;Count=$b.Count;TotalMs=1000.0*$b.TotalTicks/$last.Frequency;
            CumulativeMaxMs=1000.0*$b.MaxTicks/$last.Frequency;AtLeast10ms=$b.AtLeast10ms;
            AtLeast100ms=$b.AtLeast100ms;DeltaCount=$deltaCount;DeltaTotalMs=$deltaMs;
            DeltaMeanMs=$mean;DeltaAtLeast10ms=$deltaSlow;DeltaAtLeast100ms=$deltaVerySlow}
    }
    [pscustomobject]@{
        Notice='Inclusive wall time, not CPU time; nested buckets must not be added. Periodic snapshots include activity outside the download. Maxima are cumulative, not interval maxima. CreditRecheck measures service-loop intervals after credit-blocked TX, not exact firmware wait. WorkerInterval includes work/idle/control, not proof of scheduler starvation.'
        ComparableSnapshots=$comparable;Frequency=$last.Frequency;SessionQpc=$last.SessionQpc;
        SnapshotQpc=$last.SnapshotQpc;Rows=@($rows)
    }
}
if ($TimingLibraryOnly) { return }
Get-Rpi5TimingReport -After (Get-ItemProperty -LiteralPath 'HKLM:\SOFTWARE\Rpi5CywDirectDiag') |
    ConvertTo-Json -Depth 5
