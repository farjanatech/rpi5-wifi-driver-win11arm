# exp0.6 integrated candidate: physical test checklist

This candidate has not yet demonstrated working Wi-Fi on hardware. Previous
physical evidence covers SDIO identification and core discovery only. A passing
GitHub build, loaded driver or firmware response is not proof of networking.

## Before installation

- Use the Raspberry Pi 5, not the development PC. Keep the working UEFI at
  source `bda4c47` and confirm the active cooler still spins.
- Keep wired Ethernet and local display/keyboard available for recovery.
- Back up important data and retain the previous driver package. This remains
  a test-signed kernel driver; the existing installer verifies the Pi, UEFI,
  Test Signing, package hashes, certificate time and SYS/CAT signer.
- Use WPA2-Personal/AES with PMF optional, not WPA3-only or enterprise.
- Do not change Secure Boot, BCD, storage drivers, partitions or Windows images.

## Install and connect

1. Extract the entire ZIP. Run `Install-RPi5-WiFi-Driver.cmd` on the Pi.
2. Restart Windows. The new installer recognizes `CM_PROB_NONE` as healthy
   instead of trying the unsupported device-enable command.
3. Run `Connect-RPi5-WiFi.cmd`, approve elevation, and wait for firmware startup.
   Supply your actual two-letter country, exact SSID and WPA2 password.
   Credentials are not saved: rerun the utility after a restart/resume.
4. Confirm `AuthenticatedLink=True`. Separately check the adapter has a DHCP
   IPv4 address and default gateway; 169.254.x.x is not DHCP success.
5. With a local console, briefly unplug wired Ethernet and verify gateway
   reachability, DNS, a web page, and several minutes of upload/download.
   Merely browsing while wired Ethernet remains active does not prove Wi-Fi.
6. Reconnect Ethernet if anything fails. Run `Run-RPi5-WiFi-Diagnostics.cmd`.
   Share the ZIP and what happened; never send your password or PMK.

## Diagnostic phases

| Phase | Meaning |
|---|---|
| 400 | Reading packaged firmware and board configuration |
| 410 | Halting CR4, holding D11, measuring RAM banks |
| 420 | Firmware upload and readback verification |
| 430 | Starting firmware and requesting high-throughput chip clock |
| 440 | Function 2 transport enabled |
| 500 | Configured but not authenticated |
| 510 | Applying country and WPA2 settings |
| 520 | Joining / waiting for firmware authentication |
| 600 | Firmware reports association and WPA2 handshake completion |

Phase 600 is not a claim of DHCP, DNS or successful packet traffic.
Diagnostics contain numeric firmware command/error and link event/reason, not
credentials. Captured setup/Windows logs can still include device identifiers;
review the ZIP before posting it publicly.

## Limitations and recovery

Conservative 1-bit 400kHz polled PIO; no speed claim, scan list, saved profile,
WPA3, enterprise authentication or promiscuous capture. The locally administered
MAC changes when the adapter initializes. Power handling is implemented but
untested physically; avoid sleep/hibernate during the first test.

For failures, disable only the CYW43455 adapter or use Device Manager's Roll
Back Driver when available. PnPUtil does not force-downgrade an installed newer
driver simply by adding the older INF. Do not delete unrelated drivers or
reflash Windows/UEFI as part of troubleshooting this candidate.
