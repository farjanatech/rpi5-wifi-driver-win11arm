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
Raspberry Pi 5 CYW43455 Windows 11 ARM64 - 0.7.1.16 EXPERIMENTAL F2 BUFFER-PIO
Immediate rollback baseline: 0.7.1.11 / 6ae93623c8767eda050b8c408250d3ec3ce19bfb
Direct experiment baseline: 0.7.1.15 / 66e0198609913fcc407c595e580e39c83777b60f
Architecture rollback: 0.7.1.9 / 0bc5ed12e9438cd82555d7bc8ae77211aa6f236b
Rejected threshold-16 experiment: 0.7.1.12 - DO NOT RESTORE
Earlier post-RX dispatch + bounded fast credit wake mode: $TxCreditScheduling
Two-frame host TX glom compiled candidate: $TxGlom2
Two-frame service-status amortization: ENABLED, hard cap = 2 ordinary F2 frames
Aligned F2 block PIO: ENABLED, 32 ULONG / 128-byte bounded register-buffer bursts

v0.7.1.15 proved a large reduction in TX status checks, credit waits and queue
pressure, but Fast.com changed only ~40 -> 42 Mbps upload while download fell
82 -> 64 Mbps. v0.7.1.16 keeps the successful bounded Service-Burst2 behavior
and targets the common F2 PIO data path used by both RX/download and TX/upload.

The SDIO protocol, 512-byte block size, block counts, FIFO addressing, 50 MHz /
4-bit bus mode, 64-frame active queue, 128-frame backlog, 32-frame glom pressure
threshold, two-frame glom cap, firmware/radio policy and SDPCM framing are unchanged.

For aligned 512-byte F2 blocks, four 128-byte READ/WRITE_REGISTER_BUFFER_ULONG
bursts replace 128 individual ULONG register accesses. IoStopped is rechecked
between each 128-byte burst. Unaligned buffers retain the exact scalar path.
Partial FIFO errors are still terminal and are never replayed.

Qualification: use Fast.com under the same AP/band/location, then diagnostics
only. The packaged all-in-one utility is carried forward unchanged from
v0.7.1.14-fix2 and should not be used for performance qualification.
Detailed per-F1/F2 timing remains disabled in this low-overhead package.

The driver is test-signed, not Microsoft production-signed.
"@ | Set-Content (Join-Path $stage 'README-TESTING.txt') -Encoding UTF8

$receiptPath=Join-Path $stage 'SOURCE_REVISION.txt'
$receipt=Get-Content -LiteralPath $receiptPath -Raw
$receipt=$receipt.Replace('driver_version=0.7.1.4',"driver_version=0.7.1.16-experimental`nstable_runtime_baseline=6ae93623c8767eda050b8c408250d3ec3ce19bfb`ndirect_experiment_baseline=66e0198609913fcc407c595e580e39c83777b60f`ntx_credit_scheduling=$TxCreditScheduling`ntx_glom2=$TxGlom2`ntx_service_burst2=1`ntx_service_burst_max=2`ntx_glom_pressure_threshold=32`nfifo_buffer_pio=1`nfifo_buffer_pio_burst_words=32`nqualification=fast.com-plus-diagnostics`nall_in_one_utility=0.7.1.14-fix2-unchanged")
$receipt=$receipt.Replace('performance_branch=better-improvement',"performance_branch=$env:GITHUB_REF_NAME")
$receipt=$receipt.Replace('performance_baseline=16533ac0e7e477f5c604882d8cc82081119e3f90','performance_baseline=66e0198609913fcc407c595e580e39c83777b60f')
$receipt=$receipt.Replace('measurement_utility_version=0.6.27.1','measurement_utility_version=0.7.1.14-fix2-unchanged')
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
Write-Host "Packaged v0.7.1.16 F2 buffer-PIO experiment; Service-Burst2 retained and v0.7.1.11 remains immediate rollback."
