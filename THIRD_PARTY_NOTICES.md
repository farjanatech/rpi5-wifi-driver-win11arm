# Third-party notices

## Integrated candidate 0.6 references and redistributed firmware

exp0.6.20 join preference follows Linux v6.12 brcmfmac/common.c
`brcmf_c_set_joinpref_default`, `fwil_types.h` join preference TLVs, and
include/brcmu_wifi.h constants: RSSI_DELTA +8 on WLC_BAND_5G, followed by RSSI.
These Broadcom ISC sources are covered by the notice below. The bounded
connection integration and explicit unsupported/error diagnostics are local.
This ranks candidates; it does not change transmit power or country limits.

exp0.6.10 operating-speed sequencing was informed by the user-supplied
`sdio-operating-speed (1).patch` from the shared Claude discussion. The mode
implementation is rewritten with explicit startup synchronization, failure
recovery, capability checks and tests. CCCR register definitions are protocol
constants cross-checked against Linux include/linux/mmc/sdio.h. No firmware
binary, calibration data, or regulatory settings are modified by this change.

exp0.6.9 pending-NBL completion, retry-on-firmware-busy and transmit-draining
design references Ahmed ARIF's `CywDrainTxQueue` and `CywQueueTxWork` in
`drivers/network/dd/cyw43455/cyw43455.c` and the bus worker in `fwil.c`, revision
`929bdd689d1e18d0ef71214741d4e16eec74409c`, Copyright 2026 Ahmed ARIF,
GPL-2.0-or-later. `tx_queue.h` is a bounded direct-host adaptation with separate
ownership tests. The SD-bus companion is not installed or represented as
compatible with this direct-host miniport.

exp0.6.2 country-request handling references Linux v6.12 brcmfmac/cfg80211.c
`brmcf_use_iso3166_ccode_fallback` and `brcmf_translate_country_code`: chip 4345
uses the requested ISO3166 country and revision zero when no board mapping is
supplied. ISC Broadcom notice below applies. Country readback validation and
numeric step diagnostics are maintained in this repository.

`src/cyw43455/firmware.c` and `network.c` adapt sequences and wire formats from
Ahmed ARIF's `cyw43455/chip.c`, `fwil.c` and `cyw43455.h` at ReactOS revision
`9bb45e56ca5e456bfe40357a161b6ea60af0356e`.
Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>, GPL-2.0-or-later.
The Windows direct-host, Ethernet lifecycle and utility integration is maintained
here under GPL-3.0-or-later. `cyw43455sdio` is a GPL-3.0-or-later reference only;
its SD-bus companion driver is not copied or installed.

BCDC control flags, CR4 sizing and SDPCM formats also reference Linux v6.12
brcmfmac `bcdc.c`, `chip.c`, `sdio.c` and `sdio.h` under the ISC notice below.

Since exp0.6.7, CI redistributes the unmodified CYW43455 firmware, CLM and Pi
board file from ahmedarif193/reactos revision
`929bdd689d1e18d0ef71214741d4e16eec74409c`, drivers/network/dd/cyw43455/fw.
Exact paths and SHA-256 are pinned in `scripts/fetch-firmware.ps1`.
`brcmfmac43455-sdio.bin` and `.clm_blob` are renamed to `cyfmac43455-sdio.*`
only to match the existing driver paths, without editing any bytes.
The source WHENCE attributes these two blobs to Broadcom and specifies
LICENCE.broadcom_bcm43xx. That complete licence is shipped as
`FIRMWARE-BROADCOM-LICENCE.txt`, and WHENCE as `FIRMWARE-WHENCE.txt`.
The source folder's 43430 files are not packaged.
The Pi board file is byte-identical to our previous RPi-Distro version. Its
complete prior copyright notice is retained as `FIRMWARE-COPYRIGHT.txt` from
RPi-Distro/firmware-nonfree `c91cd2804cf7463aab913e7247c176049f16bbd6`.
These files retain their separate upstream licences, not this project's GPL.
The runtime sets a locally administered MAC before enabling the radio; the
upstream board calibration and regulatory blob are preserved.

The historical sections below describe the probe releases, not a hardware
validation claim for the integrated candidate.

## ReactOS / Ahmed Arif driver sources

