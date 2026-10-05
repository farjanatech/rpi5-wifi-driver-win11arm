# v0.7.1.18 — bounded Service-Burst4 TX experiment

Branch: **`new-improvement-tx-service-burst4`**  
Direct baseline: **v0.7.1.17 / `c9d715760da49ef491b681a1ba5b57e6ef6ef13b`**

## Hardware motivation

v0.7.1.17 restored reliable connection and strong download performance after
rejecting v0.7.1.16's unsafe register-buffer FIFO experiment. The remaining
hardware evidence shows TX queue/backlog saturation and thousands of firmware
credit waits while the transport itself remains error-free.

Existing Service-Burst2 evidence was also clean: reused second-frame transfers
completed without observed service-reuse errors. The next isolated experiment
therefore amortizes fresh F1 status service over a slightly larger, still
strictly bounded real-credit window.

## Exact change

For ordinary (non-glom) TX:

1. A burst always starts with the unchanged fresh F1 transport service.
2. The driver observes the real firmware credit window.
3. If 2–4 frames are pending in this same pump and matching credits exist,
   the first frame may grant 1–3 followers.
4. Before each follower, the driver rechecks:
   - cached global flow state,
   - priority flow state,
   - a real remaining firmware credit.
5. Followers are still independent F2 transfers.
6. BUSY, transport error, mapping failure, lifecycle/cancellation, or pump end
   discards all unused permission. It never carries to a later pump.

A four-frame burst therefore requires four real credits. No credit is invented,
no F2 is replayed, and the sequence counter advances only after successful F2.

## Explicitly unchanged

- v0.7.1.17 fixed-address SDHCI FIFO PIO implementation;
- RX batching/read-ahead/glom behavior;
- firmware, country, connection and radio policy;
- 64-frame active TX queue and 128-frame deferred backlog;
- two-frame host TX Glom2 and its pressure threshold;
- pre/post RX pump budgets and retry policy;
- SDPCM framing;
- frozen all-in-one utility;
- test-signing model.

## CI qualification

Regression tests cover 1/2/3/4-credit windows, sequence wrap, cached flow stop,
failure on a reused F2 with no replay, cancellation invalidating remaining
permission, pump-budget containment, and Glom2 precedence.

## Hardware qualification

Install TX mode 1 first. Verify connection, then compare Fast.com download and
upload against v0.7.1.17 under the same AP, 5 GHz band and placement. Collect
diagnostics after each run. Reject v0.7.1.18 if download/stability regresses or
if upload does not materially improve.
