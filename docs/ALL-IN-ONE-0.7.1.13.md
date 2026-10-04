# v0.7.1.13 — consolidated connect + measurement baseline

Branch: **`new-improvement-all-in-one`**  
Runtime baseline: **v0.7.1.11 / `6ae93623c8767eda050b8c408250d3ec3ce19bfb`**  
Rejected performance experiment: **v0.7.1.12 threshold-16**  
Architecture rollback: **v0.7.1.9**

## Rule for this version

v0.7.1.13 does **not** tune the driver.

CI compares every file under `src/` byte-for-byte against the green
v0.7.1.11 baseline and fails if any production driver source changes. The INF
and installer version are bumped only so Windows/package evidence can identify
this consolidated build.

The reason is simple: the threshold-16 v0.7.1.12 experiment reduced real-world
performance and the old test workflow did not measure upload Mbps directly.
No further TX scheduler, queue, glom, SDIO, firmware or radio change will be
made without a measured reason.

## One user utility

The package exposes one testing entry point:

**`RPi5-WiFi-AllInOne.cmd`**

It offers:

1. Connect / reconnect Wi-Fi.
2. Show connection status.
3. **Connect + full download/upload test + final diagnostics**.
4. Diagnostics only.

The generated PowerShell utility embeds the separately unit-tested helper
sources into one payload. They are extracted to a random temporary directory
only for the duration of the operation and then removed. The package root does
not expose the historical list of individual tester scripts.

## Full test output

The recommended Full Test produces one outer ZIP:

`RPI5-WIFI-ALL-IN-ONE-YYYYMMDD-HHMMSS.zip`

It contains:

- connection output;
- the existing bounded download/performance evidence;
- a dedicated upload benchmark;
- upload samples and summary;
- upload-only driver counter deltas;
- one final diagnostics snapshot;
- a short all-in-one summary.

The embedded download test is told to defer its own diagnostics so the
all-in-one workflow collects a single final diagnostic set after upload.

## Upload measurement

Upload uses Cloudflare's public Speed Test upload endpoint:

`https://speed.cloudflare.com/__up`

The test sends **4 × 4 MiB** synthetic zero-filled payloads. Curl is explicitly
bound to the CYW43455 IPv4 address and the utility refuses the measurement if
another IPv4 default route is active.

The report records:

- each upload sample Mbps;
- median / average / minimum / maximum upload Mbps;
- adapter transmitted-byte delta;
- upload-only deltas for:
  - `TxPackets`
  - `TxQueueFull`
  - `TxBacklogFull`
  - `TxBacklogAccepted`
  - `TxBacklogPromoted`
  - `TxGlomChains`
  - `TxGlomFrames`
  - `TxGlomErrors`
  - `TxCreditWaits`
  - `Cmd53Timeouts`
  - `WorkerFailureCount`
  - `DisconnectCount`

Cloudflare receives the public IP used for the test, as with its normal public
speed-test service. No Wi-Fi password is written to the result ZIP.

## What happens next

Do not make another driver performance build from a threshold guess.

First collect repeated v0.7.1.13 all-in-one results. Use the download result,
upload result and upload-only driver deltas to decide whether the next
bottleneck is firmware credits, host queue pressure, per-frame F1 overhead,
F2 transfer efficiency, or something outside the driver.

Only then create a new isolated performance branch with one measured change.
