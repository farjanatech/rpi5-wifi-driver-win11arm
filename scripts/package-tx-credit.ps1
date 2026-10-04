param(
    [Parameter(Mandatory=$true)][string]$Configuration,
    [Parameter(Mandatory=$true)][string]$Platform,
    [ValidateSet('0','1')][string]$TxCreditScheduling='1'
)
Set-StrictMode -Version Latest
$ErrorActionPreference='Stop'
# Keep the original packaging/signing/installer implementation unchanged.
& (Join-Path $PSScriptRoot 'package-ci.ps1') -Configuration $Configuration -Platform $Platform
$root=Split-Path -Parent $PSScriptRoot
$stage=Join-Path $root 'artifacts\rpi5cyw-test-driver'
Copy-Item (Join-Path $root 'utility\Get-RPi5-WiFi-TxCredit.ps1') $stage
Copy-Item (Join-Path $root 'utility\Test-RPi5-WiFi-Lan.ps1') $stage
Copy-Item (Join-Path $root 'docs\TX-CREDIT-EXPERIMENT.md') $stage
Copy-Item (Join-Path $root 'docs\ALL-IN-ONE-FIX.md') $stage
@"
Raspberry Pi 5 CYW43455 Windows 11 ARM64 - 0.7.1.8
Protected stable baseline: 0.7.1.4 / c0b032543f945707c82ffc1b05a8ca7012821560
Earlier post-RX dispatch + bounded fast credit wake enabled: $TxCreditScheduling
Mode 1 is the promoted main/default build after successful hardware and CI testing.

Keep the ORIGINAL 0.7.1.4 package for rollback. Reconnect with the existing
connection utility after installation. Do not change country, firmware or UEFI.
Read ALL-IN-ONE-FIX.md first for the current experiment and all hardware gates.
TX-CREDIT-EXPERIMENT.md is the historical 0.7.1.5 design, NOT this scheduler.
Get-RPi5-WiFi-TxCredit.ps1 reads passive, periodic diagnostic snapshots only.
Detailed per-F1/F2 timing is disabled in this low-overhead package; use matched
opt-in detailed builds for timing evidence, not invented zero-cost estimates.

The driver is test-signed, not Microsoft production-signed.
"@ | Set-Content (Join-Path $stage 'README-TESTING.txt') -Encoding UTF8

$receiptPath=Join-Path $stage 'SOURCE_REVISION.txt'
$receipt=Get-Content -LiteralPath $receiptPath -Raw
$receipt=$receipt.Replace('driver_version=0.7.1.4',"driver_version=0.7.1.8-experimental`ntx_credit_scheduling=$TxCreditScheduling")
$receipt=$receipt.Replace('performance_branch=better-improvement',"performance_branch=$env:GITHUB_REF_NAME")
$receipt=$receipt.Replace('performance_baseline=16533ac0e7e477f5c604882d8cc82081119e3f90','performance_baseline=c0b032543f945707c82ffc1b05a8ca7012821560')
$receipt | Set-Content -LiteralPath $receiptPath -Encoding UTF8
Get-ChildItem $stage -File | Where-Object { $_.Name -ne 'SHA256SUMS.txt' } | Sort-Object Name | ForEach-Object {
    $hash=Get-FileHash $_.FullName -Algorithm SHA256
    "$($hash.Hash)  $($_.Name)"
} | Set-Content (Join-Path $stage 'SHA256SUMS.txt') -Encoding ASCII
Write-Host "Packaged TX mode $TxCreditScheduling for v0.7.1.8; mode 1 is the promoted default."
