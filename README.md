# Raspberry Pi 5 CYW43455 Wi-Fi driver for Windows 11 ARM64

Experimental Windows 11 ARM64 driver work for the Raspberry Pi 5 onboard Infineon/Cypress CYW43455 Wi-Fi controller.

## Current milestone

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
