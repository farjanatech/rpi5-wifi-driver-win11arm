# Measurement-only readiness utility 0.6.27.1

Keep driver **exp0.6.27 installed**. This ZIP contains only utilities and their
documentation: no driver, firmware, certificate, installer or startup-task
configuration tool. It does not promise a speed increase. Its purpose is to
identify where time is spent during the slower parts of the existing test.

## One-click use on the Raspberry Pi

1. Extract this entire ZIP into a **new folder**, separate from the driver package.
   Do not replace files in the driver ZIP: its integrity manifest must stay intact.
2. If already connected, leave that connection running. Otherwise copy your local
   `WiFi.private.json` beside the commands if you use one, or answer the normal
   country/SSID/password prompts. Do not upload the private file. A recognized
   existing protected startup profile can still be used automatically.
3. For a Wi-Fi-only test, manually disconnect wired Ethernet/VPNs. Keep Ethernet
   available for recovery. No router rename or another test device is needed.
4. Double-click **`Check-RPi5-WiFi-Readiness.cmd`** and approve Administrator access.
   It runs the comparative test once and creates a readiness ZIP on the Desktop.
5. Share **that one readiness ZIP**; its nested performance ZIP includes the
   timing data and diagnostic report. No separate performance/diagnostic run.

No reinstall or reboot is required for this utility update. It leaves UEFI,
fan, firmware, driver settings, boot security, Windows DNS/routes and startup
task/profile configuration unchanged. A healthy connection is preserved. The
existing connector is used only if a connection is needed or you explicitly
request `-VerifyReconnect` with a private profile and an already established link.
Do not enable/change autoconnect just to collect this measurement.

## What is new

`download-samples.csv` retains the original outcome, bytes, total duration and
relative start/end columns. It adds cumulative curl timestamps and derived
intervals for DNS, TCP connection, TLS, request preparation, first-byte wait and
body download. `download-timing-summary.json` summarizes valid observations;
unknown, failed or malformed observations are not counted as zero delay.

The test still uses separate sequential one-MiB HTTPS requests to the same
endpoint, up to 128 requests or 90 seconds. URL, request strategy, byte limits,
timeouts, TLS verification and stop-on-denial rules are unchanged. The initial
probe still brings the maximum payload to 129 MiB. No extra network requests are
added. Additional metadata parsing/export has small unquantified overhead; the
headline goodput still includes it, alongside process and connection overhead.

`RequestStart100ns` / `RequestEnd100ns` bracket the existing curl process call.
The independent sampler similarly adds `SampleStart100ns` / `SampleEnd100ns`.
Both use Windows `QueryInterruptTime`: the same biased, since-boot interrupt
clock domain as the driver's `KeQueryInterruptTime` transport history. Compare
these raw values with `transport-history.json` rows' `Time100ns`; do not subtract
the trace origin twice or align them to UTC. Only same-boot overlapping evidence
can be compared. Missing clock values stay unknown.

The unit is 100 ns, **not 100-ns precision**: this clock is tick-granular. Process
windows include launch/exit overhead and do not identify the exact beginning of
curl's individual phases. Driver history is approximately one-second passive
sampling with a finite ring, not a per-request packet capture; registry counters
are periodic and non-atomic. Overlap is evidence for investigation, not proof
that every request experienced the sampled queue state. Do not suspend/resume
the Pi during the run.

## Interpretation and compatibility

`FirstByteWaitSeconds` is first-byte minus pre-transfer time; it can include
local/network/radio/server delays. `BodySeconds` is total minus first-byte time,
not pure Wi-Fi airtime. None of these fields alone identifies a guilty driver,
router or server. High PHY rates and successful pings do not establish where a
slow HTTPS transfer waited. No automatic cause verdict or reset loop is added.

Phase intervals are derived only for a complete, valid request with nonnegative,
ordered, finite cumulative values matching its total. Failure, missing or invalid
phase output leaves intervals unknown without rewriting download success/failure.

Startup receipts remain compatible with **0.6.27**. The newer measurement version
does not invalidate an existing .27 receipt or update any installed startup code.
Startup/reconnect NotTested still means missing evidence. This short run does not
prove long-term stability or fix the remaining queue/resource errors.

Definitions: [curl write-out timings](https://curl.se/docs/manpage.html#-w),
[Microsoft QueryInterruptTime](https://learn.microsoft.com/en-us/windows/win32/api/realtimeapiset/nf-realtimeapiset-queryinterrupttime).
