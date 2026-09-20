# exp0.6.8 latency candidate: physical test checklist

Install from a fresh extracted folder on the Pi, restart once, then run
Connect-RPi5-WiFi.cmd and confirm BD if in Bangladesh. Keep the existing UEFI.
Do not mix packages, change DNS, disable IPv6, or reinstall Windows.

Once authenticated, disconnect wired Ethernet for comparable tests. Run:

```powershell
ping.exe -n 20 192.168.0.1
ping.exe -n 10 1.1.1.1
nslookup.exe example.com 192.168.0.1
nslookup.exe example.com 1.1.1.1
curl.exe -4 -I --connect-timeout 15 --max-time 45 https://example.com
```

The gateway above is the one confirmed in this user's diagnostics; substitute
your actual gateway elsewhere. Save these outputs/screenshots and run
Run-RPi5-WiFi-Diagnostics.cmd after the tests, before restarting. New counters
are cumulative, not timings: Cmd53FastPolls, Cmd53WaitSleeps, Cmd53Timeouts,
TxQueueHighWater, TxQueueFull, RxBatchYields, plus TxErrors/RxErrors/RxNoBuffer.
16-ip-routing-dns.txt records configuration and routes but does not probe the
internet. It contains local network addresses; review before sharing publicly.

Compare latency/loss and website loading with exp0.6.7 under the same conditions.
If it regresses, keep diagnostics and roll back only the Wi-Fi driver using
Device Manager's Roll Back Driver if available. This is not a validated speed fix.
The known 1-bit/400 kHz PIO throughput ceiling remains. No radio/clock increase.

Engineering basis: Microsoft's [KeStallExecutionProcessor guidance](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/wdm/nf-wdm-kestallexecutionprocessor)
recommends minimizing busy waits, typically below 50 us. This candidate caps
the entire F2 transaction's explicit short-poll stalls at 40 us, not an
unbounded spin loop. The 2 ms RX budget uses interrupt-time clock granularity;
the four-frame cap still applies if that clock has not advanced. These are
code-level latency contributors, not proof of the cause of every DNS timeout.

## Historical exp0.6.7 ReactOS firmware pair checklist

Install exp0.6.7 from a fresh extracted folder on the Pi only; restart once.
The pinned ReactOS 43455 pair is firmware 7.45.229 (631467 bytes) and CLM 7163
bytes. This is an older alternate firmware, not Infineon 7.45.286. The Pi
calibration bytes, UEFI, country policy and direct-SDIO implementation are unchanged.
Both binaries are copied byte-for-byte from the requested source commit; the
driver path filenames are aliases only. Do not manually mix release folders.

Run Connect-RPi5-WiFi.cmd. Confirm BD for Bangladesh. Upload/readback should
show 631467 total bytes; if it still shows 609309 after restart, collect logs
rather than interpreting that result as a test of the new pair. Firmware
startup can still take several minutes; no speed improvement is claimed.

Collect diagnostics after the connection attempt, BEFORE another reboot. Check
CountryListMembership and CountryApplied/Revision, then authenticated link,
DHCP/gateway and traffic with wired Ethernet temporarily disconnected. A country
accepted by firmware is not itself board-specific regulatory certification.
Keep wired Ethernet for recovery and exp0.6.6 for rollback. This candidate has
not been physically tested and is not guaranteed to connect.

## Retained exp0.6.6 country diagnostics (historical introduction)

Install exp0.6.6 on the Pi only, restart once, then run Connect-RPi5-WiFi.cmd.
Keep your actual country BD; do not substitute USA or France. Firmware upload
and verification timing is unchanged (previously about 6-7 minutes).

This build changes only the same-country BADARG fallback to a full 12-byte
structure with revision -1 (CountrySetMode=4, step 16). It retains revision 0
as the initial request and full exact-country/nonnegative-revision readback.
The earlier four-byte fallback is removed. Firmware/CLM/NVRAM and UEFI are unchanged.

New saved diagnostics (DiagVersion=7): CountryListStatus, CountryListError,
CountryListCount, CountryListReplyLength, CountryListMembership. Membership is
0 unknown, 1 requested code listed, 2 requested code not listed in a validated
nonempty returned list. A list does not specify revisions, prove RF compliance,
or override country SET/readback. The query is optional and read-only (command
261, band_set=0, bounded 1024-byte buffer); unsupported, empty and malformed
responses remain inconclusive. It runs once per connection request (step 17).

If connection fails, collect diagnostics AFTER the failure without rebooting.
This preserves both the failing SET and the separate country-list result.
Keep exp0.6.5 available for rollback. This build is NOT hardware-validated.

## Retained exp0.6.5 reply decoding

exp0.6.4's post-restart log had ClmQueryStatus=0, FirmwareError=0,
ClmLoadStatus=0xffffffff and step 15 / STATUS_DEVICE_DATA_ERROR. This identifies
the exact-length check before decoding, not a confirmed CLM database failure.
The raw length was not saved and cannot be inferred exactly from that report.

exp0.6.5 separates BCDC header length metadata from the actual payload inside
the validated SDPCM frame. Copies are bounded by received bytes and allocated
capacity. The IOVAR layer requires at least the complete requested value,
copies only that value, then normalizes FirmwareReplyLength to its size.
It never fills in a missing status/country using zeros to claim success.
The transport functions were moved unchanged in structure to internal control.h
so tests exercise the same implementation rather than mocking its length output.

New registry/collector fields: FirmwareReplyDeclaredLength (full raw BCDC length
word), FirmwareReplyPayloadLength (actual bytes after BCDC header),
FirmwareRequestCapacity (command buffer size), FirmwareValueLength (IOVAR value
size), FirmwareReplyLength (normalized value length on successful IOVAR GET;
otherwise actual copied bytes, bounded by capacity). These describe the last
command; later commands overwrite them. Status-only queries do not perform IO.
Saved snapshots after connection failure preserve the failing command fields.

The country checks below still require a complete 4-byte CLM status and 12-byte
country value. Nonzero CLM status, wrong country, negative revision, truncated
reply or rejected firmware command still stop connection before radio-up.
No UEFI change, firmware speed change or networking success is claimed.

## Historical exp0.6.4 policy (four-byte fallback superseded above)

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
