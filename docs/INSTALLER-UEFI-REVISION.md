# alpha.3-US.1: installer-only UEFI revision policy update

Package `driver-perf0.7.0-alpha.3-us.1`; installer `0.7.0.2-uefi1`.
Device Manager driver version remains **0.7.0.2**. This is not a new driver
or performance build. The signed SYS, INF, catalog, certificate, firmware,
CLM, calibration, connection utilities and existing GUI are unchanged.

## What was fixed

The earlier installer stopped unless the BIOS string matched one of two exact
UEFI commits. Both that main check and the BIOS-dependent fallback device
lookup have been removed. UEFI revision is now **informational only**:
custom/future revisions, empty BIOS metadata and a failed BIOS query do not
block installation. A warning is recorded without claiming compatibility.

Any UEFI revision may be attempted, but it must still expose the expected
direct-SDIO hardware interface. A version string cannot prove correct ACPI
resources, pin routing or controller ownership. Allowing installation does not
make an incompatible UEFI work; the driver's existing hardware checks remain.

Still required: Windows ARM64, Raspberry Pi 5 identification, the exact
`ACPI\RPI0011` device, complete package hashes, matching driver/catalog signer,
valid certificate time, and the existing test-signing/security prerequisites.
An unrelated or missing ACPI device is rejected. No force-installing, old-driver
deletion, automatic reboot, UEFI edits, or security-setting changes were added.
Private profiles and opt-in startup behavior are preserved.

## Installation

1. Keep your previous complete working package for rollback. Extract this whole
   ZIP into a new folder on the Pi; do not copy just the installer into an old
   folder or manually edit the checksum file.
2. Run `Install-RPi5-WiFi-Driver.cmd` and approve elevation. The log should show
   installer `0.7.0.2-uefi1` and a nonblocking UEFI notice. Restart only if requested.
3. Use your existing connector EXE. This remains the **US-only firmware
   comparison**: use it only on a Pi physically in the United States, with
   **US** entered and confirmed in the GUI. No country policy was relaxed.
4. If it still stops, share the new `RPI5-WIFI-DRIVER-INSTALL-*.txt` log. If it
   installs but Wi-Fi fails, share the diagnostics. Do not assume a successful
   installation proves the updated UEFI's hardware interface is compatible.

There is no new speed claim or additional hardware validation. Firmware
7.45.265 WPA2 offload/US compatibility and throughput still require a physical
test. Keep using the previous working release if this experimental firmware
fails. See `PERFORMANCE-0.7.0-alpha.3-us.md` for firmware details and rollback;
this document supersedes its exact-UEFI installer requirement only.

## Reproducible packaging

GitHub Actions downloads the immutable alpha.3-US release ZIP, SHA-256
`50659B0F0708F18960768E521A1DAF56274271D14E21584360E9933B379D5498`.
It validates the original manifest, replaces the actual installer entry point,
updates these instructions/provenance and regenerates the package manifest.
Every other original file is hash-compared before publishing. No kernel
rebuild, re-signing, firmware replacement or connector rebuild occurs.

CI exercises missing/custom/erroring BIOS metadata, all three device inventory
paths and lookalike ACPI IDs using mocks. It also compares the main install
and security flow to alpha.3-US, permitting only the UEFI notice replacement.
Nothing is installed or tested on the development PC.
