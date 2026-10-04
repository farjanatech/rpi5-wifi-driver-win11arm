# Two-frame host TX glom candidate 0.7.1.10

Branch: **`new-improvement-tx-glom`**  
Protected hardware baseline: **v0.7.1.9 / `new-improvement` / `0bc5ed12e9438cd82555d7bc8ae77211aa6f236b`**  
Candidate INF version: **0.7.1.10**

## Why this experiment exists

The physical v0.7.1.9 run showed that the bounded pending-NBL backlog solved
most immediate queue-pressure rejection without introducing the observed
disconnect/SDIO/RX failures. The backlog still reached its 128-frame bound,
however, so the next experiment should make the existing queue drain faster
rather than increase that bound.

This branch therefore changes one performance mechanism only: under sustained
TX pressure it may send **two** independent one-frame NBLs in one documented
host-to-device SDPCM glom chain.

It does **not** enlarge the 64-frame active queue or 128-frame backlog, and it
does not change RX, SDIO clock/mode, DMA, interrupts, firmware binaries, country
policy, radio policy, retry timing or the v0.7.1.8/v0.7.1.9 scheduling budgets.

## Protocol basis

The format follows the Linux brcmfmac host-TX-glom implementation:

- firmware negotiation uses `bus:rxglom=1` (firmware bus-direction naming);
- every subframe has the extended SDPCM header;
- the first hardware length covers the complete padded chain;
- each subframe has its own SDPCM sequence;
- the final-frame flag is carried in the extended header;
- firmware credits bound the number of subframes.

The candidate deliberately caps this at **2 frames**, not Linux's larger
possible chains.

## Safety policy

A pair is eligible only when all of the following are true:

1. firmware negotiation explicitly enabled host TX glom;
2. at least two frames remain in the current worker TX budget;
3. the active queue is under pressure (at least 32 retained frames) or a
   deferred backlog exists;
4. the first two queue entries are separate, single-frame NBLs;
5. neither NBL is cancelled/expired and the lifecycle gate is open;
6. cached state already shows two valid firmware credits and no flow stop;
7. the sender performs one **fresh F1 service** immediately before the F2 write
   and still observes two valid credits and allowed flow.

If the fresh F1 gate no longer has room for two frames, no aggregate F2 write is
attempted. The worker immediately falls back to the existing one-frame sender,
so one usable credit is not wasted.

If the one aggregate F2 write itself fails, both constituent NBLs are failed
exactly once and are **not replayed** individually. This avoids duplicate frames
after an indeterminate bus failure.

## Negotiation policy

An explicitly reported firmware `UNSUPPORTED` result disables this experiment
and continues on the unchanged v0.7.1.9 single-frame path.

A timeout, malformed/corrupted reply, BADARG or other ambiguous failure is
terminal for that startup. The driver does not continue when it cannot know
whether firmware accepted the packet-format change.

## Diagnostics

DiagVersion **38** adds:

- `TxGlomRequested`
- `TxGlomEnabled`
- `TxGlomConfigStatus`
- `TxGlomAttempts`
- `TxGlomChains`
- `TxGlomFrames`
- `TxGlomBusyFallbacks`
- `TxGlomErrors`
- `TxGlomPayloadBytes`
- `TxGlomPaddedBytes`

All v0.7.1.9 backlog, lifecycle, transport and stability counters remain intact.

## Required hardware validation

Use the same Pi, UEFI, router/AP, band/channel, placement and Windows power
configuration as the v0.7.1.9 test.

First verify after installation:

- loaded driver version is 0.7.1.10;
- `TxGlomRequested=1`;
- ideally `TxGlomEnabled=1`; an explicit unsupported fallback is valid but
  then the run is effectively v0.7.1.9 behavior;
- `TxGlomErrors=0`.

Then run the same performance/readiness/diagnostic workload and compare against
the retained v0.7.1.9 package. A clean candidate must have:

- zero unexpected disconnects;
- zero worker failures;
- zero FIFO block failures and `FifoTransportFailed=0`;
- zero RX glom errors;
- zero CMD53 timeouts;
- zero interrupt-storm fallback;
- no material latency regression;
- lower/equal queue/backlog rejection under matched load;
- advancing `TxGlomChains` / `TxGlomFrames` if negotiation enabled.

Do not judge throughput from a single best run. Alternate matched v0.7.1.9 and
v0.7.1.10 runs and compare the distribution/spread. If the two-frame candidate
is unstable or slower beyond ordinary variation, roll back immediately.

## Rollback and next step

The rollback target is the exact successful **v0.7.1.9 `new-improvement`**
package. Keep it offline before installing this candidate.

Do **not** test four-frame aggregation yet. Four-frame glom is considered only
after two-frame hardware evidence shows stable behavior, reduced pressure and no
meaningful latency/throughput regression.
