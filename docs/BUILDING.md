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

## Driver signing

During bring-up, use only a dedicated test machine with Windows test-signing/kernel debugging configured as appropriate. Production distribution requires a properly signed Windows driver package.

## Current scaffold behavior

The initial sources only create a KMDF device and install PnP callbacks. The SDIO preparation routine deliberately returns `STATUS_NOT_IMPLEMENTED` until the Windows SDBUS transport is implemented.

A successful compile of this scaffold is therefore a project/toolchain validation step, not proof that the device can start.
