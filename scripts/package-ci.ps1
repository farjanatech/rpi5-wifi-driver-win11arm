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
    'Disable-RPi5-WiFi-Autoconnect.cmd', 'WiFi.config.example.json')) {
    Copy-Item (Join-Path $root "utility\$name") $stage
}
Copy-Item (Join-Path $root 'docs\AUTO-CONNECT.md') $stage
Copy-Item (Join-Path $root 'utility\Test-RPi5-WiFi-Performance.ps1') $stage
Copy-Item (Join-Path $root 'utility\Test-RPi5-WiFi-Performance.cmd') $stage
Copy-Item (Join-Path $root 'utility\Measure-RPi5-WiFi-Load.ps1') $stage
Copy-Item (Join-Path $root 'utility\Get-RPi5-WiFi-Radio.ps1') $stage
Copy-Item (Join-Path $root 'utility\Get-RPi5-WiFi-Radio.cmd') $stage
Copy-Item (Join-Path $root 'utility\Get-RPi5-WiFi-Timing.ps1') $stage
Copy-Item (Join-Path $root 'utility\Get-RPi5-WiFi-Transport.ps1') $stage
Copy-Item (Join-Path $root 'docs\PERFORMANCE-0.6.14.1.md') $stage
Copy-Item (Join-Path $root 'docs\PERFORMANCE-0.6.24.md') $stage
Copy-Item (Join-Path $root 'docs\INTEGRATED-TESTING.md') $stage
Copy-Item (Join-Path $root 'docs\EXP0.6.24.md') $stage

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
Raspberry Pi 5 CYW43455 Windows 11 ARM64 - direct SDIO / NDIS build

Configuration: $Configuration
Platform:      $Platform
Commit:        $env:GITHUB_SHA
Workflow run:  $env:GITHUB_SERVER_URL/$env:GITHUB_REPOSITORY/actions/runs/$env:GITHUB_RUN_ID

Architecture:
  ACPI\\RPI0011 -> NDIS 6.30 Ethernet miniport -> direct Pi 5 SDHCI -> CYW43455

This package deliberately does NOT depend on Microsoft sdbus and does not bind
to SD\\VID_02D0 child IDs. It maps the SDIO2 MMIO resource itself and performs
CMD0/CMD5/CMD3/CMD7/CMD52 and bounded CMD53 chip-ID reads directly.

Driver exp0.6.24 adds bounded receive-notification recovery and passive history,
startup-only 5 GHz preference/readback with one automatic normal-selection
fallback, and capability-checked <=50 MHz standard high-speed SDR operation.
Both card and host mode are checked; 16 chip-ID reads must pass. A failed
high-speed attempt restores and verifies default <=25 MHz timing, or stops.
Unknown/ineligible capabilities retain the verified 25 MHz path. No DDR50,
voltage switching, UEFI/fan, firmware-binary, regulatory or security changes.
The 64-frame queue, TX/RX budgets and optimized /O2 /Ot ARM64 build are retained.
No live firmware radio queries are added during the performance workload.
See EXP0.6.24.md and PERFORMANCE-0.6.24.md. Physical improvement is NOT proven;
keep .16 and .23 for rollback. There is no guaranteed throughput claim.
Install on the Pi, restart once, then run Test-RPi5-WiFi-Performance.cmd.
Historical retained features below describe earlier candidates, not new claims.

Retained exp0.6.14 caches the verified runtime backplane address window, avoiding
six redundant CMD52 operations on each repeated register access. Partial/failed
selections, direct window/reset writes, bus errors and restart invalidate it.
Slow-mode upload retains the original selection behavior. A single bus worker
owns the cache; no concurrency, clock, firmware or wire-format changes.
Adds locked 64-bit Ethernet byte/frame/error/discard statistics and the standard
NDIS OID_GEN_STATISTICS interface for Windows traffic graphs. Bytes count actual
chip transfers / host receive indications, not queued work or a fabricated rate.
Performance utility 0.6.17 retains the 0.6.14.1 workload with sequential
1 MiB downloads (90s or 128 requests), independent router ping and traffic
sampling. Up to 129 MiB total payload; see PERFORMANCE-0.6.14.1.md for limits.
It uploads no logs. HTTP rejection is inconclusive, not a zero-speed result.
This is a throughput candidate, not a guaranteed or hardware-validated speedup.
Keep exp0.6.13 for rollback; its Pi test measured 3.17 Mbps and no ping loss.

