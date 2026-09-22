# Raspberry Pi 5 CYW43455 Wi-Fi driver for Windows 11 ARM64

Experimental Windows 11 ARM64 driver work for the Raspberry Pi 5 onboard Infineon/Cypress CYW43455 Wi-Fi controller.

## Current milestone

### Integrated exp0.6.20 — automatic band preference, restored .18 transfer path

No router rename is needed. Before each connection, the firmware receives the
Linux brcmfmac default join policy: rank by signal strength with an 8 dB bonus
for 5 GHz. Both bands remain eligible under your existing SSID. This is a
ranking preference, not increased transmit power, a band lock or a speed test.
The utility distinguishes accepted policy from unsupported/default selection;
the performance report shows the actual connected band.

The .19 run used 2.4 GHz and fell to 4.02 Mbps with four incomplete downloads;
it had no queue rejections or SDIO timeouts, so the cause is not isolated.
.20 restores .18's exact SDIO code, queue and worker scheduling, removing the
unproven CMD52 fast-poll change. .18 scored 17.09 Mbps on 5 GHz; .16 scored
18.55 Mbps with the same packet scheduler and no recorded band. Neither is a
guarantee for a future run. See [exp0.6.20 instructions](docs/EXP0.6.20.md).
Keep .18 as the tested fallback. Firmware, UEFI/fan and BD country remain intact.

### Previous exp0.6.19 — poor 2.4 GHz hardware run; not the preferred candidate

.18 recovered to 17.09 Mbps with 128/128 completed downloads, 63 ms maximum
recorded queue delay, but 774 queue-full rejections and one NoResources ping.
It used 5 GHz versus .17's 2.4 GHz, so the comparison does not isolate the cap.

This candidate replaces runtime CMD52's coarse 100-us polling with at most
five 10-us waits followed by a yielding fallback and one-second deadline.
Only the verified operating bus uses it; startup remains unchanged. Four new
counters expose actual usage, fallback sleeps and timeouts. The 64-frame cap,
packet scheduler, CMD53 transfers, firmware, country, UEFI and tools stay intact.
See [exp0.6.19 instructions](docs/EXP0.6.19.md). Keep .18 as the tested fallback.
This is a targeted timing candidate, not a promise of higher speed or zero drops.

### Previous exp0.6.18 — tested recovery, retained fallback

The .17 test had zero queue-full rejects but dropped to 9.00 Mbps, with two
download timeouts, 5/77 missed router probes and a 1,875 ms maximum queue delay.
This release restores .16's **64-frame cap**. Scheduling/ownership and .17's
radio/connection/performance tools remain unchanged, enforced by CI checks.

The .17 radio snapshot confirmed 2.4 GHz/channel 6, -41 dBm, PM and MPC off.
That does not prove the cause of the slowdown. Test .18 on the unchanged router
configuration first; compare a separately identified 5 GHz connection afterward.
No forced band, new queue mechanism or claimed guaranteed speedup. See
[exp0.6.18 instructions](docs/EXP0.6.18.md). Keep the tested .16 for rollback.

### Previous exp0.6.17 — hardware run regressed; not the preferred candidate

The .16 Pi run completed 128/128 downloads at 18.55 Mbps, with 55/55 successful
router probes but 813 queue-full rejections. .17 tests a bounded 128-frame cap
while retaining .16 scheduling and ownership. Larger queues can increase
latency: this is an unvalidated candidate, not a guaranteed speed improvement.

Read-only, explicit-request radio reporting adds band/channel/RSSI/PM/MPC.
The performance tool queries once before load, never periodically during it.
Unsupported results remain unknown. Country, firmware, bus mode and UEFI stay
unchanged. See [exp0.6.17 instructions](docs/EXP0.6.17.md). Keep .16 for rollback.

### Previous exp0.6.16 — focused completion-accounting candidate

