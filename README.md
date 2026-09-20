# Raspberry Pi 5 CYW43455 Wi-Fi driver for Windows 11 ARM64

Experimental Windows 11 ARM64 driver work for the Raspberry Pi 5 onboard Infineon/Cypress CYW43455 Wi-Fi controller.

## Current milestone

### Integrated exp0.6 candidate — not yet hardware validated

Driver 0.5 has now passed core discovery on the user's Pi 5: chip 0x4345 rev 6,
11 cores, 8 CR4 banks and successful restoration. The current source builds on
that result with firmware upload/readback, measured CR4 RAM size, F2 FIFO,
SDPCM/BCDC control, WPA2-Personal AES firmware supplicant and Ethernet TX/RX.

**This is an experimental connection candidate, not a claim of working Wi-Fi.**
Host simulations and a successful ARM64 build cannot validate a physical radio.
Keep UEFI source `bda4c47`, the working fan and wired Ethernet unchanged.

After installing the test package **on the Pi only**, restart and run
`Connect-RPi5-WiFi.cmd`. Supply the actual country, SSID and WPA2 password.
The utility sends a derived PMK over an administrator-only control device;
it does not save credentials. Windows sees Ethernet, not the native Wi-Fi list.
Verify authenticated link, DHCP address, gateway reachability, DNS and real
traffic separately. Use the included diagnostics if any stage fails.

Limitations: WPA2/AES only (no WPA3 or enterprise), no scanning UI or saved
reconnect profile; locally administered MAC changes on initialization;
conservative 1-bit 400kHz polling, not a performance release. Power transitions
are handled but need hardware validation; avoid sleep/hibernate in the first test.
Recovery: Device Manager -> exact CYW43455 adapter -> Roll Back Driver, or
reinstall the previous exp0.5 package. Do not remove unrelated network drivers.

Sources and firmware hashes/licenses are recorded in `THIRD_PARTY_NOTICES.md`
and `scripts/fetch-firmware.ps1`. No UEFI, BCD, disk or Windows-image changes.

### Historical probe milestones (0.1–0.5)

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
    +-- CYW43455 firmware/control layer (integrated experimental candidate)
    |       +-- BCDC / SDPCM / chip backplane
    |
    +-- direct SDIO2 host transport
            +-- SDHCI CMD5/CMD52/CMD53
            +-- CYW43455
```

## Repository layout

- `src/driver/` - Windows kernel driver entry/PnP scaffolding.
- `src/sdio/` - direct SDHCI PIO transport (no Microsoft SD bus dependency).
- `src/cyw43455/` - CYW43455-specific chip/firmware protocol code.
- `package/` - driver INF/package files.
- `docs/` - architecture, build and bring-up notes.

## Branch strategy

The first development branch is `bringup/cyw43455-sdio-arm64`.

## Status

This repository is experimental. The integrated candidate remains unvalidated
on hardware; earlier releases are probes. Install only on the matching Pi 5
test system with recovery access available. Never install on the development PC.

## License

GPL-3.0. See `LICENSE`.
