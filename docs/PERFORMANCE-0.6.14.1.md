# Performance utility 0.6.14.1 (keep driver exp0.6.14 installed)

This is a measurement update, **not a faster driver or a driver installer**.
The Pi's previous 1 MiB download measured 16.8 Mbps, but its 16 MiB request was
rejected with HTTP 403. That left no sustained-load evidence. The traffic
statistics worked and no driver TX/RX errors or queue-full events were recorded
in that short capture. One public ping timed out; its cause is not established.

## Run on the Raspberry Pi, not the development PC

1. Extract the entire performance utility ZIP into a **new folder**. Do not
   overwrite files in the signed/hashed driver installer package.
2. Leave exp0.6.14 installed. **No installer or reboot is needed.**
3. Unplug wired Ethernet and disconnect VPNs. Close other downloads/videos for
   the baseline. Keep Ethernet available for recovery.
4. Run **Test-RPi5-WiFi-Performance.cmd**, approve the administrator prompt and
   enter your existing Wi-Fi details if asked. BD is for a Pi in Bangladesh.
   Alternatively copy your existing `WiFi.private.json` beside the utility;
   never share that file. A profile is not included in this public package.
5. During the repeated download stage, watch Task Manager > Performance >
   Ethernet 2. Record whether its graph moves. The utility captures the actual
   counters, but cannot verify the UI itself.
6. Share the new `RPI5-WIFI-PERFORMANCE-*.zip` from the Pi desktop. The ZIP
   contains device/network information, not your password. Nothing is uploaded
   automatically. No separate diagnostics command is required.

## Workload and limits

The utility retains route checks, pings, DNS/HTTPS checks and a 1 MiB quick test.
It then requests consecutive **1 MiB** HTTPS downloads from the documented
Cloudflare speed-test download endpoint. The sustained-workload stage stops at
**90 seconds or 128 requests**, whichever comes first. Each request is capped
at 15 seconds (or the remaining budget); process cleanup may take an extra
second. Total requested download payload is at most **129 MiB**, plus protocol
overhead. No uploads, insecure TLS flags, server-limit bypass or IP overrides.

HTTP errors (including 403/429), redirects and invalid-sized responses stop
the workload immediately. Three consecutive transport failures also stop it.
There is no automatic alternate-server retry after a server rejection. A
server failure is explicitly inconclusive, not a zero-Mbps driver result.
The endpoint can still reject requests; no external server is guaranteed.

`load-summary.json` reports successful bytes divided by **whole-stage elapsed
time**, including failed attempts, request/TLS startup and sampling overhead.
Failed/partial bodies are excluded from that conservative effective rate.
Repeated small transfers are not one continuous TCP transfer or a PHY-rate test.
The byte cap may finish the stage early; inspect its actual duration before
claiming sustained stability. These measurements do not isolate ISP/router/RF
effects from the driver.

## Evidence for the next driver decision

- `download-samples.csv`: each request's start/end, status, bytes and timing.
- `load-timeline.csv`: independent timestamped router pings, Windows byte/rate
  counters and selected driver snapshots. Sampling cadence is roughly 1-2s
  and may be slower if Windows calls stall. Rates include all adapter traffic.
  The first sample/counter reset has no invented rate.
- Registry snapshots are cumulative, non-atomic and refreshed by the driver
  about every 30 seconds. Identical snapshot timestamps are **not live queue
  readings**. Full before/after snapshots and Windows protocol counters remain.
- `sampler-errors.txt` records observation failures; a missing timeline means
  loaded-latency validation is incomplete.

Correlate slow download intervals with router latency, queue/credit changes,
SDIO waits and Windows traffic. Only then decide whether receive scheduling,
transmit backpressure or SDIO batching needs a kernel change. Keep the verified
4-bit/25 MHz mode, existing firmware, country handling, UEFI and fan unchanged.

The standalone package contains no SYS/CAT/CER, installer, scheduled-task setter
or UEFI files. The unchanged connection helper may connect using your supplied
credentials; observation does not modify network settings. CI tests use mocked
downloads and counters, not the development PC's Wi-Fi hardware.