Retained exp0.6.13 behavior gives each CMD53 phase its own bounded short-poll
budget on the verified operating bus: at most 50 us per phase / 150 us total,
in individual 10-us stalls, then yielding. The previous shared 40-us allowance
could be exhausted before buffer/transfer completion. No unbounded busy waits.
Startup/upload/recovery, cancellation and 250-ms phase deadlines are retained.
New counters separate runtime command/buffer/completion sleeps and F1/F2 waits,
and record cumulative scheduler sleep time. The performance tool adds download
hostname DNS and HTTPS phase timings, and waits for a post-test driver snapshot.
exp0.6.12 confirmed browsing and HTTPS, but still showed queue congestion,
ping timeouts and DNS failure for the download. This is a targeted latency
candidate, NOT a hardware-proven speed or reliability fix. Keep .12 for rollback.
Existing checksum observations and validated ICMP echo matching are retained;
they never modify packets or export the private in-memory echo ring.
UEFI, firmware, country, MAC generation, bus mode and queue size are unchanged.
Retained exp0.6.11 features: optional editable local credentials/startup connection,
packet-path counters, firmware MAC readback and explicit RX NBL initialization.
It is NOT a confirmed packet-loss fix. exp0.6.10 verified 4-bit/25 MHz on the
user's Pi but its performance capture lost every ping and failed DNS/HTTPS.
See AUTO-CONNECT.md; never publish WiFi.private.json or include it in reports.
Counters distinguish submitted-to-chip TX, received-from-chip RX and indicated
Windows RX by protocol, with state/format/filter/allocation drop evidence.
These are counts only, not payload/MAC/credential captures or AP ACK evidence.
The 4-bit/25 MHz operating-bus path is retained.
Firmware upload/readback stays conservative. After F2 startup, default timing
and 4-bit width are set on both ends, then the clock targets <=25 MHz.
Sixteen matching read-only chip-ID CMD53 probes are required. Failed upgrades
restore a verified 1-bit/400 kHz configuration or stop without unsafe cleanup.
No association is attempted after an upgrade/verification failure.
Run Test-RPi5-WiFi-Performance.cmd after one reboot: it prompts for connection,
then saves one desktop report ZIP with bus/route/ping/DNS/HTTPS/download results
and diagnostics. Unplug wired Ethernet and disconnect VPNs for this test.
The tool requests example.com and up to 1 MiB from speed.cloudflare.com;
logs stay local and passwords are not recorded. No settings are changed.
Firmware, country policy, UEFI and existing pending-send logic are unchanged.
The exp0.6.9 pending-transmit/backpressure implementation is retained:
Windows sends remain pending until every frame is transferred to the chip.
Firmware-busy sends retain their place and retry without duplicating completed
frames. Bounded bursts run before and after receive polling. No per-packet
allocation; still at most 64 retained frames and 64 outstanding NBLs.
Cancellation, pause, disconnect, stop and power-down return pending ownership.
Requests expire after 30 seconds rather than being held indefinitely.
New completion/expiry/credit diagnostics and isolated optional Windows stats.
The exp0.6.8 SDIO polling engine is unchanged; runtime bus speed is upgraded.
Physical exp0.6.7 showed authentication, DHCP, ping, DNS and HTTPS responses,
but high latency and intermittent DNS timeouts. exp0.6.8 recorded 1274 queue-full
rejections matching transmit errors. This build is not a proven speed fix.
Keep the previous packages for rollback. See INTEGRATED-TESTING.md.
This release packages the user-requested ReactOS CYW43455 firmware/CLM pair:
firmware 7.45.229 (631467 bytes), CLM 7163 bytes. The firmware version is OLDER,
not the newer Infineon 7.45.286. No radio-parameter or regulatory-data edits.
Calibration bytes are identical; source commit, hashes and licences are retained.
The exp0.6.6 full country request using revision -1 is unchanged.
A read-only supported-country query records membership/count/status in diagnostics.
An unsupported or malformed list is unknown, not proof the country is absent.
The exp0.6.5 bounded reply decoding is retained.
This update reuses a matching existing country and, only after revision-zero
BADARG, tries the same country's firmware-selected revision once. A complete
matching readback is required before radio-up. It checks loaded CLM status and
records the country selection path. No USA or alternate-country fallback.
Without an optional private configuration, the utility remembers only country.
The upload/readback counters and PIO transfer format are unchanged.
The utility no longer stops a progressing upload after three minutes. It reports
120 seconds without observed progress or a 30-minute observation limit without
stopping/resetting the driver. Collect diagnostics before rebooting on either.
exp0.6.6 reported 116 country entries without BD and rejected the BD requests.
The pair demonstrated basic packet traffic on the user's Pi with exp0.6.7,
but that is not board-specific RF certification or validation of this new build.
It uploads firmware, checks RAM readback, uses SDPCM/BCDC and a polled packet path.
It DOES NOT prove successful Wi-Fi until tested physically on the Pi.
Run Connect-RPi5-WiFi.cmd as administrator AFTER installation and restart.
Enter your actual country, SSID and WPA2 password, or supply WiFi.private.json.
NetworkPhase: 400 files, 410 CR4/RAM, 420 upload, 421 readback, 422 NVRAM/vector,
430 CPU start,
440 F2 ready, 500 firmware configured/radio down, 520 joining, 600 authenticated.
Use diagnostics if any step fails. Do not replace UEFI or reinstall Windows.
First candidate limitations: WPA2-Personal/AES only, no WPA3/enterprise,
no scanning UI or continuous reconnect service; firmware uploads at 1-bit/400kHz,
no performance claim. Use Ethernet for recovery and do not use sleep/hibernate.
MAC is locally administered and regenerated on adapter initialization.
Keep UEFI exp.0.3 (source bda4c47); this package contains NO UEFI update.

Use only with the matching UEFI build that exposes ACPI\\RPI0011 and leaves
MAX_50MHZ_MODE untouched. Confirm the physical fan operates normally after boot.

One-click installation:
  Extract the complete ZIP, then double-click Install-RPi5-WiFi-Driver.cmd.
  Approve the Administrator prompt. The installer verifies the package and
  matching UEFI/device before trusting the test certificate or installing.
  It runs the diagnostic collector automatically after the installation attempt.
  Save your work before installation. If a reboot is requested, restart manually
  and run Run-RPi5-WiFi-Diagnostics.cmd again. No uninstall is required first.
  Recovery: Device Manager -> this adapter -> Driver -> Roll Back Driver (if
  available), or disable only this adapter and reinstall the previous package.
  Do not remove unrelated network/storage drivers or reflash Windows.

Security:
  This is a test-signed kernel driver. The installer refuses to enable Test
  Signing or change Secure Boot. When those prerequisites are already satisfied,
  it verifies the package signer and adds the included test certificate to the
  machine Root and TrustedPublisher stores. Remove the driver and certificate
  after testing if this experimental package is no longer required.
"@ | Set-Content (Join-Path $stage 'README-TESTING.txt') -Encoding UTF8

@"
driver_repository=$env:GITHUB_REPOSITORY
driver_version=0.6.24
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
