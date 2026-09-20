# exp0.6.4 country compatibility candidate: physical test checklist

exp0.6.3 physically completed all 609,309 upload/readback bytes and failed at
country-set for BD with BADARG (-2). exp0.6.4 changes only country connection
handling, diagnostics and country-prompt convenience; it is not proven working.

Connection order: radio DOWN; check clmload_status; read existing country;
reuse an exact full match, otherwise request ISO/revision 0. Only BADARG on
that explicit request permits one four-byte ISO-only country IOVAR request.
That asks the firmware for its default revision for the same ISO country,
not another country. Require a 12-byte readback with matching abbreviation
and locale and a nonnegative revision before proceeding to authentication.
Short/mismatched readback, transport errors and rejected fallback stop setup.
This intentionally does not accept undocumented alias mappings or guesses.

ConnectStep additions: 14 country-initial-read, 15 regulatory-data-status,
16 country-auto-revision. Existing steps 1-13 and live status ABI are preserved.
Registry telemetry: CountryBefore (packed ISO bytes), CountryBeforeRevision,
CountrySetMode (0 none, 1 reused, 2 explicit revision 0, 3 firmware-selected),
CountryExplicitError (first explicit rejection), ClmLoadStatus and ClmQueryStatus.
ClmLoadStatus 0xffffffff means unavailable, not successful; query status -23
firmware unsupported is the only optional case. Nonzero CLM load status fails.
The collected driver-registry file contains these fields after the request.

The connection utility saves only ConfirmedCountry under
HKCU\Software\Farjanatech\RPi5WiFi after a request is submitted. Pressing Enter
confirms that saved country is still your actual location, or type a new one.
It does not save credentials, automatically guess USA, or detect location.

On the Pi: install, restart once, run Connect, confirm BD, and enter the WPA2/AES
SSID/password. Check the displayed verified country, authenticated link, DHCP,
gateway, DNS and real traffic separately. On failure collect diagnostics before
rebooting; no UEFI update or Windows reflash is needed to test this candidate.

## Retained exp0.6.3 startup progress behavior

This is a diagnostic/utility update, not a claimed fix for a stalled transfer.
The proven 64-byte transfer method, clock and country-setting policy are unchanged.
During startup, watch Uploaded/Verified byte counts rather than phase alone.
The utility reports after 120 seconds without observed advancement, or after
30 minutes overall; neither stops, resets or retries a driver transfer. Keep
the Pi running and collect diagnostics before rebooting if that happens.

Live status ABI v3 (96 bytes) adds total/uploaded/verified bytes, RAM address,
length, direction, status and stage, plus cached last command/argument/response/
interrupt fields. Older 32/48-byte clients remain supported. These are best-effort
live fields, not an atomic transaction snapshot. No status query issues SDIO IO.
TransferStage: 0 none, 1 window selection, 2 CMD53, 3 completed, 4 cancelled.
TransferStatus 0x103 means pending; zero means the operation completed successfully.
FirmwareUploadedBytes excludes padding/NVRAM/vector writes; FirmwareBytes counts
only compared, verified readback. Upload 100% is not verification/startup success.
Registry snapshots update every five seconds when the worker can run, plus phase/
exit boundaries. If a call stalls, the live status file is more current than the
saved snapshot. The collector now includes `15-live-driver-status.txt`.

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
