# Functions only: importing this file does not query a clock, device or network.
function Get-Rpi5DownloadOptionalProperty {
    param($Value, [string]$Name)
    if ($null -ne $Value -and $null -ne $Value.PSObject.Properties[$Name]) {
        return $Value.PSObject.Properties[$Name].Value
    }
    return $null
}

function ConvertTo-Rpi5DownloadSeconds {
    param($Value, [switch]$AllowExponent)
    if ($null -eq $Value) { return $null }
    # Curl emits an invariant decimal, never a localized comma or NaN/Infinity.
    $text = [Convert]::ToString($Value, [Globalization.CultureInfo]::InvariantCulture)
    $pattern = if ($AllowExponent) { '^[0-9]+(?:\.[0-9]+)?(?:[eE][+-]?[0-9]+)?$' }
        else { '^[0-9]+(?:\.[0-9]+)?$' }
    if ($text -notmatch $pattern) { return $null }
    $style = if ($AllowExponent) { [Globalization.NumberStyles]::Float }
        else { [Globalization.NumberStyles]::AllowDecimalPoint }
    $number = 0.0
    if (-not [double]::TryParse($text, $style,
            [Globalization.CultureInfo]::InvariantCulture, [ref]$number) -or
        [double]::IsNaN($number) -or [double]::IsInfinity($number) -or $number -lt 0) {
        return $null
    }
    return $number
}

