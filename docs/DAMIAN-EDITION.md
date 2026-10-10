# Damian Edition Wi-Fi 0.7.1.22-damian.2

This edition binds the CYW43455 driver to `WFD0 / ACPI\RPI1060` in
[Farjanatech Damian Edition UEFI](https://github.com/farjanatech/rpi5-uefi/tree/damian-edition),
for Raspberry Pi 5 C1 running Windows 11 ARM64.

The owner reported that 0.7.1.22-damian.2 works perfectly on their Pi 5 C1 on
2026-10-10 and requested promotion to `main`. This update changes runtime
diagnostics and FIFO queue refill. The user-reported result does not establish
the original slowdown's root cause. SDIO/FIFO transfers, firmware, clock policy,
WPA2, scanning, queue capacities and bounded transport recovery are preserved.
See [STABILITY-UPDATE.md](STABILITY-UPDATE.md) for the changes and rollback.

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
   **Damian Edition 0.7.1.22**. Set the two-letter country for your physical
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

The working result above is user-reported, not a claim that every item below has
been completed. Fresh CI packages retain `hardware_validation=pending` because
CI cannot validate each newly built binary on a physical Pi.

After installation and reboot, record:

- Device Manager: RPI1060 has the CYW43455 network driver and no problem code;
  RPI0011 still has Damian's IRQ driver. Display, fan, USB, Ethernet and Bluetooth
  continue to work as before.
- Scan sees nearby 2.4 GHz and 5 GHz networks. Connect, DHCP, DNS and normal web
  access succeed. The adapter appears as Ethernet, as in v0.7.1.20.
- Disconnect/reconnect and saved-profile auto-connect after a cold boot succeed.
- Sustained download/upload and an idle interval produce no new disconnects.
  Suspend/resume and D0 silicon are not qualified by this release.

If startup or connection fails, extract the driver ZIP and run
`RPi5-WiFi-AllInOne.cmd`, choosing diagnostics collection. Share the resulting
diagnostic ZIP. It records RPI1060 separately from the Damian RPI0011 IRQ and
RPI1025 board devices. The older measurement options retain their existing
behavior; use normal traffic for qualification.

For recovery, switch UEFI back to **Standard SD Bus (Damian)**. The direct Wi-Fi
device will disappear and its GUI will report that Direct NDIS mode is required.
Do not reinstall the legacy RPI0011 Wi-Fi package while using Damian firmware.
Rolling back to v0.7.1.20 requires the original compatible non-Damian firmware.

## Source and release isolation

The tested 0.7.1.22 source is included in the default `main` branch. Its
`damian-edition/stability-diagnostics` branch and commit `5786f6a` are retained.
The `v0.7.1.21-damian.1` release remains available for rollback. Source promotion
does not replace the published binaries or change their prerelease designation.
CI restricts kernel changes to diagnostic capture/persistence and FIFO refill,
checks the pinned firmware contract, runs native/installer and driver
regressions, builds ARM64 binaries, and test-signs the catalog for the new INF.
