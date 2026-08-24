param(
    [Parameter(Mandatory=$true)][string]$Configuration,
    [Parameter(Mandatory=$true)][string]$Platform
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent $PSScriptRoot
$stage = Join-Path $root 'artifacts\rpi5cyw-test-driver'
if (Test-Path $stage) { Remove-Item $stage -Recurse -Force }
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

$pdb = Get-ChildItem $root -Filter 'rpi5cyw.pdb' -File -Recurse -ErrorAction SilentlyContinue |
    Where-Object { $_.FullName -notmatch '\\artifacts\\|\\packages\\' } |
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
CMD0/CMD5/CMD3/CMD7/CMD52 directly.

The driver remains disconnected until the CYW43455 firmware/SDPCM/BCDC and
association datapath are completed. A successful CMD52 diagnostic is a hardware
protocol milestone, NOT a claim that Wi-Fi is working.

Use only with the matching UEFI build that exposes ACPI\\RPI0011 and leaves
MAX_50MHZ_MODE untouched. Confirm the physical fan operates normally after boot.

Security:
  This is a test-signed kernel driver. The installer refuses to enable Test
  Signing or change Secure Boot. When those prerequisites are already satisfied,
  it verifies the package signer and adds the included test certificate to the
  machine Root and TrustedPublisher stores. Remove the driver and certificate
  after testing if this experimental package is no longer required.
"@ | Set-Content (Join-Path $stage 'README-TESTING.txt') -Encoding UTF8

@"
driver_repository=$env:GITHUB_REPOSITORY
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