This project uses the following source tree as a technical and architectural
reference:

- <https://github.com/ahmedarif193/reactos>
- pinned submodule revision: `9130f67a8e8c759da5acbbfe613f776b07b21698`
- relevant paths: `drivers/network/dd/rp1gem`,
  `drivers/network/dd/cyw43455`, `drivers/network/dd/cyw43455sdio`, and
  `drivers/bus/sd`

Those sources identify themselves as GPL-2.0-or-later. This repository is
distributed under GPL-3.0 and retains the upstream project and author history
through the pinned submodule. Firmware files in the upstream tree carry their
own Broadcom/Cypress notices; exp0.6.7 packages only the files described above.

The current direct-SDIO probe is not represented as a working Wi-Fi driver.

## Country protocol compatibility references (exp0.6.4)

exp0.6.6 references Ahmed ARIF's ReactOS CywSetCountry full-structure revision
-1 protocol (GPL-2.0-or-later, Copyright 2026 Ahmed ARIF). The bounded fallback
and tests here are independently implemented; no upstream firmware is bundled.
https://github.com/ahmedarif193/reactos/blob/929bdd689d1e18d0ef71214741d4e16eec74409c/drivers/network/dd/cyw43455/chip.c

The read-only country-list wire layout and command 261 are documented by
Infineon WHD whd_wifi_get_country_list and wl_country_list_t. No vendor source
is copied; our parser independently validates lengths/counts before reading.
https://github.com/Infineon/wifi-host-driver/blob/master/WHD/COMPONENT_WIFI6/src/whd_wifi_api.c
https://github.com/Infineon/wifi-host-driver/blob/master/WHD/COMPONENT_WIFI6/src/include/whd_wlioctl.h

exp0.6.5 additionally references Linux v6.12 brcmfmac/bcdc.c and fwil.c for
buffer metadata versus received control payload and IOVAR value-copy semantics.
The bounded parser and host integration tests are maintained here; no Linux
kernel build or runtime code is loaded on the development PC.
https://github.com/torvalds/linux/blob/v6.12/drivers/net/wireless/broadcom/brcm80211/brcmfmac/bcdc.c
https://github.com/torvalds/linux/blob/v6.12/drivers/net/wireless/broadcom/brcm80211/brcmfmac/fwil.c

The new connection policy is independently implemented. Linux v6.12 brcmfmac
cfg80211.c documents reading the current country before setting it and the
4345 ISO/revision-zero fallback (ISC license, Broadcom; license below).
https://github.com/torvalds/linux/blob/v6.12/drivers/net/wireless/broadcom/brcm80211/brcmfmac/cfg80211.c

The legacy four-byte country IOVAR wire format is also visible in the published
Broadcom/Cypress wlm.c utility's country-selection routine, blob
013f8af51faeb5dc31eb7c6133c5f9714f705c9a. No source from that utility is copied
or distributed here; it is a protocol reference only.
https://nest-open-source.googlesource.com/nest-learning-thermostat/6.1/cypress-utility/+/refs/heads/master/src/wl/exe/wlm.c

This compatibility attempt is not a regulatory certification. No alternate
country or arbitrary revision is selected when the requested locale is rejected.

## Linux brcmfmac register/clock-sequence reference

Source: Linux v6.12, drivers/net/wireless/broadcom/brcm80211/brcmfmac/sdio.c
and sdio.h, and include/brcm_hw_ids.h. The new chip probe uses the documented
F1 window, ALP clock sequence, and 4345 chip identifier. The Windows PIO and
bounded startup/restore implementation is maintained in this repository.

Copyright (c) 2010 Broadcom Corporation
Copyright (c) 2014 Broadcom Corporation

Driver 0.5 also references Linux v6.12 brcmfmac/chip.c EROM descriptor fields,
ARM CR4 capability register, and 4345 RAM-base mapping, plus
include/linux/bcma/bcma.h core IDs and the chipcommon.h EROM-pointer offset.
Its bounded parser and read-only discovery are not a complete Linux chip attach
port: CPU/core reset and RAM-bank writes are deliberately not performed.

Permission to use, copy, modify, and/or distribute this software for any
purpose with or without fee is hereby granted, provided that the above
copyright notice and this permission notice appear in all copies.

THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION
OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
