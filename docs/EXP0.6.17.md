# exp0.6.17: bounded queue headroom and explicit radio diagnostics

Baseline .16 completed 128/128 one-MiB downloads at 18.55 Mbps in 57.89 seconds.
All 55 router probes succeeded (27 ms average, 69 ms maximum), but periodic
snapshots recorded 813 queue-full rejections. This is a promising single run,
not a controlled causal proof or long-term certification.

## Changes

- Increase the fixed admission cap from 64 to 128 retained Ethernet frames.
  At most 193,792 bytes of frame payload are retained, plus NDIS metadata/backing
  storage. This is NOT a total pool-memory bound. Genuine exhaustion is still
  rejected. Multi-NB ownership, 30-second expiry, completion accounting,
  cancellation, pause/power and failure completion are unchanged.
- Preserve .16's 4-TX / bounded RX / 4-TX schedule and diagnostic placement.
  Do not restore .15 per-RX-frame transmit interleaving or 256-frame admission.
  Larger capacity may trade fewer rejections for more latency. Hardware testing
  must reject this candidate if latency/stalls worsen; no speed promise.
- Add explicit-request GETs for channel, RSSI, PM mode and minimum-power mode.
  A single existing bus worker performs them. No second bus owner, SET, scan,
  radio reset, country substitution or forced band. They are requested once
  BEFORE the performance workload, never by the in-load sampler. They can
  briefly delay other traffic while querying; do not run during a speed test.
- Unsupported/malformed responses remain unknown, with per-query status/error.
  Timeout/allocation/transport failures stop further queries; no retries. The
  connection's original firmware diagnostic fields are preserved. A link loss
  invalidates the returned sample. Registry fields are last-query observations,
  not live signal readings; use the tool's capture time and generation.
- Existing `mpc=0` and PM-off SETs remain unchanged. This only verifies readback.
  No negotiated speed is fabricated in NDIS/Task Manager. The band is only
  labelled if hardware and target channels match and no scan is reported.

## Run on the Pi

1. Keep exp0.6.16 for rollback. Extract the entire .17 ZIP into a new folder.
2. Run `Install-RPi5-WiFi-Driver.cmd`, approve elevation and restart once.
3. Copy your existing `WiFi.private.json` beside the tools if using that profile.
   Never edit the hashed public example or share the private profile.
4. Unplug Ethernet, disconnect VPNs and close unrelated downloads. Run
   `Test-RPi5-WiFi-Performance.cmd`. It connects, reads radio state, tests and
   collects diagnostics. Send the desktop `RPI5-WIFI-PERFORMANCE-*.zip`.
5. For radio information alone after connecting, use `Get-RPi5-WiFi-Radio.cmd`.

Same workload: up to 90 seconds/128 one-MiB requests, at most 129 MiB download
payload plus overhead. No credentials or reports are uploaded automatically.
Compare against .16: speed, completion count, ping loss/latency, maximum queue
delay, rejections and TX credits, not just a speedtest peak. If worse, select
the retained .16 driver in Device Manager and restart. Do not change UEFI.

Country BD, firmware/CLM/NVRAM, fan/UEFI, 4-bit/25 MHz SDIO, private-profile
behavior and existing connection IOCTLs are unchanged. No band preference is
introduced until actual radio observations are available.

## Validation

GitHub checks actual .16 source identity for packet transfer, receive, send,
worker budgets and queue ownership. Host tests cover bounded queue admission,
multi-NB retention, callback re-entry, lifecycle/fault handling, actual firmware
GET transport, signed RSSI, malformed/unsupported/timeout responses, utility
parsing and the pre-load-only query. ARM64 build/signing/package checks follow.
These are not Pi hardware tests and do not establish a performance improvement.

Protocol references: Linux v6.12 brcmfmac `fwil.h` commands 29, 127 and 85,
`fwil_types.h` channel and SCB value layouts (ISC, Broadcom). No firmware files
or borrowed code from the user's Linux optimization PDF are incorporated.
