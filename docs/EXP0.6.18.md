# exp0.6.18: restore the 64-frame limit and retain radio diagnostics

## Evidence and exact scope

The .17 run confirmed driver 0.6.17.0, DiagVersion 17 and a 128-frame cap.
Its pre-load radio GET snapshot reported 2.4 GHz/channel 6, -41 dBm, PM off
and MPC off. All four readbacks succeeded; no SDIO timeout/checksum error was
recorded. Country, firmware and UEFI did not fail.

However, sustained throughput fell from .16's 18.55 Mbps to 9.00 Mbps. .17
completed 96/98 attempted downloads (two timeouts), hit the 90-second limit,
lost 5/77 router probes and averaged 82.5 ms for successful loaded probes.
The maximum recorded queue delay was 1,875 ms versus .16's 74 ms. Zero
queue-full rejects did not represent a successful performance improvement.
These separate runs do not isolate all environmental factors or prove causality.

This release makes **one runtime behavior change versus .17: 128 -> 64 frames**.
It restores .16's cap while retaining the same completion-accounting fix and
send/receive scheduling. Radio GET code, connection tools, private-profile
handling and performance workload are byte-for-byte unchanged from .17.
Metadata/installer/collector versions and documentation are updated.

There is no forced 5 GHz band, faster SDIO clock, new queue policy, extra
background polling, firmware change or UEFI/fan change. Genuine queue-full
rejections may recur. Speed recovery is a hypothesis requiring a Pi test.

## On the Pi

1. Keep exp0.6.16 as the known tested fallback. Extract the .18 ZIP into a new folder.
2. Run `Install-RPi5-WiFi-Driver.cmd`, approve elevation, then restart once.
   Normal version upgrade from .17; no preliminary uninstall is needed.
3. Copy your existing `WiFi.private.json` beside the utilities if using it.
   Never edit the hashed example or share the private file.
4. Leave the router/SSID unchanged for the first comparison. Unplug Ethernet,
   disconnect VPNs and close unrelated downloads. Run `Test-RPi5-WiFi-Performance.cmd`.
5. Send the resulting desktop `RPI5-WIFI-PERFORMANCE-*.zip`. It includes radio
   information and diagnostics; no separate command is required.

The performance tool still identifies itself as **0.6.17** because its code
and workload are unchanged. The driver/installer/collector identify as .18,
with DiagVersion 18 and TxQueueLimit 64. This is intentional, not a stale install.

After establishing this baseline, test a separately named 5 GHz SSID and update
only the SSID in the private profile (and password if different). The report
must confirm the actual band; sharing an SSID never proves a 5 GHz connection.
That is a separate comparison, not an automatically performed router change.

Same workload: up to 90 seconds or 128 one-MiB requests; at most 129 MiB payload
plus protocol overhead. No automatic uploads. Judge completed transfers, speed,
loss/latency and queue delay together. If worse than .16, use Device Manager to
select/roll back to its retained driver and restart. Do not reinstall Windows.

## Verification

CI pins both .16 and .17 source baselines: .16 scheduling/ownership/cap and
.17 radio/utility identity. It exercises actual queue/lifecycle code, radio
GET protocol/fault tests and PowerShell utilities, then builds/test-signs ARM64.
This is an experimental test-signed candidate, not hardware validation or
Microsoft production signing.
