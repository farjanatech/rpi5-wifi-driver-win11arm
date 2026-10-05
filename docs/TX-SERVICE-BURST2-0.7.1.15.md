# v0.7.1.15 — bounded two-credit TX service burst

Branch: **`new-improvement-tx-service-burst2`**  
Immediate rollback: **v0.7.1.11 / `6ae93623c8767eda050b8c408250d3ec3ce19bfb`**  
Architecture rollback: **v0.7.1.9 / `0bc5ed12e9438cd82555d7bc8ae77211aa6f236b`**

## Why this experiment exists

Hardware evidence from the v0.7.1.14 measurement run showed:

- sustained download around 77–82 Mbps;
- real-world upload around 37–40 Mbps;
- zero disconnect/CMD53/FIFO/glom failures in the stable runtime;
- very high `TransportTxStatusChecks` relative to transmitted packets;
- in the measured download stages, successful two-frame glom chains reduced
  exactly one status check per chain.

The strongest next hypothesis is therefore **per-frame host TX service/status
overhead**, not queue capacity, RF, RX throughput or SDIO clock rate.

## Exact runtime change

The existing single-frame path does:

1. fresh F1 transport service/status read;
2. verify flow state;
3. verify a real firmware credit;
4. one ordinary F2 transfer.

v0.7.1.15 keeps that sequence for the first frame.

If, and only if, that same fresh F1 service proves that:

- lifecycle remains runnable;
- global/priority flow control is clear;
- the current TX sequence has a real credit; and
- the following TX sequence also has a real credit,

then the current TX pump receives a **single-use grant** for one following
ordinary data frame.

That second frame:

- remains an independent F2 transfer;
- rechecks cached global/priority flow state immediately before F2;
- rechecks that a real remaining firmware credit exists;
- performs **no second F1/status service**;
- consumes the grant whether it succeeds or fails;
- is never replayed after an F2 error.

Maximum burst size is therefore exactly **2 ordinary F2 frames per fresh F1**.

## Grant lifetime

The grant is a local variable inside one `CywTxPump()` invocation.

It cannot survive:

- the end of the current pump;
- a later worker iteration;
- an RX cycle;
- a control/firmware command;
- pause/restart;
- D-state transition;
- disconnect/reconnect;
- worker restart.

A one-frame pump budget cannot create a grant that escapes into the next pump.

## Interaction with existing two-frame glom

The proven pressure-only two-frame glom remains unchanged and keeps precedence.

- glom threshold remains **32 retained active frames**, or immediate when a
  deferred backlog already exists;
- glom cap remains exactly **2 frames**;
- active queue remains **64 frames**;
- deferred backlog remains **128 frames**.

If a service-reuse grant is already active, the next ordinary frame consumes
that grant instead of starting a new glom decision. After that one frame, normal
glom selection resumes.

This avoids stacking two different amortization mechanisms on the same fresh
service state.

## Unchanged areas

v0.7.1.15 does **not** change:

- negotiated v0.7.1.11 extended SDPCM framing;
- `bus:rxglom` negotiation;
- firmware/control framing;
- RX batching or RX glom;
- SDIO clock, bus width or voltage;
- CMD52/CMD53 implementation;
- 64-frame active queue;
- 128-frame backlog;
- backlog expiry/cancel semantics;
- two-frame glom protocol or threshold;
- country/radio/power policy;
- interrupt policy;
- NDIS completion ownership;
- fast-credit retry timing.

## New diagnostics

DiagVersion **41** publishes:

- `TxGlomPressureThreshold=32`
- `TxServiceBurstEnabled=1`
- `TxServiceBurstMax=2`
- `TxServiceBurstGrants`
- `TxServiceBurstSecondAttempts`
- `TxServiceBurstSecondSuccess`
- `TxServiceBurstSecondBusy`
- `TxServiceBurstSecondErrors`
- `TxServiceBurstSavedStatusChecks`

The stale threshold-16 registry value from the rejected v0.7.1.12 experiment is
therefore overwritten with the actual current threshold, 32.

## Qualification target

Do not use the abandoned sustained-test workflow as the primary benchmark.

Use the same AP, 5 GHz band, placement and Fast.com procedure used for the
roughly 82 Mbps download / 40 Mbps upload reference.

A good result should show:

- upload materially above the ~40 Mbps baseline;
- download not materially worse;
- `TxServiceBurstSecondSuccess` and
  `TxServiceBurstSavedStatusChecks` increasing;
- `TransportTxStatusChecks` growing more slowly relative to `TxPackets`;
- no increase in disconnects, worker failures, CMD53 timeouts, FIFO failures,
  TX-glom errors or RX-glom errors;
- queue/backlog pressure no worse.

If throughput does not improve, or any stability signal regresses, reject the
experiment and return to v0.7.1.11.
