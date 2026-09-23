# exp0.6.27: restore immediate send completions

Experimental, test-signed Windows 11 ARM64 candidate for the Raspberry Pi 5.
This is a focused performance-regression correction, not a certified final
driver or a claim of measured speed on this new build.

## Hardware evidence and decision

| Candidate | HTTPS goodput | Completed one-MiB downloads | Queue rejects | NoResources load probes |
| --- | ---: | ---: | ---: | ---: |
| .24 | 28.96 Mbps | 128/128 | 4,537 | 2 |
| .25 | 28.99 Mbps | 128/128 | 6,010 | 2 |
| .26 | 17.74 Mbps | 128/128 | 5,021 | 2 |

The .26 run took 60.52 seconds versus .25's 37.04 seconds: about 39% lower
goodput. The extra time was inside HTTPS transfers, not readiness-tool overhead.
The same 5-GHz BSSID and verified four-bit 50-MHz bus were retained; there were
no recorded SDIO timeouts or firmware-halt errors. Radio retry counts also
changed, so this single-run comparison does not isolate the entire cause.

The .26 batching change completed 44,423 successful NBLs in 21,081 callbacks.
That reduced callback count but held admission capacity until batch handoff.
When the pending queue became empty, reentrant replacement sends could miss
the unused budget of that transmit pump and wait until the next pump. This
does not by itself trigger the worker's idle sleep. It is a plausible pacing
regression, not proof of the entire measured slowdown.

## Exact scope

- Restore the complete .25 transmit queue implementation: each fully processed
  send is returned immediately, outside the lock, before advancing the pump.
  A multi-buffer send still completes only after all its buffers are processed.
- Remove the unused batching helper. No ACK filtering, extra threads, larger
  queue, packet replay or new timing policy is introduced.
- Preserve all .25 CYW43455/SDIO code, including bounded idle retry, verified
  band selection, firmware/CLM/NVRAM, country/security, MAC handling, four-bit
  50-MHz SDR with checked 25-MHz fallback, transport gates and receive handling.
- Preserve .26's installer, optional startup-code refresh, connection locking,
  bounded reconnect and combined readiness reporting, apart from version labels.
- Keep the same 64-frame limit, 4-TX / bounded-RX / 4-TX worker budgets, optimized
  Release build and comparative 128-request/90-second performance workload.

Diagnostic version is 27 and the NDIS vendor version is 0x0006001b. The three
`TxCompletionBatch*` diagnostic fields remain for compatibility but must stay
zero: this build does not batch completions. The new scope gate compares the
packet-path source against .25; production queue tests exercise immediate
ownership handoff, reentrant sends, cancellation, errors and lifecycle boundaries.

## On the Pi only

1. Extract the complete ZIP into a new folder. Keep the complete .24 fallback.
2. If using an editable package profile, copy only your `WiFi.private.json`
   into that folder. Do not edit the hashed `WiFi.config.example.json` in place
   and never upload your private profile.
3. Run `Install-RPi5-WiFi-Driver.cmd`, then restart once. No blanket uninstall
   is needed. UEFI, fan, boot security and Windows network settings are unchanged.
4. Run `Check-RPi5-WiFi-Readiness.cmd`. It connects if needed and runs the
   comparative measurement once. Share its single readiness ZIP; it already
   contains performance and diagnostics. No separate performance run is needed.

For Wi-Fi-only measurement, manually unplug wired Ethernet and disconnect VPNs;
keep Ethernet available for recovery. No other device or SSID rename is needed.

Autoconnect remains opt-in. An already configured matching startup task receives
updated utility code without replacing its private profile. If none exists,
installation does not create one. See AUTO-CONNECT.md to enable it deliberately.
Startup **NotTested** without a current startup receipt is expected, not an
installation failure. Default readiness does not actively test reconnect.

Optional: after a connection is already established, run
`Check-RPi5-WiFi-Readiness.cmd -VerifyReconnect` from the package folder for one
bounded reconnect and measurement. Starting this option while firmware is still
loading cannot prove reconnection; that is first connection, labeled NotTested.

## Remaining limits

This rollback aims to recover the better tested behavior, not exceed its speed.
Congestion/resource errors remain unresolved until hardware evidence shows
otherwise. A successful short run is not long-duration or reboot reliability.
Native Wi-Fi UI, WPA3/enterprise, seamless roaming and general sleep/resume are
not complete. Keep the working UEFI and WPA2/AES setup; do not reinstall Windows.
