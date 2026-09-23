# exp0.6.25: bounded credit-resumption latency candidate

Experimental, test-signed Windows 11 ARM64 driver for the existing direct-SDIO
ACPI RPI0011 UEFI. Keep **exp0.6.24** as the tested fallback. This release does
not change UEFI, fan control, firmware binaries, country or WPA2 security.

## Measured baseline and remaining issue

The .24 Raspberry Pi report completed **128/128 HTTPS downloads at 28.96 Mbps**
in 37.07 seconds, on verified 5 GHz and 4-bit/50 MHz SDR. Its four successive
32-download groups measured 28.13, 29.44, 29.51 and 28.89 Mbps, without the
earlier multi-second download stalls. That is one short hardware run, not a
guarantee of sustained reliability or a controlled attribution to one change.

The same report recorded **4,537 queue-full rejections** and two `NoResources`
router probes. The other 34 load probes succeeded (median 9 ms, maximum 62 ms).
All 50 echo requests that reached the driver's send probe received matching
replies, and no RX/checksum/SDIO timeout errors were reported. Local admission
pressure remains worth addressing even though all downloads completed.

`TxCreditWaits` counts unsuccessful pump opportunities, not milliseconds or
distinct firmware stalls. Two pumps can observe the same block. Likewise,
`CreditRecheck` includes useful worker activity and is not removable wasted
time. These counters do not prove that the previous 10 ms wait caused every
queue rejection.

## Narrow change

After a worker cycle with **no RX/TX progress**, the existing event wait may
request 1 ms instead of its normal 10 ms only when `TxSeq == TxMax` explicitly
shows exhausted credits, a locked queue inspection finds pending sends and an
open admission gate, and lifecycle/global/priority flow gates permit service.
Other reasons for an unavailable TX path do not qualify.

An episode permits at most **four requested 1 ms waits**, with no further short
wait once 20 ms of monotonic time has elapsed. The driver then keeps the normal
10 ms backoff until TX progress or eligibility clears, including returned
credits, an empty queue or a flow stop. RX progress alone while credits remain
exhausted does not replenish the short-wait budget. A progressing cycle does
not perform this idle event wait.

This remains an event wait, not a CPU spin loop. Early event wakeups or timer
expiry cannot create an unbounded short-poll episode. The 20 ms check bounds
eligibility for another short request, not the scheduler's completion of an
in-flight wait. Actual waits may be earlier or longer than requested; no timer
resolution change is made. See Microsoft's
[KeWaitForSingleObject contract](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/wdm/nf-wdm-kewaitforsingleobject)
and [timer accuracy guidance](https://learn.microsoft.com/en-us/windows-hardware/drivers/kernel/timer-accuracy).

The aim is to notice resumed firmware credits sooner during a short idle gap.
It does not manufacture credits, bypass flow control, drop or rewrite TCP ACKs,
or imply that queue overflow will disappear. **Higher throughput and improved
latency are hypotheses until the Pi report confirms them.**

The existing before/after driver reports include `TxRetryVersion` and counters
`TxRetryFastRequests`, `TxRetryBackoffRequests`, `TxRetryIdleRequests`,
`TxRetryFastResumes`, `TxRetryFastTimeouts` and `TxRetryFastWakes`, plus
`TxRetryActualFastWait100ns` (accumulated actual wait) and
`TxRetryMaxFastWait100ns` (maximum actual wait). These are cumulative observations,
not a new load-time firmware query. `FastResumes` means the next worker iteration
sent after a short wait; it does **not** establish that the wait caused faster
credit return. Interpret before/after differences together with the workload,
not as isolated proof of benefit.

## Preserved from .24

- 64 held-frame admission cap and whole-NBL ownership through completion.
- 4 TX / at most 4 RX frames or 2 ms / 4 TX processing budgets.
- Cancellation, expiry, pause/power/halt and exactly-once completion rules.
- Bounded same-SSID 5 GHz preference, verified authentication and automatic fallback.
- Capability/readback-checked 50 MHz SDR and verified <=25 MHz fallback.
- Receive-notification recovery, passive history, firmware/CLM/NVRAM and BD validation.
- Optimized Release ARM64 build and the existing performance workload.

GitHub CI checks the bounded wait policy and existing lifecycle, queue,
transport, wire-format and utility regressions. Simulation/build success is
not physical validation. No development-PC driver installation is part of CI.

## Use and compare

1. Keep the complete .24 package for rollback. Install the complete .25 package
   on the Raspberry Pi and restart once.
2. Keep your existing editable `WiFi.private.json`. Do not edit the hashed
   example configuration or publish your private profile.
3. Run `Test-RPi5-WiFi-Performance.cmd` as usual and share its single resulting
   ZIP. It already contains diagnostics; no separate collector or additional
   device experiment is needed.

Compare verified goodput, complete/failed downloads, router probe results,
queue rejections, queue delay and actual band/bus mode together. A smaller
credit-wait count alone is not an improvement. Preserve .24 if the new run
regresses; do not reflash Windows, change UEFI or remove unrelated drivers.

See [performance report details](PERFORMANCE-0.6.25.md) and the retained
[.24 design notes](EXP0.6.24.md).
