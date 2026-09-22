# Performance utility 0.6.23

Run `Test-RPi5-WiFi-Performance.cmd` on the Raspberry Pi, not the development PC.
It connects using your private profile or prompts and saves one report ZIP.
It does not install a driver, change network settings, or upload the report.
Disconnect competing Ethernet/VPN routes and close unrelated downloads first.

The existing workload is unchanged: sequential 1-MiB HTTPS requests, <=90 seconds
or 128 requests, <=129 MiB requested payload plus overhead. Effective throughput
includes connection setup, incomplete attempts and observation overhead. HTTP
server rejection is inconclusive, not a measured zero-speed driver result.

Radio snapshots run BEFORE and AFTER the workload, never from the one-second
sampler or while downloads are in progress. With .23 they include optional
firmware packet/retry counters, station rates, BSSID and raw chanspec in
`radio-before.json` and `radio-after.json`. Unsupported/malformed fields are
unknown. Firmware counters can reset/wrap; rates are not measured throughput.
Do not run a separate firmware/radio query while testing.

Driver .22 timing-v1 and .23 timing-v2 are supported. In .23, detailed command
timing is normally disabled; disabled rows explicitly contain null metrics.
Worker timing remains available in `timing-report.json`. Timing is cumulative,
inclusive wall time, not CPU time; do not sum nested buckets. Snapshots can span
work outside the download window; maxima are cumulative, not interval maxima.
Transport/flow-control counters are in driver-before/after and the nested
diagnostics ZIP. Older drivers retain the same workload with unavailable
evidence explicitly marked, not guessed.

Checks contact the local gateway/configured DNS server, 1.1.1.1 for ping, and
example.com/speed.cloudflare.com for DNS/HTTPS. No TLS bypass or country/band
override is performed. Logs contain local addresses, BSSID and device details;
review before sharing publicly. Wi-Fi credentials are not logged.
