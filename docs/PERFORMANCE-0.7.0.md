# Performance candidate 0.7.0

This is an isolated development candidate on `feature/rpi-os-wifi-performance`,
based on `4f8f456b1b72d7f6b534531b02d83863ae9ed60c`. It does not replace or
modify driver exp0.6.29.1, connector exp0.6.29.2, or the working UEFI.
Passing CI is not Pi hardware validation or a promise of 100 Mbps.

## Implemented

- Function-2 multi-block CMD53 PIO: verified card multi-block capability and
  512-byte function block size, 1..32 blocks per command, fixed FIFO address,
  per-block buffer-ready checks, one absolute 250 ms command deadline.
- Whole-block TX padding is zeroed. Legacy byte mode handles short transfers
  and tails. A partial-transfer error stops; it never retries/replays FIFO data.
- Firmware-provided RX read-ahead inside runtime RX batches, with strict
  length/header validation. Control transactions retain header-first reads.
- Host RX aggregation: bounded descriptor/superframe parsing, validation of
  every child before any indication, and one child per existing RX budget slot.
  Outer headers alone update firmware credits and priority flow control.
  `bus:txglom` means device TX / host RX; host TX aggregation stays disabled.
  Explicit firmware `UNSUPPORTED` retains non-aggregated operation. Other
  configuration errors stop rather than claim a successful fast-path setup.
- Diagnostic counters identify actual block use, read-ahead, aggregates and
  failures. They are periodic cumulative observations, not packet captures.

The transport tests use the production C helpers. The register-model tests
cover 1..32 blocks, 64 KiB splitting, byte tails, invalid capabilities, partial
CRC failures, short transfers, deadlines, cancellation and no replay. RX tests
cover validated hints, control isolation, mixed event/data aggregates, retained
buffer ownership, last-child corruption, descriptor bounds and zeroed TX padding.
All existing protocol, firmware, queue, signing/installer and utility suites
remain required. Builds and tests run on GitHub; nothing is installed on the
development PC.

## Preserved

Firmware 7.45.229 and its matching CLM/calibration files, full upload readback,
50 MHz SDR negotiation/fallback, the 64-frame send queue, TX/RX fairness,
authentication, physical-country verification, automatic band policy, existing
IOCTL/connector interface and private-profile handling remain unchanged.

## Not yet implemented

Hardware-interrupt notification, DMA/scatter-gather and DDR50 are separate
remaining work. UEFI exposes an interrupt and advertises DDR50, but also marks
the controller non-coherent for DMA. Correct NDIS interrupt lifecycle, cache
and DMA-address handling, and a checked voltage/timing transition are necessary;
Linux flags cannot simply be transplanted into a Windows driver. No such
capability is claimed or silently enabled by this candidate.

## Installation and one useful comparison

Keep the complete working exp0.6.29.1 ZIP and connector exp0.6.29.2 for recovery.
Extract this package, run `Install-RPi5-WiFi-Driver.cmd` on the Pi, save work and
restart if requested. Keep the working UEFI; do not reinstall Windows, rename
router bands or delete all old driver packages. Use the existing connector EXE
and profile. Security prerequisites and test-signing warnings are unchanged.

After connecting, run the existing `Check-RPi5-WiFi-Readiness.cmd` once. The
driver-before/after snapshots include `FifoBlockReady`, `FifoBlockCommands`,
`FifoBlockBytes`, `FifoBlockFailures`, `FifoTransportFailed`, `RxReadAhead`, `RxReadAheadRejected`,
`RxGlomEnabled`, `RxGlomGroups`, `RxGlomFrames`, and `RxGlomErrors` (DWORDs;
long-running byte counters can wrap). Confirm band/channel and compare the same
workload on the same router. An enabled flag is not proof of aggregate traffic.
`FifoTransportFailed=1` is terminal until full firmware reinitialization; a
subsequent connection/control request cannot silently resume a damaged FIFO.

If it regresses or fails, retain the report then use Device Manager's Roll Back
Driver for this CYW43455 adapter if available. Otherwise select the previous
driver explicitly using Update driver / Browse / Let me pick / Have Disk and
the saved exp0.6.29.1 INF; the ordinary installer may prefer the newer version.
Do not remove the separate RP1 wired-Ethernet driver or change UEFI.
