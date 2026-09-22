# exp0.6.23: transport correctness and lower-overhead candidate

Experimental, test-signed Windows 11 ARM64 driver for the Raspberry Pi 5 only.
This implements findings from the .22 audit; speed and physical reliability
remain unverified until a Pi run. No claim that one audited defect has already
been proved responsible for the regression.

## Why

The supplied .22 run completed 66/69 downloads at 6.21 Mbps. Its first 53
requests averaged about 10.1 Mbps; failed attempts consumed about 31 seconds.
Router latency/loss also worsened during stalls. The maximum host TX queue
delay was 46 ms, without queue-full or reported SDIO timeouts. These observations
justify investigating firmware/transport handling, not a larger queue. The
historical .16 result remains 18.55 Mbps, 128/128 complete; its band is unknown.

## Changed

- Track firmware global flow-control state, including acknowledge/change races;
  check fresh F1 state before each data frame. A failed status operation never
  permits a data transmission. Control traffic retains its separate credit gate.
- Service interrupt/mailbox state once per bounded receive batch, rather than
  repeatedly for each known-pending frame. Control/reply polling remains fresh.
  Empty-FIFO, malformed-frame and transport failures remain distinct.
- Decode firmware mailbox flow/halt messages and record receive sequence
  mismatches/duplicates. These counters do not assume every mismatch is a lost
  IP packet. Firmware halt/error remains a failure, not a silent retry.
- Correct the RX-error termination bit. Still stop on a failed transfer; no
  speculative FIFO resynchronization, packet replay or unlimited retry added.
- Build Release ARM64 with compiler optimization and retain PDB symbols.
- Precompute timing thresholds once. Detailed per-command/receive-indication
  QPC instrumentation is disabled by default. Worker timing remains available.
  `TimingV2` is 568 bytes with an active-bucket mask. Disabled metrics are null
  in reports, not evidence of zero latency. Developers can build with
  `/p:Rpi5DetailedTiming=1` for full timing; both modes have host tests.
- Add explicit read-only rate/BSSID/raw-chanspec/packet-count/station queries.
  Validate reply lengths, station versions, identity and validity flags.
  Unsupported firmware fields remain unknown. Reported rates are observations,
  not a guaranteed negotiated rate or application throughput; capability flags
  do not prove that over-air aggregation is active.
- The existing performance command saves radio-before/after JSON automatically.
  Queries run outside the measured downloads and sampler. Timing and transport
  counters are retained in the same report ZIP; credentials are not captured.

## Deliberately conservative

Firmware priority-flow bits refer to precedence, not directly to the BCDC
priority number. The AP's WMM mapping is not yet verified by this driver.
`TransportPriorityMaskKnown=0` therefore retains the safe all-priority fallback;
we do NOT guess bit zero or bypass flow control. Global flow handling is fixed
independently. A validated per-priority mapping remains future work.

The proven 64-frame queue, TX4 / RX4-or-2ms / TX4 budgets, direct SDIO transfer
engine, 4-bit/25-MHz bus, firmware/CLM/NVRAM, credentials, country validation,
shared-SSID band preference, NDIS receive ownership and UEFI/fan are unchanged.
No block-mode/DMA/glom, higher clock, band forcing or router renaming is added.

## Run on the Pi

1. Extract the complete new ZIP into a fresh folder.
2. Keep/copy your existing private configuration as `WiFi.private.json`. Do not
   edit the hashed `WiFi.config.example.json`; do not publish your password.
3. Run `Install-RPi5-WiFi-Driver.cmd`, then restart the Pi once. No preliminary
   driver-store purge, UEFI change or Windows reinstall is needed.
4. Disconnect competing wired/VPN routes for measurement, close unrelated
   downloads, and run `Test-RPi5-WiFi-Performance.cmd`. It handles connection and
   diagnostics. No separate radio query or other-device experiment is needed.
5. Share its single ZIP. It includes local IP/MAC/BSSID/device information;
   review before publishing. No report is uploaded automatically.

Keep .16 available for rollback. Device Manager's Roll Back Driver or explicit
Have Disk selection may be necessary: simply adding an older INF does not force
Windows to replace a higher-ranked version. Do not remove unrelated drivers.

## Validation and interpretation

GitHub CI compiles optimized host mocks of the actual transport helpers, checks
flow races, mailbox/sequence handling, malformed/empty FIFO, errors, cancellation,
timing masks and query layouts, and builds/packages ARM64 with warnings as errors.
Scope guards keep the underlying SDIO, queue, firmware and benchmark unchanged.
Host mocks and a successful build are NOT physical hardware validation.

Transport counters are cumulative, periodically published and reset with the
worker session. Blocked durations describe observed firmware state, not exact
over-air delay. Timing totals are nested wall times and must not be added.
Firmware counters may reset/wrap or change with BSSID/association; do not compute
blind deltas. Packet-count success is not proof that a specific TCP segment was
acknowledged by its internet peer. Unknown station layouts are rejected.

The comparison workload remains sequential 1-MiB requests for <=90 seconds or
128 requests. It includes setup/timeouts and is not a PHY-rate benchmark.
