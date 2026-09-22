# Performance utility 0.6.22

Run Test-RPi5-WiFi-Performance.cmd on the Raspberry Pi with competing wired/VPN
routes disconnected. The same one-command workflow connects using your private
profile or prompt and saves one ZIP. No driver installation or network edits.

Radio reporting requires driver .17+. Driver .22 adds coherent runtime timing
snapshots, automatically decoded into timing-report.json and timing-report.txt.
Older drivers still use the same speed workload and report timing unavailable.
Do not run a separate radio query during a download.

The .14.1 workload is unchanged: sequential 1-MiB requests, <=90 seconds or 128
requests, <=129 MiB requested payload plus protocol overhead. DNS/HTTPS checks,
independent router probes and traffic sampling remain. Close unrelated downloads.
HTTP rejection is inconclusive; partial downloads do not count as completed bytes.

Timing is cumulative, inclusive wall time, not CPU time. Nested buckets cannot be
summed. Delta counters require matching sessions, monotonic counters and a newer
snapshot. Maxima are cumulative, not interval maxima. Periodic snapshots can
include work outside the download window. Worker/credit service intervals do not
prove scheduler starvation or exact firmware-credit delays.

Only example.com and speed.cloudflare.com are contacted. No report upload,
TLS bypass, band/country override or password capture. Review logs before sharing.
