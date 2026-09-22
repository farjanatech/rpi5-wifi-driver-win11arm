Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$sourcePath = Join-Path (Split-Path -Parent $PSScriptRoot) 'utility\Connect-RPi5-WiFi.ps1'
$tokens = $null; $errors = $null
[void][Management.Automation.Language.Parser]::ParseFile($sourcePath, [ref]$tokens, [ref]$errors)
if ($errors.Count) { throw 'Connection utility has syntax errors.' }
. $sourcePath -LibraryOnly
if ((Get-Rpi5ConnectStepName 18) -ne 'automatic-band-preference') { throw 'Band preference step missing.' }
if ((Get-Rpi5JoinPreferenceSummary $null) -notmatch 'unavailable') { throw 'Missing snapshot must be unknown.' }
$join = [pscustomobject]@{DiagVersion=20;JoinPreferenceAccepted=1;JoinPreferenceStatus=0;JoinPreferenceError=0}
if ((Get-Rpi5JoinPreferenceSummary $join) -notmatch '8 dB preference.*2.4 GHz remains eligible') { throw 'Accepted preference missing.' }
$join.JoinPreferenceAccepted=0;$join.JoinPreferenceStatus=3221225473;$join.JoinPreferenceError=4294967273
if ((Get-Rpi5JoinPreferenceSummary $join) -notmatch 'unsupported') { throw 'Unsupported preference hidden.' }
$join.JoinPreferenceError=-23;$join.JoinPreferenceStatus=-1073741823
if ((Get-Rpi5JoinPreferenceSummary $join) -notmatch 'unsupported') { throw 'Signed registry status mishandled.' }
$join.JoinPreferenceError=-2
if ((Get-Rpi5JoinPreferenceSummary $join) -notmatch 'not confirmed') { throw 'Failed preference falsely accepted.' }
$join.JoinPreferenceStatus=259;$join.JoinPreferenceError=0
if ((Get-Rpi5JoinPreferenceSummary $join) -notmatch 'not confirmed') { throw 'Pending preference falsely accepted.' }
$state = [byte[]]::new(48)
[BitConverter]::GetBytes([uint32]2).CopyTo($state, 0)
[BitConverter]::GetBytes([uint32]510).CopyTo($state, 4)
[BitConverter]::GetBytes([uint32]263).CopyTo($state, 12)
[BitConverter]::GetBytes([int32]-2).CopyTo($state, 16)
[BitConverter]::GetBytes([uint32]2).CopyTo($state, 32)
$message = Get-Rpi5DriverFailure $state
if ($message -notmatch 'Connection setup/runtime' -or $message -notmatch 'country-set' -or
    $message -notmatch 'firmware error -2' -or $message -match 'Firmware startup failed') {
    throw 'Connection failure is mislabelled or lacks exact setting/error.'
}
if ((Get-Rpi5ConnectStepName 9) -ne 'sup_wpa') { throw 'Connection step mapping wrong.' }
if ((Get-Rpi5ConnectStepName 16) -ne 'country-full-auto-revision') { throw 'Country fallback not identified.' }
if ((Get-Rpi5ConnectStepName 17) -ne 'supported-country-query') { throw 'Country query not identified.' }
if ((Resolve-Rpi5Country 'bd' '') -ne 'BD' -or (Resolve-Rpi5Country '' 'BD') -ne 'BD' -or
    (Resolve-Rpi5Country 'GB' 'BD') -ne 'GB') { throw 'Country confirmation/resolution failed.' }
foreach ($pair in @(@('',''), @('','invalid'), @('B','BD'), @('BD;','BD'))) {
    $rejected = $false
    try { [void](Resolve-Rpi5Country $pair[0] $pair[1]) } catch { $rejected = $true }
    if (-not $rejected) { throw 'Invalid/unknown country was silently replaced.' }
}
if ((Get-Rpi5DriverFailure ([byte[]]::new(32))) -notmatch 'Firmware startup') { throw 'Old status ABI broken.' }
$progress = [byte[]]::new(96)
[BitConverter]::GetBytes([uint32]3).CopyTo($progress, 0)
[BitConverter]::GetBytes([uint32]420).CopyTo($progress, 4)
[BitConverter]::GetBytes([uint32]609309).CopyTo($progress, 48)
[BitConverter]::GetBytes([uint32]64).CopyTo($progress, 52)
[BitConverter]::GetBytes([uint32]0x103).CopyTo($progress, 72)
$sample = Get-Rpi5StartupSample $progress
if ($sample.Text -notmatch 'Upload=64/609309' -or $sample.Text -notmatch 'TransferStatus=0x00000103') { throw 'Live counters wrong.' }
if ((Get-Rpi5StartupDecision $sample 181 0) -ne 'wait') { throw 'Progressing upload still stops after three minutes.' }
if ((Get-Rpi5StartupDecision $sample 181 120) -ne 'no-progress') { throw 'Missing inactivity detection.' }
if ((Get-Rpi5StartupDecision $sample 1800 0) -ne 'wait-limit') { throw 'Missing absolute wait bound.' }
[BitConverter]::GetBytes([uint32]128).CopyTo($progress, 52)
if ((Get-Rpi5StartupSample $progress).Key -eq $sample.Key) { throw 'Upload advancement not detected.' }
[BitConverter]::GetBytes([uint32]421).CopyTo($progress, 4)
[BitConverter]::GetBytes([uint32]64).CopyTo($progress, 56)
if ((Get-Rpi5StartupSample $progress).Text -notmatch 'Verified=64/609309') { throw 'Readback count missing.' }
[BitConverter]::GetBytes([uint32]500).CopyTo($progress, 4)
if ((Get-Rpi5StartupDecision (Get-Rpi5StartupSample $progress) 200 120) -ne 'ready') { throw 'Ready state misclassified.' }
[BitConverter]::GetBytes([uint32]1).CopyTo($progress, 8)
if ((Get-Rpi5StartupDecision (Get-Rpi5StartupSample $progress) 200 120) -ne 'error') { throw 'Error hidden by ready state.' }
foreach ($size in @(32,48)) {
    if ((Get-Rpi5StartupSample ([byte[]]::new($size))).Text -notmatch 'unavailable') { throw 'Legacy ABI misread.' }
}
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
