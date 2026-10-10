# Damian Edition Wi-Fi 0.7.1.24-damian.4

This edition binds the CYW43455 driver to `WFD0 / ACPI\RPI1060` in
[Farjanatech Damian Edition UEFI](https://github.com/farjanatech/rpi5-uefi/tree/damian-edition),
for Raspberry Pi 5 C1 running Windows 11 ARM64.

The default `main` branch contains **0.7.1.24-damian.4**, which the owner has
confirmed is stable on their Raspberry Pi 5 C1 with Windows 11 ARM64.
It fixes a reproduced bug that revoked authorization during WPA key-handshake
progress. This does not establish the cause of the reported .23 speed
fluctuations and repeated disconnects. See
[LINK-STABILITY-UPDATE.md](LINK-STABILITY-UPDATE.md) for evidence and limits.
The earlier async-diagnostics, TX-refill, warm-reset and recovery-status changes
remain included. The hardware stability result is user-reported.

## Required firmware

Use the existing [Damian Edition v0.1.0-rc1 firmware](https://github.com/farjanatech/rpi5-uefi/releases/tag/damian-edition-v0.1.0-rc1)
or the compatible `damian-edition` branch. No new firmware flash is required if
Windows already enumerates `ACPI\RPI1060`.

In UEFI, select:

`Device Manager -> Raspberry Pi Configuration -> ACPI / Device Tree -> Wi-Fi SDIO Mode -> Windows Direct NDIS (Farjanatech)`

Save and reboot. ACPI must be enabled. Standard SD Bus mode hides RPI1060 and
cannot use this package. Direct mode hides SDC1, leaving exactly one owner of
the SDIO2 MMIO region `0x1001100000` (length `0x260`) and GSI 306. Damian still
owns Wi-Fi power/pin setup; the miniport uses the resources assigned by Windows.

`ACPI\RPI0011` is Damian's RP1 IRQ provider. It must keep its Damian driver.
This edition has **no RPI0011 INF match** and does not alter other Damian devices,
including display, fan, GPIO, board power, Bluetooth, mailbox and NVRAM.

## Install

1. Keep the Damian Windows driver set installed. If the old Farjanatech Wi-Fi
   v0.7.1.20 package was installed, remove **only that Wi-Fi driver package**
   through Device Manager/Driver Store before switching editions. Do not remove
   the RPI0011 device or Damian IRQ package. If RPI0011 already shows `rpi5cyw`
   as its service, restore its Damian IRQ driver and reboot first. Setup detects
   this collision and refuses to continue; it does not delete other drivers.
2. Windows Test Signing must already be enabled and Secure Boot disabled for
   the test-signed driver. Setup does not change boot security or firmware.
3. Run `RPi5-WiFi-Setup.exe` as administrator on the Pi. It checks for a present
   RPI1060 device before trusting the bundled test certificate or installing.
4. Restart Windows. Open the Desktop Wi-Fi shortcut. The title should say
   **Damian Edition 0.7.1.24**. Set the two-letter country for your physical
   location, scan, select a WPA2-Personal/AES network and connect.
5. Save a profile and select auto-connect if wanted. Existing native profiles
   remain compatible and protected with DPAPI. This is an upgrade of the same
   Wi-Fi application/service: one edition per Windows installation. The existing
   native application directory, shortcut and startup task are reused.

The release contains the ARM64 setup, standalone ARM64 GUI, an optional driver/
diagnostics ZIP, build provenance and SHA-256 checksums. The standalone GUI alone
does not install the RPI1060 driver. The ZIP's PowerShell installer installs the
driver and diagnostic utility; use the setup EXE for the native GUI and boot task.

## Pi validation

The owner confirmed stability of the published .24 package after the host tests
and release. The published binaries and their original build metadata remain
unchanged. Fresh CI packages retain `hardware_validation=pending` because CI
cannot validate each newly built binary on a physical Pi. The report does not
qualify every item in the optional checklist below.

After installation and reboot, record:

- Device Manager: RPI1060 has the CYW43455 network driver and no problem code;
  RPI0011 still has Damian's IRQ driver. Display, fan, USB, Ethernet and Bluetooth
  continue to work as before.
- Scan sees nearby 2.4 GHz and 5 GHz networks. Connect, DHCP, DNS and normal web
  access succeed. The adapter appears as Ethernet, as in v0.7.1.20.
- Disconnect/reconnect and saved-profile auto-connect after a cold boot succeed.
- Sustained download/upload and an idle interval produce no new disconnects.
  Suspend/resume and D0 silicon are not qualified by this release.

Optional diagnostics remain available in the driver ZIP through
`RPi5-WiFi-AllInOne.cmd`. They record RPI1060 separately from the Damian RPI0011 IRQ and
RPI1025 board devices. The older measurement options retain their existing
behavior; use normal traffic for qualification.

For recovery, switch UEFI back to **Standard SD Bus (Damian)**. The direct Wi-Fi
device will disappear and its GUI will report that Direct NDIS mode is required.
Do not reinstall the legacy RPI0011 Wi-Fi package while using Damian firmware.
Rolling back to v0.7.1.20 requires the original compatible non-Damian firmware.

## Source and release isolation

The tested .24 source, commit `a268bdb`, is included in the default `main` branch.
Its `damian-edition/link-stability` branch and release tag are retained. The
promotion adds documentation of the owner's confirmation without changing code.
The owner removed the earlier .21/.22 release assets; their source history is
retained. Published .24 binaries and the existing prerelease designation are
unchanged. CI restricts the new kernel delta from .23 to link-event handling
and classification, checks the pinned firmware contract, runs native/installer
and driver regressions, builds ARM64 binaries, and test-signs the new catalog.
