# exp0.6.19: bounded runtime CMD52 fast polling

## Evidence and scope

The .18 capture confirmed 0.6.18.0 / DiagVersion 18, 4-bit 25 MHz operation,
5 GHz at -48 dBm and PM/MPC off. It completed 128/128 one-MiB downloads at
17.09 Mbps, with a maximum recorded queue delay of 63 ms. It still recorded
774 queue-full rejections, one NoResources router ping, and 20 machine-wide
IPv4 TCP retransmissions. Successful loaded pings averaged 25.17 ms (max 57).
These numbers identify remaining pressure, not its exclusive root cause.

Source inspection found that every CMD52, including the per-receive interrupt
check, still used 100-us polling even on the verified operating bus. A command
that completes in 10-50 us could therefore occupy the sole worker for 100 us.
This is a concrete avoidable wait, but the previous driver did not measure how
often it happened on the Pi. No numerical speed gain can be promised.

Only CMD52 on PASSIVE_LEVEL, verified stage 6, width 4, clock above 400 and at
most 25000 kHz uses the new path: immediate status check, at most five 10-us
stalls, then one-ms requested sleeps until completion or a one-second elapsed
deadline. Windows can round sleeps up; frequent fallback is a regression risk
and is counted. A separate 1000-sleep ceiling also bounds a non-advancing clock.
Stop is checked each iteration. Host-error bits, R5 errors, resets and failed
write verification remain errors, never success or silently retried commands.

The unchanged startup path still uses its original polling. There is no faster
clock, new bus mode, CMD53 change, queue enlargement, packet priority/coalescing,
NDIS ownership change, RX/TX scheduler change, firmware change, country override,
forced band, UEFI/fan update or private-profile change.

Microsoft recommends minimizing busy waits and using another wait mechanism
for longer intervals: [KeStallExecutionProcessor documentation](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/wdm/nf-wdm-kestallexecutionprocessor).

## Testing on the Pi

1. Keep the working .18 package. Extract .19 into a new folder.
2. Run `Install-RPi5-WiFi-Driver.cmd`, approve elevation, then restart once.
   This is a normal version upgrade; no preliminary uninstall is needed.
3. Copy your existing `WiFi.private.json` beside the utilities. Do not edit the
   hashed example and do not include the private file in shared diagnostics.
4. Keep the same router/SSID and placement. Disconnect wired Ethernet/VPN and
   close other downloads. Run `Test-RPi5-WiFi-Performance.cmd`.
5. Share the desktop performance ZIP; it includes diagnostics. Check the radio
   snapshot confirms 5 GHz before comparing with .18. Same SSID can pick either
   band, so mismatched bands cannot isolate this driver change.

Performance/radio utilities intentionally remain version 0.6.17 and their code
is unchanged. Driver/installer/collector are 0.6.19, DiagVersion 19, cap 64.
The workload remains up to 90 seconds or 128 one-MiB requests, plus one initial
one-MiB request (129 MiB maximum payload, protocol overhead additional).

New cumulative counters in driver-before/after and registry diagnostics:

- RuntimeCmd52Commands: operating-bus CMD52 waits handled by the new path.
- RuntimeCmd52FastPolls: ten-us stalls requested, at most five per command.
- RuntimeCmd52WaitSleeps: one-ms fallback sleeps requested (actual wait may be longer).
- RuntimeCmd52Timeouts: waits that exhausted their deadline/ceiling.

Judge completed downloads, throughput, latency, queue rejects/delay and SDIO
errors together. Acceptance requires no command timeouts or new transfer errors,
no sustained stalls, and a repeatable improvement under matched radio conditions.
If the fallback counter is frequent or the run regresses, keep the logs and use
Device Manager to roll back/select the retained .18 driver, then restart.
Do not reinstall Windows or change UEFI. One short run is not long-term validation.

## Verification

GitHub tests exercise actual command code with immediate/10-50-us completion,
yielding slow completion, a one-second timeout, stop during either wait path,
host/R5/readback errors, and unchanged startup/mode/IRQL/non-CMD52 behavior.
Pinned .18 source checks prohibit changes to queue/scheduler/connection/CMD53
and utility behavior. The normal ownership, protocol, radio, firmware and
PowerShell suites run before the ARM64 build and test signing.

This remains an experimental test-signed package, not production signing or
physical Pi validation. All builds/tests run on GitHub, not the development PC.
