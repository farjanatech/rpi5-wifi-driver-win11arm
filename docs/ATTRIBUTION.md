<!-- Project documentation: Farjana Akter. Existing third-party notices retain their authorship. -->

# File attribution

This driver is a rework of **Ahmed ARIF**'s Raspberry Pi drivers from his
[LiberNT repository](https://github.com/ahmedarif193/LiberNT) (formerly
`ahmedarif193/reactos`): the RP1 GEM NDIS miniport (`rp1gem`), the SD/SDIO bus
driver (`drivers/bus/sd`) and the CYW43455 Wi-Fi driver (`cyw43455`).
Original driver code: Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>.
Windows direct-SDIO rework and maintenance: **Farjana Akter**.

Historical `ahmedarif193/reactos` URLs and pinned revisions in this project are
retained for reproducibility. Each file reworked from Ahmed ARIF's driver says
so in its header, with his copyright first.
Existing Linux, Broadcom, ReactOS and other third-party notices still apply to
their respective material. These headers do not change any existing license.

## Files reworked from Ahmed ARIF's driver

These files are Ahmed ARIF's driver code reworked for Windows and carry his notice,
`Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>`.

| Project file | Upstream contribution and provenance |
| --- | --- |
| `src/driver/driver.c` | NDIS miniport/OID handling adapted from `drivers/network/dd/rp1gem/rp1gem.c` at A; introduced in local commit `aa1cfa47523403ca064efd54a151914bd4bd81e1`. Shared portions include OID tables, query-data and link-state handling. |
| `src/driver/driver.h` | NDIS adapter declarations and definitions corresponding to `drivers/network/dd/rp1gem/rp1gem.h` at A. Direct-SDIO context and later diagnostics are local extensions. |
| `src/cyw43455/firmware.c` | Chip reset, firmware/NVRAM/CLM loading and bring-up sequences from `drivers/network/dd/cyw43455/chip.c` at B; introduced in local commit `63f2152b58437469a082f1a8f6660a1e0125154c`. |
| `src/cyw43455/network.c` | SDPCM/BCDC transport, firmware-event and WPA2 integration from `cyw43455/fwil.c` and `cyw43455.h` at B. `CywLink` link-state indication from `Rp1GemIndicateLinkState` in `drivers/network/dd/rp1gem/rp1gem.c` at A. Windows Ethernet lifecycle and later bounded-worker integration are local work. |
| `src/cyw43455/network_protocol.h` | SDPCM/BCDC wire-format adaptations introduced with the network integration, using `cyw43455.h`/`fwil.c` at B and the documented Broadcom/Linux sources. Local validators and control ABI remain project work. |
| `src/cyw43455/control.h` | BCDC command/IOVAR transport extracted from attributed `network.c` in local commit `cea93c0b725b8d5f21f3667a9272dae04b3cd15f`; upstream lineage is `cyw43455/fwil.c` at B. |
| `src/cyw43455/connection.h` | WPA2 sequencing extracted from attributed `network.c` in local commit `54920b3`; upstream `cyw43455/fwil.c` at B, plus the later `CywSetCountry` reference in `chip.c` at C. |
| `src/cyw43455/transport_poll.h` | Receive polling extracted from attributed `network.c` in local commit `6e7f31363b22c2d9c9f67dda0da1f8cf225f71f6`; lineage includes `cyw43455/fwil.c` at B and receive read-ahead reference to `CywBusThread` at C. Later Linux glom/recovery references retain separate notices. |
| `src/sdio/sdio.c` | SDHCI host reset, clock setup, power/interrupt initialization, command issue and CMD52 from `drivers/bus/sd/fdo.c` (`SdBusResetHost`, `SdBusInitializeController`), `enum.c` (`SdBusSendCommand`) and `sdio.c` (`SdBusSdioCmd52`) at A; introduced in local commit `b194210`. The CMD5 retry loop, CMD53 PIO/FIFO engine, bus-speed negotiation and recovery are local work. |
| `src/sdio/sdio.h` | SDHCI register, command and SDIO OCR definitions from `sdk/include/reactos/drivers/sd/sdhci.h` and `drivers/bus/sd/enum.c` at A; introduced in local commit `b44b60e`. Local adapter interfaces remain project work. |
| `src/sdio/sdio_protocol.h` | `SdioCalculateClockDivider` from `SDHCI_CALC_CLK_DIVIDER` in `sdk/include/reactos/drivers/sd/sdhci.h` at A; introduced in local commit `b194210`. Argument builders and validators are local work. |
| `package/rpi5cyw.inf` | NDIS install sections from `drivers/network/dd/rp1gem/netrp1gem.inf` at A; introduced in local commit `7701560`. Device ID, strings, firmware copy and packaging are local work. |
| `src/cyw43455/transport_send.h` | SDPCM sending extracted from attributed `network.c` in local commit `6e7f313`; upstream lineage is `cyw43455/fwil.c` at B. Later Linux host-TX-glom work is separately credited in `THIRD_PARTY_NOTICES.md`. |

