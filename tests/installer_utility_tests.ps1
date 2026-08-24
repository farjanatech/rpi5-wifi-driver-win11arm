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
    '5a5013a','ACPI\\RPI0011','Test-Rpi5PackageManifest','Get-AuthenticodeSignature',
    'certutil.exe -addstore','pnputil.exe /add-driver','Collect-RPi5-WiFi-Diagnostics.ps1'
)) {
    if ($source -notmatch [regex]::Escape($required)) { throw "Required safety/install behavior is missing: $required" }
}
if (-not (Test-Path -LiteralPath $launcherPath -PathType Leaf)) { throw 'One-click installer launcher is missing.' }

. $scriptPath -LibraryOnly
foreach ($bad in @('..\evil.sys','folder\file.sys','C:\evil.sys','..','SHA256SUMS.txt')) {
    if (Test-Rpi5ManifestName -Name $bad) { throw "Unsafe manifest name was accepted: $bad" }
}
foreach ($good in @('rpi5cyw.sys','rpi5cyw.inf','README-TESTING.txt')) {
    if (-not (Test-Rpi5ManifestName -Name $good)) { throw "Safe manifest name was rejected: $good" }
}

Write-Output 'One-click installer syntax, safety and helper tests passed.'
