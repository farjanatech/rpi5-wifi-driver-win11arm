# exp0.6.28: separate connection app and disconnected-only scan

Experimental, test-signed Windows 11 ARM64 candidate for the Raspberry Pi 5
CYW43455 adapter. This adds a separate connection window, not native Windows
Wi-Fi integration. It is not a throughput optimization or a claim that the new
scan feature has passed physical Pi testing. Keep the complete exp0.6.27 package
for rollback.

## Visible feature and limits

- Open `RPi5-WiFi-App.cmd` for the separate app. Confirm the country where the
  Pi is physically located, request a scan, choose a supported network and enter
  its password. For the Pi in Bangladesh, confirm `BD`; do not substitute a
  different country to bypass a firmware error.
- Scanning is explicitly requested and allowed only while disconnected. It must
  not silently disconnect an active connection or periodically scan while you
  browse. Unsupported firmware, busy state, cancellation and timeout are errors
  or unavailable results, not proof that no networks exist.
- Connection remains WPA2-Personal/AES only. Seeing an SSID does not establish
  that its security mode is supported. WPA3-only, enterprise and native Windows
  Wi-Fi configuration remain outside this candidate.
- Version one of the app does not save passwords or offer startup/autoconnect
  configuration. Existing command-line connection and optional startup tools
  remain available. The driver upgrade does not enable an absent startup task
  or replace an existing private profile.
- Results reflect one bounded scan, not a permanent inventory or proof of the
  fastest band. Same-SSID automatic band selection remains the existing policy;
  no router rename or extra device is required.

## Protected behavior

The established .27 TX/RX/SDIO traffic path, immediate send completion ownership,
64-retained-frame cap and 4-TX / bounded-RX / 4-TX budgets stay unchanged. There
is no completion batching, larger queue, TCP ACK filtering, firmware change,
new bus clock, UEFI/fan change, country substitution or security-setting change.
The new scan work is confined to the disconnected control path and a separate
user interface. Existing connection authentication and credential derivation
are retained, rather than adding another password protocol.

Measurement utility 0.6.27.1 and its comparative workload are unchanged. It records
per-request timings and same-boot observation windows; these locate delays but
do not identify a server or driver as their cause. Startup receipt compatibility
remains 0.6.27 because the startup connector is unchanged. A receipt records
utility/boot evidence, not a hardware certification of this driver version.

The driver INF is 0.6.28.0; diagnostic version is 28 and NDIS vendor version is
0x0006001c. The retained diagnostic collector and measurement utility have their
own version labels, which need not equal the driver version.

## Install and use on the Pi

1. Keep the complete exp0.6.27 package. Extract the entire new driver ZIP into a
   fresh folder; do not mix old SYS/CAT files or edit hashed package examples.
2. Run `Install-RPi5-WiFi-Driver.cmd` and restart once. Do not install on the
   development PC. No blanket driver uninstall or Windows reinstall is needed.
3. Open `RPi5-WiFi-App.cmd`. If already connected, scanning is unavailable until
   you deliberately disconnect; do not interrupt a working connection merely
   to obtain another scan. Confirm the country and use a WPA2-Personal/AES network.
   After a restart, wait for **Disconnected and ready** before scanning. The
   existing firmware upload/readback can take several minutes; this feature
   does not shorten that initialization. Enter `BD` only if the Pi is physically
   in Bangladesh, tick the country confirmation, then press **Scan networks**.
   Select a supported SSID, enter its password, and press **Connect**. Closing
   the app leaves an established connection running. The app does not save the
   password or automatically create a startup task.
4. After connecting, use normal browsing. If collecting comparative evidence,
   manually unplug wired Ethernet/disconnect VPNs, then run
   `Check-RPi5-WiFi-Readiness.cmd` once. Share its readiness ZIP, not a password
   or private profile. The ZIP already contains performance and diagnostics.

Default readiness checks do not actively test reconnect. Startup `NotTested`
without a current startup receipt is expected. Existing autoconnect is opt-in
and unchanged; see AUTO-CONNECT.md. Do not publish `WiFi.private.json`.

If this candidate causes trouble, reinstall the retained exp0.6.27 package and
restart once. Keep working Ethernet available for recovery. No UEFI rollback
is part of this driver-only update.

## Validation boundary

GitHub builds and mocked tests check bounded scan parsing/control, disconnected
gates, user-interface request handling, cancellation and unchanged traffic and
authentication code. They do not prove that firmware returns usable scan data
on the Pi, that every router is supported, or that speed/reliability improves.
The .27 baseline remains the comparison, not an invented .28 hardware result.
