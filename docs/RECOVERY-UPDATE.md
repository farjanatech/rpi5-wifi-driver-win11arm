# Damian recovery candidate 0.7.1.23-damian.3

This candidate addresses a failed warm restart and misleading phase-400 status.
It is not yet hardware validated, and does not establish or eliminate the cause
of the initial runtime SDIO timeout. Keep it on `damian-edition/recovery-status`
until the Pi owner has tested it. No UEFI update is needed.

## Evidence

The October 10 capture ending `163551.zip` was collected while disconnected.
Its SHA-256 is
`17C98F781C51BEC6265B0F76C027583ABBC817D377C413B86097A481F824935B`.
All 22 manifest entries verified. The terminal snapshot preceded collection
by about 22 minutes; this is expected after the packet worker has stopped.

- One worker failed at 1,394.98 seconds of system uptime with
  `STATUS_IO_TIMEOUT (0xC00000B5)`.
- A two-block receive CMD53 timed out waiting for command completion before
  any bytes were consumed. The saved interrupt status was zero. The saved
  present-state register was `0x01DF0A06`, including data-available/data-active
  bits. This does not establish why command completion was absent.
- The one allowed recovery attempt failed during CMD5 enumeration: 18 attempts,
  zero valid responses, zero worker restarts. Recovery was no longer in progress.
- Live status nevertheless reported phase 400 and success. No new worker existed
  to replace that status, so the GUI continued to say the driver was starting.
- No firmware-originated disconnect, Windows power transition, or IRQ-storm
  fallback was recorded. These counters do not exclude electrical or firmware
  causes of the first timeout.

## Changes

1. Publish the recovery callback's terminal failure through live driver status.
   A successful thread launch preserves any newer error from that worker.
2. Before warm CMD5 enumeration, perform a bounded SDIO CCCR I/O reset through
   CMD52. Resetting SDHCI and issuing CMD0 alone do not reset the I/O card's state.
   A prior assigned relative address identifies a warm probe. Cold probe is
   unchanged. The failed FIFO transaction is never replayed.
3. Record `WarmCardResetAttempts` and `WarmCardResetStatus` in diagnostics schema
   47. A reset error is retained; validated CMD5 enumeration still decides
   readiness, allowing a card that was already physically reset to initialize.
4. Make the native GUI distinguish driver startup/recovery, terminal failure,
   and disconnected-and-ready. A disconnected but unavailable driver no longer
   prompts the user to disconnect again. The readiness gate remains enforced.

The one-recovery limit, FIFO timing, bus speed, firmware files, country policy,
queue limits, ACPI binding and UEFI resources are unchanged. This does not add
continuous reconnect, reset loops or a physical Wi-Fi power-cycle mechanism.

## Validation and use

Host tests exercise the actual SDIO implementation with a selected-card model,
including cold probe, repeated warm probe, failed reset-register read, failed
reset write, an already-reset card, and stopped I/O. The new warm-probe test
fails without the reset step. Actual recovery-callback tests inject power-down,
probe and thread-creation failures and check live status and cleanup.

Use only a package whose BUILD.txt identifies this candidate and a successful
ARM64 build. Install its setup on the Pi, restart Windows, and verify the GUI
title is 0.7.1.23. Use Wi-Fi normally. If it disconnects again, collect option
4 (diagnostics only) before restarting; the new reset fields will show whether
the card-reset attempt succeeded. The earlier capture already preserves the
current failure, so Windows can now be restarted for recovery or installation.

Rollback uses a retained 0.7.1.22 package and its matching GUI. Older GitHub
release assets were unavailable when this candidate was prepared; do not rely
on those historical download links.

Protocol references: [Linux v6.12 SDIO reset](https://github.com/torvalds/linux/blob/v6.12/drivers/mmc/core/sdio_ops.c)
and [card reinitialization](https://github.com/torvalds/linux/blob/v6.12/drivers/mmc/core/sdio.c).
