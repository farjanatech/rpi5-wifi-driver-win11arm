# Third-party notices

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
