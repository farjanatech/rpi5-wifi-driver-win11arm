Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent $PSScriptRoot
$scriptPath = Join-Path $root 'installer\Install-RPi5-WiFi-Driver.ps1'
$launcherPath = Join-Path $root 'installer\Install-RPi5-WiFi-Driver.cmd'
$tokens = $null
$errors = $null
[void][Management.Automation.Language.Parser]::ParseFile($scriptPath, [ref]$tokens, [ref]$errors)
if ($errors.Count -ne 0) { throw "PowerShell parser errors: $($errors | Out-String)" }

$source = Get-Content -LiteralPath $scriptPath -Raw
$forbidden = @(
    '(?i)\bbcdedit(?:\.exe)?\s+/(?:set|delete|deletevalue|import)',
    '(?i)\bpnputil(?:\.exe)?\s+/(?:delete-driver|disable-device|remove-device|restart-device)',
    '(?i)\bcertutil(?:\.exe)?\s+-delstore',
    '(?i)\breg(?:\.exe)?\s+(?:add|delete|import|restore)',
    '(?i)\b(?:Set|New|Remove)-ItemProperty\b',
    '(?i)\b(?:Disable|Enable|Remove)-PnpDevice\b',
    '(?i)\b(?:Restart|Stop)-Computer\b',
    '(?i)\bshutdown(?:\.exe)?\b',
    '(?i)\bInvoke-WebRequest\b|\bInvoke-RestMethod\b|\bStart-BitsTransfer\b'
)
foreach ($pattern in $forbidden) {
    if ($source -match $pattern) { throw "Forbidden operation found: $pattern" }
}

foreach ($required in @(
    'bda4c47','ACPI\\RPI0011','Test-Rpi5PackageManifest','Get-AuthenticodeSignature',
    'certutil.exe -addstore','pnputil.exe /add-driver','Collect-RPi5-WiFi-Diagnostics.ps1'
)) {
    if ($source -notmatch [regex]::Escape($required)) { throw "Required safety/install behavior is missing: $required" }
}
if (-not (Test-Path -LiteralPath $launcherPath -PathType Leaf)) { throw 'One-click installer launcher is missing.' }
if ($source -notmatch [regex]::Escape('-File $startupUpdater -RefreshExisting') -or
    $source.IndexOf('$startupUpdater =') -lt $source.IndexOf('$verifiedFiles = Test-Rpi5PackageManifest')) {
    throw 'Existing startup code must be refreshed only after package verification.'
}
if ($source -match 'Copy-Item[^\r\n]*WiFi\.private\.json') { throw 'Installer must not replace a private profile.' }

. $scriptPath -LibraryOnly
foreach ($unknownOrError in @($null, '', 'unknown', 22, 10, 28, 50, 56)) {
    if (Test-Rpi5DeviceEnabled $unknownOrError) { throw 'Unknown/problem device classified as enabled.' }
}
if (-not (Test-Rpi5DeviceEnabled 0)) { throw 'Healthy device was not recognized.' }
if (-not (Test-Rpi5DeviceEnabled 'CM_PROB_NONE')) { throw 'Healthy CIM enum was not recognized.' }
if (-not (Test-Rpi5PnpSuccess 0) -or -not (Test-Rpi5PnpSuccess 3010) -or
    (Test-Rpi5PnpSuccess 5)) { throw 'PnP exit code classification failed.' }
if ($source -notmatch [regex]::Escape('pnputil.exe /enable-device $device.InstanceId')) {
    throw 'Exact target enable is missing.'
}
foreach ($bad in @('..\evil.sys','folder\file.sys','C:\evil.sys','..','SHA256SUMS.txt')) {
    if (Test-Rpi5ManifestName -Name $bad) { throw "Unsafe manifest name was accepted: $bad" }
}
foreach ($good in @('rpi5cyw.sys','rpi5cyw.inf','README-TESTING.txt')) {
    if (-not (Test-Rpi5ManifestName -Name $good)) { throw "Safe manifest name was rejected: $good" }
}

# Synthetic package only; this test executes on GitHub, never on the Pi or
# development PC. An omitted refresh dependency must not count as verified.
$fixture = Join-Path ([IO.Path]::GetTempPath()) ('rpi5-manifest-' + [guid]::NewGuid().ToString('N'))
[void](New-Item -ItemType Directory -Path $fixture)
$manifest = @()
foreach ($name in @('a.txt','b.txt','c.txt','d.txt','e.txt','RPi5-WiFi-Operations.ps1')) {
    $path = Join-Path $fixture $name
    'synthetic test content; not executable' | Set-Content -LiteralPath $path -Encoding UTF8
    $manifest += ('{0}  {1}' -f (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash,$name)
}
$manifestPath = Join-Path $fixture 'SHA256SUMS.txt'
$manifest | Set-Content -LiteralPath $manifestPath -Encoding ASCII
if ((Test-Rpi5PackageManifest $fixture -RequiredNames @('RPi5-WiFi-Operations.ps1')) -ne 6) { throw 'Complete manifest was rejected.' }
$manifest[0..4] | Set-Content -LiteralPath $manifestPath -Encoding ASCII
$rejected=$false
try { [void](Test-Rpi5PackageManifest $fixture -RequiredNames @('RPi5-WiFi-Operations.ps1')) } catch { $rejected=$true }
if (-not $rejected) { throw 'Unmanifested startup dependency was accepted.' }
@($manifest + $manifest[0]) | Set-Content -LiteralPath $manifestPath -Encoding ASCII
$rejected=$false
try { [void](Test-Rpi5PackageManifest $fixture) } catch { $rejected=$true }
if (-not $rejected) { throw 'Duplicate manifest entry was accepted.' }

Write-Output 'One-click installer syntax, safety and helper tests passed.'
