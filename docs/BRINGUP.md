# Bring-up plan

## Milestone 1A - Device enumeration

Expected Raspberry Pi/CYW43455 SDIO IDs seen in the reference implementation:

- `SD\VID_02D0&PID_A9BF&FN_1`
- `SD\VID_02D0&PID_A9BF&FN_2`
- `SD\VID_02D0&PID_A9BF&FN_3`
- alternate `PID_4345` variants

First prove that Windows enumerates the functions before debugging the Wi-Fi protocol.

Record from the test Pi 5:

- Device Manager hardware IDs;
- parent SD host controller;
- resources;
- driver load status;
- kernel debugger output.

## Milestone 1B - SDIO transport

Current implementation status:

- [x] Open `SDBUS_INTERFACE_STANDARD` for the function PDO.
- [x] Initialize the SD bus interface.
- [x] Query `SDP_FUNCTION_NUMBER`.
- [x] Implement synchronous CMD52 read/write helpers.
- [x] Read CCCR/FBR registers during a read-only startup smoke test.
- [x] Configure function block length for FN1/FN2.
- [x] Implement a bounded synchronous CMD53 helper using an MDL-backed nonpaged bounce buffer.
- [ ] Validate the above on Raspberry Pi 5 hardware.
- [ ] Enable the required function(s) and wait for ready where Windows/SDBUS does not already manage it.
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
