# Performance utility 0.6.17

Run `Test-RPi5-WiFi-Performance.cmd` on the Pi, with Ethernet unplugged and VPNs
disconnected. It connects using the existing prompt/private profile and saves
a desktop report ZIP. No driver installation or network-setting edits.

With driver .17+, a bounded radio GET snapshot is collected before measuring
traffic: actual band/channel, RSSI, PM and MPC readback. Older drivers report
unsupported and continue with the same speed test. Do not run a separate radio
query during the test. `Get-RPi5-WiFi-Radio.cmd` is available for standalone use.

The .14.1 sustained workload is unchanged: 1 MiB per request, 90 seconds or
128 requests, at most 129 MiB total requested payload plus protocol overhead.
HTTP rejection ends the stage as inconclusive; three transport failures stop
it. Partial transfers do not count as completed bytes. Router probes and traffic
samples run independently. Close unrelated downloads for meaningful results.

Only public Internet endpoints example.com and speed.cloudflare.com are used.
No log upload, disabled TLS verification, country/band override or stored
password capture. Share the resulting `RPI5-WIFI-PERFORMANCE-*.zip` yourself.
