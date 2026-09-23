# Readiness and performance utility 0.6.27

On the Pi, after installing the complete .27 driver package and restarting,
run `Check-RPi5-WiFi-Readiness.cmd`. It preserves a healthy authenticated link
with usable IPv4/default route or establishes one if needed. The one resulting
readiness ZIP includes the performance report and diagnostics. Nothing is
uploaded automatically; do not share your editable `WiFi.private.json`.

The utilities retain .26's behavior with version labels updated. The driver
restores .25's immediate send completions; it does not change the workload.

## Interpretation

The comparative test performs sequential one-MiB HTTPS downloads for up to
90 seconds or 128 requests. Including the initial probe, payload is at most
129 MiB. Gateway pings and traffic sampling run alongside the downloads; radio
snapshots are queried only before/after. The same workload measured .24/.25 at
28.96/28.99 Mbps and .26 at 17.74 Mbps, with all 128 downloads completed in each.
Each run still had queue rejections and two NoResources load probes.

Compare throughput, complete/failed downloads, latency and resource/queue
errors together. Lower rejection counts in a slower run are not automatically
an improvement. PHY speed is not application throughput; firmware RxBad is not
a TCP-loss percentage. Missing timing evidence is not zero latency. The .27
`TxCompletionBatchCalls`, `TxCompletionBatchNbls` and `TxCompletionBatchMax`
fields should be zero because batching was removed.

HTTP rejection is inconclusive, not zero driver speed. An incomplete workload
is not a successful performance run. ReadyWithWarnings must remain visible
when congestion or missing validation evidence remains. The test is too short
to certify long-term stability or complete driver development.

## Startup and reconnect evidence

Startup evidence is captured before a new connection. Missing, old-boot or
different-version startup receipts are NotTested, not a pass. Autoconnect is
optional and is not enabled by installing the driver. Existing protected
profiles are preserved during startup-code refresh; see AUTO-CONNECT.md.

Double-clicking readiness does not request a reconnect. To explicitly test it,
start from an already authenticated connection and run from the package folder:

```powershell
.\Check-RPi5-WiFi-Readiness.cmd -VerifyReconnect
```

This briefly disconnects/reconnects once and then measures. If the initial
state was firmware startup rather than an authenticated link, reconnect remains
NotTested even when first connection succeeds. There is no continuous reset
loop and Internet/DNS failure alone does not trigger radio recovery.

For Wi-Fi-only results, manually disconnect wired Ethernet/VPNs while retaining
Ethernet for recovery. No router rename or other device is needed. The standalone
`Test-RPi5-WiFi-Performance.cmd` remains available, but do not run it again just
to duplicate the measurement already included in readiness. Review reports for
network/device details before publishing them.
