# Raspberry Pi 5 Wi-Fi - Damian Edition

**Prerelease: v0.7.1.22-damian.2** for Windows 11 ARM64 and the onboard CYW43455.

This stability branch builds on the working Damian package from `main` for
[Farjanatech Damian Edition UEFI](https://github.com/farjanatech/rpi5-uefi/tree/damian-edition).
The target is **ACPI\RPI1060**, the firmware's exclusive direct-SDIO Wi-Fi node.
**RPI0011 remains Damian's RP1 interrupt provider and is never matched by this INF.**

[Download Damian Edition setup](https://github.com/farjanatech/rpi5-wifi-driver-win11arm/releases/download/v0.7.1.22-damian.2/RPi5-WiFi-Setup.exe)
| [Installation and Pi test guide](docs/DAMIAN-EDITION.md)
| [Release assets](https://github.com/farjanatech/rpi5-wifi-driver-win11arm/releases/tag/v0.7.1.22-damian.2)

## What changes

- Dedicated RPI1060-only INF with a rebuilt, test-signed catalog.
- Native ARM64 Wi-Fi GUI and standalone setup identify Damian Edition.
- Setup requires a present Wi-Fi node and refuses an old Wi-Fi binding on
  Damian's IRQ provider. The GUI explains missing firmware mode, missing driver,
  and Windows PnP startup errors.
- Installer, readiness, auto-connect and diagnostics follow RPI1060; diagnostics
  record Damian IRQ and board devices separately.
- CI restricts kernel changes to diagnostics and FIFO refill, preserves radio
  firmware and transport policy, and verifies the existing Damian ACPI contract.

## What is carried forward

This candidate moves runtime registry diagnostics off the packet worker and
refills all available active TX slots from the existing FIFO backlog. Queue
limits, SDIO transfers, firmware, and radio policy are preserved. It has not
yet been validated on the Pi; it is not a confirmed fix for the intermittent
slowdown. See [change details and rollback](docs/STABILITY-UPDATE.md).

The repository owner confirmed the earlier **0.7.1.21-damian.1** package works
on their Raspberry Pi 5 C1 on 2026-10-10. That package remains on `main` and is
available for rollback. The current 0.7.1.22 candidate requires separate hardware
validation; earlier throughput measurements do not qualify this update.

## Firmware and coexistence

In Damian Edition UEFI, select **Wi-Fi SDIO Mode -> Windows Direct NDIS
(Farjanatech)**, save, and reboot. Existing Damian Edition v0.1.0-rc1 already
provides the required firmware contract. The new package does not flash UEFI.

```text
Damian UEFI WFD0 / ACPI\RPI1060
    -> rpi5cyw.inf -> rpi5cyw.sys -> CYW43455
    -> native Damian Edition Wi-Fi Manager

Damian RP1B.IRQ0 / ACPI\RPI0011 -> Damian IRQ driver (retained)
```

Damian Edition is now the default on `main`. The integration commit `d19ecb1` and previous Git history remain available.
The previous main implementation is preserved at commit `1e5b666`.
Install one Wi-Fi edition per Windows installation; read
the [migration notes](docs/DAMIAN-EDITION.md) before switching from the old package.
