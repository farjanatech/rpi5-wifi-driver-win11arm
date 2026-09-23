# exp0.6.26: initial usable release candidate

This is an experimental, test-signed candidate, not a certified or hardware-
validated final driver. It targets practical connection/recovery correctness
and lower host completion overhead without changing the working radio setup.

## Evidence behind the scope

The .24 and .25 Pi runs completed 128/128 one-MiB downloads at 28.96 and
28.99 Mbps respectively (about 37 seconds). This is not a meaningful speed
improvement. .25's idle retry ran only three times; congestion mainly occurred
while the worker was actively receiving. Queue-full counts were 4,537 and 6,010,
and both runs had two NoResources gateway probes. The difference between these
single runs does not establish that .25 caused the increase. Neither proves
long-duration stability. Keep the complete .24 package for fallback.

## Driver change: complete successful sends in small batches

Within the existing transmit pump, up to four fully processed NET_BUFFER_LISTs
may be returned in one NDIS completion call. Every packet still traverses the
original send path; no TCP ACK is discarded/coalesced and no packet is replayed.
This follows Microsoft's documented
[linked-list completion contract](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/ndis/nf-ndis-ndismsendnetbufferlistscomplete).

- Detached successful sends still count against the same 64-retained-frame cap
  until handoff. Their admission slots are released together before the callback.
- Multi-buffer sends complete only after their entire buffer chain is processed.
- Completion happens outside the spinlock, with pause accounting maintained
  during reentrant callbacks; the driver never touches returned NBLs afterward.
- A batch flushes at four completions, the checked two-ms age, or any pump exit,
  including credit block, error, cancellation/pause, empty queue or budget end.
  An in-flight SDIO operation can exceed two ms; there is no added timer/wait.
- Pending cancellation/error paths retain their existing immediate completions.
  Already fully processed sends keep their successful status.

This can reduce host callback/lock overhead and expose several free admission
slots together. It does not create firmware credits or reduce SDIO transfers.
Reduced queue rejection or greater throughput remains a hypothesis for Pi testing.
Diagnostic version 26 adds `TxCompletionBatchCalls`, `TxCompletionBatchNbls`
and `TxCompletionBatchMax`; a one-NBL batch is also included in these counters.

## Usability and validation

The utilities serialize competing connection operations, preserve an already
healthy connection when appropriate, and require authentication plus usable IPv4
and a default route. An explicit reconnect first observes disconnection rather
than trusting a previous authenticated status. There is no endless reset loop.

`Check-RPi5-WiFi-Readiness.cmd` collects startup/current-link evidence and the
existing performance test into one report. An optional explicit reconnect test
is separate from passive startup evidence. Missing evidence is not a pass;
Internet/DNS failure alone is not a reason to reset the radio.

Private profiles remain editable, local and unpublished. Startup connection is
opt-in. This update does not change Windows DNS, routing configuration, UEFI,
fan settings, Secure Boot, Test Signing or the router.

## Unchanged and not claimed complete

The .24 firmware/CLM/NVRAM, real-country handling, WPA2/AES/PMK sequence,
MAC handling, verified band selection, capability-checked 50-MHz SDR with
25-MHz fallback, SDIO transfer engine, fresh flow/credit gates, receive behavior,
64-frame admission and 4-TX / bounded-RX / 4-TX budgets remain. .25's bounded
idle retry is retained. The comparative performance workload is unchanged.

Native Windows Wi-Fi UI, WPA3/enterprise networks, seamless roaming, arbitrary
sleep/resume recovery, certification and unrestricted hardware compatibility
are not part of this initial milestone. A successful short test is not proof
of any of them. Speed is not guaranteed to exceed .24 or .25.

## On the Pi only

1. Extract the complete new ZIP into a new folder. Keep the .24 ZIP.
2. Run `Install-RPi5-WiFi-Driver.cmd`. No blanket driver uninstall is needed.
3. Keep your private profile local; never edit the hashed public example in place.
4. Restart once, then run `Check-RPi5-WiFi-Readiness.cmd`.
5. Share its single readiness ZIP, not the profile. Use normal browsing too;
   report any stall or disconnect rather than repeatedly reinstalling.

For Wi-Fi-only measurement, unplug wired Ethernet and disconnect VPNs manually;
keep Ethernet available for recovery. No extra test device or SSID rename is needed.
