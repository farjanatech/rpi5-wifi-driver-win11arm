# exp0.6.15: bounded transmit bursts and earlier credit service

## Evidence and scope

On exp0.6.14, the 2026-09-21 23:32 performance capture completed all 128 small
downloads: 128 MiB in 73.25 seconds, 14.66 Mbps including setup overhead.
Four of 70 gateway probes returned `NoResources`. The periodic before/after
snapshots showed 8,808 additional queue-full rejections and the same increase
in the old aggregate TxErrors counter. Runtime SDIO waits/timeouts and checksum
errors did not increase. These counters include background traffic and are not
atomic. They identify an admission problem, not proof of every pause's cause.

## Changes

1. Return an admission slot before the NDIS send-completion callback can
   synchronously submit replacement work. Track callbacks in progress separately
   so pause still sees outstanding work until ownership handoff finishes.
2. Retain up to 256 pending frames/NBLs, rather than 64. Fixed metadata storage;
   at most 387,584 bytes of Ethernet payload in admitted NB chains (underlying
   protocol allocations/metadata may be larger). No packet-time allocation,
   unbounded overflow queue or early success completion. Whole multi-NB chains
   stay charged until their NBL completes. Actual exhaustion still returns a
   resource error. This burst allowance is NOT the firmware's credit window.
3. Offer two sends after each complete received frame, using newly received
   credits sooner. Preserve the four-frame/2ms receive budget and four-frame
   transmit opportunities at the cycle boundaries. Stop/pause/bus faults stop
   the cycle; no new bus thread, FIFO format or credit arithmetic.

DiagVersion=15 adds `TxQueueLimit`, `TxQueueFrames`, `TxBurstAdmissions`,
`TxOversizedNbl` and `TxInterleavedPackets`. Queue depth remains a periodic
snapshot, not an instantaneous history. `TxBurstAdmissions` counts NBLs admitted
when retained frames exceed 64; it is not a speed or prevented-loss measurement.
No UEFI/fan, firmware/CLM/NVRAM, country, MAC, bus clock, DMA or IRQ change.

The queue follows NDIS pending-send ownership and completion rules:
[Microsoft MiniportSendNetBufferLists](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/ndis/nc-ndis-miniport_send_net_buffer_lists).
The admission limit remains a safety bound, not a guarantee that all bursts fit.
Increasing retained work can increase queue latency; that is a release criterion,
not something to hide by only reporting fewer rejects.

## Tests and hardware acceptance

GitHub tests compile the actual queue and scheduling helpers. Cases cover a
128-frame burst, exhaustion at the real limit, multi-NB retained-byte accounting,
full-queue reentrant completion, no completion before bus transfer, no-credit
retry, cancellation, timeout, pause/power-down, mapping/bus failures, callback
ownership poisoning, per-RX credit service and scheduler fairness/stop paths.
These simulations and a WDK ARM64 build cannot certify physical speed/stability.

1. Keep the exp0.6.14 package for rollback. Extract the entire exp0.6.15 ZIP into
   a new folder on the Pi; do not edit hashed `WiFi.config.example.json`.
2. Run `Install-RPi5-WiFi-Driver.cmd`, approve elevation, and restart once.
   Do not install on the development PC. Existing UEFI/Test Signing is unchanged.
3. Optionally copy your existing `WiFi.private.json` beside the new tools. Do not
   publish it. The public package contains only a blank example.
4. Disconnect wired Ethernet/VPNs, close unrelated downloads, and run
   `Test-RPi5-WiFi-Performance.cmd`. No separate diagnostics command is needed.
   The workload is unchanged: up to 90 seconds/128 one-MiB requests; up to
   129 MiB total requested payload plus protocol overhead. Reports stay local.
5. Share its desktop ZIP and say whether Task Manager's Ethernet 2 graph moves.

Compare effective Mbps, completion/failure counts, `NoResources`, queue-full
delta, maximum queue delay, credit waits and driver/Windows traffic. Do not call
this a win if fewer rejects come with worse stalls/latency. If the candidate
regresses, reinstall the retained exp0.6.14 package and reboot. No UEFI rollback
or Windows reinstallation is needed for this driver-only comparison.
