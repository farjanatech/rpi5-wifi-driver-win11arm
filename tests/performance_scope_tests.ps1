Set-StrictMode -Version Latest
$ErrorActionPreference='Stop'
$root=Split-Path -Parent $PSScriptRoot

function Read-RepoFile([string]$Path) {
    return (Get-Content -LiteralPath (Join-Path $root $Path) -Raw).Replace("`r`n","`n")
}

$header=Read-RepoFile 'src/driver/driver.h'
if($header -notmatch '#define\s+RPI5CYW_TX_LIMIT\s+64u'){throw 'The proven 64-frame TX admission cap changed.'}

$rx=Read-RepoFile 'src/cyw43455/rx_config.h'
if($rx -notmatch 'CywInt\(A,"bus:txglom",1\)'){throw 'Host RX aggregation is not enabled.'}
if($rx -notmatch 'CywInt\(A,"bus:rxglom",0\)'){throw 'Host TX aggregation changed without dedicated validation.'}

$sdioHeader=Read-RepoFile 'src/sdio/sdio.h'
if($sdioHeader -notmatch '#define\s+CYW_SDIO_HIGH_SPEED_CLOCK_KHZ\s+50000UL'){throw 'Verified 50 MHz SDR ceiling changed.'}
if($sdioHeader -notmatch '#define\s+CYW_SDIO_OPERATING_CLOCK_KHZ\s+25000UL'){throw '25 MHz fallback changed.'}

$fifo=Read-RepoFile 'src/sdio/fifo_blocks.h'
if($fifo -notmatch '#define\s+CYW_FIFO_BLOCK_SIZE\s+512UL'){throw 'Function-2 block size changed.'}
if($fifo -match 'SDHCI_TRNS_DMA'){throw 'DMA must remain a separately validated transport change.'}

$inf=Read-RepoFile 'package/rpi5cyw.inf'
if($inf -notmatch '(?m)^DriverVer\s*=\s*09/29/2026,0\.7\.1\.1\s*$'){throw 'Current main driver version is not 0.7.1.1.'}

$driver=Read-RepoFile 'src/driver/driver.c'
$network=Read-RepoFile 'src/cyw43455/network.c'
if($sdioHeader -notmatch '#define\s+SDHCI_INT_CARD_INT\s+0x00000100UL'){throw 'SDHCI card-interrupt definition is missing.'}
if($driver -notmatch 'NdisMRegisterInterruptEx'){throw 'NDIS interrupt registration is missing.'}
if($driver -notmatch 'NdisMSynchronizeWithInterruptEx'){throw 'Interrupt rearm synchronization is missing.'}
if($driver -notmatch 'CywNetworkWake\(Adapter\)'){throw 'Interrupt DPC does not wake the single bus worker.'}
if($network -notmatch 'Rpi5CywInterruptRearm\(A\)'){throw 'Worker does not rearm the card interrupt after service.'}
if($network -notmatch 'wait\.QuadPart=-100000'){throw 'Bounded 10 ms polling fallback was removed.'}

$installer=Read-RepoFile 'installer/Install-RPi5-WiFi-Driver.ps1'
foreach($pin in @('SupportedUefiRevisions','Get-Rpi5CompatibleUefiRevision','bda4c47','838d87d')) {
    if($installer.Contains($pin)){throw "Fixed UEFI revision dependency remains: $pin"}
}
if($installer -notmatch 'ACPI\\\\RPI0011'){throw 'Installer no longer requires the exact ACPI target.'}

$workflow=Read-RepoFile '.github/workflows/build-arm64-driver.yml'
if($workflow -notmatch 'branches:\s*\[main\]'){throw 'Driver CI is not attached to main.'}
foreach($stale in @('bringup/cyw43455-sdio-arm64','feature/rpi-os-wifi-performance','feature/perf-alpha2-queue-pressure','driver-perf0.7.0-alpha.2')) {
    if($workflow.Contains($stale)){throw "Stale branch/release dependency remains in active driver CI: $stale"}
}

Write-Output 'PASS: main 0.7.1.1 keeps the proven queue/RX/SDR/PIO boundaries and adds synchronized card-interrupt wakeups with polling fallback.'
