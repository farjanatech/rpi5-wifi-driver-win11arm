# v0.7.1.19 — Adaptive TX Hybrid

Branch: **`new-improvement-adaptive-tx-hybrid`**  
Direct baseline: **v0.7.1.18 / `50e5b5d9e4456c93b0504e07c8756504284e8a7c`**  
Upload behavior reference: **main v0.7.1.8 / `d5d61aa0c2d864162615589fc93171252c5a6305`**

## Goal

Hardware testing showed that v0.7.1.18 retained strong download and stability,
while the user observed materially better upload on main v0.7.1.8. Rather than
globally disabling newer TX features, this experiment combines both behaviors
according to Ethernet frame size.

## Policy

**Small path: Ethernet frame length <= 512 bytes**

- normal 64-frame active queue;
- may use the existing 128-frame deferred backlog;
- may use Glom2 under the existing pressure/credit gates;
- may use Service-Burst4 under real-credit/flow gates.

This path is intended for ACK/control and other short traffic where transaction
amortization can help download.

**Bulk path: Ethernet frame length > 512 bytes**

- active queue only; never deferred to the 128-frame backlog;
- never eligible for Glom2;
- never eligible for Service-Burst4 follower reuse;
- each frame starts with its own fresh F1 transport service before F2.

This intentionally recreates main-style pacing for sustained upload without
discarding the newer small-packet optimizations.

## Safety

- The 512-byte threshold is inclusive on the small path; 513 bytes is bulk.
- Multi-NB NBL classification uses its largest constituent Ethernet frame.
- A transport-level safety gate also refuses Burst4 follower grants for bulk.
- Existing credit, flow, cancellation, no-replay and lifecycle rules remain.
- v0.7.1.18 RX, fixed-port SDIO, connection, firmware/radio, pressure pump,
  retry policy and post-RX scheduling are byte-for-byte protected by CI.

## Diagnostics

DiagVersion 44 adds:

- TxAdaptiveHybridEnabled
- TxAdaptiveSmallMax
- TxAdaptiveSmallFrames
- TxAdaptiveBulkFrames
- TxAdaptiveSmallBacklogAccepted
- TxAdaptiveBulkBackpressure
- TxAdaptiveSmallGlomChains
- TxAdaptiveSmallBurstStarts
- TxAdaptiveBulkFreshF1Attempts

## Qualification

Install TX mode 1 first. Under the same AP, 5 GHz band and physical placement,
compare both Fast.com download and upload against v0.7.1.18 and the remembered
main behavior. Collect diagnostics immediately afterward. A successful result
should retain v0.7.1.18-class download/stability while recovering upload.
