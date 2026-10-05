# v0.7.1.17 — fixed-port F2 PIO rollback

Branch: **`new-improvement-f2-buffer-pio`**  
Direct source baseline: **v0.7.1.15 / `66e0198609913fcc407c595e580e39c83777b60f`**  
Rejected build: **v0.7.1.16 / register-buffer PIO**  
Stable rollback: **v0.7.1.11 / `6ae93623c8767eda050b8c408250d3ec3ce19bfb`**

## Why v0.7.1.16 was rejected

Pi 5 hardware testing reached connection phase 510 and then failed at
`country-initial-read` with `STATUS_INVALID_DEVICE_STATE (0xC0000184)`.
The immediately preceding F2 control transaction had already latched
`FifoTransportFailed`.

v0.7.1.16 replaced repeated accesses to the single SDHCI FIFO data register
with `READ_REGISTER_BUFFER_ULONG` / `WRITE_REGISTER_BUFFER_ULONG`. Those
buffer helpers are not used by this project for a fixed-address FIFO port.

## v0.7.1.17 correction

v0.7.1.17 intentionally restores the **entire production `src/` tree**
byte-for-byte to the green v0.7.1.15 Service-Burst2 baseline.

For each 512-byte F2 block the driver again performs 128 repeated 32-bit
accesses to the exact same `SDHCI_BUFFER` register address. The existing
per-word `IoStopped` observation, block readiness checks, absolute command
deadline, partial-transfer failure latch and no-replay policy are restored
unchanged.

CI additionally asserts that neither `READ_REGISTER_BUFFER_ULONG` nor
`WRITE_REGISTER_BUFFER_ULONG` appears in the F2 fixed-port implementation.

## What remains unchanged

- Service-Burst2 behavior from v0.7.1.15;
- 64-frame active TX queue;
- 128-frame deferred backlog;
- two-frame glom policy and threshold;
- SDPCM framing and firmware/radio policy;
- 512-byte F2 block size and maximum 32 blocks/command;
- 50 MHz / 4-bit negotiated bus behavior;
- frozen v0.7.1.14-fix2 all-in-one utility;
- test-signing/package model.

## Hardware qualification

1. Install TX mode 1 and reboot if requested.
2. Confirm Wi-Fi connection succeeds first.
3. Run Fast.com under the same AP, 5 GHz band and physical placement.
4. Collect diagnostics after the run.
5. Compare against v0.7.1.15 and v0.7.1.11.

This is a corrective hardware-test candidate, not a Microsoft production-signed
release.
