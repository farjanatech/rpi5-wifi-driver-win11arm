# Bring-up plan

## Milestone 1A - Platform-device enumeration

The matching direct-SDIO UEFI exposes the dedicated Wi-Fi SDIO2 host as
`ACPI\RPI0011`. Microsoft `sdbus` must not enumerate or own child functions for
this design. First prove that the NDIS miniport binds only to this ACPI device.

Record from the test Pi 5:

- Device Manager hardware IDs;
- allocated SDIO2 MMIO and interrupt resources;
- resources;
- driver load status;
- kernel debugger output.

## Milestone 1B - SDIO transport

Current implementation status:

- [x] Map the SDIO2 MMIO resource assigned to `ACPI\RPI0011`.
- [x] Reset the host and configure an identification clock.
- [x] Run three bounded CMD5 query/voltage-request cycles each at 400, 200 and
  100 kHz, reject invalid R4 data, and record raw response, interrupt,
  command-reset, host-control, timeout, clock, power and line-state diagnostics.
- [x] Implement bounded CMD0/CMD5/CMD3/CMD7 command polling.
- [x] Implement CMD52 reads and read CCCR/FBR identity registers.
- [x] Implement CMD52 writes with masked read-after-write verification.
- [ ] Configure and enable function 1/function 2.
- [x] Implement a bounded byte-mode PIO CMD53 read helper (1..512 bytes).
- [x] Add function-1 enable, ALP clock and 16 ChipCommon ID reads, with restoration.
- [ ] Validate the new CMD53 path on the Raspberry Pi (host simulation is not hardware proof).
- [ ] Validate the above on Raspberry Pi 5 hardware.
- [ ] Validate repeated CMD53 reads/writes against known-safe CYW43455 registers/RAM.
- [ ] Register and acknowledge card interrupts.

### First hardware test

Build/install the ARM64 package and capture the kernel debugger output for each enumerated function.

Expected success line:

```text
RPI5CYW: SDIO ready fn=<n> block=<size> CCCR=0x.. IOEx=0x.. IORx=0x.. FBR=0x..
```

If startup fails, capture the exact `RPI5CYW:` error line and the Device Manager status/code.

Do not proceed to firmware loading until repeated SDIO reads/writes are stable.

## Milestone 1C - Chip/backplane

1. Select Broadcom backplane window.
2. Read chip ID/core information.
3. Locate ChipCommon, SDIO, ARM CR4 and memory cores.
4. Validate RAM base/size assumptions for CYW43455.

## Milestone 1D - Firmware

1. Halt/reset the firmware CPU as required.
2. Upload `brcmfmac43455-sdio.bin`.
3. Append/prepare board NVRAM.
4. Release firmware CPU.
5. Wait for firmware ready indication.
6. Download CLM blob if required.
7. Issue a harmless BCDC query and verify the response.

Firmware binaries are not committed yet. Their licensing and provenance must be preserved when they are added.

## Milestone 2 - Ethernet-style NDIS data path and Wi-Fi control

Only after Milestone 1 is stable:

- keep the NDIS Ethernet-style adapter requested by the user;
- provide a restricted control interface/utility for scan results and credentials;
- implement connect/disconnect and firmware security configuration;
- implement WPA2 first; advertise no unimplemented WPA3 capability;
- connect TX/RX data path;
- implement power transitions.

## Debugging rule

A failed stage should be diagnosed at the lowest working layer. For example, do not debug WiFiCx scan callbacks while CMD53 or firmware event reception is still unreliable.

## Driver 0.4 probe phases

200: save/enable F1; 210: wait F1 ready; 220: ALP clock request/availability;
230: save/select backplane window; 240: CMD53 chip ID; 250: all 16 IDs matched;
260: chip reads succeeded but restoration failed. ProbePhase survives the NDIS
Stage=120 marker. LastStatus and ProbeRestoreStatus must both be zero.
IoEnable/IoReady/ChipClockCsr record observations during the probe, not post-restore
card state. No card IRQs, core resets, firmware/RAM writes or network connection
are attempted. F1, clock control and window registers are restored best-effort;
power-cycle the Pi if restoration fails. Diagnostics record any cleanup failure.
