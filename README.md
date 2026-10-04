> **`new-improvement-tx-glom` candidate: v0.7.1.10.** This branch preserves
> the v0.7.1.9 64-frame active queue + 128-frame backlog and tests only
> negotiated, pressure-only **two-frame host TX glom**. Keep v0.7.1.9 as the
> hardware rollback. See [TX glom 0.7.1.10](docs/TX-GLOM2-0.7.1.10.md).
>
> **`new-improvement` rollback candidate: v0.7.1.9.** This branch keeps the proven
> 64-frame active TX window from v0.7.1.8 and adds a separate bounded 128-frame
> pending-NBL backlog so temporary active-queue pressure can be retained instead
> of immediately rejected. It is a CI/hardware-validation candidate, not yet the
> default release. See [TX backlog 0.7.1.9](docs/TX-BACKLOG-0.7.1.9.md).
>
> **Current default: v0.7.1.8 on `main`.** The promoted mode-1 path keeps the
> stable v0.7.1.4 TX budgets, uses earlier post-RX dispatch, skips futile TX
> attempts when firmware credits are exactly exhausted, and uses bounded fast
> event wakes before the existing 10 ms fallback. The original v0.7.1.4 remains
> the full rollback reference.
>
# Raspberry Pi 5 CYW43455 Wi-Fi driver for Windows 11 ARM64

Experimental Windows 11 ARM64 driver work for the Raspberry Pi 5 onboard Infineon/Cypress CYW43455 Wi-Fi controller.

## Current capabilities and remaining improvement areas

The project has moved beyond bring-up experiments into a hardware-tested,
internet-capable driver. The table below summarizes the current state without
keeping the long historical milestone log in the main README.

| Area | What we have achieved | What may still need improvement |
|---|---|---|
| **Current release** | **v0.7.1.8 is the default on `main`**, with mode 1 promoted after the best recent hardware result and a full green CI run. | Continue validating the promoted build over longer real-world use and keep v0.7.1.4 available as a rollback reference. |
| **Hardware bring-up** | Direct Raspberry Pi 5 SDIO2 access to the onboard CYW43455 is working through the `ACPI\\RPI0011` device, including chip/core discovery, firmware startup and runtime transport. | Broader UEFI/platform compatibility is still limited; the driver remains targeted at the tested Raspberry Pi 5 setup. |
| **Connection & Internet** | WPA2-Personal/AES connection, DHCP/IP traffic, gateway access, DNS and HTTPS/Internet traffic have been demonstrated on real hardware. | WPA3, enterprise authentication and native Windows Wi-Fi/WLAN UX are not the current focus; Windows still sees an Ethernet-style NDIS adapter. |
| **TX path** | `main` has the proven **64-frame** active queue and mode-1 credit scheduling. `new-improvement` adds the bounded **128-frame deferred backlog**. `new-improvement-tx-glom` keeps both limits unchanged and may combine exactly **2** one-frame NBLs into one F2 transfer under pressure. | Hardware-validate whether two-frame glom drains backlog pressure faster without latency, throughput, lifecycle, or stability regression. |
| **RX path** | Bounded RX batching, validated SDPCM parsing, read-ahead handling and glom validation are implemented with malformed/fault paths covered by tests. | Further RX changes should only be made if measurements identify RX as a real bottleneck; current evidence points more strongly to TX queue/credit pressure. |
| **SDIO transport** | Direct CMD52/CMD53 PIO transport, runtime flow control, bounded polling, error propagation and tested high-speed/4-bit operation are in place. | DMA/block-mode or more aggressive transfer batching could improve efficiency, but should be pursued only after timing data proves SDIO transaction overhead is the limiting factor. |
| **Stability** | Recent v0.7.1.8 hardware testing completed the full download workload with no observed disconnect, worker failure, FIFO failure, RX glom error, CMD53 timeout or interrupt-storm fallback. | Longer soak tests, sleep/resume, D0/D3, Pause/Restart, adapter restart and AP/router reconnect should continue to be exercised. |
| **Performance** | The best recent v0.7.1.8 Internet workload reached about **27.9 Mbps**, materially better than the immediately preceding experimental builds. | Improve repeatability and reduce queue-full events without sacrificing latency or stability; do not increase queue size simply to hide pressure. |
| **Diagnostics** | One-click diagnostics, readiness checks, transport/radio/timing snapshots, TX-credit diagnostics and performance tooling are available for hardware analysis. | Keep normal release overhead low and use detailed timing only when needed for targeted bottleneck analysis. |
| **CI / regression safety** | Optimized ARM64 builds, both TX modes, ASAN host tests, transport/ownership/lifecycle tests, packaging, signing and rollback/source-isolation guards are automated. | Physical Pi testing remains necessary for performance and radio behavior; CI cannot prove real RF/Internet performance. |
| **Release status** | The driver is usable on the tested Raspberry Pi 5 and v0.7.1.8 is now the project default. | It is still **test-signed**, not a Microsoft production-signed general-purpose Windows driver. |

Detailed historical experiment notes remain available under `docs/` for anyone
who needs the development record, but they are intentionally no longer repeated
in the main README.

## Architecture

```text
Windows 11 ARM64
    |
    +-- NDIS 6.30 Ethernet miniport
    |
    +-- CYW43455 firmware/control layer (integrated experimental candidate)
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
- `docs/` - architecture, build and bring-up notes.

## Branch strategy

`main` is the current default release branch. New performance or stability
work should be isolated on experimental branches and promoted only after the
relevant CI checks and Raspberry Pi hardware validation.

## Status

v0.7.1.8 on `main` is hardware-tested and Internet-capable on the matching
Raspberry Pi 5 setup. The project is still experimental/test-signed rather than
a production-certified Windows Wi-Fi driver. Keep recovery access and a known
rollback package available when testing new changes.

## License

GPL-3.0. See `LICENSE`.
