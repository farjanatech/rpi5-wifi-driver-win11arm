> **Current default release: v0.7.1.20 on `main`.**
> This is the Raspberry Pi 5 hardware-validated build promoted after sustained
> real-world use with approximately **80 Mbps download / 80 Mbps upload** and no
> recurrence of the prior runtime disconnect. The exact tested source is frozen
> at **`c16aa318da490350126739add45223a186ab0a47`** on
> **`release-v0.7.1.20`**.
>
> v0.7.1.20 preserves the adaptive TX hybrid: Ethernet frames up to 512 bytes
> may use the bounded deferred backlog, Glom2 and Service-Burst4, while larger
> bulk-upload frames use active-only admission with a fresh F1 service before
> every F2 transfer. The stability update accepts an already-observed SDHCI
> readiness/completion event before declaring the software deadline expired and
> permits one bounded adapter lifecycle recovery after a fatal runtime transport
> fault. Failed/partial FIFO transactions are never replayed.
>
# Raspberry Pi 5 CYW43455 Wi-Fi driver for Windows 11 ARM64

Experimental Windows 11 ARM64 driver work for the Raspberry Pi 5 onboard Infineon/Cypress CYW43455 Wi-Fi controller.

## Current capabilities and remaining improvement areas

The project has moved beyond bring-up experiments into a hardware-tested,
internet-capable driver. The table below summarizes the current state without
keeping the long historical milestone log in the main README.

| Area | What we have achieved | What may still need improvement |
|---|---|---|
| **Current release** | **v0.7.1.20 is the default on `main`**. The exact hardware-tested source is frozen on `release-v0.7.1.20` at `c16aa318da490350126739add45223a186ab0a47`. | Keep future performance/stability experiments isolated from the frozen release and promote only after hardware validation. |
| **Hardware bring-up** | Direct Raspberry Pi 5 SDIO2 access to the onboard CYW43455 is working through the `ACPI\\RPI0011` device, including chip/core discovery, firmware startup and runtime transport. | Broader UEFI/platform compatibility is still limited; the driver remains targeted at the tested Raspberry Pi 5 setup. |
| **Connection & Internet** | WPA2-Personal/AES connection, DHCP/IP traffic, gateway access, DNS and HTTPS/Internet traffic have been demonstrated on real hardware. | WPA3, enterprise authentication and native Windows Wi-Fi/WLAN UX are not the current focus; Windows still sees an Ethernet-style NDIS adapter. |
| **TX path** | The v0.7.1.20 adaptive hybrid keeps a **64-frame active queue**. Frames up to **512 bytes** may use the bounded **128-frame deferred backlog**, Glom2 and Service-Burst4; frames above 512 bytes use active-only admission and fresh-F1-per-frame pacing. | Preserve this hardware-validated policy as the baseline; test any future TX change independently before promotion. |
| **RX path** | Bounded RX batching, validated SDPCM parsing, read-ahead and glom validation are implemented. v0.7.1.20 also fixes the block-wait deadline ordering exposed by the prior RX-glom timeout. | Keep RX behavior unchanged unless new diagnostics identify a reproducible bottleneck or fault. |
| **SDIO transport** | Direct CMD52/CMD53 fixed-address PIO, negotiated block mode, runtime flow control, bounded polling, error propagation and tested 50 MHz / 4-bit operation are in place. Already-observed SDHCI completion/readiness wins over an expired software deadline; genuine partial FIFO failures remain terminal/no-replay. | Treat the v0.7.1.20 transport as frozen release behavior; pursue any DMA or deeper batching only on experimental branches. |
| **Stability** | v0.7.1.20 completed extended normal use after the prior timeout fix with no observed disconnect recurrence. A fatal runtime transport fault also has one bounded adapter lifecycle recovery path, without replaying the failed FIFO transaction. | Continue broader soak, sleep/resume, D0/D3, Pause/Restart and AP reconnect testing while keeping the release branch unchanged. |
| **Performance** | Hardware testing of the promoted v0.7.1.20 build reached approximately **80 Mbps download / 80 Mbps upload** on Fast.com under the tested setup. | Treat this result as the current performance reference and reject future changes that regress either direction or stability. |
| **Diagnostics** | One-click diagnostics, readiness checks, transport/radio/timing snapshots, TX-credit diagnostics and performance tooling are available for hardware analysis. | Keep normal release overhead low and use detailed timing only when needed for targeted bottleneck analysis. |
| **CI / regression safety** | Optimized ARM64 builds, both TX modes, ASAN host tests, transport/ownership/lifecycle tests, packaging, signing and rollback/source-isolation guards are automated. | Physical Pi testing remains necessary for performance and radio behavior; CI cannot prove real RF/Internet performance. |
| **Release status** | **v0.7.1.20 is the promoted default on `main`**, with its exact tested source frozen on `release-v0.7.1.20`. | It remains **test-signed**, not a Microsoft production-signed general-purpose Windows driver. |

The repository keeps only operational documentation needed for the current
release. Historical architecture/bring-up/experiment notes are preserved in Git
history and in the frozen release source instead of cluttering `main`.

## Architecture

```text
Windows 11 ARM64
    |
    +-- NDIS 6.30 Ethernet miniport
    |
    +-- CYW43455 firmware/control layer
    |       +-- BCDC / SDPCM / chip backplane
    |
    +-- direct SDIO2 host transport
            +-- SDHCI CMD5/CMD52/CMD53
            +-- CYW43455
```

## Repository layout

- `src/driver/` - Windows kernel driver entry/PnP scaffolding.
- `src/sdio/` - direct SDHCI PIO transport (no Microsoft SD bus dependency).
- `src/cyw43455/` - CYW43455-specific chip/firmware protocol code.
- `package/` - driver INF/package files.
- `docs/BUILDING.md` - current ARM64 build/signing notes.
- `docs/AUTO-CONNECT.md` - optional startup/autoconnect instructions.

## Branch strategy

`main` is the current default release branch. `release-v0.7.1.20` is the frozen
hardware-tested v0.7.1.20 source reference and should not be advanced. New
performance or stability work should be isolated on experimental branches and
promoted only after full CI plus Raspberry Pi hardware validation.

## Status

v0.7.1.20 on `main` is the current hardware-tested default for the matching
Raspberry Pi 5 setup, with approximately 80/80 Mbps observed in the validated
Fast.com test and stable extended use after the RX/FIFO timeout fix. The project
is still experimental/test-signed rather than a production-certified Windows
Wi-Fi driver. Keep the frozen `release-v0.7.1.20` reference available when
testing future changes.

## License

GPL-3.0. See `LICENSE`.
