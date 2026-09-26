# Performance alpha.3-US: official Raspberry Pi firmware comparison

Driver INF version **0.7.0.2**. Experimental/test-signed, for a Raspberry Pi 5
**physically located in the United States only**. Not a speed mode, RF
certification or proven improvement. No hardware validation has been completed.

## What changes, and what does not

Based on preserved alpha.1 commit `6e652fb86aef595cf6f6fbf6f05c1769d5673055`.
The firmware changes from 7.45.229 to Raspberry Pi OS's standard **7.45.265**
with its corresponding CLM file. Board configuration is byte-identical.

The only host runtime change is an admission gate: both explicit scans and
connections must request **US**. Other codes are rejected without scheduling
work or rewriting the user's request. The original country SET and matching
readback checks remain required before scanning/association. This does not
auto-detect physical location; the user must confirm it in the existing app.

Every other driver source byte is protected against alpha.1 in CI: SDIO PIO,
multi-block transfers, validated read-ahead, aggregate decoding, packet budgets,
64-frame queue, scheduling, retries, authentication, band policy and diagnostics.
The connector, utilities and installer safeguards are unchanged (installer/INF
version identifiers advance). The alpha.2 queue-pressure change is NOT included.
UEFI, fan, boot/security settings and existing private credentials are untouched.
Diagnostic ABI stays at version 30; identify this package by INF 0.7.0.2.

## Exact upstream files

All files come from RPi-Distro/firmware-nonfree commit
`3bab0f823f5b53150b76aab77093adef6655b920`, not a floating branch.

| File under debian/added-firmware | Bytes | SHA-256 |
| --- | ---: | --- |
| cypress/cyfmac43455-sdio-standard.bin | 609309 | D608F866582519C0A28D86DB43040F4F1B98DD1D153E72E9752586546B4A36C3 |
| cypress/cyfmac43455-sdio.clm_blob | 2676 | 9823842CAE9FB9A5DD1E5FB31F595516EC7DEEE341354BEF30BB3026EEE29CC1 |
| brcm/brcmfmac43455-sdio.txt | 2074 | CA709BE81A78BDB6932936374F39943ACBD7AF07FAE6151011127599A3CE9E3D |

The standard image's FWID is `01-b677b91b`; it is renamed only to match the
existing Windows firmware filename. Pi 5 Model B aliases and the distribution's
standard-priority selection were checked. This is not evidence of which variant
is installed on any particular Raspberry Pi OS system.
The full 430523-byte upstream `debian/copyright` is included unmodified, SHA-256
`07082FD0BB65E32C73AF39A2292DD6C83F169BE7427D338BA1FE34F9B86C0E30`.
Read its Cypress terms before installation; these blobs are not GPL software.

## Important compatibility limits

This is newer than our working 7.45.229, but not a newly discovered binary:
the same 7.45.265/CLM/calibration bytes were packaged in early experiments.
Recorded exp0.6.6 findings listed 116 countries without BD and rejected both
BD revision requests. It is NOT an update for Bangladesh, or a way to bypass
its radio rules. This US variant was requested for physical US use.

Country acceptance alone does not prove scanning, firmware WPA2 offload,
authentication, DHCP, traffic, reboot stability or speed. Our existing driver
requires `sup_wpa` firmware authentication offload; Pi OS's host authentication
stack is different. A 7.45.265 version string is not proof of that capability.
Do not ignore an unsupported-command error or weaken country readback to
connect. If initialization/authentication fails, collect diagnostics and roll
back; do not change UEFI or reinstall Windows.

CI runs the real host protocol/lifecycle tests, fault simulations, exhaustive
US admission checks and memory sanitizers, checks unchanged performance scope,
builds ARM64, pins firmware bytes and test-signs the package. It cannot execute
the CYW43455 firmware or certify radio operation. No speed is promised.

## Installation and one bounded comparison

1. Keep the complete alpha.1/alpha.2 ZIPs and a working recovery connection.
   Do not uninstall or delete prior drivers. If an existing startup profile
   requests a different country, disable startup through the existing connector
   before installing; this package will not rewrite the profile.
2. On the US-located Pi only, extract the entire ZIP and run
   `Install-RPi5-WiFi-Driver.cmd`. Save work and restart if Windows requests it.
   The development PC is not a test device. No manual firmware-file replacement.
3. Open the existing connector exp0.6.29.2 EXE, enter **US**, confirm physical
   location, scan and connect. A saved non-US profile must be edited/resaved
   manually only when the Pi is in the US. Startup remains opt-in.
4. Once connected, run `Check-RPi5-WiFi-Readiness.cmd` once. It includes the
   existing performance workload and diagnostics. Keep Ethernet for recovery
   but unplug it for measurement. Share the readiness ZIP and installer log;
   never share the private profile/password. If connection fails, share only
   diagnostics instead of repeatedly retrying speed tests.

Compare with alpha.1 on the SAME US-located Pi, access point, band/channel,
signal and workload. Different countries, APs, internet conditions or PHY rates
do not establish a driver speed gain. Look at successful bytes/time, latency,
queue rejections, retries and transport errors, not a single peak speed number.
Keep this version only if a useful improvement does not sacrifice reliability.

## Rollback

Use Device Manager for **Raspberry Pi 5 CYW43455 Direct SDIO Network Adapter**:
Roll Back Driver, or Update Driver -> Browse -> Let me pick -> Have Disk and
select the complete prior package's `rpi5cyw.inf` (alpha.1 is 0.7.0.0,
alpha.2 is 0.7.0.1). An older installer alone may not supersede 0.7.0.2.
Restart if requested and verify the selected version. Roll back the whole
package so the signed firmware and CLM pair follows the chosen driver.
Do not delete the RP1 wired Ethernet driver or manually mix firmware/CLM files.

## Sources

- [Official firmware and board files](https://github.com/RPi-Distro/firmware-nonfree/tree/3bab0f823f5b53150b76aab77093adef6655b920/debian/added-firmware)
- [Standard/minimal package selection](https://github.com/RPi-Distro/firmware-nonfree/blob/3bab0f823f5b53150b76aab77093adef6655b920/debian/config/defines.toml)
- [Upstream copyright/licences](https://github.com/RPi-Distro/firmware-nonfree/blob/3bab0f823f5b53150b76aab77093adef6655b920/debian/copyright)
- [Recorded earlier BD rejection](https://github.com/farjanatech/rpi5-wifi-driver-win11arm/blob/6e652fb86aef595cf6f6fbf6f05c1769d5673055/README.md#integrated-exp067-candidate--user-requested-reactos-firmware-pair)
