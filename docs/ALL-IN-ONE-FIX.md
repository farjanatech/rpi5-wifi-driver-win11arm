# all-in-one-fix: 0.7.1.7 exhausted-credit scheduling experiment

**Experimental, NOT merge-ready. The branch name does not mean every subsystem
has been changed or every problem has been fixed.**

Protected stable source: `c0b032543f945707c82ffc1b05a8ca7012821560` (main, 0.7.1.4).
Parent experiment: `fe0800a7945d79ce7fc19b1bf326e8ba5cffceb8` (0.7.1.5).
`main` and the previous experiment branches remain unchanged.

## One driver variable

0.7.1.5 mode 1 combined earlier post-RX dispatch with extra low-pressure sends.
0.7.1.6 removed those extra sends. 0.7.1.7 keeps the exact 0.7.1.4 TX budgets
and the earlier post-RX dispatch, then changes only the known-exhausted-credit
worker behavior in mode 1: if a runnable pending queue has TxSeq == TxMax, the
worker skips the futile initial TX pump and waits on the existing wake event
with the original 10 ms polling timeout as a safety fallback. SDIO interrupts
signal that wake immediately; an RX frame that reopens credits is processed and
then the original post-RX pump runs without any added send budget.

| Package/build flag | Behavior |
|---|---|
| 0.7.1.7 / Rpi5TxCreditScheduling=0 | Matched diagnostic control: original dispatch order, original retry policy and original TX budgets. Default for manual builds. |
| 0.7.1.7 / Rpi5TxCreditScheduling=1 | Earlier post-RX dispatch plus event-first waiting for exact exhausted credits; TX budgets remain identical to mode 0. |
| Original 0.7.1.4 | Uninstrumented stable reference and complete rollback target. |

Do not confuse the new 0.7.1.7 mode 1 with 0.7.1.5 mode 1: the latter contains the
removed lower-pressure extension. Verify INF version, SOURCE_REVISION.txt,
package hash, installed file hash and the live SchedulingEnabled snapshot flag.
The installer source still has an old version banner; it is not a driver-version
or loaded-mode check. Reboot after replacing a package before measuring. An
on-disk SYS hash alone cannot establish which image is currently loaded.

No changes to packet format, 64-frame admission, SDIO/PIO/DMA/clock modes, RX,
firmware/CLM/NVRAM, association/authentication, country/radio, UEFI, interrupts,
power/lifecycle handling or FIFO error/recovery code are included. The original retry helper remains unchanged; mode 1 bypasses its legacy 1 ms fast-retry episode only while exact firmware credit exhaustion is already known.
The original pressure pump, ownership queue and all 48 original source files
remain exact after removing the previously marked additive TX splices. This
branch only reduces the previous experimental wrapper and defaults it off.

## Diagnostics and tests

The existing 464-byte TxCreditV1 layout is unchanged. SchedulingEnabled 0/1 is
interpreted with the driver version and commit. In 0.7.1.7 ExtraPasses/Frames
refer ONLY to the original high-pressure extension, not new low-pressure sends.
F1/F2 timing is disabled in the normal pair; unavailable is not zero overhead.
The TX snapshot reader still rejects nonadvancing/mixed sessions, changed modes,
frequency/worker changes, regressed/saturated counters, and invalid snapshots.

The new budget regression was introduced before the driver change and rejects
the 0.7.1.5 mode-1 extension. It runs the original ownership fixture plus all
4,225 queue/credit combinations, BUSY/failure/no-replay, cancel/Pause/D3/flow,
clock rollback, deadline and reentrant/poisoned-NBL cases. The actual post-RX
source slice is compiled and executed with mock external calls in both modes
to test order, exactly one dispatch, exporter deadline/phase and failure exits.
Existing CI, transport, interrupt, ASAN, utility, connector and ARM64 build tests
remain required. These are simulations/build checks, NOT physical-Pi tests.

## LAN measurement helper (separate tooling, no driver change)

`Test-RPi5-WiFi-Lan.ps1` runs a supplied, trusted iperf3 executable. It does not
download/install iperf3, open firewall ports, alter routes, change adapters,
install a driver, modify UEFI/BCD, or upload any result.

Use a wired server on the same LAN as the Pi's AP. Start its iperf3 server:

```text
iperf3 -s
```

