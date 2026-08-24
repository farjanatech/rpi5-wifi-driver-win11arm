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
- [x] Retry CMD5 three times each at 400, 200 and 100 kHz while recording raw
  response, interrupt, command-reset, clock, power and line-state diagnostics.
- [x] Implement bounded CMD0/CMD5/CMD3/CMD7 command polling.
- [x] Implement CMD52 reads and read CCCR/FBR identity registers.
- [ ] Implement CMD52 writes with read-after-write verification.
- [ ] Configure and enable function 1/function 2.
- [ ] Implement a bounded PIO CMD53 helper.
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

## Milestone 2 - WiFiCx

Only after Milestone 1 is stable:

- add WiFiCx/NetAdapterCx device creation;
- expose station capabilities;
- map scan results/events;
- implement connect/disconnect;
- add WPA2/WPA3 key handling;
- connect TX/RX data path;
- implement power transitions.

## Debugging rule

A failed stage should be diagnosed at the lowest working layer. For example, do not debug WiFiCx scan callbacks while CMD53 or firmware event reception is still unreliable.
