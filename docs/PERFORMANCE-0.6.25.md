# Performance utility 0.6.25

Run `Test-RPi5-WiFi-Performance.cmd` on the Raspberry Pi after installing the
complete driver package and restarting once. Keep the existing private config;
no SSID rename, new router or additional device is required. Disconnect wired
Ethernet and VPNs for a Wi-Fi-only measurement, retaining Ethernet for recovery.

## Same measurement, single report

The workload is unchanged: sequential 1 MiB HTTPS downloads for 90 seconds or
128 requests, independent gateway pings and Windows traffic sampling. Total
payload is capped at 129 MiB including the initial probe. It connects as needed
and uploads no diagnostic files automatically. HTTP rejection is inconclusive,
not a measured zero driver speed.

The report includes before/after radio observations, runtime timing, verified
bus speed, passive `transport-history.json` and the diagnostic ZIP. The unchanged
workload receives no extra firmware radio queries during downloads. Share the
single resulting performance ZIP; a separate diagnostic run is unnecessary
for a normally completed test.

## Compare .25 against .24

The .24 reference run completed 128/128 downloads at **28.96 Mbps in 37.07 s**.
It nevertheless recorded 4,537 queue-full rejects and two `NoResources` load
pings. The .25 candidate targets bounded credit-resumption latency while
retaining .24's band, bus, firmware and queue limits. Higher speed or fewer
rejections is not guaranteed.

The short-wait policy requires explicit exhausted credits (`TxSeq == TxMax`),
pending sends, open lifecycle/flow/admission gates and no RX/TX progress.
It requests at most four 1 ms event waits per episode, stops issuing them at
20 ms elapsed, then keeps 10 ms backoff until TX progress or eligibility clears.
RX progress alone with still-exhausted credits does not reset this budget.
These are requested delays, not exact scheduling promises; an in-flight wait
can exceed the episode check. Microsoft's
[wait contract](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/wdm/nf-wdm-kewaitforsingleobject)
and [timer accuracy guidance](https://learn.microsoft.com/en-us/windows-hardware/drivers/kernel/timer-accuracy)
explain why actual timing varies.

Evaluate throughput and completion reliability alongside local resource
failures, queue delay and router latency. Fewer raw credit-wait counts alone
does not establish less wasted time or better speed. A short successful test
does not prove sustained stability, and results from different bands or radio
conditions are not controlled comparisons. Keep .24 available for rollback.

## Reading evidence correctly

- `BUS VERIFIED` requires all 16 chip-ID reads and successful verification.
  Above 25 MHz, current .24-or-newer high-speed mode/status evidence is required.
  A recovered and reverified 25 MHz path remains valid, but is not a 50 MHz result.
- Radio fields describe actual observations. PHY rates are not application
  throughput; AMPDU capability does not prove active aggregation.
- Transport history requires driver .24 or newer. It is a passive rolling
  128-sample record, about once per second, using monotonic time rather than
  UTC. It is not a packet capture; gaps do not alone prove a blocked driver.
- Timing buckets overlap. Disabled buckets are unavailable, not zero latency.
  Periodic counter/timing snapshots can include activity outside the workload.
- The existing before/after driver reports contain `TxRetryVersion`,
  `TxRetryFastRequests`, `TxRetryBackoffRequests`, `TxRetryIdleRequests`,
  `TxRetryFastResumes`, `TxRetryFastTimeouts`, `TxRetryFastWakes`,
  `TxRetryActualFastWait100ns` and `TxRetryMaxFastWait100ns`. Compare cumulative
  differences and actual waits; a `FastResumes` count only means the next worker
  iteration sent after a short wait, not that the wait caused the improvement.

Reports contain local network/device details; review them before publishing.
Passwords and the private config are not included. The full driver package
also includes `EXP0.6.25.md` with the driver change notes.
