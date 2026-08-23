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

$pdb = Get-ChildItem $root -Filter 'rpi5cyw.pdb' -File -Recurse -ErrorAction SilentlyContinue |
    Where-Object { $_.FullName -notmatch '\\artifacts\\|\\packages\\' } |
    Sort-Object LastWriteTime -Descending | Select-Object -First 1
if ($pdb) { Copy-Item $pdb.FullName (Join-Path $stage 'rpi5cyw.pdb') -Force }

$signtool = Find-Tool 'signtool.exe'
$inf2cat = Find-Tool 'inf2cat.exe'
if (-not $signtool) { throw 'signtool.exe was not found in restored WDK/SDK packages or Windows Kits.' }
if (-not $inf2cat) { throw 'inf2cat.exe was not found in restored WDK packages or Windows Kits.' }

Write-Host "SignTool: $($signtool.FullName)"
Write-Host "Inf2Cat:  $($inf2cat.FullName)"

$subject = 'CN=RPI5 CYW43455 GitHub Test Driver'
$cert = New-SelfSignedCertificate -Type CodeSigningCert -Subject $subject `
    -CertStoreLocation 'Cert:\CurrentUser\My' -KeyExportPolicy Exportable `
    -HashAlgorithm SHA256 -NotAfter (Get-Date).AddYears(2)

$cerPath = Join-Path $stage 'rpi5cyw-test.cer'
Export-Certificate -Cert $cert -FilePath $cerPath -Force | Out-Null

& $signtool.FullName sign /v /fd SHA256 /sha1 $cert.Thumbprint (Join-Path $stage 'rpi5cyw.sys')
if ($LASTEXITCODE -ne 0) { throw "SignTool failed for SYS with exit code $LASTEXITCODE" }

# Current Inf2Cat identifiers for Windows 11 ARM64. Target 25H2, 24H2 and
# 22H2 so the bring-up package can be validated on the common Pi 5 test builds.
& $inf2cat.FullName /driver:$stage /os:10_25H2_ARM64,10_GE_ARM64,10_NI_ARM64 /verbose
if ($LASTEXITCODE -ne 0) { throw "Inf2Cat failed with exit code $LASTEXITCODE" }

$cat = Get-ChildItem $stage -Filter '*.cat' -File | Select-Object -First 1
if (-not $cat) { throw 'Inf2Cat succeeded but no catalog was produced.' }

& $signtool.FullName sign /v /fd SHA256 /sha1 $cert.Thumbprint $cat.FullName
if ($LASTEXITCODE -ne 0) { throw "SignTool failed for CAT with exit code $LASTEXITCODE" }

@"
Raspberry Pi 5 CYW43455 Windows 11 ARM64 - SDIO bring-up build

Configuration: $Configuration
Platform:      $Platform
Commit:        $env:GITHUB_SHA
Workflow run:  $env:GITHUB_SERVER_URL/$env:GITHUB_REPOSITORY/actions/runs/$env:GITHUB_RUN_ID

THIS IS AN EXPERIMENTAL TEST-SIGNED DRIVER.
It is currently intended only to validate SDIO enumeration/CMD52 bring-up.
It is not yet a functional Windows Wi-Fi driver.

Target preparation:
1. Use a recoverable Raspberry Pi 5 Windows test installation.
2. Secure Boot must not prevent test-signed kernel drivers.
3. Enable Windows test signing from an elevated prompt:
     bcdedit /set testsigning on
   then reboot.
4. Run install-test-driver.ps1 as Administrator.
5. Run collect-diagnostics.ps1 as Administrator and send its output ZIP back for analysis.
"@ | Set-Content (Join-Path $stage 'README-TESTING.txt') -Encoding UTF8

@'
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$me = [Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()
if (-not $me.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) { throw 'Run as Administrator.' }
$dir = Split-Path -Parent $MyInvocation.MyCommand.Path
$cer = Join-Path $dir 'rpi5cyw-test.cer'
$inf = Join-Path $dir 'rpi5cyw.inf'
certutil.exe -addstore -f Root $cer
certutil.exe -addstore -f TrustedPublisher $cer
pnputil.exe /add-driver $inf /install
pnputil.exe /scan-devices
Write-Host 'Install attempted. Run collect-diagnostics.ps1 next.'
'@ | Set-Content (Join-Path $stage 'install-test-driver.ps1') -Encoding UTF8

@'
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Continue'
$dir = Split-Path -Parent $MyInvocation.MyCommand.Path
$stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
$out = Join-Path $dir "diagnostics-$stamp"
New-Item -ItemType Directory -Path $out -Force | Out-Null

function Capture([string]$Name, [scriptblock]$Command) {
    try { & $Command 2>&1 | Out-String -Width 500 | Set-Content (Join-Path $out $Name) -Encoding UTF8 }
    catch { ($_ | Out-String) | Set-Content (Join-Path $out $Name) -Encoding UTF8 }
}

Capture 'windows.txt' { Get-ComputerInfo | Format-List * }
Capture 'bcdedit.txt' { bcdedit /enum all }
Capture 'secureboot.txt' { try { Confirm-SecureBootUEFI } catch { $_ } }
Capture 'pnputil-devices.txt' { pnputil /enum-devices /connected; pnputil /enum-devices /problem; pnputil /enum-drivers }
Capture 'pnp-cyw-sd.txt' {
    Get-PnpDevice -PresentOnly:$false -ErrorAction SilentlyContinue |
      Where-Object { $_.InstanceId -match 'VID_02D0|PID_A9BF|PID_4345|^SD\\' -or $_.FriendlyName -match 'CYW|Broadcom|Cypress|Infineon|SDIO|SD Host' } |
      Format-List *
}
Capture 'service.txt' { sc.exe query rpi5cyw; sc.exe qc rpi5cyw; reg.exe query 'HKLM\SYSTEM\CurrentControlSet\Services\rpi5cyw' /s }
Capture 'enum-sd-registry.txt' { reg.exe query 'HKLM\SYSTEM\CurrentControlSet\Enum\SD' /s }
Capture 'system-events.txt' {
    $start=(Get-Date).AddHours(-12)
    Get-WinEvent -FilterHashtable @{LogName='System';StartTime=$start} -ErrorAction SilentlyContinue |
      Where-Object { $_.ProviderName -match 'Kernel-PnP|DriverFrameworks|Service Control Manager' -or $_.Message -match 'rpi5cyw|VID_02D0|CYW43455' } |
      Select-Object TimeCreated,Id,LevelDisplayName,ProviderName,Message | Format-List
}
$setup = Join-Path $env:windir 'INF\setupapi.dev.log'
if (Test-Path $setup) { Copy-Item $setup (Join-Path $out 'setupapi.dev.log') -Force }
$zip = Join-Path $dir "RPI5-WIFI-DIAGNOSTICS-$stamp.zip"
Compress-Archive -Path (Join-Path $out '*') -DestinationPath $zip -Force
Write-Host "Diagnostics: $zip"
'@ | Set-Content (Join-Path $stage 'collect-diagnostics.ps1') -Encoding UTF8

$hashes = Get-ChildItem $stage -File | ForEach-Object { Get-FileHash $_.FullName -Algorithm SHA256 }
$hashes | Format-Table -AutoSize | Out-String | Set-Content (Join-Path $stage 'SHA256SUMS.txt') -Encoding UTF8

Write-Host "Packaged test driver at $stage"
