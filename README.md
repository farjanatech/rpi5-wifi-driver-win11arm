# Raspberry Pi 5 CYW43455 Wi-Fi driver for Windows 11 ARM64

Experimental Windows 11 ARM64 driver work for the Raspberry Pi 5 onboard Infineon/Cypress CYW43455 Wi-Fi controller.

## Current milestone

**Milestone 1: SDIO + firmware bring-up**

The initial target is intentionally below the Windows Wi-Fi stack:

1. Enumerate the CYW43455 SDIO functions on Raspberry Pi 5.
2. Open the Windows SD bus interface.
3. Prove CMD52/CMD53 transfers.
4. Read the CYW43455 chip/core identity.
5. Download firmware/NVRAM/CLM data.
6. Receive a valid firmware control response.

WiFiCx/NetAdapterCx integration comes after the SDIO transport and firmware path are proven on real hardware.

## Architecture

```text
Windows 11 ARM64
    |
    +-- WiFiCx / NetAdapterCx          (later milestone)
    |
    +-- CYW43455 firmware/control layer
    |       +-- BCDC
    |       +-- SDPCM
    |       +-- chip/backplane
    |
    +-- SDIO transport
            +-- Windows SDBUS
            +-- Raspberry Pi 5 SD host
            +-- CYW43455
```

## Repository layout

- `src/driver/` - Windows kernel driver entry/PnP scaffolding.
- `src/sdio/` - Windows SD bus transport.
- `src/cyw43455/` - CYW43455-specific chip/firmware protocol code.
- `package/` - driver INF/package files.
- `docs/` - architecture, build and bring-up notes.

## Branch strategy

The first development branch is `bringup/cyw43455-sdio-arm64`.

## Status

This repository is experimental. The driver is not yet suitable for normal use and should only be installed on a test Windows ARM64 system with kernel debugging/recovery access available.

## License

GPL-3.0. See `LICENSE`.
