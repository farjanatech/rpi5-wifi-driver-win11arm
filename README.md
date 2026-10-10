# Raspberry Pi 5 Wi-Fi - Damian Edition

**Prerelease: v0.7.1.21-damian.1** for Windows 11 ARM64 and the onboard CYW43455.

This branch integrates the existing v0.7.1.20 driver with
[Farjanatech Damian Edition UEFI](https://github.com/farjanatech/rpi5-uefi/tree/damian-edition).
The target is **ACPI\RPI1060**, the firmware's exclusive direct-SDIO Wi-Fi node.
**RPI0011 remains Damian's RP1 interrupt provider and is never matched by this INF.**

[Download Damian Edition setup](https://github.com/farjanatech/rpi5-wifi-driver-win11arm/releases/download/v0.7.1.21-damian.1/RPi5-WiFi-Setup.exe)
| [Installation and Pi test guide](docs/DAMIAN-EDITION.md)
| [Release assets](https://github.com/farjanatech/rpi5-wifi-driver-win11arm/releases/tag/v0.7.1.21-damian.1)

## What changes

- Dedicated RPI1060-only INF with a rebuilt, test-signed catalog.
- Native ARM64 Wi-Fi GUI and standalone setup identify Damian Edition.
- Setup requires a present Wi-Fi node and refuses an old Wi-Fi binding on
  Damian's IRQ provider. The GUI explains missing firmware mode, missing driver,
  and Windows PnP startup errors.
- Installer, readiness, auto-connect and diagnostics follow RPI1060; diagnostics
  record Damian IRQ and board devices separately.
- CI freezes the working kernel source and radio firmware, verifies the existing
  Damian ACPI contract, and builds/tests the new package on its own branch.

## What is carried forward

The kernel is unchanged from v0.7.1.20: direct SDIO, NDIS 6.30 Ethernet-style
adapter, 2.4/5 GHz scanning, WPA2-Personal/AES, saved DPAPI-protected profiles,
reboot auto-connect, and bounded runtime recovery. Control IOCTLs and profile
format remain compatible. Test Signing is required. Windows displays this as
an Ethernet-style adapter; use the bundled GUI to manage Wi-Fi.

The v0.7.1.20 kernel was hardware-tested with the older firmware. **This new
Damian combination still needs Pi hardware validation**; existing throughput
measurements are not a claim about this prerelease.

## Firmware and coexistence

In Damian Edition UEFI, select **Wi-Fi SDIO Mode -> Windows Direct NDIS
(Farjanatech)**, save, and reboot. Existing Damian Edition v0.1.0-rc1 already
provides the required firmware contract. The new package does not flash UEFI.

```text
Damian UEFI WFD0 / ACPI\RPI1060
    -> rpi5cyw.inf -> unchanged rpi5cyw.sys -> CYW43455
    -> native Damian Edition Wi-Fi Manager

Damian RP1B.IRQ0 / ACPI\RPI0011 -> Damian IRQ driver (retained)
```

The original `main`, `release-v0.7.1.20`, and v0.7.1.20 release are retained.
This edition lives on `damian-edition/rpi1060-integration` and is released as a
separate prerelease. Install one Wi-Fi edition per Windows installation; read
the [migration notes](docs/DAMIAN-EDITION.md) before switching from the old package.
