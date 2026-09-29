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
if($inf -notmatch '(?m)^DriverVer\s*=\s*09/29/2026,0\.7\.1\.2\s*$'){throw 'Current better-improvement driver version is not 0.7.1.2.'}

$driver=Read-RepoFile 'src/driver/driver.c'
$network=Read-RepoFile 'src/cyw43455/network.c'
if($sdioHeader -notmatch '#define\s+SDHCI_INT_CARD_INT\s+0x00000100UL'){throw 'SDHCI card-interrupt definition is missing.'}
if($driver -notmatch 'NdisMRegisterInterruptEx'){throw 'NDIS interrupt registration is missing.'}
if($driver -notmatch 'NdisMSynchronizeWithInterruptEx'){throw 'Interrupt rearm synchronization is missing.'}
if($driver -notmatch 'CywNetworkWake\(Adapter\)'){throw 'Interrupt DPC does not wake the single bus worker.'}
if($driver -notmatch 'CYW_SDIO_CCCR_INT_PENDING'){throw 'PASSIVE rearm does not validate CCCR INTx.'}
if($driver -notmatch 'SDHCI_INT_STATUS_ENABLE'){throw 'CARD_INT status-enable masking is missing.'}
if($driver -match 'Rpi5CywInterruptWrite32\(Adapter, SDHCI_INT_STATUS, SDHCI_INT_CARD_INT\)'){
    throw 'CARD_INT is still incorrectly dismissed with W1C.'
}
if($driver -notmatch 'InterruptStormFallback'){throw 'Bounded interrupt-storm fallback is missing.'}
if($network -notmatch 'Rpi5CywInterruptRearm\(A,interruptWake,interruptUseful\)'){
    throw 'Worker does not rearm with observed interrupt work state.'
}
if($network -notmatch 'wait\.QuadPart=-100000'){throw 'Bounded 10 ms polling fallback was removed.'}

$installer=Read-RepoFile 'installer/Install-RPi5-WiFi-Driver.ps1'
foreach($pin in @('SupportedUefiRevisions','Get-Rpi5CompatibleUefiRevision','bda4c47','838d87d')) {
    if($installer.Contains($pin)){throw "Fixed UEFI revision dependency remains: $pin"}
}
if($installer -notmatch 'ACPI\\\\RPI0011'){throw 'Installer no longer requires the exact ACPI target.'}

$workflow=Read-RepoFile '.github/workflows/build-arm64-driver.yml'
if($workflow -notmatch 'branches:\s*\[main,\s*better-improvement\]'){
    throw 'Driver CI is not attached to main and better-improvement.'
}
foreach($stale in @('bringup/cyw43455-sdio-arm64','feature/rpi-os-wifi-performance','feature/perf-alpha2-queue-pressure','driver-perf0.7.0-alpha.2')) {
    if($workflow.Contains($stale)){throw "Stale branch/release dependency remains in active driver CI: $stale"}
}

Write-Output 'PASS: better-improvement 0.7.1.2 keeps the proven data path, masks CARD_INT in both host enable registers, validates CCCR INTx before PASSIVE rearm, and bounds empty-wake storms with polling fallback.'
