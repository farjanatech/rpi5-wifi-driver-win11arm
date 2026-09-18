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

Builds and tests for distributed packages run in GitHub Actions. The driver
links against `ndis.lib` and directly owns the dedicated ACPI RPI0011 SDIO2
controller. It does not use `sdbus.lib`, WDF, or WiFiCx.

## Driver signing

During bring-up, use only a dedicated test machine with Windows test-signing/kernel debugging configured as appropriate. Production distribution requires a properly signed Windows driver package.

## Current bring-up behavior

Driver 0.4 remains an experimental NDIS hardware probe, not working Wi-Fi.

During its serialized PASSIVE_LEVEL miniport initialization it:

1. maps the ACPI resource and initializes the SDHCI host;
2. runs CMD5/CMD3/CMD7 and CMD52 identification;
3. enables F1 and requests its ALP clock;
4. selects the ChipCommon backplane window;
5. performs 16 four-byte CMD53 reads of chip ID;
6. restores the saved window, clock control and F1 enable state.

A successful new probe is identified in the diagnostic ZIP by:

```text
ProbePhase=250, LastStatus=0, ProbeRestoreStatus=0, Cmd53ReadCount=16, ChipId=0x4345
```

The CMD53 helper reads byte-mode transfers only, with address/length bounds,
separate command/data/transfer completion handling, and reset/zero-output on
failure. Startup reads the ID register only: no arbitrary memory scan or RAM write.

A successful build and host simulation do not prove physical SDIO behavior.
GitHub compiles the actual sdio.c/chip.c against a fake-register test platform;
tests cover 512 transfer lengths, simultaneous status bits, R5/data errors,
timeouts, bounds, verification mismatch and each CMD52 failure/restore position.
The ARM64 WDK build then compiles the same source against real Windows headers.
