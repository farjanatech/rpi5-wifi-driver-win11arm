# Readiness and performance utility 0.6.26

On the Raspberry Pi, use `Check-RPi5-WiFi-Readiness.cmd` after installing the
complete package and restarting. The resulting single ZIP combines readiness
evidence with the existing performance/diagnostic report; nothing is uploaded
automatically. Keep your private profile local and retain .24 for rollback.

Startup evidence is captured before any connect/reconnect. A missing, old-boot
or different-version startup receipt is **Not tested**, not success. A healthy
authenticated connection with usable IPv4/default route is preserved. Testing
a reconnect is an explicit separate option; the utility does not run a reset
loop or treat a remote Internet failure as proof the radio needs restarting.

The comparative load remains sequential one-MiB HTTPS downloads, limited to
90 seconds or 128 requests. Including the initial probe, payload is at most
129 MiB. At roughly 29 Mbps this finishes in about 37 seconds, so it cannot
certify long-term stability. Gateway pings and traffic sampling run alongside
the downloads. Firmware radio snapshots are taken only before/after the load.

HTTP server rejection is inconclusive, not zero driver speed. A report that
did not reach the workload end is incomplete even if an older script returned
exit code zero. NoResources probes, queue rejections, failed downloads and
route ambiguity must remain visible; raw throughput alone is not a pass.

## Comparing the candidate

.24: 28.96 Mbps, 128/128 downloads, 4,537 queue-full rejections, two NoResources
load probes. .25: 28.99 Mbps, 128/128, 6,010 rejections, two NoResources probes.
These are single short runs, not a controlled proof of a regression.

.26 batches only genuinely processed send completions. Compare throughput,
download failures, gateway latency, resource errors, retained queue high-water
and queue delay together. New counters `TxCompletionBatchCalls`,
`TxCompletionBatchNbls` and `TxCompletionBatchMax` establish whether batching
was exercised; their values alone do not establish a speed improvement.

The before/after reports retain radio, bus verification, timing and passive
transport history. PHY speed is not application throughput. Periodic driver
counters can include activity outside the workload; firmware RxBad is not
directly a TCP-loss percentage. Unavailable/disabled timing is not zero latency.

Readiness reports contain network/device details but no copied private profile,
password or PMK. Review them before publishing. `Test-RPi5-WiFi-Performance.cmd`
remains available for a standalone comparative run; there is no need to run it
again immediately after the combined readiness command.
