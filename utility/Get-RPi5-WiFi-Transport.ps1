[CmdletBinding()]
param([switch]$TransportLibraryOnly)
Set-StrictMode -Version Latest
$ErrorActionPreference='Stop'

function Get-Rpi5TransportCounterDelta {
    param([uint32]$Before,[uint32]$After)
    # The ABI explicitly stores modulo-2^32 counters. Widen before subtracting
    # so PowerShell never casts a negative value to an unsigned integer.
    return [uint64](([uint64]$After+[uint64]4294967296-[uint64]$Before)%[uint64]4294967296)
}
function ConvertFrom-Rpi5Transport {
    param($Diagnostic)
    if($null -eq $Diagnostic -or -not $Diagnostic.PSObject.Properties['DiagVersion'] -or
       $Diagnostic.DiagVersion -lt 24 -or -not $Diagnostic.PSObject.Properties['TransportTraceV1']) {
        throw 'Current transport trace unavailable; exp0.6.24 must finish startup and publish diagnostics first.'
    }
    [byte[]]$data=$Diagnostic.TransportTraceV1
    if($data.Length -ne 16416){throw 'Unexpected transport trace size.'}
    $version=[BitConverter]::ToUInt32($data,0)
    $entryBytes=[BitConverter]::ToUInt32($data,4)
    $capacity=[BitConverter]::ToUInt32($data,8)
    $count=[BitConverter]::ToUInt32($data,12)
    $next=[BitConverter]::ToUInt32($data,16)
    $serial=[BitConverter]::ToUInt32($data,20)
    $origin=[BitConverter]::ToUInt64($data,24)
    if($version -ne 1 -or $entryBytes -ne 128 -or $capacity -ne 128 -or
       $count -eq 0 -or $count -gt $capacity -or $next -ge $capacity -or
       ($count -lt $capacity -and ($next -ne $count -or $serial -ne $count))) {
        throw 'Unsupported, empty, or invalid transport trace header.'
    }
    $fields=@('Serial','Flags','NetworkPhase','LastPending','LastInterrupt','LastMailbox',
        'RxFrames','EmptyReads','TxPacketsLow','RxPacketsLow','PendingReads','PendingEmpty',
        'StatusReads','StatusNoEvents','FrameNotifications','MailReads','FallbackReads',
        'FallbackFrames','FallbackMailbox','ServiceErrors','SequenceMismatches','QueueDepth',
        'QueueFull','CreditWaits','TxSequence','TxMaximum','TxFlow','GlobalFlow','EchoLate','EchoMaxMs')
    $counters=@('RxFrames','EmptyReads','TxPacketsLow','RxPacketsLow','PendingReads','PendingEmpty',
        'StatusReads','StatusNoEvents','FrameNotifications','MailReads','FallbackReads','FallbackFrames',
        'FallbackMailbox','ServiceErrors','SequenceMismatches','QueueFull','CreditWaits','EchoLate')
    $firstSlot=if($count -eq $capacity){$next}else{0}
    $previous=$null
    $rows=@(for($i=0;$i -lt $count;$i++) {
        $slot=($firstSlot+$i)%$capacity
        $offset=32+$entryBytes*$slot
        $stamp=[BitConverter]::ToUInt64($data,$offset)
        $values=[ordered]@{Slot=$slot;Time100ns=$stamp;SecondsSinceTraceOrigin=($stamp-$origin)/10000000.0}
        for($j=0;$j -lt $fields.Count;$j++) {
            $values[$fields[$j]]=[BitConverter]::ToUInt32($data,$offset+8+4*$j)
        }
        $row=[pscustomobject]$values
        if($stamp -lt $origin -or $row.Flags -gt 31 -or $row.LastPending -gt 255 -or
           $row.TxSequence -gt 255 -or $row.TxMaximum -gt 255 -or $row.TxFlow -gt 255 -or $row.GlobalFlow -gt 1) {
            throw 'Invalid transport trace entry.'
        }
        $elapsed=$null;$deltas=$null
        if($null -ne $previous) {
            if($stamp -le $previous.Time100ns -or
               (Get-Rpi5TransportCounterDelta $previous.Serial $row.Serial) -ne 1) {
                throw 'Transport trace is not in monotonic chronological/serial order.'
            }
            $elapsed=($stamp-$previous.Time100ns)/10000000.0
            $deltaValues=[ordered]@{}
            foreach($field in $counters) {
                $deltaValues[$field]=Get-Rpi5TransportCounterDelta $previous.$field $row.$field
            }
            $deltas=[pscustomobject]$deltaValues
        }
        $row | Add-Member -NotePropertyName SecondsSincePrevious -NotePropertyValue $elapsed
        $row | Add-Member -NotePropertyName CounterDeltas -NotePropertyValue $deltas
        $previous=$row
        $row
    })
    if($rows[-1].Serial -ne $serial){throw 'Transport header and newest entry serial disagree.'}
    [pscustomobject]@{Version=$version;EntryBytes=$entryBytes;Capacity=$capacity;Count=$count;Next=$next;
        Serial=$serial;Origin100ns=$origin;TimestampKind='Monotonic100nsSinceBoot';Rows=$rows}
}
function Get-Rpi5TransportReport {
    param($Diagnostic)
    $trace=ConvertFrom-Rpi5Transport $Diagnostic
    $trace | Add-Member -NotePropertyName Notice -NotePropertyValue (
        'Passive one-second worker observations, not a stall diagnosis. Times are monotonic 100 ns ticks since boot, NOT UTC or wall-clock time. No samples are fabricated across worker/control gaps. LastPending/LastInterrupt/LastMailbox are last observed values, not simultaneous fresh register reads. QueueDepth is an advisory concurrent NDIS-admission snapshot. CounterDeltas are modulo-2^32 differences between retained adjacent samples. Tx/Rx packet totals are low 32 bits. Firmware/AP latency cannot be inferred solely from these host observations. The ring retains at most 128 samples; its periodic registry copy may be older than collection time.'
    )
    return $trace
}
if($TransportLibraryOnly){return}
Get-Rpi5TransportReport -Diagnostic (Get-ItemProperty -LiteralPath 'HKLM:\SOFTWARE\Rpi5CywDirectDiag') |
    ConvertTo-Json -Depth 6
