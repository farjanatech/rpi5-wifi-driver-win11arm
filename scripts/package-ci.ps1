param(
    [Parameter(Mandatory=$true)][string]$Configuration,
    [Parameter(Mandatory=$true)][string]$Platform
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent $PSScriptRoot
$stage = Join-Path $root 'artifacts\rpi5cyw-test-driver'
if (Test-Path -LiteralPath $stage) { throw 'Driver staging directory already exists; use a clean CI checkout.' }
New-Item -ItemType Directory -Path $stage -Force | Out-Null

function Find-Tool([string]$Name) {
    $candidates = @()
    $candidates += Get-ChildItem (Join-Path $root 'packages') -Filter $Name -File -Recurse -ErrorAction SilentlyContinue
    $kits = "${env:ProgramFiles(x86)}\Windows Kits\10\bin"
    if (Test-Path $kits) {
        $candidates += Get-ChildItem $kits -Filter $Name -File -Recurse -ErrorAction SilentlyContinue
    }
    $preferred = $candidates | Where-Object { $_.FullName -match '\\x64\\' } | Sort-Object FullName -Descending | Select-Object -First 1
    if (-not $preferred) { $preferred = $candidates | Sort-Object FullName -Descending | Select-Object -First 1 }
    return $preferred
}

$sys = Get-ChildItem $root -Filter 'rpi5cyw.sys' -File -Recurse -ErrorAction SilentlyContinue |
    Where-Object { $_.FullName -notmatch '\\artifacts\\|\\packages\\' } |
    Where-Object { $_.FullName -match ('\\'+[regex]::Escape($Platform)+'\\'+[regex]::Escape($Configuration)+'\\') } |
    Sort-Object LastWriteTime -Descending | Select-Object -First 1
if (-not $sys) { throw 'rpi5cyw.sys was not produced by MSBuild.' }

$inf = Join-Path $root 'package\rpi5cyw.inf'
if (-not (Test-Path $inf)) { throw 'package\rpi5cyw.inf is missing.' }

Copy-Item $sys.FullName (Join-Path $stage 'rpi5cyw.sys') -Force
Copy-Item $inf (Join-Path $stage 'rpi5cyw.inf') -Force
Copy-Item (Join-Path $root 'LICENSE') (Join-Path $stage 'LICENSE') -Force
Copy-Item (Join-Path $root 'THIRD_PARTY_NOTICES.md') (Join-Path $stage 'THIRD_PARTY_NOTICES.md') -Force
Copy-Item (Join-Path $root 'diagnostics\Collect-RPi5-WiFi-Diagnostics.ps1') (Join-Path $stage 'Collect-RPi5-WiFi-Diagnostics.ps1') -Force
Copy-Item (Join-Path $root 'diagnostics\Run-RPi5-WiFi-Diagnostics.cmd') (Join-Path $stage 'Run-RPi5-WiFi-Diagnostics.cmd') -Force
& (Join-Path $PSScriptRoot 'fetch-firmware.ps1') -Destination $stage
& (Join-Path $root 'tests\firmware_package_tests.ps1') -Directory $stage
Copy-Item (Join-Path $root 'utility\Connect-RPi5-WiFi.ps1') $stage
Copy-Item (Join-Path $root 'utility\Connect-RPi5-WiFi.cmd') $stage
foreach ($name in @('Set-RPi5-WiFi-Autoconnect.ps1', 'Enable-RPi5-WiFi-Autoconnect.cmd',
    'Disable-RPi5-WiFi-Autoconnect.cmd', 'WiFi.config.example.json',
    'RPi5-WiFi-Operations.ps1','Check-RPi5-WiFi-Readiness.ps1','Check-RPi5-WiFi-Readiness.cmd')) {
    Copy-Item (Join-Path $root "utility\$name") $stage
}
foreach ($name in @('RPi5-WiFi-App.cmd','RPi5-WiFi-App.ps1','RPi5-WiFi-Scan.ps1')) {
    Copy-Item (Join-Path $root "utility\$name") $stage
}
Copy-Item (Join-Path $root 'docs\AUTO-CONNECT.md') $stage
Copy-Item (Join-Path $root 'utility\Test-RPi5-WiFi-Performance.ps1') $stage
Copy-Item (Join-Path $root 'utility\Test-RPi5-WiFi-Performance.cmd') $stage
Copy-Item (Join-Path $root 'utility\Measure-RPi5-WiFi-Load.ps1') $stage
Copy-Item (Join-Path $root 'utility\RPi5-WiFi-DownloadTiming.ps1') $stage
Copy-Item (Join-Path $root 'utility\RPi5-WiFi-MeasurementClock.ps1') $stage
Copy-Item (Join-Path $root 'utility\Get-RPi5-WiFi-Radio.ps1') $stage
Copy-Item (Join-Path $root 'utility\Get-RPi5-WiFi-Radio.cmd') $stage
Copy-Item (Join-Path $root 'utility\Get-RPi5-WiFi-Timing.ps1') $stage
Copy-Item (Join-Path $root 'utility\Get-RPi5-WiFi-Transport.ps1') $stage
Copy-Item (Join-Path $root 'docs\PERFORMANCE-0.6.14.1.md') $stage
Copy-Item (Join-Path $root 'docs\PERFORMANCE-0.6.24.md') $stage
Copy-Item (Join-Path $root 'docs\PERFORMANCE-0.6.25.md') $stage
Copy-Item (Join-Path $root 'docs\INTEGRATED-TESTING.md') $stage
Copy-Item (Join-Path $root 'docs\EXP0.6.24.md') $stage
Copy-Item (Join-Path $root 'docs\EXP0.6.25.md') $stage
Copy-Item (Join-Path $root 'docs\EXP0.6.26.md') $stage
Copy-Item (Join-Path $root 'docs\PERFORMANCE-0.6.26.md') $stage
Copy-Item (Join-Path $root 'docs\EXP0.6.27.md') $stage
Copy-Item (Join-Path $root 'docs\PERFORMANCE-0.6.27.md') $stage
Copy-Item (Join-Path $root 'docs\EXP0.6.28.md') $stage
Copy-Item (Join-Path $root 'docs\EXP0.6.29.md') $stage
Copy-Item (Join-Path $root 'docs\PERFORMANCE-0.7.0.md') $stage
Copy-Item (Join-Path $root 'docs\PERFORMANCE-0.6.27.1.md') $stage

$pdb = Get-ChildItem $root -Filter 'rpi5cyw.pdb' -File -Recurse -ErrorAction SilentlyContinue |
    Where-Object { $_.FullName -notmatch '\\artifacts\\|\\packages\\' } |
    Where-Object { $_.FullName -match ('\\'+[regex]::Escape($Platform)+'\\'+[regex]::Escape($Configuration)+'\\') } |
    Sort-Object LastWriteTime -Descending | Select-Object -First 1
if ($pdb) { Copy-Item $pdb.FullName (Join-Path $stage 'rpi5cyw.pdb') -Force }

$signtool = Find-Tool 'signtool.exe'
$inf2cat = Find-Tool 'inf2cat.exe'
if (-not $signtool) { throw 'signtool.exe was not found.' }
if (-not $inf2cat) { throw 'inf2cat.exe was not found.' }

$subject = 'CN=RPI5 CYW43455 GitHub Test Driver'
$cert = New-SelfSignedCertificate -Type CodeSigningCert -Subject $subject `
    -CertStoreLocation 'Cert:\CurrentUser\My' -KeyExportPolicy Exportable `
    -HashAlgorithm SHA256 -NotAfter (Get-Date).AddYears(2)

$cerPath = Join-Path $stage 'rpi5cyw-test.cer'
Export-Certificate -Cert $cert -FilePath $cerPath -Force | Out-Null

& $signtool.FullName sign /v /fd SHA256 /sha1 $cert.Thumbprint (Join-Path $stage 'rpi5cyw.sys')
if ($LASTEXITCODE -ne 0) { throw "SignTool failed for SYS with exit code $LASTEXITCODE" }

& $inf2cat.FullName /driver:$stage /os:10_25H2_ARM64,10_GE_ARM64,10_NI_ARM64 /verbose
if ($LASTEXITCODE -ne 0) { throw "Inf2Cat failed with exit code $LASTEXITCODE" }

$cat = Get-ChildItem $stage -Filter '*.cat' -File | Select-Object -First 1
if (-not $cat) { throw 'Inf2Cat succeeded but no catalog was produced.' }
& $signtool.FullName sign /v /fd SHA256 /sha1 $cert.Thumbprint $cat.FullName
if ($LASTEXITCODE -ne 0) { throw "SignTool failed for CAT with exit code $LASTEXITCODE" }

@"
Raspberry Pi 5 CYW43455 Windows 11 ARM64 - performance candidate 0.7.0

Configuration: $Configuration
Platform:      $Platform
Commit:        $env:GITHUB_SHA
Workflow run:  $env:GITHUB_SERVER_URL/$env:GITHUB_REPOSITORY/actions/runs/$env:GITHUB_RUN_ID

Isolated branch: feature/rpi-os-wifi-performance
Baseline: driver exp0.6.29.1 / connector exp0.6.29.2 (unchanged releases)
Architecture: ACPI\\RPI0011 -> NDIS Ethernet miniport -> direct SDHCI -> CYW43455

New: negotiated F2 multi-block PIO, validated RX read-ahead, complete-before-
delivery RX aggregate parsing. No hardware speed or reliability guarantee.
See PERFORMANCE-0.7.0.md for implementation, limitations, tests and rollback.
Interrupt/DMA/DDR50 support is NOT implemented in this candidate.
Firmware 7.45.229, matching CLM/calibration, country/band policy, queue limits,
authentication, connector ABI and the working UEFI are unchanged.

Install only on the Pi: extract the whole ZIP, run Install-RPi5-WiFi-Driver.cmd
and approve elevation. Save work and restart if requested. Continue using the
existing connector exp0.6.29.2 EXE/profile; it is not bundled/rebuilt here.
The installer checks package hashes, signer, ARM64, ACPI device, matching UEFI
and existing security prerequisites. It does not enable Test Signing, change
Secure Boot/BCD/UEFI, delete old drivers, or replace private credentials.
This test-signed driver requires the already configured test environment.
Its verified test certificate is added to Root/TrustedPublisher on installation.

Keep working exp0.6.29.1 for rollback. A lower-version installer alone may not
select an older driver: use the exact adapter's Roll Back Driver, or Have Disk
with the previous INF if necessary. Never remove unrelated network/storage
drivers. No Windows reinstall, router rename or UEFI change is needed.

After connecting with the existing app, Check-RPi5-WiFi-Readiness.cmd collects
the existing bounded workload and diagnostics once. Logs stay local. Keep wired
Ethernet available for recovery, but unplug it during throughput measurement.
New counters in driver-before/after.txt distinguish fast-path use from mere
enablement. Passing CI is not hardware certification; compare on the same Pi,
router, band/channel and workload before choosing this over the stable branch.

Historical EXP/PERFORMANCE documents in the ZIP describe earlier versions;
PERFORMANCE-0.7.0.md is authoritative for this candidate.
"@ | Set-Content (Join-Path $stage 'README-TESTING.txt') -Encoding UTF8

@"
driver_repository=$env:GITHUB_REPOSITORY
driver_version=0.7.0
performance_branch=feature/rpi-os-wifi-performance
performance_baseline=4f8f456b1b72d7f6b534531b02d83863ae9ed60c
measurement_utility_version=0.6.27.1
startup_receipt_compatibility=0.6.27
build_configuration=$Configuration
timing_default=worker-only; detailed command and receive-indication clocks disabled
driver_commit=$env:GITHUB_SHA
workflow_run=$env:GITHUB_SERVER_URL/$env:GITHUB_REPOSITORY/actions/runs/$env:GITHUB_RUN_ID
reactos_reference=9130f67a8e8c759da5acbbfe613f776b07b21698
matching_acpi_id=ACPI\\RPI0011
"@ | Set-Content (Join-Path $stage 'SOURCE_REVISION.txt') -Encoding UTF8

@'
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$me = [Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()
if (-not $me.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) { throw 'Run as Administrator.' }
if ([Runtime.InteropServices.RuntimeInformation]::OSArchitecture -ne [Runtime.InteropServices.Architecture]::Arm64) {
    throw 'This experimental package is only for Windows ARM64 on Raspberry Pi 5.'
}
$dir = Split-Path -Parent $MyInvocation.MyCommand.Path
$cer = Join-Path $dir 'rpi5cyw-test.cer'
$inf = Join-Path $dir 'rpi5cyw.inf'
$sys = Join-Path $dir 'rpi5cyw.sys'
$cat = Join-Path $dir 'rpi5cyw.cat'

$device = Get-PnpDevice -PresentOnly:$false -ErrorAction SilentlyContinue |
    Where-Object { $_.InstanceId -match '^ACPI\\RPI0011(?:\\|$)' } |
    Select-Object -First 1
if (-not $device) {
    throw 'ACPI\\RPI0011 was not found. Install only with the matching direct-SDIO UEFI.'
}

try {
    if (Confirm-SecureBootUEFI) {
        throw 'Secure Boot is enabled. This test-signed driver cannot be used safely in that state.'
    }
} catch [System.PlatformNotSupportedException] {
    Write-Warning 'Secure Boot status is unavailable on this firmware.'
}

$boot = (& bcdedit.exe /enum '{current}' 2>&1 | Out-String)
if ($LASTEXITCODE -ne 0 -or $boot -notmatch '(?im)^testsigning\s+Yes\s*$') {
    throw 'Windows Test Signing is not enabled. This installer will not change boot security settings.'
}

$certificate = [Security.Cryptography.X509Certificates.X509Certificate2]::new($cer)
foreach ($signedFile in @($sys, $cat)) {
    $signature = Get-AuthenticodeSignature -LiteralPath $signedFile
    if (-not $signature.SignerCertificate -or
        $signature.SignerCertificate.Thumbprint -ne $certificate.Thumbprint) {
        throw "Signer mismatch for $signedFile. Refusing to trust or install this package."
    }
}

certutil.exe -addstore -f Root $cer
if ($LASTEXITCODE -ne 0) { throw 'Failed to trust the test certificate in LocalMachine Root.' }
certutil.exe -addstore -f TrustedPublisher $cer
if ($LASTEXITCODE -ne 0) { throw 'Failed to trust the test certificate in LocalMachine TrustedPublisher.' }
pnputil.exe /add-driver $inf /install
if ($LASTEXITCODE -ne 0) { throw "PnPUtil rejected the driver package: $LASTEXITCODE" }
pnputil.exe /scan-devices
Write-Host 'Direct-SDIO driver installation attempted.'
Write-Host 'Run collect-direct-sdio-diagnostics.ps1 next.'
'@ | Set-Content (Join-Path $stage 'install-test-driver.ps1') -Encoding UTF8

# Replace the minimal CI installer with the audited self-elevating one-click installer.
Copy-Item (Join-Path $root 'installer\Install-RPi5-WiFi-Driver.ps1') (Join-Path $stage 'install-test-driver.ps1') -Force
Copy-Item (Join-Path $root 'installer\Install-RPi5-WiFi-Driver.cmd') (Join-Path $stage 'Install-RPi5-WiFi-Driver.cmd') -Force

@'
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Continue'
$dir = Split-Path -Parent $MyInvocation.MyCommand.Path
$stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
$out = Join-Path $dir "direct-sdio-diagnostics-$stamp"
New-Item -ItemType Directory -Path $out -Force | Out-Null

function Capture([string]$Name, [scriptblock]$Command) {
    try { & $Command 2>&1 | Out-String -Width 500 | Set-Content (Join-Path $out $Name) -Encoding UTF8 }
    catch { ($_ | Out-String) | Set-Content (Join-Path $out $Name) -Encoding UTF8 }
}

Capture 'DIRECT-SDIO-RESULT.txt' {
    'Raspberry Pi 5 CYW43455 DIRECT SDIO / NDIS diagnostic'
    ''
    $key = 'HKLM:\SOFTWARE\Rpi5CywDirectDiag'
    if (Test-Path $key) {
        $d = Get-ItemProperty $key
        "Stage=$($d.Stage)"
        ('LastStatus=0x{0:X8}' -f ([uint32]$d.LastStatus))
        ('MMIO=0x{0:X8}{1:X8} Length=0x{2:X}' -f ([uint32]$d.RegPhysHi),([uint32]$d.RegPhysLo),([uint32]$d.RegLength))
        ('HostVersion=0x{0:X4} Capabilities=0x{1:X8} Capabilities2=0x{2:X8}' -f ([uint32]$d.HostVersion),([uint32]$d.Capabilities),([uint32]$d.Capabilities2))
        ('CMD5 probe=0x{0:X8} SDIO OCR=0x{1:X8} functions={2}' -f ([uint32]$d.Cmd5ProbeResponse),([uint32]$d.SdioOcr),$d.SdioFunctions)
        ('RCA=0x{0:X4}' -f ([uint32]$d.RelativeAddress))
        ('CCCR rev=0x{0:X2} IOEx=0x{1:X2} IORx=0x{2:X2} F1 IF=0x{3:X2} F2 IF=0x{4:X2}' -f ([uint32]$d.CccrRevision),([uint32]$d.IoEnable),([uint32]$d.IoReady),([uint32]$d.F1InterfaceCode),([uint32]$d.F2InterfaceCode))
        ''
        if ([int]$d.Stage -ge 90 -and [uint32]$d.LastStatus -eq 0) {
            'DIRECT SDIO RESULT: CMD52 PATH REACHED SUCCESSFULLY'
            'This proves host-to-CYW SDIO command communication only; it is not yet working Wi-Fi.'
        } else {
            'DIRECT SDIO RESULT: PROBE DID NOT REACH COMPLETE CMD52 READS'
            'Use LastCommand/LastArgument/LastInterruptStatus/LastResponse from registry.txt to locate the hardware/protocol failure.'
        }
    } else {
        'DIRECT SDIO RESULT: DRIVER DIAGNOSTIC REGISTRY KEY NOT FOUND'
    }
}
Capture 'registry.txt' { reg.exe query 'HKLM\SOFTWARE\Rpi5CywDirectDiag' /s }
Capture 'windows.txt' { Get-ComputerInfo | Format-List WindowsProductName,WindowsVersion,OsBuildNumber,OsArchitecture,BiosFirmwareType,BiosVersion }
Capture 'bcdedit.txt' { bcdedit /enum '{current}' }
Capture 'pnp-rpi5wifi.txt' {
    Get-PnpDevice -PresentOnly:$false -ErrorAction SilentlyContinue |
      Where-Object { $_.InstanceId -match 'RPI0011' -or $_.FriendlyName -match 'CYW43455|Direct SDIO' } |
      Format-List *
}
Capture 'pnp-properties.txt' {
    $dev = Get-PnpDevice -PresentOnly:$false -ErrorAction SilentlyContinue | Where-Object { $_.InstanceId -match 'RPI0011' } | Select-Object -First 1
    if ($dev) {
        $dev | Format-List *
        Get-PnpDeviceProperty -InstanceId $dev.InstanceId -ErrorAction SilentlyContinue | Format-Table KeyName,Type,Data -AutoSize
    } else { 'ACPI RPI0011 device not found' }
}
Capture 'service.txt' { sc.exe query rpi5cyw; sc.exe qc rpi5cyw; reg.exe query 'HKLM\SYSTEM\CurrentControlSet\Services\rpi5cyw' /s }
Capture 'pnputil.txt' { pnputil /enum-devices /connected; pnputil /enum-devices /problem; pnputil /enum-drivers }
Capture 'netadapters.txt' { Get-NetAdapter -IncludeHidden | Format-List Name,InterfaceDescription,Status,LinkSpeed,MacAddress,DriverInformation,DriverFileName,PnPDeviceID }
Capture 'system-events.txt' {
    $start=(Get-Date).AddHours(-6)
    Get-WinEvent -FilterHashtable @{LogName='System';StartTime=$start} -ErrorAction SilentlyContinue |
      Where-Object { $_.ProviderName -match 'Kernel-PnP|NDIS|Service Control Manager' -or $_.Message -match 'RPI0011|rpi5cyw|CYW43455' } |
      Select-Object TimeCreated,Id,LevelDisplayName,ProviderName,Message | Format-List
}
$setup = Join-Path $env:windir 'INF\setupapi.dev.log'
if (Test-Path $setup) { Copy-Item $setup (Join-Path $out 'setupapi.dev.log') -Force }
$zip = Join-Path $dir "RPI5-CYW43455-DIRECT-SDIO-DIAGNOSTICS-$stamp.zip"
Compress-Archive -Path (Join-Path $out '*') -DestinationPath $zip -Force
Write-Host "Diagnostics: $zip"
'@ | Set-Content (Join-Path $stage 'collect-direct-sdio-diagnostics.ps1') -Encoding UTF8

# Keep the original command name, but route it to the audited one-click collector.
@'
& (Join-Path $PSScriptRoot 'Collect-RPi5-WiFi-Diagnostics.ps1') @args
exit $LASTEXITCODE
'@ | Set-Content (Join-Path $stage 'collect-direct-sdio-diagnostics.ps1') -Encoding UTF8

Get-ChildItem $stage -File | Sort-Object Name | ForEach-Object {
    $hash = Get-FileHash $_.FullName -Algorithm SHA256
    "$($hash.Hash)  $($_.Name)"
} | Set-Content (Join-Path $stage 'SHA256SUMS.txt') -Encoding ASCII

Write-Host "Packaged direct-SDIO NDIS driver at $stage"
