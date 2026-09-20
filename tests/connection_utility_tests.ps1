Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$sourcePath = Join-Path (Split-Path -Parent $PSScriptRoot) 'utility\Connect-RPi5-WiFi.ps1'
$tokens = $null; $errors = $null
[void][Management.Automation.Language.Parser]::ParseFile($sourcePath, [ref]$tokens, [ref]$errors)
if ($errors.Count) { throw 'Connection utility has syntax errors.' }
. $sourcePath -LibraryOnly
if (-not (Test-Rpi5ConnectionInput 'BD' 'test-network')) { throw 'Valid connection rejected.' }
foreach ($bad in @('', 'B', '123', 'bd', 'BD;')) {
    if (Test-Rpi5ConnectionInput $bad 'test') { throw 'Invalid country accepted.' }
}
foreach ($bad in @('', ('a' * 33), ([string][char]0x20AC * 11))) {
    if (Test-Rpi5ConnectionInput 'BD' $bad) { throw 'Invalid SSID byte length accepted.' }
}
# Published WPA PSK known-answer vector: password / IEEE / 4096 / 32 bytes.
$key = [Rpi5WifiControl]::Derive(
    [Text.Encoding]::ASCII.GetBytes('password'), [Text.Encoding]::ASCII.GetBytes('IEEE'))
try {
    $actual = [BitConverter]::ToString($key).Replace('-', '')
    if ($actual -ne 'F42C6FC52DF0EBEF9EBB4B90B38A5F902E83FE1B135A70E23AED762E9710A12E') {
        throw 'WPA2 PMK derivation failed known-answer test.'
    }
} finally { [Array]::Clear($key, 0, $key.Length) }
$source = Get-Content -LiteralPath $sourcePath -Raw
foreach ($code in @('0x12A000', '0x126004', '0x12A008')) {
    if (-not $source.Contains($code)) { throw 'Control ABI mismatch.' }
}
foreach ($forbidden in @('Start-Transcript', 'Set-ItemProperty', 'Set-Content', 'Add-Content', 'Out-File', 'bcdedit', 'pnputil')) {
    if ($source.Contains($forbidden)) { throw "Connection utility must not perform $forbidden" }
}
Write-Output 'PASS: connection utility syntax, input bounds, WPA2 PMK known-answer and credential non-persistence checks.'
