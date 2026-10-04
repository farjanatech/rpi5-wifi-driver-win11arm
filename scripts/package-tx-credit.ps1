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
Copy-Item (Join-Path $root 'utility\Get-RPi5-WiFi-TxCredit.ps1') $stage
Copy-Item (Join-Path $root 'utility\Test-RPi5-WiFi-Lan.ps1') $stage
Copy-Item (Join-Path $root 'docs\TX-CREDIT-EXPERIMENT.md') $stage
Copy-Item (Join-Path $root 'docs\ALL-IN-ONE-FIX.md') $stage
Copy-Item (Join-Path $root 'docs\TX-GLOM2-0.7.1.10.md') $stage
@"
Raspberry Pi 5 CYW43455 Windows 11 ARM64 - 0.7.1.10 EXPERIMENTAL
Protected hardware baseline: 0.7.1.9 / 0bc5ed12e9438cd82555d7bc8ae77211aa6f236b
Earlier post-RX dispatch + bounded fast credit wake mode: $TxCreditScheduling
Two-frame host TX glom compiled candidate: $TxGlom2

Keep the ORIGINAL 0.7.1.9 new-improvement package for rollback. Reconnect with
the existing connection utility after installation. Do not change country,
firmware, UEFI, router settings or backlog limits for the comparison.
Read TX-GLOM2-0.7.1.10.md before testing. This build negotiates bus:rxglom and
uses at most two one-frame NBLs per aggregate under TX pressure. Explicit
firmware UNSUPPORTED falls back to the v0.7.1.9 path; ambiguous setup failures
fail closed. Get-RPi5-WiFi-TxCredit.ps1 remains passive diagnostics.
Detailed per-F1/F2 timing is disabled in this low-overhead package.

The driver is test-signed, not Microsoft production-signed.
"@ | Set-Content (Join-Path $stage 'README-TESTING.txt') -Encoding UTF8

$receiptPath=Join-Path $stage 'SOURCE_REVISION.txt'
$receipt=Get-Content -LiteralPath $receiptPath -Raw
$receipt=$receipt.Replace('driver_version=0.7.1.4',"driver_version=0.7.1.10-experimental`ntx_credit_scheduling=$TxCreditScheduling`ntx_glom2=$TxGlom2")
$receipt=$receipt.Replace('performance_branch=better-improvement',"performance_branch=$env:GITHUB_REF_NAME")
$receipt=$receipt.Replace('performance_baseline=16533ac0e7e477f5c604882d8cc82081119e3f90','performance_baseline=0bc5ed12e9438cd82555d7bc8ae77211aa6f236b')
$receipt | Set-Content -LiteralPath $receiptPath -Encoding UTF8
Get-ChildItem $stage -File | Where-Object { $_.Name -ne 'SHA256SUMS.txt' } | Sort-Object Name | ForEach-Object {
    $hash=Get-FileHash $_.FullName -Algorithm SHA256
    "$($hash.Hash)  $($_.Name)"
} | Set-Content (Join-Path $stage 'SHA256SUMS.txt') -Encoding ASCII
Write-Host "Packaged v0.7.1.10 TX mode $TxCreditScheduling with two-frame glom=$TxGlom2; v0.7.1.9 remains rollback."