exp0.6.15's hardware test regressed (three completed downloads, then three
timeouts). This candidate restores exp0.6.14's worker source **exactly** and
its 64-frame admission limit. The only functional driver change versus .14 is
completion accounting: free an admission slot before the NDIS callback, while
tracking a separate callback-in-progress reference for pause safety. Retained
diagnostics/version changes are checked separately. A GitHub scope test rejects
any other driver-source changes versus the pinned .14 commit.

This is not a promise of restored speed or zero queue errors. Keep .14 for
rollback, install .16 on the Pi, reboot once and run the included sustained test.
See [exp0.6.16 instructions](docs/EXP0.6.16.md). Firmware, UEFI/fan, 25 MHz bus
settings, country and private configuration handling are unchanged.

### Previous exp0.6.15 candidate — hardware test regressed; do not prefer it

The exp0.6.14 sustained test completed 128 MiB at 14.7 Mbps over 73 seconds,
but recorded queue-full rejections and four `NoResources` router probes. This
candidate releases a completion's admission slot before calling back into NDIS,
while retaining a separate completion reference for pause safety. A bounded
256-frame host queue replaces the 64-frame cap; payload admission, pending
ownership, cancellation and expiry remain bounded. After each received frame,
the worker offers two pending sends using the updated chip credit window.
The chip credit limit, receive fairness, firmware, UEFI and 25 MHz clock are
unchanged. This is not a promise of zero drops or higher speed; a larger host
queue can also increase latency. See [exp0.6.15 testing](docs/EXP0.6.15.md).
Keep exp0.6.14 for rollback. Install, restart once, then run the included
`Test-RPi5-WiFi-Performance.cmd` for the same bounded workload.

### Performance utility 0.6.14.1 — keep driver exp0.6.14 installed

The latest Pi capture measured a 16.8 Mbps **short** download and working Windows
traffic counters. The large test received HTTP 403, so sustained performance
remains unverified. A standalone utility now runs bounded repeated 1 MiB requests
with independent router-ping/traffic sampling, clear server-error handling and
timestamped records. No driver, firmware or UEFI change, and no reinstall/reboot.
Read [instructions and data limits](docs/PERFORMANCE-0.6.14.1.md).

### Integrated exp0.6.14 candidate — fewer bus commands and Windows traffic counters

exp0.6.13 measured 3.17 Mbps, all 16 ping replies, zero queue-full rejections and
an 18-ms maximum queue wait. Sustained browser/video traffic still needs work.
This version avoids reselecting the same runtime backplane window: after three
verified register writes, repeated accesses skip six redundant CMD52 operations.
Failures, direct window writes, host resets and restart invalidate the cache.
Only the single bus worker uses it, and slow upload behavior stays unchanged.

Implements locked 64-bit NDIS traffic statistics for Windows graphs; no fake
link rate. The test now includes a 16 MiB/60-second capped download with router
pings during load. All probes total up to 17 MiB; logs remain local.
No UEFI/firmware/country/bus-mode/queue-size change. Preserve exp0.6.13 for
rollback. A source-level reduction in bus operations is not a measured speedup
until tested on the Pi. See [testing](docs/INTEGRATED-TESTING.md).

### Integrated exp0.6.13 candidate — phase-aware operating-bus polling

The exp0.6.12 Pi test established browsing and HTTPS, with zero observed bad
checksums and matching MAC readback, but remaining ping loss, full TX queues and
intermittent DNS failure. This update targets a source-level latency hazard:
one 40-us allowance was shared across command, buffer-ready and completion.
Each phase now gets up to 50 us of short polling on the verified operating bus,
then yields; the transaction can request at most 150 us of stalls total.
Tests independently delay the three events, rather than reporting all at once.
Timeout, cancellation, firmware upload, slow recovery, 4-bit/25 MHz mode,
pending-send ownership and the 64-frame limit are unchanged. No UEFI changes.

Runtime counters identify remaining sleeps by phase/function and elapsed sleep
time. The performance tool tests the download hostname's DNS, reports HTTPS
timings and requests a snapshot after the tests finish. No DNS settings change.
Retains editable private configuration/startup support; never edit the hashed
public example. See [auto-connect](docs/AUTO-CONNECT.md).

