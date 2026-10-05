# v0.7.1.20 — Stability Recovery

Direct baseline: **v0.7.1.19 / `1dbe4b11ef06ee4dde360e5fc8172732d53e0ecc`**

## Hardware evidence

After v0.7.1.19 achieved approximately 80 Mbps download and 78 Mbps upload,
the adapter later disconnected during browsing. The post-failure snapshot
showed:

- NetworkStatus / LastWorkerFailureStatus: `0xC00000B5` (STATUS_IO_TIMEOUT)
- Disconnect source: worker
- Cmd53Timeouts: 1
- FifoBlockFailures: 1
- FifoTransportFailed: 1
- RxGlomErrors: 1
- Cmd53BytesTransferred at failure: 2048 bytes
- Firmware disconnect/deauth/disassoc/link-down counters: 0
- Power D3 transitions: 0

The RX glom poller increments RxGlomErrors when a pending superframe FIFO read
fails, proving this event occurred on the RX/glom block path rather than the
adaptive bulk-upload policy.

## Fix 1 — hardware event before software deadline

v0.7.1.19 read SDHCI status, checked the absolute deadline, and only then
checked whether the requested buffer-ready/completion event was already
present. A PASSIVE worker descheduled past the deadline could therefore report
a false timeout even though hardware had completed while the worker was away.

v0.7.1.20 keeps errors first, then accepts an already-observed SDHCI
readiness/completion condition, and only if the event is still absent applies
the deadline. This does not replay or extend a pending FIFO transaction.

`FifoLateCompletionAccepted` records when that late-but-already-complete path
is used.

## Fix 2 — one bounded runtime lifecycle recovery

A fatal runtime transport failure previously terminated the worker and left
`N->Ready=FALSE`, so later CONNECT/DISCONNECT IOCTLs returned
`STATUS_DEVICE_NOT_READY` until a reboot/device restart.

v0.7.1.20 may queue exactly one NDIS work-item recovery after an already-ready
runtime worker fails with IO timeout/device error (or a latched FIFO invalid
state). It reuses the existing tested network power-off/power-on lifecycle to
start a new firmware/SDIO session. It does not retain credentials, automatically
reassociate, or replay the failed FIFO request.

## Performance policy unchanged

The v0.7.1.19 adaptive TX source files are protected byte-for-byte:
small <=512-byte frames retain backlog/Glom2/Burst4, while >512-byte bulk frames
remain active-only with fresh F1 before every F2.

## New diagnostics

DiagVersion 45 adds FIFO last-failure phase/progress evidence,
FifoLateCompletionAccepted, and RuntimeRecovery* counters.
