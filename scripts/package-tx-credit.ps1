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
Raspberry Pi 5 CYW43455 Windows 11 ARM64 - 0.7.1.18 EXPERIMENTAL SERVICE-BURST4
Immediate rollback baseline: 0.7.1.11 / 6ae93623c8767eda050b8c408250d3ec3ce19bfb
Direct experiment baseline: 0.7.1.17 / c9d715760da49ef491b681a1ba5b57e6ef6ef13b
Architecture rollback: 0.7.1.9 / 0bc5ed12e9438cd82555d7bc8ae77211aa6f236b
Rejected threshold-16 experiment: 0.7.1.12 - DO NOT RESTORE
Earlier post-RX dispatch + bounded fast credit wake mode: $TxCreditScheduling
Two-frame host TX glom compiled candidate: $TxGlom2
Service-Burst4 status amortization: ENABLED, hard cap = 4 ordinary F2 frames
F2 block PIO: v0.7.1.17 fixed-port implementation UNCHANGED

v0.7.1.17 restored stable download/connection behavior on Raspberry Pi 5
hardware. The remaining hardware evidence points to TX status/credit pressure.
v0.7.1.18 changes only ordinary TX service reuse: one fresh F1 may authorize
up to three following F2 frames when the matching real credits already exist.

The SDIO protocol, 512-byte block size, block counts, FIFO addressing, 50 MHz /
4-bit bus mode, 64-frame active queue, 128-frame backlog, 32-frame glom pressure
threshold, two-frame glom cap, firmware/radio policy and SDPCM framing are unchanged.

The v0.7.1.17 fixed-address SDHCI FIFO implementation is byte-for-byte frozen.
Service-Burst4 never grants credit: it snapshots up to four real firmware
credits after fresh F1 service, then rechecks cached flow plus remaining credit
before each reused-service F2. Glom2 keeps precedence. Any busy/error/cancel
invalidates the remaining grant; partial FIFO errors remain terminal/no-replay.

Qualification: use Fast.com under the same AP/band/location, then diagnostics
only. The packaged all-in-one utility is carried forward unchanged from
v0.7.1.14-fix2 and should not be used for performance qualification.
Detailed per-F1/F2 timing remains disabled in this low-overhead package.

The driver is test-signed, not Microsoft production-signed.
"@ | Set-Content (Join-Path $stage 'README-TESTING.txt') -Encoding UTF8

$receiptPath=Join-Path $stage 'SOURCE_REVISION.txt'
$receipt=Get-Content -LiteralPath $receiptPath -Raw
$receipt=$receipt.Replace('driver_version=0.7.1.4',"driver_version=0.7.1.18-experimental-service-burst4`nstable_runtime_baseline=c9d715760da49ef491b681a1ba5b57e6ef6ef13b`ndirect_experiment_baseline=c9d715760da49ef491b681a1ba5b57e6ef6ef13b`ntx_credit_scheduling=$TxCreditScheduling`ntx_glom2=$TxGlom2`ntx_service_burst4=1`ntx_service_burst_max=4`ntx_glom_pressure_threshold=32`nfifo_buffer_pio=0`nfifo_fixed_port_pio=1`nfixed_port_pio=v0.7.1.17-unchanged`nv0_7_1_16_buffer_pio=rejected-hardware`nqualification=fast.com-plus-diagnostics`nall_in_one_utility=0.7.1.14-fix2-unchanged")
$receipt=$receipt.Replace('performance_branch=better-improvement',"performance_branch=$env:GITHUB_REF_NAME")
$receipt=$receipt.Replace('performance_baseline=16533ac0e7e477f5c604882d8cc82081119e3f90','performance_baseline=c9d715760da49ef491b681a1ba5b57e6ef6ef13b')
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
Write-Host "Packaged v0.7.1.18 Service-Burst4 experiment on unchanged v0.7.1.17 fixed-port transport."
