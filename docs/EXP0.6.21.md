# exp0.6.21: bounded firmware-hinted receive read-ahead

## What the evidence does and does not establish

The best retained result is .16: 18.55 Mbps, 128/128 downloads, radio band not
recorded. .18 achieved 17.09 Mbps on 5 GHz, 128/128 completed. .20 measured
14.50 Mbps on Archer-Six at 2.4 GHz/channel 6, also 128/128 completed. The .20
report included one 4.83-second download, one under-load router timeout, and
20 queue-full rejections. Its shared-SSID preference was accepted, but an
accepted policy is not proof of 5 GHz association or optimal throughput.

The separate .20 phone hotspot report confirms 5 GHz and -38 dBm. Router pings
averaged 14 ms and HTTPS succeeded, but all three sustained downloads ended
with curl 56, after partial payloads. There is no valid sustained Mbps result.
The recorded SDIO timeouts, checksum errors and queue rejections were zero;
the system-wide TCP reset counter rose by nine. These observations do not
identify who reset connections or prove a driver, phone, server or carrier
root cause. We do not blame the phone or request another-device experiment.

Band/AP/run conditions differ, so those results cannot isolate one change.
The Ethernet-style Windows interface itself is not evidence of the bottleneck.
The current driver uses serialized, non-aggregated byte-mode PIO, not a mature
DMA/interrupt-driven/aggregated bus stack. Its per-frame overhead is a concrete
optimization target, but not a proven explanation for every failed transfer.

## Reference review and focused implementation

Reviewed these files at ReactOS commit
`929bdd689d1e18d0ef71214741d4e16eec74409c`:

- [cyw43455/fwil.c](https://github.com/ahmedarif193/reactos/blob/929bdd689d1e18d0ef71214741d4e16eec74409c/drivers/network/dd/cyw43455/fwil.c):
  firmware-hinted read-ahead, rounded-length validation, interrupt-driven bus
  worker, batched receives and glom support.
- [cyw43455/sdio.c](https://github.com/ahmedarif193/reactos/blob/929bdd689d1e18d0ef71214741d4e16eec74409c/drivers/network/dd/cyw43455/sdio.c):
  SD-bus requests, block transfers and preallocated MDLs.
- [cyw43455sdio/cyw43455sdio.c](https://github.com/ahmedarif193/reactos/blob/929bdd689d1e18d0ef71214741d4e16eec74409c/drivers/network/dd/cyw43455sdio/cyw43455sdio.c):
  a separate function-1 companion using SdBusSubmitRequest, not a drop-in
  replacement for this ACPI-bound direct-host driver.
- [Linux v6.12 brcmfmac/sdio.c](https://github.com/torvalds/linux/blob/v6.12/drivers/net/wireless/broadcom/brcm80211/brcmfmac/sdio.c):
  next-length interpretation, header-first fallback, rounded-length validation.

Only the bounded receive read-ahead is adapted now. A normal 1530-byte SDPCM
frame currently requires a 64-byte header read, then 512+512+444-byte reads.
With a validated 1536-byte hint, it uses 512+512+512 instead: one fewer CMD53.
The four-frame batch uses 13 rather than 16 F2 commands if its last three
frames are hinted full-size packets. This is NOT a 25% speed guarantee: other
commands, radio time, TCP behavior and hint availability remain relevant.

Safety and compatibility:

- Retain exact .20 SDIO command engine, 4-bit/25 MHz verified bus mode, queue
  cap 64, ownership/completion rules, TX pump and four-frame/two-ms RX budget.
- Use only 64..2048-byte, 16-byte-aligned hints from the last valid frame.
  Consume each hint once. Short/large/absent hints use the original 64-byte
  header-first path. Unknown frame length is never invented.
- Clear hints at RX batch boundaries and on stop/empty/error. The synchronous
  control poller disables both use and storage of hints, preserving country,
  security, radio and connection command exchanges. No public IOCTL ABI change.
- Validate real length/complement, offset, channel and exact rounded hinted
  length before delivering any packet. Byte-mode FIFO addressing/chunking and
  per-packet interrupt/mailbox servicing remain unchanged. The existing full
  RX buffer also accommodates a validated unsolicited control frame; it is
  returned as control, never indicated as Ethernet data.
- Mismatch/transport failure invalidates state, terminates the bad FIFO frame
  and fails closed with the original error. It is NOT retried from mid-frame.
- No aggregation, DMA, interrupts, extra worker, priority boost, larger queue,
  clock increase, security bypass, forced band, or country substitution added.

## Install and use on the Raspberry Pi only

1. Extract the new package into a new folder. Retain .20/.18 for rollback.
2. Run `Install-RPi5-WiFi-Driver.cmd` and restart once; no preliminary uninstall.
3. Put your existing `WiFi.private.json` beside the new utilities. Do not edit
   the hashed example file, or share/upload your private profile.
4. Run `Connect-RPi5-WiFi.cmd` if your existing autoconnect task has not already
   connected. Keep your BD country and current SSID/password. Both router bands
   remain eligible under the existing automatic 5 GHz preference.
5. Use your normal websites. No other device or router rename is required.
   The unchanged `Test-RPi5-WiFi-Performance.cmd` remains available if you want
   a comparable report; it connects and collects diagnostics automatically.

If connectivity regresses, use Device Manager to roll back/select retained
.20 or .18 and restart. Do not change UEFI, format disks or reinstall Windows.

## Validation and visibility

Driver/installer/collector version 0.6.21, DiagVersion 21. Performance/radio
utilities remain 0.6.17 because their workload and code are unchanged.
The read-ahead harness compiles the actual production poller with simulated
hardware calls and receive/event delivery. It covers every frame size 49..2048,
all 256 hint values, command boundaries/savings, batches, firmware control,
credit/flow preservation, malformed lengths/headers, I/O faults at every call,
empty FIFO, stop, unsupported aggregation, and the maximum legacy control frame.
CI also retains the SDIO, connection, NDIS ownership, firmware, traffic stats,
radio and utility suites. Scope tests pin unrelated source to .20.

Six new non-sensitive counters are collected with existing diagnostics:
`RxHeaderReads`, `RxReadAheadAttempts`, `RxReadAheadFrames`,
`RxReadAheadSavedCommands`, `RxReadAheadMismatch`, `RxReadAheadHintIgnored`.
Header/attempt counters count requests, including failed or empty reads;
frames/savings count validated supported frames. Savings are actual byte-mode
command-count differences versus header-first, not estimated Mbps. Ignored
hints are normal fallbacks, not corrupt packets. No packet payload is logged.

Automated tests are not hardware qualification. The candidate aims to reduce
known overhead while retaining the successful baseline; neither a new speed
record nor a fix for the hotspot resets is claimed before hardware evidence.