function Add-Rpi5DownloadTimingToSample {
    param($Sample, $Result)
    # No pipeline output: callers invoke this beside the existing outcome parser.
    # Never revise Outcome, byte accounting, deadlines or the original total.
    $fields = [ordered]@{
        TimingStatus = 'Missing'
        CumulativeDnsSeconds = $null
        CumulativeConnectSeconds = $null
        CumulativeTlsSeconds = $null
        CumulativePreTransferSeconds = $null
        CumulativeFirstByteSeconds = $null
        CumulativeTotalSeconds = $null
        DnsSeconds = $null
        TcpSeconds = $null
        TlsSeconds = $null
        RequestPreparationSeconds = $null
        FirstByteWaitSeconds = $null
        BodySeconds = $null
        RequestStart100ns = $null
        RequestEnd100ns = $null
        ClockStatus = 'Unavailable'
        ClockKind = $null
    }

    $clockStart = Get-Rpi5DownloadOptionalProperty $Result 'RequestStart100ns'
    $clockEnd = Get-Rpi5DownloadOptionalProperty $Result 'RequestEnd100ns'
    $clockKind = Get-Rpi5DownloadOptionalProperty $Result 'ClockKind'
    if ($null -ne $clockStart -or $null -ne $clockEnd) {
        $fields.ClockStatus = 'Invalid'
        $startText = [Convert]::ToString($clockStart, [Globalization.CultureInfo]::InvariantCulture)
        $endText = [Convert]::ToString($clockEnd, [Globalization.CultureInfo]::InvariantCulture)
        $start = [uint64]0; $end = [uint64]0
        if ($null -ne $clockStart -and $null -ne $clockEnd -and
            $clockKind -ceq 'QueryInterruptTime100nsSinceBoot' -and
            $startText -match '^[0-9]+$' -and $endText -match '^[0-9]+$' -and
            [uint64]::TryParse($startText, [Globalization.NumberStyles]::None,
                [Globalization.CultureInfo]::InvariantCulture, [ref]$start) -and
            [uint64]::TryParse($endText, [Globalization.NumberStyles]::None,
                [Globalization.CultureInfo]::InvariantCulture, [ref]$end) -and $end -ge $start) {
            $fields.RequestStart100ns = $start
            $fields.RequestEnd100ns = $end
            $fields.ClockKind = $clockKind
            $fields.ClockStatus = 'Valid'
        }
    }

    $output = Get-Rpi5DownloadOptionalProperty $Result 'Output'
    $lines = [regex]::Matches([string]$output, '(?m)^RPI5_PHASE([^\r\n]*)\r?$')
    if ($lines.Count -gt 1) {
        $fields.TimingStatus = 'Duplicate'
    } elseif ($lines.Count -eq 1) {
        $body = $lines[0].Groups[1].Value
        $parts = @()
        if ($body.StartsWith('|', [StringComparison]::Ordinal)) { $parts = @($body.Substring(1).Split('|')) }
        $values = [Collections.Generic.List[double]]::new()
        $fields.TimingStatus = 'Malformed'
        if ($parts.Count -eq 6) {
            foreach ($part in $parts) {
                $value = ConvertTo-Rpi5DownloadSeconds $part
                if ($null -eq $value) { break }
                $values.Add($value)
            }
        }
        if ($values.Count -eq 6) {
            $ordered = $true
            for ($i = 1; $i -lt 6; $i++) {
                if ($values[$i] -lt $values[$i - 1]) { $ordered = $false; break }
            }
            $total = ConvertTo-Rpi5DownloadSeconds (Get-Rpi5DownloadOptionalProperty $Sample 'TransferSeconds') -AllowExponent
            if (-not $ordered) {
                $fields.TimingStatus = 'NonMonotonic'
            } elseif ((Get-Rpi5DownloadOptionalProperty $Sample 'Outcome') -cne 'Complete' -or
                (Get-Rpi5DownloadOptionalProperty $Result 'ExitCode') -ne 0 -or
                (Get-Rpi5DownloadOptionalProperty $Result 'TimedOut') -ne $false) {
                # Curl's zero values after an early failure do not prove zero delay.
                $fields.TimingStatus = 'IncompleteRequest'
            } elseif ($null -eq $total -or $total -le 0 -or
                [math]::Abs($total - $values[5]) -gt 0.000002) {
                $fields.TimingStatus = 'TotalMismatch'
            } else {
                $fields.TimingStatus = 'Valid'
                $fields.CumulativeDnsSeconds = $values[0]
                $fields.CumulativeConnectSeconds = $values[1]
                $fields.CumulativeTlsSeconds = $values[2]
                $fields.CumulativePreTransferSeconds = $values[3]
                $fields.CumulativeFirstByteSeconds = $values[4]
                $fields.CumulativeTotalSeconds = $values[5]
                $fields.DnsSeconds = $values[0]
                $fields.TcpSeconds = $values[1] - $values[0]
                $fields.TlsSeconds = $values[2] - $values[1]
                $fields.RequestPreparationSeconds = $values[3] - $values[2]
                $fields.FirstByteWaitSeconds = $values[4] - $values[3]
                $fields.BodySeconds = $values[5] - $values[4]
            }
        }
    }
    $Sample | Add-Member -NotePropertyMembers $fields -Force
}