This candidate is not yet hardware-validated. Keep exp0.6.12 for rollback.
Install, reboot once and run **Test-RPi5-WiFi-Performance.cmd** on the Pi.

### Integrated exp0.6.10 candidate — verified 4-bit/25 MHz operating mode

The exp0.6.9 hardware capture still filled all 64 pending slots with 920
queue-full rejections. The bus also remained at 1-bit/400 kHz. This release
targets that confirmed throughput limit, not a larger queue or DNS workaround.
After the unchanged firmware upload/readback, the driver selects default-speed
4-bit mode and targets 25 MHz (no 50 MHz, DMA, block-mode or IRQ changes).
Sixteen matching read-only chip-ID transfers are required before configuration.
Every startup explicitly synchronizes card and host to slow mode before data
transfers. Failed upgrades recover to matching slow mode where possible, then
stop connection startup; an unverified fast bus is never silently used.

Install on the Pi, reboot once, then run **Test-RPi5-WiFi-Performance.cmd**.
It connects and collects one ZIP: bus state, routing, gateway/internet ping,
DNS, HTTPS, bounded 1 MiB download and the complete diagnostics. Disconnect
wired Ethernet/VPNs for an unambiguous Wi-Fi test. No logs are uploaded.
Hardware throughput and signal timing still require validation on the Pi.
See [testing and rollback](docs/INTEGRATED-TESTING.md).

### Integrated exp0.6.9 candidate — pending sends and bounded backpressure

The user's exp0.6.8 logs record 1274 transmit errors and exactly 1274 full-queue
rejections, with the 64-frame queue reaching capacity and no CMD53 timeouts.
This version adapts the ReactOS pending-send design: NBL ownership returns only
after all its frames transfer, not immediately after admission to the queue.
Previously accepted copied sends were legal NDIS behavior, but offered poor
backpressure for this slow software transport; this is not a claim that early
completion inherently violates NDIS.

The same bounded 64-frame capacity remains; no unbounded memory queue. Firmware
busy/credit exhaustion retains unsent work. A reused nonpaged staging buffer
removes packet-time allocation. Four-frame transmit bursts run before/after
receive polling, with cancellation, pause, stop/power-down and a 30-second
request expiry. Cancellation can abort unsent frames, not retract a frame
already handed to the chip. Completion means chip transfer, not an over-air ACK.

Exact queue/dispatch code is shared with host lifecycle tests. Diagnostics retain
partial output and put optional Windows statistics in a separate file. The
firmware, country, UEFI/fan, bus speed and transfer format are unchanged. Hardware
latency/throughput remains unvalidated; faster SDIO is deliberately a separate
change. See docs/INTEGRATED-TESTING.md for physical checks and rollback.

### Integrated exp0.6.8 candidate — bounded polling and packet fairness

Physical exp0.6.7 results on one Pi show BD accepted, WPA2 authentication,
DHCP, public-IP ping, DNS replies and an HTTPS HEAD response. They also show
high latency and intermittent DNS timeouts; reliable browsing is not established.

This candidate gives F2 CMD53 completions a maximum 40 microseconds of short
polling per transaction before yielding, and bounds each phase by an elapsed
250 ms deadline. The receive worker services send work after at most four
frames or 2 ms (a frame already in progress completes first), instead of 32
frames, and does not take an idle sleep immediately after received traffic.
Counters expose scheduler sleeps, timeouts, queue pressure and RX batch yields.
The collector now saves routes, actual gateway addresses, interface metrics,
DNS addresses and neighbor state, without changing settings or external probes.

Firmware/CLM/NVRAM, country checks, UEFI/fan, 1-bit 400 kHz bus and PIO frame
format remain unchanged. No speed guarantee: this build needs physical A/B
testing against exp0.6.7. Keep that package for rollback. Host simulation is
not a hardware benchmark. See docs/INTEGRATED-TESTING.md.

