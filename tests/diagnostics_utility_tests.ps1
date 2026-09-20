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
    '(?i)\bshutdown(?:\.exe)?\b',
    '(?i)\b(?:Set|New|Remove)-(?:NetRoute|NetIPAddress|NetIPInterface|DnsClientServerAddress)\b',
    '(?i)\bipconfig(?:\.exe)?\s+/(?:renew|release|flushdns)'
)
foreach ($pattern in $forbidden) {
    if ($source -match $pattern) { throw "Forbidden state-changing operation found: $pattern" }
}

if ($source -notmatch "ACPI\\\\RPI0011") { throw 'Expected RPI0011 collection is missing.' }
if ($source -notmatch 'Compress-Archive') { throw 'ZIP creation is missing.' }
foreach ($requiredDiagnostic in @(
    'Cmd5AttemptCount','Cmd5ValidAttempt','Cmd5SuccessAttempt','ResponseValid',
    'PresentStateBefore','ClockControlAfter','PowerControlAfter','HostControl2After',
    'TimeoutControlAfter','CmdLineBefore','Get-Rpi5DeviceByAcpiId',
    '16-ip-routing-dns.txt','Get-NetRoute','NextHop','InterfaceMetric',
    'Get-NetIPAddress','Get-DnsClientServerAddress','Get-NetNeighbor','Get-NetAdapterStatistics'
)) {
    if ($source -notmatch [regex]::Escape($requiredDiagnostic)) {
        throw "Expected CMD5 retry diagnostic is missing: $requiredDiagnostic"
    }
}
if (-not (Test-Path -LiteralPath $launcherPath -PathType Leaf)) { throw 'One-click launcher is missing.' }

. $scriptPath -LibraryOnly
$capture = Invoke-Rpi5ReadOnlyCapture { 'route evidence'; 'DNS evidence'; throw 'optional statistics unavailable' }
if ($capture.Text -notmatch 'route evidence' -or $capture.Text -notmatch 'DNS evidence' -or
    $capture.Text -notmatch 'COLLECTION ERROR' -or $capture.Failure -ne 'optional statistics unavailable') {
    throw 'Partial diagnostic evidence was discarded after a late failure.'
}
$capture = Invoke-Rpi5ReadOnlyCapture { 'success' }
if ($capture.Text -notmatch 'success' -or $capture.Failure) { throw 'Successful capture failed.' }
$capture = Invoke-Rpi5ReadOnlyCapture { throw 'early failure' }
if ($capture.Failure -ne 'early failure') { throw 'Early capture failure was not recorded.' }
$capture = Invoke-Rpi5ReadOnlyCapture { }
if ($capture.Text -ne '' -or $capture.Failure) { throw 'Empty capture failed.' }
if ($source -notmatch '17-optional-windows-statistics.txt') { throw 'Optional statistics not isolated.' }
Write-Output 'Capture preserves partial evidence and handles empty/success/failure cases.'
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

$boot = [datetime]'2026-09-18T00:00:00Z'
$good = [pscustomobject]@{
    SnapshotTimeUtc=$boot.AddMinutes(1).ToFileTimeUtc(); ProbePhase=250
    LastStatus=0; ProbeRestoreStatus=0; Cmd53ReadCount=16; ChipId=0x4345
}
if ((Get-Rpi5ProbeResult $good 'Running' $boot) -notlike 'PASS:*') { throw 'Good probe rejected.' }
if ((Get-Rpi5ProbeResult $good 'Stopped' $boot) -notlike '*not running*') { throw 'Stopped driver accepted.' }
if ((Get-Rpi5ProbeResult $good 'Running' $boot.AddHours(1)) -notlike '*predate*') { throw 'Stale data accepted.' }
$good.ProbeRestoreStatus = 1
if ((Get-Rpi5ProbeResult $good 'Running' $boot) -like 'PASS:*') { throw 'Restore failure accepted.' }
$good.ProbeRestoreStatus = 0
$good.Cmd53ReadCount = 15
if ((Get-Rpi5ProbeResult $good 'Running' $boot) -like 'PASS:*') { throw 'Incomplete reads accepted.' }
Write-Output 'Freshness and CMD53 summary tests passed.'
$inventory = [pscustomobject]@{
    SnapshotTimeUtc=$boot.AddMinutes(1).ToFileTimeUtc(); ProbePhase=350
    LastStatus=0; ProbeRestoreStatus=0; Cmd53ReadCount=38; ChipId=0x4345
    CoreInventoryComplete=1; Cmd53WriteCount=0
}
if ((Get-Rpi5ProbeResult $inventory 'Running' $boot) -notlike 'PASS:*core inventory*') { throw 'Core inventory rejected.' }
$inventory.Cmd53WriteCount = 1
if ((Get-Rpi5ProbeResult $inventory 'Running' $boot) -like 'PASS:*') { throw 'Unexpected writes accepted.' }
$inventory.Cmd53WriteCount = 0
$inventory.CoreInventoryComplete = 0
if ((Get-Rpi5ProbeResult $inventory 'Running' $boot) -like 'PASS:*') { throw 'Incomplete inventory accepted.' }
Write-Output 'Core inventory diagnostics tests passed.'
