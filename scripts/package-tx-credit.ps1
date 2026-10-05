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
Raspberry Pi 5 CYW43455 Windows 11 ARM64 - 0.7.1.20 STABILITY RECOVERY
Immediate rollback baseline: 0.7.1.11 / 6ae93623c8767eda050b8c408250d3ec3ce19bfb
Direct experiment baseline: 0.7.1.19 / 1dbe4b11ef06ee4dde360e5fc8172732d53e0ecc
Architecture rollback: 0.7.1.9 / 0bc5ed12e9438cd82555d7bc8ae77211aa6f236b
Rejected threshold-16 experiment: 0.7.1.12 - DO NOT RESTORE
Earlier post-RX dispatch + bounded fast credit wake mode: $TxCreditScheduling
Two-frame host TX glom compiled candidate: $TxGlom2
Adaptive TX: <=512-byte frames may use backlog + Glom2 + Service-Burst4; >512-byte frames use main-style active-only + fresh-F1 pacing
F2 block PIO: v0.7.1.17 fixed-port implementation UNCHANGED

v0.7.1.19 reached about 80 Mbps download / 78 Mbps upload on hardware, but a
later RX glom block transfer produced one STATUS_IO_TIMEOUT. Diagnostics showed
one Cmd53 timeout, one FIFO block failure, one RX glom error and worker-source
disconnect, with no firmware deauth and no D3 power transition. v0.7.1.20 keeps
the v0.7.1.19 adaptive TX policy unchanged and targets that stability failure.

The SDIO protocol, fixed-port PIO, RX path, firmware/radio policy, 50 MHz /
4-bit bus mode, 64-frame active queue, small-frame 128-frame backlog capacity,
Glom2 format, Burst4 cap, worker scheduling and SDPCM framing are unchanged.

The fixed-address SDHCI FIFO data movement remains unchanged. Block-mode wait
logic now accepts an already-observed SDHCI completion/readiness event before
declaring the software deadline expired, preventing scheduler-preemption false
timeouts. Genuine partial FIFO faults remain terminal/no-replay. One fatal
runtime transport failure may queue one bounded adapter lifecycle restart; no
credentials are retained and no failed FIFO transaction is replayed.

Qualification: use Fast.com under the same AP/band/location, then diagnostics
only. The packaged all-in-one utility is carried forward unchanged from
v0.7.1.14-fix2 and should not be used for performance qualification.
Detailed per-F1/F2 timing remains disabled in this low-overhead package.

The driver is test-signed, not Microsoft production-signed.
"@ | Set-Content (Join-Path $stage 'README-TESTING.txt') -Encoding UTF8

$receiptPath=Join-Path $stage 'SOURCE_REVISION.txt'
$receipt=Get-Content -LiteralPath $receiptPath -Raw
$receipt=$receipt.Replace('driver_version=0.7.1.4',"driver_version=0.7.1.20-stability-recovery`nstable_runtime_baseline=1dbe4b11ef06ee4dde360e5fc8172732d53e0ecc`ndirect_experiment_baseline=1dbe4b11ef06ee4dde360e5fc8172732d53e0ecc`ntx_credit_scheduling=$TxCreditScheduling`ntx_glom2=$TxGlom2`nmain_upload_reference=d5d61aa0c2d864162615589fc93171252c5a6305`nfifo_event_before_deadline=1`nruntime_recovery_max=1`nruntime_recovery_replays_fifo=0`nadaptive_tx_hybrid=1`nadaptive_small_max=512`nadaptive_small_backlog=1`nadaptive_bulk_backlog=0`nadaptive_small_glom2=$TxGlom2`nadaptive_small_service_burst4=1`nadaptive_bulk_fresh_f1=1`ntx_service_burst4=1`ntx_service_burst_max=4`ntx_glom_pressure_threshold=32`nfifo_buffer_pio=0`nfifo_fixed_port_pio=1`nfixed_port_pio=v0.7.1.17-unchanged`nv0_7_1_16_buffer_pio=rejected-hardware`nqualification=fast.com-plus-diagnostics`nall_in_one_utility=0.7.1.14-fix2-unchanged")
$receipt=$receipt.Replace('performance_branch=better-improvement',"performance_branch=$env:GITHUB_REF_NAME")
$receipt=$receipt.Replace('performance_baseline=16533ac0e7e477f5c604882d8cc82081119e3f90','performance_baseline=1dbe4b11ef06ee4dde360e5fc8172732d53e0ecc')
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
Write-Host "Packaged v0.7.1.20 stability recovery on unchanged v0.7.1.19 adaptive TX policy."