### Integrated exp0.6.7 candidate — user-requested ReactOS firmware pair

Package the CYW43455 .bin and .clm_blob from ahmedarif193/reactos revision
929bdd689d1e18d0ef71214741d4e16eec74409c, drivers/network/dd/cyw43455/fw.
Firmware version 7.45.229 is OLDER than the previous 7.45.265, not the separately
discussed Infineon 7.45.286. Firmware size is 631467 bytes and CLM size 7163 bytes.
The accompanying Pi board file is byte-identical to the previous calibration.
Pinned hashes, the Broadcom licence and upstream WHENCE are included. Two
filenames are aliased to the existing driver paths; no binary bytes are edited.

Physical exp0.6.6 reported 116 country entries without BD and rejected both BD
requests. This release tests a different firmware/CLM combination; it does not
force US/IN or weaken the country SET/readback checks. Chip-host code, timing,
UEFI/fan and Windows installation remain unchanged. Country acceptance alone
does not certify RF limits on the Pi board or prove authentication/IP/traffic.
This candidate is not hardware-validated; keep exp0.6.6 available for rollback.
CI checks the real staged firmware bytes/licences and simulates RAM transfers
for both old/new firmware lengths; it cannot execute the chip firmware.

### Integrated exp0.6.6 candidate — full country request and supported-country evidence

Physical exp0.6.5 passed upload/readback and CLM status checks but firmware
rejected BD revision 0 and the four-byte fallback with BADARG (-2). This version
replaces that fallback with the complete 12-byte country structure, revision -1,
as used by the referenced ReactOS CywSetCountry routine. Both abbreviation and
locale remain BD (or the actual user-confirmed country). Matching full readback
with a nonnegative revision is still mandatory before radio-up. Mode 4 identifies
this new path; mode 3 identifies the older four-byte request.

A bounded read-only WLC_GET_COUNTRY_LIST query records whether the requested
country is listed, the count, reply length, query status and firmware error.
Unknown/unsupported/malformed/empty results never mean the country is absent.
Listing is diagnostic evidence only, not permission to bypass SET/readback.
No automatic USA/France substitution, firmware/CLM/NVRAM or UEFI/fan change.

An early exp0.6.1 already used revision -1 but lacked precise failure-step logs;
there is no evidence that this format alone solves the Pi failure. This is a
targeted compatibility/diagnostic candidate, not a proven working Wi-Fi release.
Host tests exercise real connection/control code; physical testing is still needed.

### Integrated exp0.6.5 candidate — firmware reply-length decoding

exp0.6.4 stopped at the new CLM status check after the query itself succeeded.
The check confused transport-buffer length with the requested four-byte value.
exp0.6.5 bounds decoding by actual SDPCM reply bytes and caller capacity, then
copies only a complete requested IOVAR value. Extra buffer space is accepted;
short values, malformed framing and firmware errors are still rejected.
Country readback benefits from the same fix without relaxing country policy.

The actual control/IOVAR code now shares a host harness with the actual
connection sequence. Regression cases cover full-buffer/padded replies, echoed
buffer metadata, truncation, mismatched replies, allocation failures and a real
nonzero CLM status. Diagnostics retain separate raw and decoded length fields.
The precise raw length in the exp0.6.4 Pi report was not recorded, so these are
protocol regression cases, not a captured-packet replay or hardware validation.
UEFI, firmware upload, fan and country-selection rules are unchanged.

### Integrated exp0.6.4 candidate — country compatibility

Physical exp0.6.3 diagnostics confirmed full firmware upload/readback, then
failure specifically at country-set (BD, firmware BADARG -2). This candidate
reads the existing country before changing it, reuses a complete exact match,
and tries the same-country four-byte legacy request once only when explicit
revision zero is rejected with BADARG. The firmware chooses its default revision;
a full matching country/revision readback is still required before radio-up.
It never substitutes USA, another country, or brute-forces regulatory revisions.

