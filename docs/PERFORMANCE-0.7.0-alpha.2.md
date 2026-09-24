# Performance alpha.2: bounded queue-pressure relief

Driver version **0.7.0.1**, isolated branch `feature/perf-alpha2-queue-pressure`.
The user-reported best build, **driver-perf0.7.0-alpha.1**, remains at
`6e652fb86aef595cf6f6fbf6f05c1769d5673055`. Its branch, release, ZIP and tag must
not be overwritten. Stable driver exp0.6.29.1, connector exp0.6.29.2 and UEFI
are also unchanged. This candidate is optional and does not claim a new speed
record or completion of hardware validation.

## Evidence and the narrow change

The September 24 alpha.1 hardware report completed all 128 HTTPS requests.
5 GHz RSSI was -48 dBm before/after; all 60 under-load gateway probes succeeded
(11.3 ms mean, 31 ms maximum). Block transfers and receive aggregation were
active without recorded transfer/aggregation failures. During the reporting
interval the driver recorded 3,589 full-queue rejections and 17,295 blocked
send attempts. These counters do not prove the cause of every delay.

Normal scheduling stays at four TX frames, up to four RX frames/two ms,
then four TX frames. The **post-RX** pump alone can now extend its four-frame
pass by at most four frames when all of these conditions hold:

- The normal four sends succeeded and at least 32 retained frames remain.
- The link is published/authorized, lifecycle gates are open, the FIFO is
  healthy, and valid firmware credits are available.
- Firmware global/priority flow control permits transmission.

The extension stops below 16 retained frames, on unavailable credits/flow,
pause/stop/disconnect, a busy/error result, or after its two-ms between-frame
budget. An in-flight transfer/completion cannot be preempted by that budget;
existing SDIO deadlines still apply. Every frame still uses the original
fresh F1 status check and original send/completion code.

The **64-frame admission cap is unchanged**. No packet is coalesced, reordered,
replayed, or completed early. There is no per-received-frame TX scheduling.
No timer-resolution, firmware, power, radio, UEFI or SDIO timing change is made.
The extension can help only when credits exist; it cannot create credits or
guarantee zero queue rejections. This is a tested scheduling hypothesis, not a
claim that the hardware bottleneck has been conclusively fixed.

New cumulative diagnostic DWORDs (DiagVersion 31):

- `TxPressurePasses`: extensions selected after the normal four sends.
- `TxPressureFrames`: frames actually transferred in extensions.
- `TxPressureDeadlineYields`: extensions stopped by elapsed/backward time.

Use these with `TxQueueFull`, `TxQueueMaxDelayMs`, `TxCreditWaits`, load latency,
and throughput. A rising `TxPressureFrames` proves use, not a speed improvement.

## Firmware receive-error evidence

Alpha.1's raw `FirmwareRxBad` increased by 54,016. The five-word
`BRCMF_C_GET_PKTCNTS` layout matches Linux's `brcmf_pktcnt_le`; Linux describes
this field only as failed RX packets. It does not provide a reason breakdown
or establish how these failures map to this particular HTTPS workload.
The report had zero SDIO/aggregate parsing errors, no new host TCP retransmits,
and no failed HTTP requests. None of those facts rules out radio-side issues.

Do not label this count as SDIO corruption, TCP loss percentage, or a proven
firmware defect. Existing optional before/after radio snapshots retain it for
comparison without adding firmware queries inside the packet loop. A firmware
swap or RF setting change is **not justified by this evidence alone**.

Reference: Raspberry Pi Linux `fwil_types.h`, `brcmf_pktcnt_le`:
https://github.com/raspberrypi/linux/blob/8e8c07957368233228a9af82b9e99209653ef1e7/drivers/net/wireless/broadcom/brcm80211/brcmfmac/fwil_types.h

## Verification and limits

GitHub tests execute the production pressure wrapper and original TX queue
with mocked NDIS/transport, plus the production eligibility gate. Coverage
includes all 65,536 modulo-256 credit windows, pressure thresholds, no-credit
and low-pressure baseline behavior, deadline/backward-clock handling, busy and
failed transfers, flow changes, cancellation/pause, completion reentry and
poisoned returned NBL ownership. AddressSanitizer covers these paths as well as
the existing block/aggregation paths. A scope test enforces byte-identical
alpha.1 SDIO, aggregation, queue ownership, auth, band, firmware and utilities.

CI cannot measure real Pi throughput, reboot autoconnect, router-loss recovery
or long-duration reliability. The previous readiness report marked startup
and reconnect **NotTested**, not passed or broken. Keep those as separate
hardware validation requirements before promoting any candidate to stable.
Hardware interrupts, DMA and DDR50 remain deferred.

## Install, compare and recover

1. Keep the complete alpha.1 package; do not uninstall it or delete old drivers.
2. Extract the complete alpha.2 ZIP and run `Install-RPi5-WiFi-Driver.cmd` on
   the Pi. Save work and restart if requested. This installs test-signed
   0.7.0.1 using the same signing/platform safeguards as alpha.1.
3. Connect with the existing connector EXE/profile. No UEFI or router change.
4. Use the same real-world speed check and run `Check-RPi5-WiFi-Readiness.cmd`
   once. The resulting ZIP includes performance and diagnostics. A sequential
   1-MiB HTTPS workload is not the same metric as a sustained browser speed test.

Retain alpha.1 as the preferred build if speed or reliability regresses.
Use Device Manager's **Roll Back Driver** for the CYW43455 adapter if available.
Otherwise use **Update driver > Browse > Let me pick > Have Disk** with the
saved alpha.1 `rpi5cyw.inf` (version 0.7.0.0). Simply running an older installer
may not override Windows' newer-driver ranking. Do not remove the separate
RP1 wired-Ethernet driver, change boot security, or reinstall Windows.
