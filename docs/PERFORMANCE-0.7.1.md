# Performance 0.7.1: SDIO card-interrupt wakeups

Driver version **0.7.1.0** on the single authoritative `main` branch.

This is a focused latency/stability candidate. It preserves the 0.7.0.1 packet
format, 64-frame TX admission cap, immediate NDIS completion ownership,
multi-block Function-2 PIO, RX read-ahead/aggregation, 4-bit 50 MHz SDR with
verified fallback, firmware/CLM/NVRAM, authentication, country handling and
band policy.

## Change

The UEFI already supplies a line interrupt resource for the SDIO controller.
The driver now registers that resource with NDIS and enables only the SDHCI
**card interrupt** as a hardware wake source.

- ISR checks only the card-interrupt status, masks that signal and dismisses
  only its host latch. It never clears command/data completion events.
- DPC only wakes the existing PASSIVE_LEVEL network worker.
- The existing worker remains the sole owner of CMD52, CMD53, backplane and
  FIFO operations.
- After the worker services firmware/FIFO state, rearm is synchronized with
  the ISR through `NdisMSynchronizeWithInterruptEx`.
- D3/stop paths quiesce the signal before teardown.
- If the interrupt resource is absent or NDIS registration fails, initialization
  continues with the existing bounded polling path.
- The normal 10 ms idle poll and bounded 1 ms exhausted-credit retry remain as
  fallback/recovery observations.

No DMA, scatter/gather, DDR50, host-TX glom, larger queue, firmware change or
radio-setting change is included.

## Diagnostics

DiagVersion is **32**. New values include:

- `InterruptResourceCount`, `InterruptResourceFlags`, `InterruptVector`,
  `InterruptLevel`
- `InterruptRegisterStatus`, `InterruptRegistered`, `InterruptType`
- `InterruptNeedsRearm`
- `InterruptIsrCount`, `InterruptDpcCount`, `InterruptRearmCount`,
  `InterruptSpuriousCount`

A healthy active run should show successful registration and increasing ISR/DPC
counts during traffic. Rearms should follow handled interrupts. A nonzero
spurious count is evidence to inspect, not automatically a failure.

## Hardware validation

Use the same Pi/router/band and compare against the pre-change main commit
`bdfa6f5b3bbe130e41abd67475411e94afd8a0f1`.

After install/restart and connection, run `Check-RPi5-WiFi-Readiness.cmd`
once and retain its ZIP. Compare throughput, gateway latency, DNS/HTTPS
responsiveness, `TxQueueFull`, `TxQueueMaxDelayMs`, `TxCreditWaits`,
`Cmd53WaitSleeps`, `RuntimeF2WaitSleeps`, transport blocked time and the new
interrupt counters. Also test an ordinary reboot and one disconnect/reconnect.

If interrupt registration is unavailable, the driver should still operate via
polling; that is a fallback result, not proof of an interrupt speedup. If the
driver regresses, restore the prior main commit/package rather than changing
firmware, UEFI, router settings or queue limits.
