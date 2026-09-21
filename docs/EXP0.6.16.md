# exp0.6.16: isolate completion accounting on the exp0.6.14 baseline

The .15 run completed only 3 of 6 downloads and timed out on the last three
after partial HTTP 200 responses. Its whole-test effective rate was 0.52 Mbps
versus .14's 14.66 Mbps; successful router replies under load averaged 144 ms
versus 16 ms. Zero queue rejections in a collapsed workload is not proof of a
fix. These observations do not establish which change/environment factor was
responsible. .16 deliberately narrows the candidate instead of adding features.

## Exact scope

- `network.c`, including receive/send scheduling and diagnostic-write placement,
  is identical to exp0.6.14 commit `6f255d3f20b65ad5b52132a216043a2f99f848a5`.
- Restore the 64-frame cap, 30-second expiry and original admission behavior.
  The .15 per-received-frame sends and 256-frame allowance are removed.
- Retain only the functional completion-accounting change: an admission slot
  is released immediately before giving an NBL back to NDIS. A separate
  `Completing` count keeps pause/lifecycle accounting accurate until the callback
  returns. No fake early send success and no access to an NBL after completion.
- Retain read-only queue/oversized-NBL observations. DiagVersion is 16 and
  `TxQueueLimit` is 64. `TxBurstAdmissions` and `TxInterleavedPackets` remain
  zero because those .15 features are absent, not because the workload is idle.
- Firmware/CLM/NVRAM, UEFI/fan, clock/width, packet format, MAC/country,
  connection and private-profile behavior are unchanged.

GitHub enforces source identity against the .14 baseline and the exact permitted
queue/completion/diagnostic/version changes. Host tests compile the actual queue:
full-capacity callback re-entry, completion lifetime visibility, multi-NB retained
accounting, true capacity exhaustion, credit waits, cancellation, pause/power,
expiry, mapping failure, bus failure and no NBL use after ownership return.
The ARM64 WDK build and package checks also run. These are not hardware tests.

## Run on the Pi only

1. Keep your exp0.6.14 package. Extract the entire .16 driver ZIP into a new folder.
2. Run **Install-RPi5-WiFi-Driver.cmd**, approve elevation, and **restart once**.
   This is a version upgrade from .15; no manual uninstall is normally needed.
3. If using a private profile, copy `WiFi.private.json` beside the new utilities.
   Do not edit the hashed public example or share the private profile.
4. Unplug Ethernet/disconnect VPNs and close unrelated downloads. Run the included
   **Test-RPi5-WiFi-Performance.cmd**. It connects and collects the report; no
   separate diagnostics run is needed. Watch Task Manager's Ethernet 2 graph.
5. Share the desktop `RPI5-WIFI-PERFORMANCE-*.zip`.

Same workload as before: up to 90 seconds or 128 one-MiB requests, at most
129 MiB requested download payload across the tests plus protocol overhead.
The report includes failed attempts in elapsed time and excludes partial bodies
from verified-byte speed. Neither private credentials nor reports are uploaded.

Accept this candidate only if sustained transfers recover without excessive
latency/timeouts. Check queue-full deltas too; the accounting fix cannot prevent
all genuine 64-frame exhaustion. If worse than .14, return to the retained .14
driver using Device Manager's rollback/driver selection, then restart. No UEFI
or Windows reinstallation. Do not claim a guaranteed speed improvement.
