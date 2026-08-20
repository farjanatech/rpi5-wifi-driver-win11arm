# Building

## Intended toolchain

Use a current Visual Studio 2022 installation with the Windows Driver Kit (WDK) and ARM64 driver build support.

The project is intentionally limited to `ARM64` configurations.

## Visual Studio

Open `rpi5-cyw43455.sln`, select either:

- `Debug | ARM64`, or
- `Release | ARM64`

and build the solution.

## Command line

From a Visual Studio/WDK developer environment:

```powershell
msbuild .\rpi5-cyw43455.sln /m /p:Configuration=Debug /p:Platform=ARM64
```

The project links against `sdbus.lib`, which provides the Windows SD bus helper routines used by this driver.

## Driver signing

During bring-up, use only a dedicated test machine with Windows test-signing/kernel debugging configured as appropriate. Production distribution requires a properly signed Windows driver package.

## Current bring-up behavior

The driver now contains the first Windows-native SDBUS transport slice.

During `EvtDevicePrepareHardware` it:

1. opens `SDBUS_INTERFACE_STANDARD` for the SDIO function PDO;
2. initializes the bus interface without enabling card interrupts yet;
3. queries `SDP_FUNCTION_NUMBER`;
4. configures block length to 64 bytes for function 1 and 512 bytes for function 2;
5. performs read-only CMD52 checks of CCCR/FBR registers.

A successful device start should emit a debugger line similar to:

```text
RPI5CYW: SDIO ready fn=1 block=64 CCCR=0x.. IOEx=0x.. IORx=0x.. FBR=0x..
```

The source also contains a bounded synchronous CMD53 helper, but the driver does not execute CMD53 automatically during startup. That is intentional: first hardware validation should be non-destructive.

A successful compile proves only that the WDK/SDBUS-facing source is accepted by the toolchain. Real SDIO behavior still has to be validated on the Raspberry Pi 5.
