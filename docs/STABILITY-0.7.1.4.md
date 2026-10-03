# Stability 0.7.1.4: persistent disconnect and power evidence

Branch: **`better-improvement`**  
Driver version: **0.7.1.4**  
Protected hardware baseline: **0.7.1.2 / `16533ac0e7e477f5c604882d8cc82081119e3f90`**

## Purpose

0.7.2.0 could run the full 128 MiB workload successfully, but the user's Pi
later disconnected after a few idle minutes. The available logs ended before
that event, so they could not distinguish firmware deauthentication from an NDIS
power transition, Pause/Restart, explicit disconnect, or a worker failure.

0.7.1.4 therefore makes no performance-path change. It returns to the proven
0.7.1.2 transport and adds persistent lifecycle evidence only.

## New persistent evidence

The driver now records, without clearing on reconnect:

- total disconnect count
- last disconnect source, event, status, reason and network phase
- exact 64-bit interrupt-time timestamp of the last disconnect
- firmware DEAUTH (event 5/6), DISASSOC (11/12), LINK-down (16), authorization
  loss and other disconnect counts
- explicit disconnect request count
- worker start/restart/failure/exit counts and last failure status/timestamp
- NDIS D0/D1/D2/D3 transition counts, last state and timestamp
- NDIS Pause/Restart counts and timestamps
- surprise-removal and shutdown counts

Disconnect source IDs:
1 = firmware event  
2 = explicit disconnect request  
3 = NDIS power transition  
4 = unexpected worker failure

These values survive a later reconnect so the evidence that caused the drop is
not overwritten by the next successful association.

## Unchanged from 0.7.1.2

CI requires exact 0.7.1.2 content for:

- SDHCI / SDIO / Function-2 FIFO PIO
- 50 MHz and 25 MHz clock policy
- TX single-frame sender
- 64-frame TX queue and ownership logic
- TX pressure and retry policy
- RX read-ahead and RX aggregation
- firmware startup/upload
- connection/authentication/country/radio paths
- transport service and polling
- complete hardware interrupt implementation

There is no TX glom, DMA, DDR50, firmware binary, radio or UEFI-policy change.

## Test

Install, reboot, connect normally, then leave the Pi idle until the observed
auto-disconnect occurs. Do not manually reconnect before collecting diagnostics
if possible. Immediately run `Check-RPi5-WiFi-Readiness.cmd` and the one-click
diagnostic collector.

The decisive fields are `LastDisconnectSource`, the firmware event/status/reason
fields, D0-D3 counters, Pause/Restart counters and worker-failure evidence.
