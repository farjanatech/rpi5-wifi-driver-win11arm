# Architecture

## Goal

Provide Windows 11 ARM64 support for the Raspberry Pi 5 onboard CYW43455 while
presenting the completed data path to Windows as an Ethernet adapter. Keep the
direct SDIO host, CYW43455 protocol and NDIS layers separate.

## Layers

### 1. Windows integration

The branch uses an NDIS 6.30 Ethernet miniport based on the proven RP1 GEM
Windows-facing structure. It will eventually own Ethernet TX/RX queues, link
state, statistics and power transitions. Wi-Fi scanning, credentials and
association require a separate, explicit control design; an Ethernet miniport
does not receive Windows WLAN requests.

### 2. CYW43455 core

Hardware-specific code belongs under `src/cyw43455/` and should remain mostly independent of WDF/WiFiCx. Planned components include:

- backplane/core enumeration;
- firmware download;
- NVRAM handling;
- CLM download;
- BCDC control messages;
- SDPCM framing;
- firmware events.

### 3. SDIO transport

`src/sdio/` owns direct access to the Raspberry Pi 5 SDIO2 SDHCI controller.
The matching UEFI exposes that controller as `ACPI\RPI0011` and deliberately
prevents Microsoft `sdbus` from binding to it.

Initial transport goals:

- validate and map the SDIO2 MMIO resource;
- reset and initialize the dedicated host controller;
- CMD52/direct-byte access;
- CMD53 extended transfers;
- function block-size configuration;
- function enable/readiness;
- interrupt registration;
- safe PnP/power teardown without affecting the microSD controller.

## Source-port policy

The ReactOS CYW43455 implementation is a hardware/firmware reference, not the Windows-facing architecture. Port code in small reviewable units and preserve upstream copyright/license notices for copied or derived material.

Do not copy ReactOS-only debug, build, device-interface, or Native-802.11 glue into this project unless it is explicitly adapted for Windows and justified.
