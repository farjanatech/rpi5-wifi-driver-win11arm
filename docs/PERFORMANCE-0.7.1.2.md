# Performance 0.7.1.2: bounded SDIO interrupt servicing

Branch: **`better-improvement`**  
Driver version: **0.7.1.2**

This candidate is based on main 0.7.1.1 at
`1b70aacdda3efce85d049d229342fbafcc467b5e`. It keeps the proven data path
unchanged: 64-frame TX admission, immediate NDIS completion ownership,
multi-block Function-2 PIO, RX read-ahead/aggregation, 4-bit 50 MHz SDR with
25 MHz fallback, firmware/CLM/NVRAM, authentication, country handling and the
UEFI-version-independent installer.

## Hardware evidence that motivated this branch

The September 29 physical Pi run of 0.7.1.1 completed all 128 sequential HTTPS
requests (128 MiB) at 34.32 Mbps with zero SDIO block failures and zero RX glom
errors. The same run also exposed an interrupt-control defect:

- `InterruptIsrCount`: 2,049,559 -> 2,723,419
- `InterruptDpcCount`: 2,049,537 -> 2,723,346
- about 673,860 extra interrupts during the radio/report interval
- `TransportPendingReads`: 2,697,615
- `TransportPendingEmpty`: 2,694,956

Therefore the storm was **not** simply a permanently asserted Function-1/2
pending bit. Almost every storm-driven worker pass observed CCCR INTx empty.

## Host-level correction

Linux SDHCI masks SDIO `CARD_INT` in both the Interrupt Status Enable and
Interrupt Signal Enable registers while the SDIO core services the function
interrupt. It does not blindly W1C the level-like `CARD_INT` bit.

0.7.1.2 follows that host-controller behavior while preserving the Windows
single-bus-owner architecture:

1. ISR recognizes `SDHCI_INT_CARD_INT`.
2. ISR masks CARD_INT in **both** `INT_STATUS_ENABLE` and
   `INT_SIGNAL_ENABLE`.
3. ISR does not issue CMD52/CMD53 and does not W1C CARD_INT.
4. DPC only records an IRQ wake and signals the existing PASSIVE worker.
5. Before rearm, the worker reads SDIO CCCR `INTx` (0x05).
6. If Function 1 or Function 2 is still pending, CARD_INT stays masked and the
   ordinary bounded poll loop continues servicing the device.
7. Only after the source is quiescent does
   `NdisMSynchronizeWithInterruptEx` re-enable CARD_INT in both host registers.

The NDIS enable callback no longer bypasses this PASSIVE source check.

## Storm safety net

An empty INTx after a real interrupt can be normal because the worker may have
already serviced the source. Therefore one empty wake is not treated as an
error.

If **32 consecutive interrupt-driven worker passes** observe no RX/firmware
work and no Function-1/2 pending state, the hardware interrupt path is disabled
for the remainder of that power session and the driver continues on the proven
bounded polling path. A D3/D0 reinitialization permits one fresh interrupt trial.

This prevents a controller/firmware mismatch from consuming a CPU indefinitely
while preserving networking.

## Diagnostics

DiagVersion **33** adds:

- `InterruptWakePending`
- `InterruptNdisEnableCalls`, `InterruptNdisDisableCalls`
- `InterruptPendingReads`, `InterruptPendingReadFailures`
- `InterruptPendingF1`, `InterruptPendingF2`, `InterruptPendingEmpty`
- `InterruptRearmDeferred`
- `InterruptUsefulWakeCount`
- `InterruptEmptyWakeCount`, `InterruptEmptyWakeStreak`
- `InterruptStormFallback`, `InterruptStormFallbackCount`
- `InterruptStatusEnable`, `InterruptSignalEnable`

## Pi validation

Use the same router/band/channel and run `Check-RPi5-WiFi-Readiness.cmd` and
the 128 MiB performance test.

A good hardware result should show:

- `InterruptRegistered=1`
- interrupt counters increasing with real traffic, not millions while idle
- `InterruptStormFallback=0` if the corrected host masking works
- or `InterruptStormFallback=1` with stable networking if the platform still
  produces empty card interrupts
- `FifoBlockFailures=0`, `FifoTransportFailed=0`, `RxGlomErrors=0`
- no material regression in throughput, gateway latency, DNS/HTTPS completion
  or reboot/reconnect reliability

Do not change UEFI, firmware, router settings, queue depth, DMA or DDR50 for this
comparison. Host-TX aggregation remains a separate follow-up after the interrupt
path is proven bounded.
