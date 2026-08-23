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