The source headers at these historical revisions specify GPL-2.0-or-later.
The existing GPL-3.0-or-later notices on this project's adaptations are retained.
The current LiberNT branch is not substituted for the historical source license.

- **A:** [`9130f67a8e8c759da5acbbfe613f776b07b21698`](https://github.com/ahmedarif193/LiberNT/tree/9130f67a8e8c759da5acbbfe613f776b07b21698/drivers/network/dd), also pinned by `third_party/reactos-ahmed`.
- **B:** [`9bb45e56ca5e456bfe40357a161b6ea60af0356e`](https://github.com/ahmedarif193/LiberNT/tree/9bb45e56ca5e456bfe40357a161b6ea60af0356e/drivers/network/dd/cyw43455).
- **C:** [`929bdd689d1e18d0ef71214741d4e16eec74409c`](https://github.com/ahmedarif193/LiberNT/tree/929bdd689d1e18d0ef71214741d4e16eec74409c/drivers/network/dd/cyw43455).

## Files using Ahmed ARIF's work as a reference

These files name Farjana Akter for the local implementation and credit Ahmed
ARIF as a design/protocol reference. This does not label them verbatim copies.

| Project file | Reference |
| --- | --- |
| `src/cyw43455/chip.c` | `cyw43455/chip.c` at A: register/clock and chip-discovery reference alongside Linux brcmfmac. |
| `src/cyw43455/tx_queue.h` | `CywDrainTxQueue`/`CywQueueTxWork` in `cyw43455/cyw43455.c` and the bus worker in `fwil.c` at C. |
| `src/cyw43455/tx_types.h` | Local bounded pending-NBL types introduced with the queue design in local commit `b94ec1c`, whose reference is the same upstream queue work at C. |
| `src/cyw43455/scan_protocol.h` | Escan formats cross-checked against `cyw43455/cyw43455.h`; primary wire-format and parser references remain the documented Linux sources. |
| `src/cyw43455/scan_sequence.h` | Escan use cross-checked against `cyw43455/fwil.c`; the bounded Windows sequence uses the documented Linux protocol reference. |

## Other project files

All remaining project-authored source, tests, scripts, installation files,
build configuration and documentation carry Farjana Akter's attribution.
Reference-only Linux/Broadcom notices already present in those files are kept.
Including an attributed production header in a test does not make the test a
copy of that upstream source.

Two files use adjacent attribution files to preserve their formats and bytes:

- `native/Assets/RPi5-WiFi.ico`: Farjana Akter's credit is in
  `native/Assets/RPi5-WiFi.ico.attribution.txt`.
- `utility/WiFi.config.example.json`: Farjana Akter's credit is in
  `utility/WiFi.config.example.json.attribution.txt`.

The GPL text in `LICENSE` retains the Free Software Foundation's authorship and
is unchanged. The `third_party/reactos-ahmed` submodule retains its upstream
authors, revision and contents. Firmware, calibration data and third-party
license texts fetched when packaging retain their separate upstream attribution;
they are not assigned to Farjana Akter or Ahmed ARIF merely because this project
redistributes them. See [third-party notices](../THIRD_PARTY_NOTICES.md).

When moving adapted code into another file, carry its upstream attribution with
it and update this file's provenance table.