The loaded CLM status is checked before country setup. A reported CLM error or
malformed reply prevents connection; only a specifically unsupported status
query is optional. New saved diagnostics record the initial country/revision,
selection path, explicit rejection and CLM query/result. The utility remembers
only the user-confirmed country in HKCU and offers it for confirmation next time;
it never saves SSIDs, passwords or PMKs and does not infer physical location.

UEFI/fan, chip firmware, NVRAM, SDIO clock and upload/readback implementation are
unchanged. This is a targeted compatibility candidate, not a hardware-validated
working Wi-Fi release. Keep exp0.6.3 available for rollback.

### Integrated exp0.6.3 candidate — live firmware progress

exp0.6.2's repeated phase-420 utility timeouts did not show whether the firmware
upload was advancing. exp0.6.3 adds successful upload and verified-readback byte
counters, total bytes, last RAM operation/status, and cached SDIO command fields
to read-only live status. Saved progress is refreshed at most every five seconds
during transfers, plus phase and exit boundaries. The diagnostic ZIP includes
`15-live-driver-status.txt`. The transfer method and country policy are unchanged.

The utility reports bytes/percent and elapsed/no-progress time every five
seconds. It keeps observing beyond three minutes while counters advance, with
a 30-minute maximum. After 120 seconds without observed byte/phase advancement,
it asks for diagnostics; this is not proof of a hardware hang and does not stop
the driver. Do not reboot repeatedly while investigating slow firmware startup.

### exp0.6.2 country configuration and precise failures

The user's exp0.6.1 Pi report confirms all 609,309 firmware bytes passed readback
and startup reached phase 500. Connection setup then failed at phase 510 with
SET_VAR / BADARG (-2); the old log cannot identify which variable was rejected.
exp0.6.2 changes the ISO country revision from -1 to 0, following Linux's 4345
fallback, verifies the country returned by the firmware before enabling the
radio, and records all 13 connection steps. It does not substitute countries or
ignore a firmware rejection. This addresses a supported compatibility issue;
the exact cause of the previous BADARG remains unproven until physical retesting.

Physical exp0.6 diagnostics exposed rejection of the first 512-byte F1 RAM
write (`0x95000000`, R5 `0x1100`, phase 420). exp0.6.1 configures/verifies F1
at 64 bytes and uses RAM requests no larger than 64 bytes. New regression
tests replay that response and test full-size firmware across window boundaries.
The upload correction is now physically confirmed; Wi-Fi is not yet validated.

Driver 0.5 has now passed core discovery on the user's Pi 5: chip 0x4345 rev 6,
11 cores, 8 CR4 banks and successful restoration. The current source builds on
that result with firmware upload/readback, measured CR4 RAM size, F2 FIFO,
SDPCM/BCDC control, WPA2-Personal AES firmware supplicant and Ethernet TX/RX.

**This is an experimental connection candidate, not a claim of working Wi-Fi.**
Host simulations and a successful ARM64 build cannot validate a physical radio.
Keep UEFI source `bda4c47`, the working fan and wired Ethernet unchanged.

After installing the test package **on the Pi only**, restart and run
`Connect-RPi5-WiFi.cmd`. Supply the actual country, SSID and WPA2 password.
The utility sends a derived PMK over an administrator-only control device;
it does not save credentials. Windows sees Ethernet, not the native Wi-Fi list.
Verify authenticated link, DHCP address, gateway reachability, DNS and real
traffic separately. Use the included diagnostics if any stage fails.

Limitations: WPA2/AES only (no WPA3 or enterprise), no scanning UI or saved
reconnect profile; locally administered MAC changes on initialization;
conservative 1-bit 400kHz polling, not a performance release. Power transitions
are handled but need hardware validation; avoid sleep/hibernate in the first test.
Recovery: Device Manager -> exact CYW43455 adapter -> Roll Back Driver if
available. Otherwise disable only that adapter and collect diagnostics over
wired Ethernet. PnPUtil does not force-downgrade a higher-ranked installed driver.
Do not remove unrelated network drivers.

