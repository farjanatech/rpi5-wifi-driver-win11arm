# TX backlog candidate 0.7.1.9 — new-improvement

Branch: **`new-improvement`**  
Protected baseline: **v0.7.1.8 main / `d5d61aa0c2d864162615589fc93171252c5a6305`**  
Candidate INF version: **0.7.1.9**

## Purpose

v0.7.1.8 improved the firmware-credit wake path, but sustained traffic can
still fill the proven 64-frame active TX queue. Previously, a temporary full
active queue caused a new NBL to complete immediately with
`NDIS_STATUS_RESOURCES`.

0.7.1.9 isolates one ownership/backpressure change: keep the exact 64-frame
active transport window, but retain additional NDIS-owned sends in a separate
bounded software backlog until active capacity becomes available.

This is not TX glom, a larger firmware-credit window, a larger active queue,
DMA, DDR50, RX tuning, firmware replacement or a radio-policy change.

## Design

- Active SDPCM/SDIO queue remains **64 frames**.
- Deferred backlog is bounded to **128 frames**.
- Each individual NBL is still limited to at most 64 Ethernet frames, so every
  accepted backlog NBL can eventually fit in the active window.
- New NBLs cannot overtake already-backlogged NBLs.
- At most one backlog NBL is promoted per ownership release; the normal worker
  pump then continues using the existing v0.7.1.8 frame budgets.
- Promotion preserves the original submission timestamp, cancellation ID,
  frame/byte charge and NBL/NB ownership.
- Backlogged NBLs participate in Pause/D3/Halt outstanding accounting.
- Cancellation and 30-second expiry apply while an NBL is still backlogged.
- Flush paths complete active and backlog ownership exactly once.
- Final `TxQueueFull` increments only when both the active window and bounded
  backlog cannot retain the new NBL. Merely using the backlog is not counted as
  a rejected Windows send.

## Diagnostics

DiagVersion 37 adds:

- `TxBacklogLimit`
- `TxBacklogCurrent` — current deferred frames
- `TxBacklogNblCurrent`
- `TxBacklogHighWater`
- `TxBacklogAccepted`
- `TxBacklogPromoted`
- `TxBacklogFull`
- `TxBacklogExpired`
- `TxBacklogCancelled`
- `TxBacklogMaxDelayMs`

Existing `TxQueueHighWater`, `TxQueueFull`, `TxQueueFrames`,
`TxQueueMaxDelayMs`, `TxCreditWaits`, lifecycle counters and transport
fault counters remain available.

## Protected v0.7.1.8 behavior

The branch scope guard pins v0.7.1.8 and rejects unrelated source changes.
In particular, this candidate does not change:

- 64-frame active TX limit
- mode-1 early post-RX dispatch
- bounded four-fast-wake credit retry policy
- high-pressure four-frame extension/deadline
- RX batching/read-ahead/glom
- SDIO CMD52/CMD53 PIO and 50 MHz/4-bit policy
- interrupt masking/rearm/fallback
- firmware, CLM, NVRAM, country or band policy

## Host/CI validation

`tests/tx_backlog_tests.c` first runs the original queue ownership suite and
then validates:

1. 64 active + 128 deferred frames remain bounded.
2. The 193rd single-frame NBL is rejected rather than growing memory without
   bound.
3. FIFO promotion occurs before newer submissions can overtake deferred work.
4. Cancellation completes backlog NBLs exactly once.
5. Backlog expiry uses the existing 30-second ownership lifetime.
6. Pause/lifecycle flush drains both ownership classes.
7. A multi-frame deferred NBL is promoted only when enough active frame
   capacity exists.
8. ASAN exercises the same ownership paths.

Both TX scheduling modes still build in CI. Mode 1 remains the package default.

## Raspberry Pi validation gate

Do not merge this candidate to main from CI alone. On the Pi, compare directly
against the exact v0.7.1.8 package using the same UEFI, router/AP, band/channel
and placement.

Required clean workload evidence:

- workload completes
- unexpected disconnect delta = 0
- worker failure delta = 0
- FIFO block failure delta = 0
- `FifoTransportFailed=0`
- RX glom error delta = 0
- CMD53 timeout delta = 0
- interrupt storm fallback delta = 0
- `TxBacklogFull=0` for ordinary sustained tests
- no material latency regression
- queue/backlog maximum delay remains bounded

Run upload, download, mixed traffic, long idle, Pause/Restart, adapter
disable/enable, D0/D3 where available, and AP loss/reappearance. Keep v0.7.1.8
as the rollback package.

## Next experiment

Only after 0.7.1.9 proves stable should a separate branch test small host TX
aggregation (2 frames, then 4 frames). Do not combine aggregation with this
backpressure candidate.
