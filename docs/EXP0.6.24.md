# exp0.6.24: reliability and throughput candidate

Experimental test-signed Windows 11 ARM64 driver for the existing direct-SDIO
ACPI RPI0011 UEFI. This release does not change UEFI or fan control. GitHub
simulation/build success is not physical Raspberry Pi validation.

## Why this change

The preceding .23 hardware report achieved 9.31 Mbps effective verified goodput
(99/101 completed downloads), up from .22's 6.21 Mbps, but two failed attempts
consumed 30.15 seconds. The radio was on 2.4 GHz channel 6 despite a shared-SSID
5 GHz preference. No SDPCM sequence errors or CMD53 timeouts were recorded.
Queue/credit snapshots do not establish queue overflow as the cause of the
stall. Firmware receive-error/retry counters increased, but they do not locate
the cause or give an exact packet-loss percentage. The historical .16 result
of 18.55 Mbps remains a useful fallback reference, not a controlled comparison.

## Receive reliability

When CCCR reports no pending interrupt and no cached receive hint exists, the
worker checks F1 status if no successful status read occurred for at least
100 ms. Normal RX/TX status service refreshes that timer. FIFO reads still
require a real FRAME or NAKHANDLED indication; there is no speculative read,
packet replay, reset loop, blind interrupt enable, or enlarged queue.

This addresses a possible notification blind spot, not a proven root cause.
New counters show whether the fallback finds any real notifications. A fixed
128-entry, one-second passive history records existing transport/queue/progress
observations. It contains no packet contents, credentials, SSID or addresses.
Only the existing periodic diagnostics writer exports it; the packet path does
not write registry values. History gaps can indicate delayed sampling, not
necessarily a blocked driver. Quiet traffic alone is not classified as failure.

## Same SSID, automatic band selection

Startup uses the Linux brcmfmac BAND-then-RSSI join-preference format rather
than the previous 8 dB RSSI offset. It does not lock the radio to 5 GHz. The
initial authenticated candidate is checked by BSSID/channel/RSSI readback
before publishing the Windows link. A weak 5 GHz candidate (below -70 dBm),
timeout or invalid readback allows one ordinary RSSI-only selection attempt
on the same SSID, with the same country and security. Each join wait is bounded
to 15 seconds; an in-flight command can add its existing bounded timeout.
The preference is restored before normal traffic. An explicitly unsupported
preference uses the legacy firmware-selection path and is reported as such.

An accepted preference is not proof that firmware chose 5 GHz, or that 5 GHz
will be faster. Diagnostics and the before/after radio report show the actual
selection. No router rename or second-device experiment is required.

## SDIO throughput

Firmware upload/readback remains conservative. After F2 startup, the existing
4-bit default-timing path is established first. Standard SD high-speed SDR
is attempted only with supported host version/timing, an explicit host
high-speed capability, known clock, card CCCR revision and SHS capability.
Host/card mode and clock readbacks are checked. Sixteen matching read-only
chip-ID CMD53 reads must pass before association.

If high-speed configuration or data verification fails, both ends are restored
to default <=25 MHz and all 16 reads must pass again. Inconsistent restoration
or repeated data failure stops startup. Unknown/ineligible capabilities stay
at verified <=25 MHz. BusHighSpeedRejectMask and status fields explain this;
BusHighSpeedActive alone is not proof of successful verification.

This is <=50 MHz SDR, **not DDR50**. No CMD11, voltage change, UHS tuning,
host-platform quirk, DMA or block/glom engine is introduced. It cannot promise
the ~216 Mbps 5 GHz local-LAN result reported for Raspberry Pi OS.

## Preserved and validation

- 64-frame queue; 4 TX / bounded RX / 4 TX scheduling, same download workload.
- Firmware/CLM/NVRAM bytes, BD validation, WPA2-Personal/AES, MAC handling.
- Optimized Release ARM64 /O2 /Ot with symbols; detailed per-command timing off.
- No changes to the development PC's drivers, network, certificates or boot.

GitHub CI runs production-helper simulations for capability rejection,
configuration/readback failure and safe rollback; band preference/authentication
readback and fallback; notification freshness/error paths; passive history
schema/wrap checks; existing ownership, wire, statistics and utility tests.
Physical validation remains required. Keep .16 and .23 for rollback.

Install the complete ZIP on the Pi, restart once, then run
`Test-RPi5-WiFi-Performance.cmd`. It connects as needed and creates the single
usual report ZIP, including actual band/bus and passive receive history.
Do not edit the hashed example config; keep editable credentials in your local
`WiFi.private.json`, outside public reports and source control.

## Primary references

- [Linux SDIO high-speed negotiation](https://github.com/torvalds/linux/blob/v6.12/drivers/mmc/core/sdio.c)
- [Linux SDHCI definitions](https://github.com/torvalds/linux/blob/v6.12/drivers/mmc/host/sdhci.h)
- [Linux brcmfmac join preference and disconnect](https://github.com/torvalds/linux/blob/v6.12/drivers/net/wireless/broadcom/brcm80211/brcmfmac/cfg80211.c)
- [Linux brcmfmac SDIO notification service](https://github.com/torvalds/linux/blob/v6.12/drivers/net/wireless/broadcom/brcm80211/brcmfmac/sdio.c)
- [Raspberry Pi 5 SDIO hardware description](https://www.raspberrypi.com/news/introducing-raspberry-pi-5/)