function Get-Rpi5DownloadTimingSummary {
    param([object[]]$Samples = @(), [double]$SlowThresholdSeconds = 0.5)
    if ([double]::IsNaN($SlowThresholdSeconds) -or [double]::IsInfinity($SlowThresholdSeconds) -or
        $SlowThresholdSeconds -lt 0) { $SlowThresholdSeconds = 0.5 }
    $complete = 0; $valid = 0; $clockValid = 0; $slow = 0; $slowValid = 0
    $statusCounts = [ordered]@{}
    $sums = [ordered]@{
        DnsSeconds = 0.0; TcpSeconds = 0.0; TlsSeconds = 0.0
        RequestPreparationSeconds = 0.0; FirstByteWaitSeconds = 0.0; BodySeconds = 0.0
    }
    $total = 0.0
    foreach ($sample in $Samples) {
        if ((Get-Rpi5DownloadOptionalProperty $sample 'Outcome') -ceq 'Complete') { $complete++ }
        $duration = ConvertTo-Rpi5DownloadSeconds (Get-Rpi5DownloadOptionalProperty $sample 'TransferSeconds') -AllowExponent
        $isSlow = $null -ne $duration -and $duration -gt $SlowThresholdSeconds
        if ($isSlow) { $slow++ }
        if ((Get-Rpi5DownloadOptionalProperty $sample 'ClockStatus') -ceq 'Valid') { $clockValid++ }
        $status = Get-Rpi5DownloadOptionalProperty $sample 'TimingStatus'
        if ([string]::IsNullOrEmpty([string]$status)) { $status = 'Missing' }
        # Import-Csv supplies strings, and .NET may serialize small differences
        # with an exponent. Parse those invariantly; never treat an empty cell
        # or malformed claimed-Valid row as measured zero.
        $row = [ordered]@{}
        $rowTotal = $null
        if ($status -ceq 'Valid') {
            $rowTotal = ConvertTo-Rpi5DownloadSeconds (Get-Rpi5DownloadOptionalProperty $sample 'CumulativeTotalSeconds') -AllowExponent
            $rowValid = (Get-Rpi5DownloadOptionalProperty $sample 'Outcome') -ceq 'Complete' -and
                $null -ne $duration -and $duration -gt 0 -and $null -ne $rowTotal -and
                [math]::Abs($duration - $rowTotal) -le 0.000002
            $rowSum = 0.0
            foreach ($name in @($sums.Keys)) {
                $row[$name] = ConvertTo-Rpi5DownloadSeconds (Get-Rpi5DownloadOptionalProperty $sample $name) -AllowExponent
                if ($null -eq $row[$name]) { $rowValid = $false } else { $rowSum += $row[$name] }
            }
            if ($null -eq $rowTotal -or [math]::Abs($rowSum - $rowTotal) -gt 0.000002) { $rowValid = $false }
            if (-not $rowValid) { $status = 'InvalidSummaryInput' }
        }
        if (-not $statusCounts.Contains($status)) { $statusCounts[$status] = 0 }
        $statusCounts[$status]++
        if ($status -ceq 'Valid') {
            # Summarize only helper-validated complete requests. Cumulative fields
            # overlap; never add them together or call their totals averages.
            $valid++
            if ($isSlow) { $slowValid++ }
            $total += $rowTotal
            foreach ($name in @($sums.Keys)) { $sums[$name] += $row[$name] }
        }
    }
    if ($valid -eq 0) {
        $total = $null
        foreach ($name in @($sums.Keys)) { $sums[$name] = $null }
    }
    [pscustomobject][ordered]@{
        SchemaVersion = 1
        AttemptCount = @($Samples).Count
        CompleteRequestCount = $complete
        FailedOrInvalidRequestCount = @($Samples).Count - $complete
        ValidTimingCount = $valid
        UnknownTimingCount = @($Samples).Count - $valid
        TimingStatusCounts = [pscustomobject]$statusCounts
        ValidClockCount = $clockValid
        UnknownClockCount = @($Samples).Count - $clockValid
        SlowThresholdSeconds = $SlowThresholdSeconds
        SlowAttemptCount = $slow
        SlowValidTimingCount = $slowValid
        SumValidRequestTotalSeconds = $total
        SumValidPhaseSeconds = [pscustomobject]$sums
        Attribution = 'NotDetermined'
        Caveats = @(
            'Phase sums cover only completed requests with valid ordered timing fields; they exclude failed/unknown attempts and process/observer overhead.'
            'Slow means curl total exceeded the stated descriptive threshold; it is not a diagnosis or a throughput target.'
            'DNS/TCP/TLS/first-byte/body delays can include host queuing, Wi-Fi retransmissions and remote network/server delay; phase timing alone cannot identify their cause.'
            'Correlate valid same-boot interrupt-clock windows with existing passive driver history and load probes; history is coarse and may be stale, not a packet trace.'
            'Request clock windows include process-launch and collection overhead; cumulative curl phases are not exact absolute timestamps within those windows.'
        )
    }
}
