param(
    [Parameter(Mandatory=$true)][string]$Configuration,
    [Parameter(Mandatory=$true)][string]$Platform,
    [ValidateSet('0','1')][string]$TxCreditScheduling='1',
    [ValidateSet('0','1')][string]$TxGlom2='1'
)
Set-StrictMode -Version Latest
$ErrorActionPreference='Stop'
# Keep the original packaging/signing/installer implementation unchanged.
& (Join-Path $PSScriptRoot 'package-ci.ps1') -Configuration $Configuration -Platform $Platform
$root=Split-Path -Parent $PSScriptRoot
$stage=Join-Path $root 'artifacts\rpi5cyw-test-driver'
@"
Raspberry Pi 5 CYW43455 Windows 11 ARM64 - 0.7.1.14 MEASUREMENT-ONLY
Hardware rollback baseline: 0.7.1.9 / 0bc5ed12e9438cd82555d7bc8ae77211aa6f236b
Failed framing predecessor: 0.7.1.10 / a8eb3a6ff480821be885faf398e25857a248dc8f
Earlier post-RX dispatch + bounded fast credit wake mode: $TxCreditScheduling
Two-frame host TX glom compiled candidate: $TxGlom2

Keep the ORIGINAL v0.7.1.11 package as the immediate performance rollback and
v0.7.1.9 as the architecture rollback. Open RPi5-WiFi-AllInOne.cmd after installation.
Do not change country,
firmware, UEFI, router settings or backlog limits for the comparison.
Use RPi5-WiFi-AllInOne.cmd for connection, status, sustained download/upload and diagnostics.
The driver runtime is intentionally the exact green v0.7.1.11 source; this package
contains no new TX threshold, queue, glom, SDIO, firmware or radio tuning.
The all-in-one measurement now runs 1/2/4-stream sustained download and upload stages,
forcing a fresh read-only driver snapshot before and after every stage so TX credit,
queue, backlog, glom, transport-status and CMD53 write deltas are stage-specific.
Detailed per-F1/F2 timing is disabled in this low-overhead package.

The driver is test-signed, not Microsoft production-signed.
"@ | Set-Content (Join-Path $stage 'README-TESTING.txt') -Encoding UTF8

$receiptPath=Join-Path $stage 'SOURCE_REVISION.txt'
$receipt=Get-Content -LiteralPath $receiptPath -Raw
$receipt=$receipt.Replace('driver_version=0.7.1.4',"driver_version=0.7.1.14-measurement-only`nruntime_baseline=6ae93623c8767eda050b8c408250d3ec3ce19bfb`ntx_credit_scheduling=$TxCreditScheduling`ntx_glom2=$TxGlom2`nall_in_one_utility=1`nmeasurement_profile=sustained-download-1-2-4-plus-upload-1-2-4`nsnapshot_boundary=read-only-radio-query")
$receipt=$receipt.Replace('performance_branch=better-improvement',"performance_branch=$env:GITHUB_REF_NAME")
$receipt=$receipt.Replace('performance_baseline=16533ac0e7e477f5c604882d8cc82081119e3f90','performance_baseline=6ae93623c8767eda050b8c408250d3ec3ce19bfb')
$receipt=$receipt.Replace('measurement_utility_version=0.6.27.1','measurement_utility_version=0.7.1.14-all-in-one')
$receipt=$receipt.Replace('startup_receipt_compatibility=0.6.27','startup_receipt_compatibility=not-packaged')
$receipt | Set-Content -LiteralPath $receiptPath -Encoding UTF8

foreach($required in @('RPi5-WiFi-AllInOne.ps1','RPi5-WiFi-AllInOne.cmd')){
    if(-not (Test-Path -LiteralPath (Join-Path $stage $required) -PathType Leaf)){
        throw "Consolidated utility missing from package: $required"
    }
}
foreach($forbidden in @(
    'Connect-RPi5-WiFi.ps1','Connect-RPi5-WiFi.cmd',
    'Test-RPi5-WiFi-Performance.ps1','Test-RPi5-WiFi-Performance.cmd',
    'Check-RPi5-WiFi-Readiness.ps1','Check-RPi5-WiFi-Readiness.cmd',
    'Collect-RPi5-WiFi-Diagnostics.ps1','Run-RPi5-WiFi-Diagnostics.cmd',
    'Get-RPi5-WiFi-Radio.ps1','Get-RPi5-WiFi-Timing.ps1','Get-RPi5-WiFi-Transport.ps1',
    'Get-RPi5-WiFi-TxCredit.ps1','Test-RPi5-WiFi-Lan.ps1','RPi5-WiFi-App.ps1','RPi5-WiFi-App.cmd',
    'RPi5-WiFi-Operations.ps1','Measure-RPi5-WiFi-Load.ps1',
    'RPi5-WiFi-DownloadTiming.ps1','RPi5-WiFi-MeasurementClock.ps1',
    'Set-RPi5-WiFi-Autoconnect.ps1','Enable-RPi5-WiFi-Autoconnect.cmd','Disable-RPi5-WiFi-Autoconnect.cmd'
)){
    if(Test-Path -LiteralPath (Join-Path $stage $forbidden)){
        throw "Legacy standalone tester leaked into consolidated package: $forbidden"
    }
}
Get-ChildItem $stage -File | Where-Object { $_.Name -ne 'SHA256SUMS.txt' } | Sort-Object Name | ForEach-Object {
    $hash=Get-FileHash $_.FullName -Algorithm SHA256
    "$($hash.Hash)  $($_.Name)"
} | Set-Content (Join-Path $stage 'SHA256SUMS.txt') -Encoding ASCII
Write-Host "Packaged v0.7.1.14 measurement-only utility with exact v0.7.1.11 runtime; no new driver tuning."
