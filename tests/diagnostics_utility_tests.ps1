Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent $PSScriptRoot
$scriptPath = Join-Path $root 'diagnostics\Collect-RPi5-WiFi-Diagnostics.ps1'
$launcherPath = Join-Path $root 'diagnostics\Run-RPi5-WiFi-Diagnostics.cmd'
$tokens = $null
$errors = $null
[void][Management.Automation.Language.Parser]::ParseFile($scriptPath, [ref]$tokens, [ref]$errors)
if ($errors.Count -ne 0) {
    throw "PowerShell parser errors: $($errors | Out-String)"
}

$source = Get-Content -LiteralPath $scriptPath -Raw
$forbidden = @(
    '(?i)\bbcdedit(?:\.exe)?\s+/(?:set|delete|deletevalue|import)',
    '(?i)\bpnputil(?:\.exe)?\s+/(?:add-driver|delete-driver|disable-device|enable-device|remove-device|restart-device)',
    '(?i)\bcertutil(?:\.exe)?\s+-(?:addstore|delstore)',
    '(?i)\breg(?:\.exe)?\s+(?:add|delete|import|restore)',
    '(?i)\b(?:Set|New|Remove)-ItemProperty\b',
    '(?i)\b(?:Disable|Enable|Remove)-PnpDevice\b',
    '(?i)\b(?:Restart|Stop)-Computer\b',
    '(?i)\bshutdown(?:\.exe)?\b'
)
foreach ($pattern in $forbidden) {
    if ($source -match $pattern) { throw "Forbidden state-changing operation found: $pattern" }
}

if ($source -notmatch "ACPI\\\\RPI0011") { throw 'Expected RPI0011 collection is missing.' }
if ($source -notmatch 'Compress-Archive') { throw 'ZIP creation is missing.' }
foreach ($requiredDiagnostic in @('Cmd5AttemptCount','Cmd5SuccessAttempt','PresentStateBefore','ClockControlAfter','PowerControlAfter')) {
    if ($source -notmatch [regex]::Escape($requiredDiagnostic)) {
        throw "Expected CMD5 retry diagnostic is missing: $requiredDiagnostic"
    }
}
if (-not (Test-Path -LiteralPath $launcherPath -PathType Leaf)) { throw 'One-click launcher is missing.' }

. $scriptPath -LibraryOnly
$oldUser = $env:USERNAME
$oldComputer = $env:COMPUTERNAME
$oldProfile = $env:USERPROFILE
try {
    $env:USERNAME = 'DiagnosticPerson'
    $env:COMPUTERNAME = 'DiagnosticComputer'
    $env:USERPROFILE = 'C:\Users\DiagnosticPerson'
    $protected = Protect-Rpi5DiagnosticText 'DiagnosticPerson DiagnosticComputer C:\Users\DiagnosticPerson AA-BB-CC-DD-EE-FF'
    foreach ($secret in @('DiagnosticPerson','DiagnosticComputer','AA-BB-CC-DD-EE-FF')) {
        if ($protected -match [regex]::Escape($secret)) { throw "Redaction failed for $secret" }
    }
} finally {
    $env:USERNAME = $oldUser
    $env:COMPUTERNAME = $oldComputer
    $env:USERPROFILE = $oldProfile
}

if ((ConvertTo-Rpi5Hex32 0) -ne '0x00000000') { throw 'Hex conversion failed for zero.' }
if ((ConvertTo-Rpi5Hex32 305419896) -ne '0x12345678') { throw 'Hex conversion failed for known value.' }

Write-Output 'Diagnostics utility syntax, safety and helper tests passed.'
