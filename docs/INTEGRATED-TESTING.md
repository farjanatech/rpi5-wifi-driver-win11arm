# exp0.6.2 country-setting candidate: physical test checklist

This candidate has not yet demonstrated working Wi-Fi on hardware. Previous
physical evidence now includes exp0.6.1's full 609,309-byte firmware readback and
phase 500 startup. A passing build, loaded driver or firmware response is not
proof of networking. Its connection request failed at phase 510 / SET_VAR (-2).

exp0.6.2 uses ISO country revision zero (Linux's 4345 fallback) and requires a
complete matching country readback before continuing. Keep your actual country:
`BD` for Bangladesh. Never substitute another country to bypass rejection.
The old log lacks the failed variable name; the revision was a compatibility
defect found in review, not yet the proven cause of that particular rejection.

exp0.6 installed but failed at its first 512-byte F1 RAM write (CMD53 argument
`0x95000000`, R5 `0x1100`, phase 420). exp0.6.1 explicitly configures/verifies
F1's 64-byte block size and caps RAM byte-mode requests at 64 bytes. This is a
targeted compatibility correction that passed the user's upload/readback test.
The exact hardware size limit has not been measured.

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
| 420 | Firmware upload |
| 421 | Full firmware readback verification |
| 422 | NVRAM and reset-vector writes |
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
`RamTransferAddress`, `RamTransferLength` and `RamTransferWrite` identify the
most recent RAM operation (write=1, read=0); `FirmwareBytes` counts verified
firmware bytes, not uploaded bytes. Failed transfers are not blindly retried.

Connection errors now report `ConnectStep`: 1 radio-down, 2 country-set,
3 country-readback, 4 infrastructure, 5 authentication-mode, 6 AES-cipher,
7 WPA2-mode, 8 mfp, 9 sup_wpa, 10 wpaie, 11 PMK, 12 radio-up, 13 join-SSID.
Only step identifiers are logged, never SSID/password/PMK contents.
CountryRequested/Applied pack the first letter in the low byte and the second
in the next byte (`BD` = `0x4442`). CountryRevision is the firmware readback.
The control utility now labels connection errors correctly instead of calling
them firmware-startup failures. Collect diagnostics before restarting on error.

## Limitations and recovery

Conservative 1-bit 400kHz polled PIO; no speed claim, scan list, saved profile,
WPA3, enterprise authentication or promiscuous capture. The locally administered
MAC changes when the adapter initializes. Power handling is implemented but
untested physically; avoid sleep/hibernate during the first test.

For failures, disable only the CYW43455 adapter or use Device Manager's Roll
Back Driver when available. PnPUtil does not force-downgrade an installed newer
driver simply by adding the older INF. Do not delete unrelated drivers or
reflash Windows/UEFI as part of troubleshooting this candidate.
