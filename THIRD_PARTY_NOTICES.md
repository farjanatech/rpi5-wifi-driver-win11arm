# Third-party notices

## Integrated candidate 0.6 references and redistributed firmware

`src/cyw43455/firmware.c` and `network.c` adapt sequences and wire formats from
Ahmed ARIF's `cyw43455/chip.c`, `fwil.c` and `cyw43455.h` at ReactOS revision
`9bb45e56ca5e456bfe40357a161b6ea60af0356e`.
Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>, GPL-2.0-or-later.
The Windows direct-host, Ethernet lifecycle and utility integration is maintained
here under GPL-3.0-or-later. `cyw43455sdio` is a GPL-3.0-or-later reference only;
its SD-bus companion driver is not copied or installed.

BCDC control flags, CR4 sizing and SDPCM formats also reference Linux v6.12
brcmfmac `bcdc.c`, `chip.c`, `sdio.c` and `sdio.h` under the ISC notice below.

CI redistributes unmodified Cypress firmware, CLM and Raspberry Pi 5 board
configuration from RPi-Distro/firmware-nonfree at
`c91cd2804cf7463aab913e7247c176049f16bbd6`. Exact paths and SHA-256 are pinned
in `scripts/fetch-firmware.ps1`. The complete upstream `debian/copyright`
is shipped as `FIRMWARE-COPYRIGHT.txt`. These files have their own licenses,
are for Cypress hardware, and are not relicensed under this project's GPL.
The firmware's standard image is renamed but its bytes are not modified.
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
own Broadcom/Cypress notices and are not included in the current driver package.

The current direct-SDIO probe is not represented as a working Wi-Fi driver.

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
