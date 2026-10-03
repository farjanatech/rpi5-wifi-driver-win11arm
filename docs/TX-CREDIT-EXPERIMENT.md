# TX credit/pump experiment — NOT merge-ready

Protected baseline: `c0b032543f945707c82ffc1b05a8ca7012821560` (main, v0.7.1.4).
Experiment: `experimental/tx-credit-pump-v0.7.1.4`, candidate INF 0.7.1.5.
No separate v0.7.1.4 tag existed when work started. The full baseline SHA, not a
moving branch name, is the source rollback anchor. Keep the original baseline
binary package as well. This document is a protocol, not hardware evidence.

## One logical change: post-RX TX credit use

The diagnostic control uses `Rpi5TxCreditScheduling=0`. Mode 1 uses the same
observations but executes the post-RX pump immediately after the unchanged RX
batch, before periodic diagnostic exporters. It first runs the exact existing
four-frame pump/high-pressure extension. Only if that extension did not start
may an open, nonempty queue with confirmed usable credits spend up to four more
frames. Every extra frame rechecks the existing lifecycle/flow/credit gate and
uses the original ownership pump and fresh F1 service. A stopped, busy or failed
transfer never gets a second pass or replay. The original high-pressure pass is
never followed by a second extension/deadline.

The ceiling remains eight post-RX frames (four pre-RX frames unchanged). The
extension has the original two-millisecond **between-frame** deadline; it cannot
preempt an already-running transfer or completion callback. This is not a hard
two-millisecond bound on an entire worker iteration. No timer-resolution change,
per-RX-frame TX, invented credits, SDPCM coalescing, early NBL completion, or queue
enlargement is introduced. Neither the original credit validator nor RX code is
changed. Low-pressure TX can consume more of the existing ceiling: real latency
and receive fairness must therefore be measured, not assumed.

## Diagnostics and interpretation

`TxCreditV1` is one atomic 464-byte, versioned REG_BINARY snapshot under the
existing `HKLM:\SOFTWARE\Rpi5CywDirectDiag` key. The runtime worker is the only
writer. No packet contents, SSIDs, passwords or other credentials are collected.
The existing diagnostics collector already exports registry values; the new
reader validates this snapshot against current `TimingV2` and worker identity.
Snapshots are periodic (normally about 30 seconds), not synchronized to an
iperf3 start/stop. A stopped exporter is not evidence of zero new errors.

Use `Get-RPi5-WiFi-TxCredit.ps1 -SaveSnapshot before.json`, run the workload, and
later use `Get-RPi5-WiFi-TxCredit.ps1 -BeforeSnapshot before.json -SaveSnapshot after.json`.
Run long enough to capture advancing driver snapshots. A rejected mixed snapshot
can occur during export; collect again. `-ReadSnapshot after.json` supports
analysis without accessing a device. Files contain only allowed diagnostic
blobs, worker identity and capture time. They are not a full stability report.

| Observation | Meaning / limitation |
|---|---|
| CreditSamples and window bins | Original pre-transfer gate observations, not time-weighted credit occupancy. Modulo-256 windows above 64 are invalid, never permission to send. |
| GateCreditZero / Invalid / Lifecycle / Priority | Distinct causes observed at the original gate. Existing `TxCreditWaits` semantics are intentionally unchanged. |
| F1Calls / StatusReads / StatusAcks / MailboxReads / FlowBusy | Work caused by the existing fresh data-send status service. No status-check elision. |
| F2Calls / Errors / Cmd53Writes | Data FIFO calls, failures, and CMD53 writes observed inside those calls. Not a synthetic estimate from packet counts. |
| F2PayloadBytes / PaddedBytes | Successfully transferred SDPCM payload (includes BCDC) versus padded FIFO bytes. Neither is application goodput. |
| PumpCalls / Frames / BudgetHits / PendingEnds | Inclusive pump observations, including nested one-frame extension pumps. Do not sum nested pump/worker/F1/F2 times. |
| QueueEntriesSampleMax / QueueRetainedSampleMax | Sampled cumulative maxima; not admission counts, not exact continuous queue occupancy. Keep existing TxQueueFull and TxQueueMaxDelayMs too. |
| RxCreditReopens / WindowGrows | Before/after completed RX-batch observations, not individual firmware credit messages. |
| RxEndToPostPump timing | Gap after RX batch observations to TX dispatch, useful for detecting exporter delay. Not packet RTT. |

F1/F2 timing is **opt-in** (`Rpi5DetailedTiming=1`); default packages report it as
unavailable, not zero. Durations are QPC elapsed time, not CPU utilization. The
reader rejects nonadvancing snapshots, changed sessions/modes/frequency/worker,
counter regression and saturation. Maxima are always labeled cumulative, never
subtracted to invent interval maxima. Missing data never passes a merge gate.
Additional counters, two queue samples and aggregate clocks have an overhead;
use matched instrumentation for the scheduler comparison, then compare with the
unaltered v0.7.1.4 package before any merge decision.

## Reproducible build controls

