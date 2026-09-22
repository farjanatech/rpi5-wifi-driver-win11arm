[CmdletBinding()]
param([switch]$LibraryOnly, [switch]$NoPause)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'Get-RPi5-WiFi-Timing.ps1') -TimingLibraryOnly

function Get-Rpi5BusAssessment {
    param($Diagnostic)
    if ($null -eq $Diagnostic -or -not $Diagnostic.PSObject.Properties['BusModeStage']) {
        return 'Updated runtime diagnostics missing. Restart after installing exp0.6.14.'
    }
    if ($Diagnostic.DiagVersion -ge 10 -and $Diagnostic.BusModeStage -eq 6 -and
        $Diagnostic.BusWidth -eq 4 -and $Diagnostic.BusActualKhz -gt 400 -and
        $Diagnostic.BusActualKhz -le 25000 -and $Diagnostic.BusVerifyReads -eq 16 -and
        $Diagnostic.BusUpgradeStatus -eq 0 -and $Diagnostic.BusVerifyStatus -eq 0) {
        return "BUS VERIFIED: 4-bit, calculated $($Diagnostic.BusActualKhz) kHz; 16 chip-ID reads passed. Not a throughput result."
    }
    return "BUS NOT VERIFIED: stage=$($Diagnostic.BusModeStage), width=$($Diagnostic.BusWidth), calculated kHz=$($Diagnostic.BusActualKhz). See saved status fields."
}
function Test-Rpi5ExclusiveRoute {
    param([object[]]$Routes, [int]$InterfaceIndex)
    return $Routes.Count -gt 0 -and @($Routes | Where-Object {
        $_.InterfaceIndex -ne $InterfaceIndex
    }).Count -eq 0
}
function Test-Rpi5NewerSnapshot {
    param($Snapshot, [long]$EndStamp)
    return $null -ne $Snapshot -and $null -ne $Snapshot.PSObject.Properties['SnapshotTimeUtc'] -and
        [long]$Snapshot.SnapshotTimeUtc -gt $EndStamp
}
function Invoke-Rpi5BoundedProcess {
    param([string]$File, [string[]]$Arguments, [int]$Seconds)
    # Arguments are generated here, not supplied by a downloaded document.
    $info = [Diagnostics.ProcessStartInfo]::new()
    $info.FileName = $File
    $info.Arguments = ($Arguments | ForEach-Object { '"' + $_.Replace('"','\"') + '"' }) -join ' '
    $info.UseShellExecute = $false; $info.CreateNoWindow = $true
    $info.RedirectStandardOutput = $true; $info.RedirectStandardError = $true
    $process = [Diagnostics.Process]::new(); $process.StartInfo = $info
    try {
        [void]$process.Start()
        $stdout = $process.StandardOutput.ReadToEndAsync()
        $stderr = $process.StandardError.ReadToEndAsync()
        $timedOut = -not $process.WaitForExit($Seconds * 1000)
        if ($timedOut) { $process.Kill(); $process.WaitForExit() }
        [pscustomobject]@{ ExitCode=$process.ExitCode; TimedOut=$timedOut;
            Output=($stdout.GetAwaiter().GetResult() + $stderr.GetAwaiter().GetResult()) }
    } finally { $process.Dispose() }
}
function ConvertFrom-Rpi5DownloadResult {
    param($Result, [long]$ExpectedBytes = 1048576)
    $http = 0; $bytes = 0L; $seconds = 0.0
    $valid = $Result.Output -match 'RPI5_METRIC\|(\d{3})\|(\d+)\|([0-9.]+)'
    if ($valid) {
        $http = [int]$Matches[1]; $bytes = [long]$Matches[2]
        $seconds = [double]::Parse($Matches[3], [Globalization.CultureInfo]::InvariantCulture)
    }
    $outcome = if ($http -ge 400) { 'ServerRejected' }
        elseif ($Result.TimedOut -or $Result.ExitCode -ne 0) { 'TransportFailed' }
        elseif (-not $valid -or $http -ne 200 -or $bytes -ne $ExpectedBytes -or $seconds -le 0) { 'InvalidResponse' }
        else { 'Complete' }
    [pscustomobject]@{ Outcome=$outcome; Http=$http; Bytes=$bytes; TransferSeconds=$seconds;
        ExitCode=$Result.ExitCode; TimedOut=$Result.TimedOut }
}
function Invoke-Rpi5RepeatedDownload {
    param([scriptblock]$Request, [scriptblock]$OnSample, [scriptblock]$Now,
        [int]$DurationSeconds=90, [int]$MaxRequests=128)
    # Fixed 1 MiB requests, sequential, with no retry of HTTP denial/rate limits.
    # Effective Mbps includes DNS/TLS, failed attempts and observation overhead.
    $watch = [Diagnostics.Stopwatch]::StartNew()
    if ($null -eq $Now) { $Now = { $watch.Elapsed.TotalSeconds }.GetNewClosure() }
    $start = & $Now; $attempt = 0; $completed = 0; $failures = 0; $consecutive = 0
    $verifiedBytes = 0L; $reason = 'TimeLimit'
    while ($attempt -lt $MaxRequests) {
        $elapsed = (& $Now) - $start
        $remaining = [math]::Floor($DurationSeconds - $elapsed)
        if ($remaining -lt 1) { break }
        $attempt++
        $result = & $Request ([int][math]::Min(15, $remaining))
        $sample = ConvertFrom-Rpi5DownloadResult $result
        $sample | Add-Member -NotePropertyName Attempt -NotePropertyValue $attempt
        $sample | Add-Member -NotePropertyName StartSeconds -NotePropertyValue $elapsed
        $sample | Add-Member -NotePropertyName EndSeconds -NotePropertyValue ((& $Now) - $start)
        if ($sample.Outcome -eq 'Complete') {
            $completed++; $consecutive=0; $verifiedBytes += $sample.Bytes
        } else { $failures++; $consecutive++ }
        if ($null -ne $OnSample) { & $OnSample $sample }
        if ($sample.Outcome -in @('ServerRejected','InvalidResponse')) { $reason=$sample.Outcome; break }
        if ($consecutive -ge 3) { $reason='RepeatedTransportFailure'; break }
        if ($attempt -eq $MaxRequests) { $reason='RequestByteCap' }
    }
    $elapsed = (& $Now) - $start
    $rate = if ($completed -gt 0 -and $elapsed -gt 0) { $verifiedBytes * 8 / $elapsed / 1000000 } else { $null }
    [pscustomobject]@{ StopReason=$reason; ElapsedSeconds=$elapsed; Attempts=$attempt;
        Completed=$completed; Failed=$failures; VerifiedBytes=$verifiedBytes; EffectiveMbps=$rate;
        Measurement='Sequential HTTPS workload; includes connection/setup overhead. Not PHY rate.' }
}
if ($LibraryOnly) { return }
$identity = [Security.Principal.WindowsIdentity]::GetCurrent()
$principal = [Security.Principal.WindowsPrincipal]::new($identity)
if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    $launch = @('-NoProfile','-ExecutionPolicy','Bypass','-File', ('"{0}"' -f $PSCommandPath))
    if ($NoPause) { $launch += '-NoPause' }
    Start-Process -FilePath (Join-Path $PSHOME 'powershell.exe') -Verb RunAs -ArgumentList $launch
    return
}
$resultDirectory = $null
try {
    if ($env:PROCESSOR_ARCHITECTURE -ne 'ARM64' -or
        -not (Get-CimInstance Win32_PnPEntity | Where-Object DeviceID -like 'ACPI\RPI0011\*')) {
        throw 'Run this utility on the Raspberry Pi 5 with the CYW43455 driver, not the development PC.'
    }
    Write-Output 'Performance utility 0.6.22 for installed exp0.6.14 or newer. This utility does not install drivers.'
    Write-Output 'Unplug wired Ethernet and disconnect VPNs for this test. No adapters or settings are changed.'
    Write-Output 'The test requests example.com and up to 129 MiB of download payload from speed.cloudflare.com (plus protocol overhead). Repeated-download stage: up to 90 seconds. No logs are uploaded.'
    # Never transcript credential entry. The existing utility owns credential
    # prompts/clearing; all saved performance output starts after it returns.
    try { & (Join-Path $PSScriptRoot 'Connect-RPi5-WiFi.ps1') }
    catch { Write-Warning "Connection utility: $($_.Exception.Message). Diagnostics will still be saved." }
    $desktop = [Environment]::GetFolderPath('Desktop')
    if (-not $desktop) { $desktop = $env:TEMP }
    $resultDirectory = Join-Path $desktop ('RPI5-WIFI-PERFORMANCE-' + (Get-Date -Format yyyyMMdd-HHmmss) + '-' + [guid]::NewGuid().ToString('N').Substring(0,6))
    [void](New-Item -ItemType Directory -Path $resultDirectory)
    $report = Join-Path $resultDirectory 'performance.txt'
    # Machine-wide counters, not a payload capture. Before/after helps locate
    # packets discarded after the miniport indication boundary. Other traffic
    # can contribute; never label these as exclusively this test's counters.
    try {
        $protocolBefore = Invoke-Rpi5BoundedProcess (Join-Path $env:windir 'System32\netstat.exe') @('-s') 10
        $protocolBefore | Format-List * | Out-String -Width 500 |
            Set-Content (Join-Path $resultDirectory 'windows-protocol-before.txt')
        Get-NetAdapter | Where-Object InterfaceDescription -like '*CYW43455*' |
            Get-NetAdapterBinding | Select-Object Name,DisplayName,ComponentID,Enabled |
            Format-Table -AutoSize | Out-String -Width 500 |
            Set-Content (Join-Path $resultDirectory 'adapter-bindings.txt')
    } catch { Write-Warning 'Optional Windows protocol/binding snapshot was unavailable.' }
    function Write-Report {
        param([string]$Text)
        $Text | Add-Content -LiteralPath $report -Encoding UTF8
        Write-Output $Text
    }
    function Save-Step {
        param([string]$Name, [string]$File, [string[]]$Arguments, [int]$Seconds)
        Write-Report "`r`n--- $Name --- UTC=$([datetime]::UtcNow.ToString('o'))"
        try {
            $result = Invoke-Rpi5BoundedProcess $File $Arguments $Seconds
            Write-Report "ExitCode=$($result.ExitCode) TimedOut=$($result.TimedOut)"
            Write-Report $result.Output
        } catch { Write-Report "TEST ERROR: $($_.Exception.Message)" }
    }
    $diagKey = 'HKLM:\SOFTWARE\Rpi5CywDirectDiag'
    Write-Report "Performance utility 0.6.22 report; UTC=$([datetime]::UtcNow.ToString('o'))"
    # One explicit radio GET snapshot BEFORE the measured workload, never from
    # the one-second sampler or during downloads. Older drivers remain usable.
    $radioTool=Join-Path $PSScriptRoot 'Get-RPi5-WiFi-Radio.ps1'
    $radioReady=$true
    $radioDriver=Get-ItemProperty -LiteralPath $diagKey -ErrorAction SilentlyContinue
    if ((Test-Path -LiteralPath $radioTool) -and $null -ne $radioDriver -and
        $radioDriver.PSObject.Properties['DiagVersion'] -and $radioDriver.DiagVersion -ge 17) {
        Write-Report 'Read-only radio snapshot before load:'
        try {
            $radioResult=Invoke-Rpi5BoundedProcess (Join-Path $PSHOME 'powershell.exe') @('-NoProfile','-ExecutionPolicy','Bypass','-File',$radioTool) 30
            Write-Report $radioResult.Output
            $radioReady= -not $radioResult.TimedOut -and $radioResult.ExitCode -eq 0
        } catch { $radioReady=$false;Write-Report "Radio query failed: $($_.Exception.Message)" }
    } else {
        Write-Report 'Radio snapshot unavailable: requires exp0.6.17+ and its radio utility. No band is assumed.'
    }
    Write-Report 'Counters are cumulative periodic driver snapshots, not atomic per-test measurements.'
    $before = Get-ItemProperty -LiteralPath $diagKey -ErrorAction SilentlyContinue
    $before | Format-List * | Out-String -Width 500 | Set-Content (Join-Path $resultDirectory 'driver-before.txt')
    Write-Report (Get-Rpi5BusAssessment $before)
    try {
        if (-not $radioReady) { throw 'Radio query completion was not confirmed; skip workload to avoid overlapping a pending query. Diagnostics will still be collected.' }
        $live = [Rpi5WifiControl]::Call(0x126004, $null)
        if ([BitConverter]::ToUInt32($live,8) -ne 0 -or [BitConverter]::ToUInt32($live,28) -ne 1) {
            throw 'Driver does not report a currently authenticated link. Network tests skipped.'
        }
        $adapters = @(Get-NetAdapter | Where-Object InterfaceDescription -like '*CYW43455*')
        if ($adapters.Count -ne 1) { throw 'Expected exactly one CYW43455 adapter.' }
        $adapter = $adapters[0]
        $ip = $null
        for ($attempt=0; $attempt -lt 30; $attempt++) {
            $ip = Get-NetIPAddress -InterfaceIndex $adapter.ifIndex -AddressFamily IPv4 -ErrorAction SilentlyContinue |
                Where-Object { $_.IPAddress -notlike '169.254.*' -and $_.AddressState -eq 'Preferred' } | Select-Object -First 1
            if ($null -ne $ip) { break }
            Start-Sleep -Seconds 2
        }
        if ($null -eq $ip) { throw 'No usable DHCP/IPv4 address after 60 seconds.' }
        $configuration = Get-NetIPConfiguration -InterfaceIndex $adapter.ifIndex
        Write-Report ($configuration | Format-List * | Out-String -Width 500)
        $routes = @(Get-NetRoute -AddressFamily IPv4 -DestinationPrefix '0.0.0.0/0' | Where-Object {
            $routeInterface = Get-NetIPInterface -InterfaceIndex $_.InterfaceIndex -AddressFamily IPv4 -ErrorAction SilentlyContinue
            $null -ne $routeInterface -and $routeInterface.ConnectionState -eq 'Connected'
        })
        if (-not (Test-Rpi5ExclusiveRoute $routes $adapter.ifIndex)) {
            throw 'No exclusive CYW43455 default route. Performance tests skipped to avoid measuring Ethernet/VPN. Unplug/disconnect the competing connection and rerun this same utility.'
        }
        # More-specific routes/VPN overrides can supersede the default route.
        $chosen = @(Find-NetRoute -RemoteIPAddress '1.1.1.1')
        if (@($chosen | Where-Object InterfaceIndex -ne $adapter.ifIndex).Count) {
            throw 'The selected internet route does not belong exclusively to CYW43455.'
        }
        $gateway = @($configuration.IPv4DefaultGateway)[0].NextHop
        $dns = @(Get-DnsClientServerAddress -InterfaceIndex $adapter.ifIndex -AddressFamily IPv4).ServerAddresses | Select-Object -First 1
        $system = Join-Path $env:windir 'System32'
        Save-Step 'Gateway latency/loss (8 requests)' (Join-Path $system 'ping.exe') @('-4','-n','8','-w','1000',$gateway) 25
        Save-Step 'Internet IPv4 latency/loss (8 requests)' (Join-Path $system 'ping.exe') @('-4','-n','8','-w','1000','1.1.1.1') 25
        if ($dns) { Save-Step 'Configured DNS server, A query' (Join-Path $system 'nslookup.exe') @('-type=A','-timeout=2','-retry=1','example.com',$dns) 15 }
        if ($dns) { Save-Step 'Download hostname, configured DNS server, A query' (Join-Path $system 'nslookup.exe') @('-type=A','-timeout=2','-retry=1','speed.cloudflare.com',$dns) 15 }
        $common = @('-4','--noproxy','*','--interface',$ip.IPAddress,'--connect-timeout','10','--silent','--show-error')
        Save-Step 'HTTPS headers; certificate verification enabled' (Join-Path $system 'curl.exe') ($common + @('--max-time','25','-I','-w','dns_seconds=%{time_namelookup} tcp_seconds=%{time_connect} tls_seconds=%{time_appconnect} first_byte_seconds=%{time_starttransfer} total_seconds=%{time_total}','https://example.com')) 30
        Save-Step '1 MiB bounded HTTPS download; bytes/sec is application throughput' (Join-Path $system 'curl.exe') ($common + @('--max-time','45','--fail','-o','NUL','-w','http=%{http_code} bytes=%{size_download} bytes_per_second=%{speed_download} total_seconds=%{time_total}','https://speed.cloudflare.com/__down?bytes=1048576')) 50
        # Sampling is independent of curl so long requests cannot hide pauses.
        # The sampler is read-only and has its own parent/lifetime guards.
        $sampler = $null
        try {
            $samplerArgs = @('-NoProfile','-ExecutionPolicy','Bypass','-File',('"{0}"' -f (Join-Path $PSScriptRoot 'Measure-RPi5-WiFi-Load.ps1')),
                '-InterfaceIndex',$adapter.ifIndex,'-Gateway',$gateway,'-OutputDirectory',('"{0}"' -f $resultDirectory),
                '-OwnerPid',$PID,'-OwnerStartTicks',(Get-Process -Id $PID).StartTime.ToUniversalTime().Ticks)
            $sampler = Start-Process -FilePath (Join-Path $PSHOME 'powershell.exe') -ArgumentList $samplerArgs -WindowStyle Hidden -PassThru -RedirectStandardOutput (Join-Path $resultDirectory 'sampler-output.txt') -RedirectStandardError (Join-Path $resultDirectory 'sampler-errors.txt')
            for ($wait=0; $wait -lt 10; $wait++) {
                if ((Test-Path -LiteralPath (Join-Path $resultDirectory 'sampling.ready')) -or $sampler.HasExited) { break }
                Start-Sleep -Seconds 1
            }
            if (-not (Test-Path -LiteralPath (Join-Path $resultDirectory 'sampling.ready'))) { throw 'Load sampler did not start. See sampler-errors.txt; no sustained speed result.' }
            Write-Report "Repeated-download START UTC=$([datetime]::UtcNow.ToString('o')); 1 MiB/request, 90s or 128 requests, whichever comes first."
            $request = {
                param($seconds)
                Invoke-Rpi5BoundedProcess (Join-Path $system 'curl.exe') ($common + @('--max-time',"$seconds",'--max-filesize','1048576','--fail','-o','NUL','-w','RPI5_METRIC|%{http_code}|%{size_download}|%{time_total}','https://speed.cloudflare.com/__down?bytes=1048576')) ($seconds + 1)
            }
            $observe = {
                param($sample)
                $sample | Add-Member -NotePropertyName EndUtc -NotePropertyValue ([datetime]::UtcNow.ToString('o'))
                $sample | Export-Csv -LiteralPath (Join-Path $resultDirectory 'download-samples.csv') -NoTypeInformation -Append -Encoding UTF8
                Write-Information ("Download {0}: {1}, HTTP {2}, {3} bytes, {4:N2}s" -f $sample.Attempt,$sample.Outcome,$sample.Http,$sample.Bytes,$sample.TransferSeconds) -InformationAction Continue
            }
            $summary = Invoke-Rpi5RepeatedDownload -Request $request -OnSample $observe
            $summary | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $resultDirectory 'load-summary.json') -Encoding UTF8
            Write-Report ($summary | Format-List * | Out-String)
            Write-Report "Repeated-download END UTC=$([datetime]::UtcNow.ToString('o'))"
            Write-Report 'HTTP rejection is a server response, not a measured Wi-Fi speed failure. Partial/failed bodies do not contribute to verified-byte throughput. A short/failed workload does not establish sustained stability.'
        } finally {
            if ($null -ne $sampler) {
                'stop' | Set-Content -LiteralPath (Join-Path $resultDirectory 'sampling.stop')
                if (-not $sampler.WaitForExit(5000)) { $sampler.Kill(); $sampler.WaitForExit(); Write-Report 'Load sampler stopped at deadline; partial samples saved.' }
                Write-Report "Load sampler exit=$($sampler.ExitCode)"
                $sampler.Dispose()
            }
        }
        try {
            Get-NetAdapterStatistics -Name $adapter.Name | Format-List * | Out-String -Width 500 |
                Set-Content (Join-Path $resultDirectory 'adapter-traffic-after.txt')
        } catch { Write-Report 'Windows traffic statistics unavailable; retain driver counters.' }
    } catch { Write-Report "PERFORMANCE INCOMPLETE: $($_.Exception.Message)" }
    try {
        $protocolAfter = Invoke-Rpi5BoundedProcess (Join-Path $env:windir 'System32\netstat.exe') @('-s') 10
        $protocolAfter | Format-List * | Out-String -Width 500 |
            Set-Content (Join-Path $resultDirectory 'windows-protocol-after.txt')
    } catch { Write-Report 'Optional Windows protocol counters were unavailable.' }
    Write-Report 'Waiting up to 35 seconds for a driver snapshot newer than the END of the tests (no device restart).'
    # Comparing against the pre-test stamp can accept a snapshot taken halfway
    # through the tests and omit the download's congestion/errors.
    $endSnapshot = Get-ItemProperty -LiteralPath $diagKey -ErrorAction SilentlyContinue
    $stamp = if ($null -ne $endSnapshot) { $endSnapshot.SnapshotTimeUtc } else { 0 }
    $timingEndStamp=0
    try { $timingEndStamp=(ConvertFrom-Rpi5Timing $endSnapshot).SnapshotQpc } catch { $timingEndStamp=0 }
    $freshSnapshot = $false
    for ($attempt=0; $attempt -lt 35; $attempt++) {
        $after = Get-ItemProperty -LiteralPath $diagKey -ErrorAction SilentlyContinue
        if (Test-Rpi5NewerSnapshot $after $stamp) {
            $timingFresh=$true
            if($after.DiagVersion -ge 22){
                try { $timingFresh=(ConvertFrom-Rpi5Timing $after).SnapshotQpc -gt $timingEndStamp }
                catch { $timingFresh=$false }
            }
            if($timingFresh){$freshSnapshot = $true; break}
        }
        Start-Sleep -Seconds 1
    }
    Write-Report "Post-test driver snapshot refreshed=$freshSnapshot. Counters remain periodic/non-atomic."
    $after | Format-List * | Out-String -Width 500 | Set-Content (Join-Path $resultDirectory 'driver-after.txt')
    try {
        $timing=Get-Rpi5TimingReport -Before $before -After $after
        $timing | ConvertTo-Json -Depth 5 | Set-Content (Join-Path $resultDirectory 'timing-report.json') -Encoding UTF8
        $timing.Rows | Format-Table -AutoSize | Out-String -Width 500 |
            Set-Content (Join-Path $resultDirectory 'timing-report.txt')
        Write-Report $timing.Notice
        Write-Report "Timing snapshots comparable=$($timing.ComparableSnapshots); see timing-report.json."
    } catch { Write-Report "Optional runtime timing unavailable: $($_.Exception.Message)" }
    try {
        $collection = Invoke-Rpi5BoundedProcess (Join-Path $PSHOME 'powershell.exe') @('-NoProfile','-ExecutionPolicy','Bypass','-File',(Join-Path $PSScriptRoot 'Collect-RPi5-WiFi-Diagnostics.ps1'),'-NoPause','-OutputDirectory',$resultDirectory) 180
        Write-Report "Diagnostic collection: ExitCode=$($collection.ExitCode) TimedOut=$($collection.TimedOut)"
        Write-Report $collection.Output
    }
    catch { Write-Report "DIAGNOSTICS ERROR: $($_.Exception.Message)" }
    $zip = "$resultDirectory.zip"
    Compress-Archive -Path (Join-Path $resultDirectory '*') -DestinationPath $zip
    Write-Output "`r`nShare this one report ZIP: $zip"
    Write-Output 'It contains local network addresses/device logs. Review before sharing publicly. No password was saved.'
} catch {
    Write-Output "Test could not complete: $($_.Exception.Message)"
    if ($resultDirectory) { Write-Output "Partial results: $resultDirectory" }
} finally { if (-not $NoPause) { [void](Read-Host 'Press Enter to close') } }
