# Performance utility 0.6.24

Run `Test-RPi5-WiFi-Performance.cmd` on the Raspberry Pi after installing the
complete driver package and restarting once. Keep the current private config;
no SSID rename, new router or additional device is required. Disconnect wired
Ethernet and VPNs for a Wi-Fi-only measurement. Keep Ethernet for recovery.

The utility connects as needed and retains the same workload as previous
comparisons: sequential 1 MiB HTTPS downloads for 90 seconds or 128 requests,
independent gateway pings and Windows traffic sampling. Total payload is
bounded to 129 MiB including the earlier probe. Internet requests are made to
the documented test endpoints; no diagnostic files are uploaded automatically.
HTTP rejection is inconclusive, not zero driver speed.

The report includes before/after radio measurements, runtime timing, verified
bus speed and `transport-history.json` (driver .24 required). The history is
a passive rolling 128-sample record, approximately one sample per second,
using monotonic time since boot, not UTC. Missing/invalid data is unavailable,
never interpreted as zero errors. It is not a packet capture. The unchanged
workload receives no extra radio queries while downloads are running.

`BUS VERIFIED` requires all 16 chip-ID reads and successful verification.
For a calculated clock above 25 MHz it also requires current .24 high-speed
mode/status evidence. A recovered and reverified 25 MHz mode is valid but is
not described as a 50 MHz improvement. Authentication or bus verification
alone does not prove good throughput or Internet availability.

Share the single resulting performance ZIP. It already contains diagnostics;
there is no need to run the separate collector for a normal completed test.
Reports contain local network/device details; review before publishing.
Passwords and the private config are not included.
