Set-StrictMode -Version Latest
$ErrorActionPreference='Stop'
$root=Split-Path -Parent $PSScriptRoot

function Read-RepoFile([string]$Path) {
    return (Get-Content -LiteralPath (Join-Path $root $Path) -Raw).Replace("`r`n","`n")
}

$header=Read-RepoFile 'src/driver/driver.h'
if($header -notmatch '#define RPI5CYW_TX_LIMIT 64u'){throw 'The proven 64-frame TX admission cap changed.'}

$rx=Read-RepoFile 'src/cyw43455/rx_config.h'
if($rx -notmatch 'CywInt\(A,"bus:txglom",1\)'){throw 'Host RX aggregation is not enabled.'}
if($rx -notmatch 'CywInt\(A,"bus:rxglom",0\)'){throw 'Host TX aggregation changed without dedicated validation.'}

$sdioHeader=Read-RepoFile 'src/sdio/sdio.h'
if($sdioHeader -notmatch '#define CYW_SDIO_HIGH_SPEED_CLOCK_KHZ 50000UL'){throw 'Verified 50 MHz SDR ceiling changed.'}
if($sdioHeader -notmatch '#define CYW_SDIO_OPERATING_CLOCK_KHZ 25000UL'){throw '25 MHz fallback changed.'}

$fifo=Read-RepoFile 'src/sdio/fifo_blocks.h'
if($fifo -notmatch '#define CYW_FIFO_BLOCK_SIZE 512UL'){throw 'Function-2 block size changed.'}
if($fifo -match 'SDHCI_TRNS_DMA'){throw 'DMA must remain a separately validated transport change.'}

$inf=Read-RepoFile 'package/rpi5cyw.inf'
if($inf -notmatch '(?m)^DriverVer\s*=\s*09/24/2026,0\.7\.0\.1\s*$'){throw 'Current main driver version is not 0.7.0.1.'}

$workflow=Read-RepoFile '.github/workflows/build-arm64-driver.yml'
if($workflow -notmatch 'branches:\s*\[main\]'){throw 'Driver CI is not attached to main.'}
foreach($stale in @('bringup/cyw43455-sdio-arm64','feature/rpi-os-wifi-performance','feature/perf-alpha2-queue-pressure','driver-perf0.7.0-alpha.2')) {
    if($workflow.Contains($stale)){throw "Stale branch/release dependency remains in active driver CI: $stale"}
}

Write-Output 'PASS: main is the authoritative build; 64-frame TX cap, RX aggregation, 50/25 MHz SDR policy, PIO transport boundary and driver version are intact.'
