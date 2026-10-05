# Integrated testing

The current default is **v0.7.1.20** on `main`. The exact hardware-tested
driver source is frozen on `release-v0.7.1.20` at
`c16aa318da490350126739add45223a186ab0a47`.

## Hardware validation baseline

The promoted Raspberry Pi 5 result is approximately **80 Mbps download /
80 Mbps upload** on Fast.com with stable extended browsing/use and no recurrence
of the previous runtime disconnect.

For a clean comparison:

1. Install the TX mode 1 package built from the current candidate.
2. Reboot if the installer requests it.
3. Connect on the same AP, 5 GHz band and physical placement.
4. Confirm authenticated link, IPv4 address and default route.
5. Measure both download and upload.
6. Browse/use the connection for an extended period.
7. Collect diagnostics before rebooting if any disconnect or transport failure
   occurs.

A candidate should not be promoted if it materially regresses either throughput
direction or stability versus v0.7.1.20.

## Current behavior to preserve

- 64-frame active TX queue.
- Adaptive TX hybrid: Ethernet frames up to 512 bytes may use the bounded
  deferred backlog, Glom2 and Service-Burst4.
- Frames above 512 bytes use active-only admission and a fresh F1 service before
  every F2 transfer.
- Fixed-address SDHCI FIFO PIO; failed or partial FIFO transfers are never
  replayed.
- SDHCI readiness/completion already observed by hardware is accepted before a
  software deadline is declared expired.
- One bounded runtime adapter lifecycle recovery is available after a fatal
  transport failure.
- Firmware, country and WPA2 policy remain unchanged from the promoted release.

## CI requirements

Before hardware testing, both TX modes must pass the full GitHub Actions matrix:
host/ASAN tests, SDIO and firmware/control simulations, ownership/lifecycle
tests, optimized ARM64 build, test-signing and packaging.

Physical Raspberry Pi testing remains required. CI cannot prove RF behavior,
Internet throughput, or long-duration stability.

## Current reference notes

- [Adaptive TX Hybrid v0.7.1.19](TX-ADAPTIVE-HYBRID-0.7.1.19.md)
- [Stability Recovery v0.7.1.20](STABILITY-RECOVERY-0.7.1.20.md)

For architecture/build information, use `ARCHITECTURE.md`, `BUILDING.md` and
`BRINGUP.md`. Historical experiment details remain available in Git history.
