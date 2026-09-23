# Functions only: importing this file does not compile a type or query a clock.
# Call Initialize-Rpi5MeasurementClock once, outside the measured workload.
# ClockKind: QueryInterruptTime100nsSinceBoot (biased interrupt time).
# This is the same boot-time domain as the driver's KeQueryInterruptTime, not
# UTC, QPC, an unbiased clock, or seconds relative to a transport trace origin.
# Units are 100 ns; accuracy/granularity is a system clock tick, NOT 100 ns.
# Equal successive values are valid. Sleep/wake bias is included. No timer
# resolution, system clock, driver, device, registry, or network state is changed.
# Microsoft references:
# https://learn.microsoft.com/en-us/windows/win32/api/realtimeapiset/nf-realtimeapiset-queryinterrupttime
# https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/wdm/nf-wdm-kequeryinterrupttime

function Initialize-Rpi5MeasurementClock {
    # ReadClock is dependency injection for tests; production callers omit it.
    # A failed initialization is not retried within this script/runspace scope.
    # Re-importing these definitions also preserves the existing clock state.
    param([scriptblock]$ReadClock)
    if($null -ne (Get-Variable -Name Rpi5MeasurementClockState -Scope Script -ErrorAction SilentlyContinue)) {
        return
    }
    $script:Rpi5MeasurementClockState=[pscustomobject]@{
        Available=$false; Reader=$null; Last=$null
    }
    try {
        if($null -eq $ReadClock) {
            if(-not ('Rpi5WifiMeasurementClockNativeV1' -as [type])) {
                $null=Add-Type -ErrorAction Stop -TypeDefinition @'
using System.Runtime.InteropServices;
public static class Rpi5WifiMeasurementClockNativeV1
{
    [DllImport("kernel32.dll", ExactSpelling = true)]
    private static extern void QueryInterruptTime(out ulong interruptTime);
    public static ulong Read()
    {
        ulong value;
        QueryInterruptTime(out value);
        return value;
    }
}
'@
            }
            $ReadClock={ [Rpi5WifiMeasurementClockNativeV1]::Read() }
        }
        $script:Rpi5MeasurementClockState.Reader=$ReadClock
        $script:Rpi5MeasurementClockState.Available=$true
        # Resolve entry-point/read failures and warm up the call before timing.
        # Get returns only a timestamp or null; initialization emits neither.
        $null=Get-Rpi5MeasurementTimestamp
    } catch {
        $script:Rpi5MeasurementClockState.Available=$false
    }
}

function Get-Rpi5MeasurementTimestamp {
    # Nullable Int64 result: unavailable/invalid is null, never measured zero.
    # A genuine native zero is valid. Reject coercions (including rounded
    # floating-point counters), overflow, and backward/wrapped observations.
    # Latch failures so later values cannot silently reopen a broken interval.
    $variable=Get-Variable -Name Rpi5MeasurementClockState -Scope Script -ErrorAction SilentlyContinue
    if($null -eq $variable -or -not $variable.Value.Available) { return $null }
    $state=$variable.Value
    try {
        $raw=& $state.Reader
        if($raw -isnot [byte] -and $raw -isnot [sbyte] -and
           $raw -isnot [int16] -and $raw -isnot [uint16] -and
           $raw -isnot [int32] -and $raw -isnot [uint32] -and
           $raw -isnot [int64] -and $raw -isnot [uint64]) {
            throw 'Invalid interrupt-time value.'
        }
        if($raw -is [uint64] -and $raw -gt [uint64][int64]::MaxValue) {
            throw 'Interrupt-time value exceeds Int64.'
        }
        $stamp=[int64]$raw
        if($stamp -lt 0 -or ($null -ne $state.Last -and $stamp -lt $state.Last)) {
            throw 'Interrupt-time counter moved backwards.'
        }
        $state.Last=$stamp
        return $stamp
    } catch {
        $state.Available=$false
        return $null
    }
}
