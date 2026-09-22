# exp0.6.22: proven packet path plus stall timing

This candidate is a baseline restoration and measurement build, **not a confirmed speed fix**.
The best supplied sustained result remains exp0.6.16: 18.55 Mbps, 128/128 completed.
Its band was not recorded. Different runs/bands do not isolate a driver cause.

## Why this change

The .21 hardware run completed 11/15 requests (1.37 Mbps effective), with 11/52
router-ping timeouts and a cumulative queue delay maximum of 1034 ms. All six
read-ahead-related activity counters were zero except ordinary header reads;
the new optimization never activated. That does not prove it caused the slowdown.
The root cause remains unresolved. DNS alone does not explain router stalls.

## Preserved baseline

- Restore the .16 receive procedure, TX4 / RX4-or-2ms / TX4 scheduling and 64-frame
  admission cap, retaining its completion-before-reentry ownership fix.
- SDIO engine, 4-bit/25-MHz operating mode, backplane window caching, bounded
  waits, recovery, firmware binaries, MAC/country validation and UEFI unchanged.
- Keep .20's existing signal-based 5-GHz preference, with both bands eligible,
  and .18's explicit read-only band/channel reporting. No band forcing, added
  scanning, larger queues, firmware substitution or higher clock.
- Read-ahead implementation/tests removed; retired registry counters written zero.
- Public example config stays blank/hashed. Editable private config remains local.

## Low-overhead measurements

A single runtime worker owns counters. KeQueryPerformanceCounter records raw
wall-clock ticks around complete operations, not every FIFO word. No additional
sleeps, locks, allocation, packet content, credentials or per-packet registry writes.
Disabled throughout firmware upload/configuration, reset on worker/D0 restart.

Thirteen buckets: TX pump, RX batch, completed worker work, idle wait, worker
start-to-start interval, diagnostics, explicit control, CMD52, F1 CMD53, F2 RX
CMD53, F2 TX CMD53, NDIS receive indication and credit recheck interval.
Each records count, total, maximum and counts >=10ms / >=100ms. Totals saturate.

WorkerInterval includes work and intentional idle/control time; it does NOT prove
scheduler starvation. CreditRecheck measures the interval until the next service
cycle after a credit-blocked TX pump, not exact hardware-credit availability.
RX-batch/worker-work timing covers completed batches/iterations; a failed SDIO
call is still timed. Inclusive nested buckets must not be summed.

A coherent 560-byte versioned TimingV1 binary value is written with the existing
periodic/phase diagnostic publication. Diagnostics duration includes its own
publication and is visible in the next snapshot. No new firmware queries during
downloads. The ordinary counters remain periodic/non-atomic; timing before/after
can include activity outside the download window. Timing maxima are cumulative,
not interval maxima. Session/frequency/reset validation prevents false deltas.
Instrumentation overhead is not yet measured on hardware.

## Run on the Raspberry Pi only

1. Extract the new package to a fresh folder. Do not edit the public example.
2. Run Install-RPi5-WiFi-Driver.cmd as administrator; restart the Pi once.
3. Keep your existing private configuration, or run Connect-RPi5-WiFi.cmd.
4. Disconnect competing wired/VPN routes for measurement, then run
   Test-RPi5-WiFi-Performance.cmd once. No other-device experiment is needed.
5. Share its single ZIP; it includes timing-report.json and timing-report.txt.

The installer does not alter UEFI, fan, BCD, Secure Boot or test-signing settings.
Keep .16 available. If this candidate regresses, use Device Manager's driver
rollback/Have Disk selection; installing an older INF alone may not replace a
higher-ranked newer version.

## Validation and next decision

GitHub CI checks actual SDIO success/failure/timeout/reset behavior with timing,
timing arithmetic/ABI, snapshot decoding, queue lifecycle/cancellation, firmware
control, scripts and ARM64 packaging. Scope guards compare the underlying SDIO
engine/queue/budgets to .16 and the radio/join behavior to .20.
The performance workload remains sequential 1-MiB requests, <=90s or 128 requests.
No speed/reliability claim is made until a Pi run validates it.

Use the timing evidence to select one targeted next change, not a speculative
queue/clock/block-mode rewrite. Goal: first recover repeatable, loss-free previous
throughput, then improve it.
