# Raspberry Pi 5 CYW43455 Wi-Fi driver for Windows 11 ARM64

Experimental Windows 11 ARM64 driver work for the Raspberry Pi 5 onboard Infineon/Cypress CYW43455 Wi-Fi controller.

## Current milestone

**Milestone 1: direct-SDIO hardware bring-up behind an NDIS Ethernet adapter**

The current branch binds an NDIS 6.30 Ethernet miniport to the dedicated
`ACPI\RPI0011` SDIO2 host exposed by the matching UEFI. The driver maps SDIO2
directly and currently implements only a bounded CMD0/CMD5/CMD3/CMD7/CMD52
probe. It does not yet implement CMD53 data transfer, CYW43455 firmware loading,
association, transmit or receive traffic.

The CMD5 identification probe uses three bounded attempts at each of 400, 200
and 100 kHz. Every attempt records the raw response, interrupt status,
command-line reset result, and before/after clock, power and line state for
hardware diagnosis.

Windows therefore sees a disconnected Ethernet adapter even when the probe
succeeds. This is intentional diagnostic behavior, not working Wi-Fi.

## Architecture

```text
Windows 11 ARM64
    |
    +-- NDIS 6.30 Ethernet miniport
    |
    +-- CYW43455 firmware/control layer (not implemented yet)
    |       +-- BCDC / SDPCM / chip backplane
    |
    +-- direct SDIO2 host transport
            +-- SDHCI CMD5/CMD52/CMD53
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

This repository is experimental. The current artifact is a hardware probe, not
a functional network driver. Install it only on the matching Raspberry Pi 5
test system with kernel debugging and recovery access available.

## License

GPL-3.0. See `LICENSE`.
