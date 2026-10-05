# Building

## Toolchain

Use Visual Studio 2022 with the Windows Driver Kit (WDK) and ARM64 driver build
support. The driver is built for Windows 11 ARM64.

## Visual Studio

Open `rpi5-cyw43455.sln`, select `Release | ARM64`, and build the solution.

## Command line

From a Visual Studio/WDK developer environment:

```powershell
msbuild .\rpi5-cyw43455.sln /m /t:Rebuild /p:Configuration=Release /p:Platform=ARM64
```

The release CI also builds both TX scheduling modes and verifies optimized
`/O2 /Ot` compilation, host/ASAN regression tests, test-signing, and packaging.

The driver links against `ndis.lib` and directly owns the Raspberry Pi 5
`ACPI\RPI0011` SDIO2 controller. It does not use `sdbus.lib`, WDF, or
WiFiCx.

## Signing and release policy

Development packages are test-signed and require Windows Test Signing on the
test Raspberry Pi. The installer does not enable Test Signing or modify BCD/UEFI.

The current hardware-tested source is frozen on `release-v0.7.1.20` at
`c16aa318da490350126739add45223a186ab0a47`. New driver changes should be made
on an experimental branch and promoted to `main` only after full CI and
Raspberry Pi hardware validation.
