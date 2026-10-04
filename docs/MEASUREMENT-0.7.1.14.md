# v0.7.1.14 — sustained TX/RX measurement baseline

Branch: **`new-improvement-upload-measurement`**  
Runtime baseline: **v0.7.1.11 / `6ae93623c8767eda050b8c408250d3ec3ce19bfb`**

## Purpose

v0.7.1.14 changes **measurement only**. Every production driver source file
under `src/` is byte-for-byte the green v0.7.1.11 runtime. No TX threshold,
queue, glom, SDIO, firmware, radio, interrupt or lifecycle policy changes are
included.

The goal is to determine why real upload throughput appears to plateau around
40 Mbps before attempting another driver optimization.

## One all-in-one utility

The package exposes:

**`RPi5-WiFi-AllInOne.cmd`**

The recommended Full Test now runs:

1. connect / verify the CYW43455 path;
2. sustained download with **1, 2 and 4 concurrent streams**;
3. sustained upload with **1, 2 and 4 concurrent streams**;
4. one final diagnostics collection;
5. one outer ZIP for analysis.

The old 128 × 1 MiB request workload is no longer the headline speed test.

## Sustained download

Each download stage transfers a constant total of **64 MiB**:

- 1 stream × 64 MiB;
- 2 streams × 32 MiB;
- 4 streams × 16 MiB.

This shows whether receive throughput benefits from concurrency without changing
the total payload per stage.

## Sustained upload

Each upload stream sends **16 MiB**:

- 1 stream × 16 MiB;
- 2 streams × 16 MiB = 32 MiB total;
- 4 streams × 16 MiB = 64 MiB total.

The tool reports aggregate stage Mbps and the 4-stream/1-stream scaling ratio.

## Fresh driver snapshots around every stage

The v0.7.1.13 upload test could finish before the driver's periodic registry
snapshot advanced, which made upload-only driver deltas appear as zero.

v0.7.1.14 fixes that without modifying the driver. Before and after every
download/upload stage, the all-in-one tool issues the existing **read-only radio
query**. That query causes the worker to publish a fresh diagnostic snapshot.
The tool verifies that `SnapshotTimeUtc` advanced before accepting the
snapshot.

Because the boundary radio query itself uses a small amount of control traffic,
driver deltas include a small, explicitly documented boundary-query overhead.
For multi-megabyte stages this is negligible compared with the measured data
traffic.

## Stage-specific evidence

Every stage stores before/after values and monotonic deltas for the important
TX/transport counters, including:

- `TxPackets` / `RxPackets`;
- `TxQueueFull`;
- `TxBacklogAccepted`, `TxBacklogPromoted`, `TxBacklogFull`;
- `TxGlomAttempts`, `TxGlomChains`, `TxGlomFrames`;
- `TxGlomBusyFallbacks`, `TxGlomErrors`;
- `TxGlomPayloadBytes`, `TxGlomPaddedBytes`;
- `TxCreditWaits`;
- `TxPressurePasses`, `TxPressureFrames`, deadline yields;
- `TransportStatusReads`, `TransportStatusAcks`,
  `TransportTxStatusChecks`, mailbox reads and service errors;
- `Cmd53ReadCount`, `Cmd53WriteCount`;
- FIFO block commands/bytes/failures;
- worker failures, disconnects, CMD53 timeouts and RX-glom errors.

Each stage also records adapter transmitted/received byte deltas, individual
curl transfer timing, aggregate stage Mbps and per-stream payload timing.

## What we will infer from the results

- **1 stream low, 2/4 streams scale strongly:** likely per-flow/server/TCP
  limitation rather than a hard driver cap.
- **1/2/4 streams stay near the same ~40 Mbps aggregate while
  `TxCreditWaits` dominates:** firmware-credit/service scheduling is the
  likely next target.
- **Aggregate remains capped while `TransportTxStatusChecks` and F1 activity
  rise almost one-for-one with TX frames:** host status-service overhead is a
  likely target.
- **Credits are available but CMD53 write counts/transfer time dominate:** the
  host PIO/F2 write path becomes the next investigation.
- **Backlog/queue-full rises sharply with concurrency:** drain scheduling is
  still insufficient, but queue capacity should not be increased blindly.

No next driver build should be created until these measurements identify which
case actually occurs on the Pi.
