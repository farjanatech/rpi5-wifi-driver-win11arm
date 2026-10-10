# Raspberry Pi 5 Wi-Fi - Damian Edition

**Link-state correction candidate: v0.7.1.24-damian.4** for Windows 11 ARM64 and the onboard CYW43455.

This branch corrects false disconnections caused by WPA key-handshake progress
events with [Farjanatech Damian Edition UEFI](https://github.com/farjanatech/rpi5-uefi/tree/damian-edition).
The defect is reproduced in host tests. The reported .23 disconnects and
fluctuating speeds are not established as fixed on hardware.
See [the correction, evidence and limits](docs/LINK-STABILITY-UPDATE.md).
The target is **ACPI\RPI1060**, the firmware's exclusive direct-SDIO Wi-Fi node.
**RPI0011 remains Damian's RP1 interrupt provider and is never matched by this INF.**

[Download Damian Edition setup](https://github.com/farjanatech/rpi5-wifi-driver-win11arm/releases/download/v0.7.1.24-damian.4/RPi5-WiFi-Setup.exe)
| [Installation and Pi test guide](docs/DAMIAN-EDITION.md)
| [Release assets](https://github.com/farjanatech/rpi5-wifi-driver-win11arm/releases/tag/v0.7.1.24-damian.4)

## What changes

- Dedicated RPI1060-only INF with a rebuilt, test-signed catalog.
- Native ARM64 Wi-Fi GUI and standalone setup identify Damian Edition.
- Setup requires a present Wi-Fi node and refuses an old Wi-Fi binding on
  Damian's IRQ provider. The GUI explains missing firmware mode, missing driver,
  and Windows PnP startup errors.
- Installer, readiness, auto-connect and diagnostics follow RPI1060; diagnostics
  record Damian IRQ and board devices separately.
- Existing authorization survives normal key-handshake progress; real failure
  and link-down events still disconnect. Initial handshakes remain gated.
- CI executes the real event handler under fault injection and ASAN, checks the
  change boundary, and verifies the existing Damian ACPI contract.

## What is carried forward

This update moves runtime registry diagnostics off the packet worker and
refills all available active TX slots from the existing FIFO backlog. Queue
limits, SDIO transfers, firmware, and radio policy are preserved.
See [change details and rollback](docs/STABILITY-UPDATE.md).

The repository owner reported that **0.7.1.22-damian.2** works perfectly on
their Raspberry Pi 5 C1 with Windows 11 ARM64 on 2026-10-10 and requested its
promotion to `main`. Both ARM64 configurations, driver regressions,
AddressSanitizer checks, and GUI/setup self-tests passed before release.
This is a user-reported hardware result; the original slowdown's root cause
and long-duration stability have not been independently established. A later
disconnect and failed recovery motivated this separate candidate.
The later .23 candidate has user-reported speed fluctuations and recurring
disconnects. This .24 correction is kept separate from `main` pending hardware
experience. Earlier .21/.22 GitHub release assets were removed by the owner.

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

The stability branch `damian-edition/stability-diagnostics`, its tested commit
`5786f6a`, and previous Git history remain available. The new candidate is on
`damian-edition/link-stability`; it does not move the default branch.
The previous main implementation is preserved at commit `1e5b666`.
Install one Wi-Fi edition per Windows installation; read
the [migration notes](docs/DAMIAN-EDITION.md) before switching from the old package.