Sources and firmware hashes/licenses are recorded in `THIRD_PARTY_NOTICES.md`
and `scripts/fetch-firmware.ps1`. No UEFI, BCD, disk or Windows-image changes.

### Historical probe milestones (0.1–0.5)

**Milestone 1: direct-SDIO hardware bring-up behind an NDIS Ethernet adapter**

The current branch binds an NDIS 6.30 Ethernet miniport to the dedicated
`ACPI\RPI0011` SDIO2 host exposed by the matching UEFI. The driver maps SDIO2
directly and currently implements only a bounded CMD0/CMD5/CMD3/CMD7/CMD52
probe. Experimental driver 0.4 additionally enables function 1, requests ALP,
selects the ChipCommon backplane window, and performs 16 bounded CMD53 PIO
chip-ID reads at the identification clock. Temporary card settings are restored
on success and failure. It does not implement firmware loading, association,
transmit or receive traffic. It does not enable function 2 or write chip RAM.

The CMD5 identification probe uses three bounded query/voltage-request cycles
at each of 400, 200 and 100 kHz. It rejects empty or malformed R4 responses and
accepts negotiation only after a valid ready response. Every command records
the raw response, interrupt status, command-line reset result, and before/after
host-control, timeout, clock, power and line state for hardware diagnosis.

Windows therefore sees a disconnected Ethernet adapter even when the probe
succeeds. This is intentional diagnostic behavior, not working Wi-Fi.

The user physically validated CMD5/CMD52 with driver 0.3 and UEFI source
`bda4c47` on 2026-09-18. On 2026-09-20, driver 0.4 passed all 16 chip-ID reads
on the Pi: chip 0x4345 revision 6, with successful restoration.
Keep UEFI exp.0.3 installed. Do not reflash Windows for this driver update.

### Driver 0.5: core inventory before firmware work

Driver 0.5 retains those reads and scans the AI EROM with bounded reads and
descriptor validation. It locates ChipCommon, SDIO, D11 and ARM CR4 cores,
reads CR4 wrapper state, and reads CR4 RAM-bank capabilities only if the core
is already clocked and out of reset. It does not halt/wake/reset the chip CPU.
`RamBase=0x198000` is a reference-defined base, not proof of RAM capacity;
`RamBankCount=0` can mean the core was not accessible. RAM size is not measured.
Expected: ProbePhase=350, CoreInventoryComplete=1, Cmd53WriteCount=0 and both
LastStatus/ProbeRestoreStatus zero. New hardware testing is required.

A bounded CMD53 write routine is implemented and host-simulated, but startup
does not call it. RAM bank selection, RAM test writes, firmware upload/startup,
association and network traffic remain disabled/unimplemented pending the
core inventory result. Do not mistake this preflight build for a firmware or
internet-ready release.

The 0.5 installer skips device-enable when the exact adapter reports PnP
problem code zero. It does not treat exit 50 as success without checking the
device, and never force-enables a device with an unrelated/unknown problem.

## Architecture

```text
Windows 11 ARM64
    |
    +-- NDIS 6.30 Ethernet miniport
    |
    +-- CYW43455 firmware/control layer (integrated experimental candidate)
    |       +-- BCDC / SDPCM / chip backplane
    |
    +-- direct SDIO2 host transport
            +-- SDHCI CMD5/CMD52/CMD53
            +-- CYW43455
```

## Repository layout

- `src/driver/` - Windows kernel driver entry/PnP scaffolding.
- `src/sdio/` - direct SDHCI PIO transport (no Microsoft SD bus dependency).
- `src/cyw43455/` - CYW43455-specific chip/firmware protocol code.
- `package/` - driver INF/package files.
- `docs/` - architecture, build and bring-up notes.

## Branch strategy

The first development branch is `bringup/cyw43455-sdio-arm64`.

## Status

This repository is experimental. The integrated candidate remains unvalidated
on hardware; earlier releases are probes. Install only on the matching Pi 5
test system with recovery access available. Never install on the development PC.

## License

GPL-3.0. See `LICENSE`.
