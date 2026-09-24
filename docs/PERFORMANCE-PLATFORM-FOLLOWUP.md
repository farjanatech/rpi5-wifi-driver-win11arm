# Platform work after the first transport candidate

This is a source-audit/design record, not an implemented feature claim.

The working UEFI exp.0.5 (`838d87df37fe1b27c75a674fca64c1fa067413e3`)
SDC1 ACPI device exposes memory at `0x1001100000`, interrupt 306 (level/high),
`_CCA=0`, and a DSM indicating DDR50 capability. The direct-driver patch changes
its HID to RPI0011; it does not delete the interrupt resource. The current
Windows miniport maps memory only. A missing UEFI interrupt is therefore not
an established explanation for polling.

## Interrupt notifications

Use the allocated interrupt resource and NDIS interrupt registration, with a
short ISR that recognizes/masks only the card-interrupt source. DPC wakes the
existing passive bus worker; it must not perform SDIO commands or create a
second FIFO owner. Synchronize all shared interrupt-register/state changes,
rearm only after service, and close the service/rearm/wait lost-wakeup race.
Retain bounded polling as a recovery observation, not as a second bus owner.
Test interrupts during initialize/pause/D3/restart/shutdown/removal and ensure
deregistration finishes before MMIO or the worker event is freed.
See [Microsoft's MiniportInterrupt contract](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/ndis/nc-ndis-miniport_isr).

## DMA and scatter/gather

`_CCA=0` requires proper coherency handling, not raw physical addresses of
arbitrary kernel buffers. Microsoft specifically recommends WDF/WDM DMA rather
than NDIS scatter/gather DMA for ARM/ARM64. Begin with a bounded common-buffer
ADMA design using the PDO/HAL DMA adapter, validated DMA address width/host
capabilities, mapped device addresses, cache ownership and descriptor bounds.
Only then consider packet MDL scatter/gather. Handle abort/reset, completion,
D3 and surprise removal without freeing memory still accessible by hardware.
An accepted HAL allocation is not proof that the Pi's DMA translation works.
See [Microsoft's ARM/ARM64 DMA guidance](https://learn.microsoft.com/en-us/windows-hardware/drivers/network/ndis-scatter-gather-dma).

## DDR50

Validate both host and card UHS capabilities plus the board's voltage/clock
control contract. A complete implementation needs the SDIO voltage negotiation,
CMD11 and DAT-line checks where required, card timing selection, host DDR50
timing and verified readback. Do not merely write HOST_CONTROL2 or assume the
advertised DSM means the existing 3.3 V setup can be reused. Recovery must
restore a known voltage/timing pair; resetting only one endpoint is unsafe.
The existing 50 MHz SDR path remains until this sequence is implemented and
validated. DDR50 doubles raw bus signaling capacity, not necessarily internet
throughput.

Reference implementations: Raspberry Pi Linux `8e8c07957368233228a9af82b9e99209653ef1e7`,
`drivers/mmc/host/sdhci.c`, `sdhci-brcmstb.c`, `drivers/mmc/core/sdio.c`, and
`arch/arm64/boot/dts/broadcom/bcm2712-rpi-5-b.dts`. Do not transplant Linux DMA
addresses, locks or voltage callbacks into Windows.

No firmware swap is bundled with these transport changes. A future firmware
comparison must keep matching firmware/CLM and original board calibration,
record hashes and measure separately; the newer version number is not evidence
of the current bottleneck.