Use the existing documented WDK build/restore environment and full CI. Pass
`/p:Rpi5TxCreditScheduling=0` for the instrumented original policy and `=1` for
the candidate. Both use `/p:Rpi5DetailedTiming=0` for normal comparison. For an
F1/F2 timing investigation, repeat a matched pair with `Rpi5DetailedTiming=1`.
Do not compare a detailed candidate with a nondetailed control as proof of a
scheduler speedup. Package with `scripts/package-tx-credit.ps1` using the same
`-TxCreditScheduling 0|1` value and
retain `SOURCE_REVISION.txt`, build flags, commit SHA and hashes with results.
CI verifies its effective compiler definition against the recorded mode.
Version 0.7.1.5 alone does not identify the mode: check provenance and snapshot.

## LAN A/B measurement (hardware required)

Use the same numeric LAN server address on a wired peer, the same AP/router,
band/channel, placement, UEFI, Windows power configuration and driver settings.
Record server/client iperf3 versions, Pi Wi-Fi IPv4, link/RSSI observations and
server CPU/load. Ensure the wired server/link can exceed the Pi throughput;
otherwise the result is a lower bound, not a driver bottleneck diagnosis.
Disable competing network routes or explicitly bind to the Pi Wi-Fi IPv4.

On the wired LAN peer, start an existing trusted iperf3 installation:

```text
iperf3 -s
```

On the Pi, replace the placeholders with **numeric local IPv4 addresses**:

```text
iperf3 -c <LAN_SERVER_IP> -B <PI_WIFI_IP> -t 120 -O 5 -P 1 -J > upload.json
iperf3 -c <LAN_SERVER_IP> -B <PI_WIFI_IP> -t 120 -O 5 -P 1 -R -J > download.json
```

These are plain LAN TCP tests: they bypass DNS, TLS, CDN selection, remote web
servers and Internet paths. They still include Windows TCP/IP, the peer and AP;
they are not a pure physical-layer test. Use single-stream results first; add a
separate, clearly labeled multistream run, not a replacement that hides stalls.
Run independent timestamped LAN latency probes throughout idle and loaded tests;
report loss plus median/p95/p99 RTT, workload failures, duration and all safety
counter deltas. Distinguish probe scheduling delay from actual RTT. Internet
HTTP/DNS/TLS/CDN tests may remain end-to-end smoke tests, never the driver
throughput score. No WAN speed result establishes LAN improvement.

Alternate at least five comparable runs per condition. First compare unchanged
v0.7.1.4, instrumented mode 0 and mode 1; check instrumentation cost separately.
Use matching warmup/length, retain raw JSON and before/after diagnostics, and
report distribution/spread rather than the best run. Reboot or reinitialize
consistently between variants, verify the actually loaded driver and mode, and
never compare counters across a reset. Throughput improvement must exceed the
observed run-to-run variation. Do not declare improvement from one faster run.
Agree a quantitative latency tolerance before testing; until specified and
passed, the "no material latency regression" gate remains unresolved. Queue
pressure must be equal or better for matched workloads, not just fewer pump calls.

## Required safety and lifecycle validation

For every normal workload run, confirm completion and zero new disconnects,
worker failures, FIFO block failures, RX glom errors, CMD53 timeouts and interrupt
storm fallbacks. `FifoTransportFailed` must remain zero (a state flag, not a delta).
Collect the existing readiness and full diagnostics too. Record both starting
and ending counters: dirty/unknown starting state requires a clean rerun, not a
subtraction that hides pre-existing faults.

Exercise long idle, sustained download/upload, mixed small packets and bulk
traffic, and overnight operation. Separately exercise D0/D3, sleep/resume,
Pause/Restart, adapter restart and AP/router loss/reappearance, checking restored
traffic and exactly-once completion behavior. Deliberately induced disconnects
belong to separate lifecycle trials; they do not satisfy a zero-disconnect
throughput/soak gate. Follow each lifecycle trial with a clean stable workload
run meeting all zero-error gates. Record intended events separately from
unexpected disconnects; do not silently reclassify failures.

Do not merge to main unless DisconnectCount, WorkerFailureCount,
FifoBlockFailures, FifoTransportFailed, RxGlomErrors, Cmd53Timeouts and
InterruptStormFallback are zero for the clean validation run; workload succeeds;
latency does not materially regress; queue pressure is equal/better; and measured
LAN throughput improves over the original v0.7.1.4. All existing CI, ASAN,
transport, ownership, interrupt and ARM64 tests must pass too. Host simulation
and a green build are not hardware validation. No hardware pass is recorded here.

## Rollback

Mode 0 reverses scheduling only while preserving identical instrumentation.
Full rollback uses the original v0.7.1.4 binary package and the pinned baseline
SHA. Use the existing installer to reinstall that exact package, verify the
loaded version and reconnect normally. Keep an offline copy before testing,
since Wi-Fi may be unavailable during rollback. Source rollback is a separate
checkout of the baseline SHA; do not reset or force-push main. No firmware,
country/CLM/NVRAM, interrupt, RX, SDIO, PIO or queue-size experiment is included.
DMA/faster modes and any aggregation design remain separate future experiments.
