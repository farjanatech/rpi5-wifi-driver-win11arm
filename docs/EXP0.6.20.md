# exp0.6.20: automatic shared-SSID band preference on the .18 transfer baseline

## Why this change

.18 measured 17.09 Mbps, 128/128 completed requests, maximum recorded queue
delay 63 ms and 774 queue rejections on 5 GHz. .19 measured 4.02 Mbps, 43/47
completed requests and 11/71 router probe timeouts on 2.4 GHz/channel 6. .19's
new CMD52 path recorded no fallback sleeps/timeouts; its host queue did not fill.
That does not prove a speed improvement or establish the cause of the stalls.
The tests used different bands, and firmware-level radio behavior remains a
possible factor. Queue rejection counts alone are not a performance verdict.

The user cannot rename the router's bands. This version restores the complete
.18 SDIO source, queue ownership/cap and packet-worker scheduler, then adds one
pre-join policy. It does not claim that .19's command change caused the stalls.
.16's best 18.55 Mbps result used the same packet scheduler; its band is unknown.

## Automatic selection without router changes

Before SET_SSID, on the existing serialized worker, the driver sends the
`join_pref` policy used by Linux v6.12 brcmfmac's default implementation:
RSSI_DELTA, length 2, gain 8, band 5 GHz; then RSSI, length 2, gain/band zero.
That is eight bytes: `04 02 08 01 01 02 00 00`.

The firmware can rank candidates for the SAME requested SSID with an 8 dB
signal-ranking preference for 5 GHz. A substantially stronger 2.4 GHz candidate
remains eligible, as does a 2.4-only network. This is not a forced 5 GHz band,
an increased RF transmit power, a bypass of country limits, or a throughput
measurement. Actual association still depends on firmware discovery, security,
signal, regulatory availability and AP behavior. Acceptance of the command does
not prove a 5 GHz connection. It is applied at join, not a new roaming daemon
or periodic disruptive scan/reconnect loop. A successful but slow link is not
automatically benchmarked/replaced by this version.

Sources: [Linux common.c](https://github.com/torvalds/linux/blob/v6.12/drivers/net/wireless/broadcom/brcm80211/brcmfmac/common.c),
[preference format](https://github.com/torvalds/linux/blob/v6.12/drivers/net/wireless/broadcom/brcm80211/brcmfmac/fwil_types.h),
[band/boost constants](https://github.com/torvalds/linux/blob/v6.12/drivers/net/wireless/broadcom/brcm80211/include/brcmu_wifi.h).

Only an explicit unsupported IOVAR response (-23) permits proceeding with
default firmware selection, and that is reported, not described as an accepted
preference. BADARG, transport timeout, allocation or bus failures stop the join
at new step 18 (`automatic-band-preference`) with the original error intact.
Country verification and WPA2 setup still precede the join; no credentials are
retained by the new helper, logged, or included in the public package.

## Install on the Pi

1. Extract .20 into a NEW folder. Keep .18 as the tested fallback.
2. Run `Install-RPi5-WiFi-Driver.cmd` and restart once. No preliminary uninstall.
3. Copy your existing `WiFi.private.json` beside the utilities. Keep the same
   Country/SSID/Password. No new config field or router change is required.
4. Run `Test-RPi5-WiFi-Performance.cmd` with wired Ethernet/VPN disconnected
   and unrelated downloads closed. It connects, reports actual radio band,
   runs the same bounded workload and collects diagnostics automatically.
5. Share the resulting desktop performance ZIP, not your private config.

The connect utility reports whether the preference was accepted. For a normal
manual connection, `Connect-RPi5-WiFi.cmd` is still available. The automatic
startup task continues to use the existing credentials and driver connection
path; no need to recreate it just to enable the driver-side preference.

Performance and radio utilities remain 0.6.17 because their code/workload is
unchanged. Installer/collector/driver are 0.6.20; DiagVersion 20; cap 64.
New diagnostics: JoinPreferenceAccepted (1 means SET acknowledged, not readback
or proof of selected band), JoinPreferenceStatus and JoinPreferenceError.
Before attempting a join these are reset to pending. Obsolete .19 RuntimeCmd52
counters are written as zero to prevent stale registry values appearing live;
the .19 fast-poll path is removed, not running with zero activity.

## Acceptance and fallback

Target the previous 17-18.55 Mbps range with completed downloads and no sustained
stalls, not a fabricated PHY rate or guaranteed result. Confirm actual band,
throughput, router loss/latency, queue rejects/delay and bus errors together.
The workload remains 90 seconds or 128 one-MiB requests, plus one short request:
at most 129 MiB payload plus overhead, no automatic uploads.

If unsupported or still selecting 2.4 GHz, send the report: do not fake country,
change UEFI or reinstall Windows. If connectivity regresses, use Device Manager
to select/roll back to the retained .18 driver and restart.

GitHub CI pins .18 transport, queue, worker, firmware-fetch and performance/radio
code. Actual connection and BCDC/IOVAR tests assert the eight-byte policy, its
placement after country/security setup and before join, unsupported fallback,
hard failures and diagnostic reset. Firmware selection itself can only be
validated on the Pi. This is a test-signed experimental candidate, not a
production-certified or "perfect" driver.
