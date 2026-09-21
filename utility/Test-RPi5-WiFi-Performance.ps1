[CmdletBinding()]
param([switch]$LibraryOnly, [switch]$NoPause)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function Get-Rpi5BusAssessment {
    param($Diagnostic)
    if ($null -eq $Diagnostic -or -not $Diagnostic.PSObject.Properties['BusModeStage']) {
        return 'Updated runtime diagnostics missing. Restart after installing exp0.6.10.'
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
    Write-Output 'exp0.6.10: connection, bus, gateway, DNS, HTTPS and bounded download test.'
    Write-Output 'Unplug wired Ethernet and disconnect VPNs for this test. No adapters or settings are changed.'
    Write-Output 'The test requests example.com and about 1 MiB from speed.cloudflare.com. No logs are uploaded.'
    # Never transcript credential entry. The existing utility owns credential
    # prompts/clearing; all saved performance output starts after it returns.
    try { & (Join-Path $PSScriptRoot 'Connect-RPi5-WiFi.ps1') }
    catch { Write-Warning "Connection utility: $($_.Exception.Message). Diagnostics will still be saved." }
    $desktop = [Environment]::GetFolderPath('Desktop')
    if (-not $desktop) { $desktop = $env:TEMP }
    $resultDirectory = Join-Path $desktop ('RPI5-WIFI-PERFORMANCE-' + (Get-Date -Format yyyyMMdd-HHmmss) + '-' + [guid]::NewGuid().ToString('N').Substring(0,6))
    [void](New-Item -ItemType Directory -Path $resultDirectory)
    $report = Join-Path $resultDirectory 'performance.txt'
    function Write-Report {
        param([string]$Text)
        $Text | Add-Content -LiteralPath $report -Encoding UTF8
        Write-Output $Text
    }
    function Save-Step {
        param([string]$Name, [string]$File, [string[]]$Arguments, [int]$Seconds)
        Write-Report "`r`n--- $Name ---"
        try {
            $result = Invoke-Rpi5BoundedProcess $File $Arguments $Seconds
            Write-Report "ExitCode=$($result.ExitCode) TimedOut=$($result.TimedOut)"
            Write-Report $result.Output
        } catch { Write-Report "TEST ERROR: $($_.Exception.Message)" }
    }
    $diagKey = 'HKLM:\SOFTWARE\Rpi5CywDirectDiag'
    Write-Report "exp0.6.10 performance report; UTC=$([datetime]::UtcNow.ToString('o'))"
    Write-Report 'Counters are cumulative periodic driver snapshots, not atomic per-test measurements.'
    $before = Get-ItemProperty -LiteralPath $diagKey -ErrorAction SilentlyContinue
    $before | Format-List * | Out-String -Width 500 | Set-Content (Join-Path $resultDirectory 'driver-before.txt')
    Write-Report (Get-Rpi5BusAssessment $before)
    try {
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
        $common = @('-4','--noproxy','*','--interface',$ip.IPAddress,'--connect-timeout','10','--silent','--show-error')
        Save-Step 'HTTPS headers; certificate verification enabled' (Join-Path $system 'curl.exe') ($common + @('--max-time','25','-I','https://example.com')) 30
        Save-Step '1 MiB bounded HTTPS download; bytes/sec is application throughput' (Join-Path $system 'curl.exe') ($common + @('--max-time','45','--fail','-o','NUL','-w','http=%{http_code} bytes=%{size_download} bytes_per_second=%{speed_download} total_seconds=%{time_total}','https://speed.cloudflare.com/__down?bytes=1048576')) 50
    } catch { Write-Report "PERFORMANCE INCOMPLETE: $($_.Exception.Message)" }
    Write-Report 'Waiting up to 35 seconds for a later driver snapshot (no device restart).'
    $stamp = if ($null -ne $before) { $before.SnapshotTimeUtc } else { 0 }
    for ($attempt=0; $attempt -lt 35; $attempt++) {
        $after = Get-ItemProperty -LiteralPath $diagKey -ErrorAction SilentlyContinue
        if ($null -ne $after -and $after.SnapshotTimeUtc -ne $stamp) { break }
        Start-Sleep -Seconds 1
    }
    $after | Format-List * | Out-String -Width 500 | Set-Content (Join-Path $resultDirectory 'driver-after.txt')
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
