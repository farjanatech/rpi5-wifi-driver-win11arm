Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
. (Join-Path $root 'utility\Connect-RPi5-WiFi.ps1') -LibraryOnly
$temporary = Join-Path ([IO.Path]::GetTempPath()) ('rpi5-profile-test-' + [guid]::NewGuid().ToString('N'))
[void](New-Item -ItemType Directory -Path $temporary)
$path = Join-Path $temporary 'WiFi.private.json'
try {
    # Synthetic credentials only; never use a real profile in CI.
    '{"Country":"bd","SSID":"test network","Password":"test-only-123"}' | Set-Content -LiteralPath $path -Encoding UTF8
    $wifiProfile = Read-Rpi5WifiConfig $path
    if ($wifiProfile.Country -ne 'BD' -or $wifiProfile.SSID -cne 'test network' -or $wifiProfile.Password -cne 'test-only-123') { throw 'Configuration changed valid fields.' }
    foreach ($json in @(
        '{"Country":"BD","SSID":"test","Password":"short"}',
        '{"Country":"BD","SSID":"test","Password":"test-only-123\n"}',
        '{"Country":"BD","SSID":"","Password":"test-only-123"}',
        '{"Country":"BD","SSID":"test\u0000","Password":"test-only-123"}',
        '{"Country":"USA","SSID":"test","Password":"test-only-123"}',
        '{"Country":"BD","SSID":"test","Password":12345678}',
        '{"Country":"BD","SSID":"test","Password":"test-only-123","Command":"anything"}',
        '[{"Country":"BD","SSID":"test","Password":"test-only-123"}]',
        '{"Country":"BD","SSID":"test","Password":"test-only-123"',
        ('x' * 8193)
    )) {
        $json | Set-Content -LiteralPath $path -Encoding UTF8
        $rejected = $false
        try { [void](Read-Rpi5WifiConfig $path) } catch {
            $rejected = $true
            if ($_.Exception.Message -match 'test-only|anything|short') { throw 'Error disclosed configuration content.' }
        }
        if (-not $rejected) { throw 'Invalid configuration accepted.' }
    }
    $example = Get-Content -LiteralPath (Join-Path $root 'utility\WiFi.config.example.json') -Raw | ConvertFrom-Json
    if ($example.Password -ne '' -or $example.SSID -ne '') { throw 'Public template contains a filled profile.' }
    $setup = Join-Path $root 'utility\Set-RPi5-WiFi-Autoconnect.ps1'
    $tokens=$null; $errors=$null
    [void][Management.Automation.Language.Parser]::ParseFile($setup,[ref]$tokens,[ref]$errors)
    if ($errors.Count) { throw 'Autoconnect setup syntax error.' }
    . $setup -LibraryOnly
    # No task, certificate, registry or hardware changes are executed by this test.
    $source = Get-Content -LiteralPath $setup -Raw
    foreach ($required in @('S-1-5-18', 'S-1-5-32-544', 'SetAccessRuleProtection',
        '-AtStartup', '-NonInteractive', '-ExecutionTimeLimit', '-MultipleInstances IgnoreNew',
        'RPI0011', 'Architecture]::Arm64', 'ReparsePoint')) {
        if (-not $source.Contains($required)) { throw 'Startup task security/bounds guard missing.' }
    }
    $packager = Get-Content -LiteralPath (Join-Path $root 'scripts\package-ci.ps1') -Raw
    if ($packager -match "Copy-Item[^\r\n]*WiFi\.private\.json") { throw 'Packager copies a private profile.' }
    $collector = Get-Content -LiteralPath (Join-Path $root 'diagnostics\Collect-RPi5-WiFi-Diagnostics.ps1') -Raw
    if ($collector -match 'WiFi\.private\.json') { throw 'Collector must not open private profiles.' }
} finally {
    if (Test-Path -LiteralPath $path) { Remove-Item -LiteralPath $path -Force }
    Remove-Item -LiteralPath $temporary
}
Write-Output 'PASS: editable profile validation, redacted errors, blank public template and startup-task safety guards.'
