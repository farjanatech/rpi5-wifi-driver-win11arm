# Damian stability update 0.7.1.22-damian.2

This update addresses two code-level performance problems. On 2026-10-10 the
repository owner reported that the released package works perfectly on their
Pi 5 C1 and requested promotion to `main`. The tested source commit `5786f6a`
is now included in `main`. This report does not establish the original
slowdown's root cause or independently qualify long-duration stability.
The earlier 0.7.1.21 package remains available for rollback. No UEFI update is needed.

## Changes

- Runtime diagnostic registry writes previously ran on the SDIO packet worker.
  One operation in the supplied slow capture took about 81 ms. The worker now
  copies the existing diagnostic values into a preallocated buffer; a separate
  thread writes that immutable copy. Two 128 KiB buffers bound memory use.
  When both are occupied, a snapshot is skipped instead of waiting or allocating.
  Startup firmware/probe exports remain synchronous while no link is active.
- A completed multi-buffer send could release several active queue slots while
  only one older pending send was promoted. Later bulk sends were rejected
  despite free capacity. Completion now fills available slots in FIFO order
  before reentrant admission. Limits remain 64 active and 128 pending frames;
  bulk traffic still uses active-only admission and fresh F1 pacing.
- The native GUI/setup and INF identify driver version 0.7.1.22. The installer
  embeds this update and continues to require the RPI1060 device.

The diagnostic thread cannot access SDIO, live adapter state, credentials or
packets. It drains queued copies and is joined before network-worker exit and
adapter teardown. Allocation or thread creation failure disables asynchronous
runtime export without blocking networking. Registry writers serialize complete
batches, but readers should still treat the multi-value registry export as
best-effort telemetry, not a transactional database snapshot.

## Diagnostics and checks

Diagnostic schema version is 46. Existing fields and binary layouts remain.
`DiagnosticsAsyncEnabled` and `DiagnosticsAsyncStatus` report startup success;
`DiagnosticsCaptureSkipped` and `DiagnosticsCaptureOverflow` report missing
captures. `SnapshotTimeUtc`, TimingV2 and TxCreditV1 timestamps are captured on
the packet worker. Persistence can happen later. The timing diagnostics bucket
now measures capture work rather than registry persistence.

The queue regression failed on the previous source and passes with the fix.
Host checks cover immutable copied values, capacity/overflow, slow-writer
backpressure, 20,000 concurrent FIFO publications, allocation/thread failures,
drain-before-free, repeated restart/stop, queue cancellation and exactly-once
send completion. GitHub CI runs these with AddressSanitizer, existing driver
regressions, ARM64 WDK builds in both scheduling modes, and GUI/setup self-tests.
Host simulation and successful compilation do not establish hardware stability.

The original slow and healthy captures both had queue rejections, and neither
showed SDIO errors or interrupt fallback. These changes therefore have a clear
mechanical purpose but do not establish the original slowdown's root cause.

## Installation and rollback

Install the update with its bundled `RPi5-WiFi-Setup.exe`, then reboot Windows
so the new kernel driver is loaded. The GUI title identifies the package;
Device Manager should show driver version 0.7.1.22. Use Wi-Fi normally. There
is no need to force the old slowdown to recur while the connection stays fast.

If behavior worsens, use Device Manager's Roll Back Driver if available, or
choose the retained 0.7.1.21 package through Update Driver / Browse / Let me
pick / Have Disk. Reboot after rollback. The previous package is available at
[v0.7.1.21-damian.1](https://github.com/farjanatech/rpi5-wifi-driver-win11arm/releases/tag/v0.7.1.21-damian.1).
Keep the existing Damian UEFI and RPI0011 interrupt-provider driver.
