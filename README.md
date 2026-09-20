# Raspberry Pi 5 CYW43455 Wi-Fi driver for Windows 11 ARM64

Experimental Windows 11 ARM64 driver work for the Raspberry Pi 5 onboard Infineon/Cypress CYW43455 Wi-Fi controller.

## Current milestone

**Milestone 1: direct-SDIO hardware bring-up behind an NDIS Ethernet adapter**

The current branch binds an NDIS 6.30 Ethernet miniport to the dedicated
`ACPI\RPI0011` SDIO2 host exposed by the matching UEFI. The driver maps SDIO2
directly and currently implements only a bounded CMD0/CMD5/CMD3/CMD7/CMD52
probe. Experimental driver 0.4 additionally enables function 1, requests ALP,
selects the ChipCommon backplane window, and performs 16 bounded CMD53 PIO
chip-ID reads at the identification clock. Temporary card settings are restored
on success and failure. It does not implement firmware loading, association,
transmit or receive traffic. It does not enable function 2 or write chip RAM.

The CMD5 identification probe uses three bounded query/voltage-request cycles
at each of 400, 200 and 100 kHz. It rejects empty or malformed R4 responses and
accepts negotiation only after a valid ready response. Every command records
the raw response, interrupt status, command-line reset result, and before/after
host-control, timeout, clock, power and line state for hardware diagnosis.

Windows therefore sees a disconnected Ethernet adapter even when the probe
succeeds. This is intentional diagnostic behavior, not working Wi-Fi.

The user physically validated CMD5/CMD52 with driver 0.3 and UEFI source
`bda4c47` on 2026-09-18. On 2026-09-20, driver 0.4 passed all 16 chip-ID reads
on the Pi: chip 0x4345 revision 6, with successful restoration.
Keep UEFI exp.0.3 installed. Do not reflash Windows for this driver update.

### Driver 0.5: core inventory before firmware work

Driver 0.5 retains those reads and scans the AI EROM with bounded reads and
descriptor validation. It locates ChipCommon, SDIO, D11 and ARM CR4 cores,
reads CR4 wrapper state, and reads CR4 RAM-bank capabilities only if the core
is already clocked and out of reset. It does not halt/wake/reset the chip CPU.
`RamBase=0x198000` is a reference-defined base, not proof of RAM capacity;
`RamBankCount=0` can mean the core was not accessible. RAM size is not measured.
Expected: ProbePhase=350, CoreInventoryComplete=1, Cmd53WriteCount=0 and both
LastStatus/ProbeRestoreStatus zero. New hardware testing is required.

A bounded CMD53 write routine is implemented and host-simulated, but startup
does not call it. RAM bank selection, RAM test writes, firmware upload/startup,
association and network traffic remain disabled/unimplemented pending the
core inventory result. Do not mistake this preflight build for a firmware or
internet-ready release.

The 0.5 installer skips device-enable when the exact adapter reports PnP
problem code zero. It does not treat exit 50 as success without checking the
device, and never force-enables a device with an unrelated/unknown problem.

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