Confirm the server can sustain more bandwidth than the Wi-Fi link, keep the
same executable versions/hashes across builds, and retain the server's logs.
On the Pi, use Windows PowerShell 5.1 after installing/rebooting the selected
package. List the adapter/source addresses first:

```powershell
Get-NetAdapter | Format-Table ifIndex, Name, InterfaceDescription, Status
Get-NetIPAddress -AddressFamily IPv4 | Format-Table InterfaceIndex, IPAddress
```

Replace the example addresses/index below with the **actual wired server and Pi
Wi-Fi values**. Both must be canonical private IPv4 addresses; the best route
must be on-link, from the selected interface and source. Other routed/VPN/public
paths are deliberately rejected rather than silently measuring the wrong path.

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\Test-RPi5-WiFi-Lan.ps1 `
  -IperfPath C:\Tools\iperf3.exe `
  -ServerAddress 192.168.1.10 -LocalAddress 192.168.1.20 -InterfaceIndex 7 `
  -BuildLabel v0717-control -Seconds 60 -Repetitions 3 -Streams 1
```

For the dispatch-only package, use `-BuildLabel v0717-credit-wait` with
otherwise identical arguments. This label is user input, not identity proof.
For the original 0.7.1.4 baseline, run this same helper from the new package's
folder with `-BuildLabel v0714-stable`; the original driver lacks the new TX
snapshot, which is recorded as unavailable and never counted as passing.

The runner uses numeric addresses and `-B` source binding; it tests upload and
reverse download separately with five seconds omitted for warmup. It alternates
the direction order between repetitions. It captures raw iperf3 JSON, stderr,
commands, executable/driver file hashes, before/after diagnostic snapshots and
ICMP samples, then creates a uniquely named Desktop ZIP. It validates reported
connection addresses, protocol, direction, stream count, duration and receiver
results. Incomplete/error/timeout runs do not become successful zero results.
The watchdog terminates only its own iperf3 process. Failed-run evidence is kept.

ICMP follows the default route, which is checked before and after each workload;
iperf3 itself is explicitly source-bound. ICMP samples include warmup and are not
an application-latency or jitter measurement. Other route changes during a test
can invalidate interpretation. General diagnostic DWORDs are not one atomic set.
TX/Timing blobs are checked for coherence and advancing snapshots, but their
periodic export windows are broader than workload timestamps. A stale exporter
or missing snapshot is not evidence of zero errors. The tool NEVER declares a
hardware/merge gate passed, even if all throughput runs complete.

For complete validation also retain the normal diagnostics, radio observations,
installation logs and server outputs. Alternate actual installed builds in a
balanced sequence (for example stable/control/dispatch, then dispatch/control/
stable), using the same Pi/AP/server/position, streams, power and thermal state.
Do not compare one favorable run with one unfavorable run. Compare repeated
receiver goodput, loss/tail latency and workload-window queue counters. Start
with single-stream forward/reverse, then a separate matched four-stream pair.
Internet HTTPS results are supplementary only, not LAN throughput evidence.

Official option references:
- https://software.es.net/iperf/invoking.html
- https://learn.microsoft.com/en-us/powershell/module/nettcpip/find-netroute

## Merge gates and rollback

No hardware results have been generated by this implementation task. Keep the
branch unmerged until matched LAN results improve over ORIGINAL 0.7.1.4, latency
does not materially regress, queue pressure is equal or better and all workloads
complete. Required zero counters on clean throughput/soak runs:

```text
DisconnectCount = 0
WorkerFailureCount = 0
FifoBlockFailures = 0
FifoTransportFailed = 0
RxGlomErrors = 0
Cmd53Timeouts = 0
InterruptStormFallback = 0
```

Also complete D0/D3, sleep/resume, Pause/Restart, adapter restart, AP/router
reconnect, long idle/download/mixed traffic and overnight tests. Intentional
lifecycle/disconnect tests are separate from clean zero-disconnect soak trials.
There is no auto-merge, background hardware test or assumed performance gain.

Rollback scheduling to the new mode-0 control for a matched-instrumentation
comparison. Full rollback is the ORIGINAL 0.7.1.4 package and baseline SHA above.
Retain the stable package offline; select its exact INF using the existing
installation procedure and reboot, then verify the active package/version.
Do not remove unrelated drivers or change Test Signing/Secure Boot/UEFI to switch
between these already-compatible experimental packages.
