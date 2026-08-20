# Architecture

## Goal

Provide Windows 11 ARM64 support for the Raspberry Pi 5 onboard CYW43455 Wi-Fi controller while keeping hardware/firmware logic separate from the Windows WLAN-facing layer.

## Layers

### 1. Windows integration

The final WLAN-facing implementation should target WiFiCx/NetAdapterCx rather than extending the legacy Native 802.11 miniport design.

This layer will eventually own:

- adapter creation and capabilities;
- scan/connect/disconnect requests;
- authentication/cipher configuration;
- TX/RX queues;
- link-state and statistics reporting;
- power-management integration.

It is intentionally not part of the first hardware bring-up commit.

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

`src/sdio/` owns Windows SD-bus interaction. It must provide a small transport API to the CYW layer rather than leaking SDBUS details throughout the driver.

Initial transport goals:

- discover the SDIO function number;
- open the SD bus interface;
- CMD52/direct-byte access;
- CMD53 extended transfers;
- function block-size configuration;
- function enable/readiness;
- interrupt registration;
- safe PnP/power teardown.

## Source-port policy

The ReactOS CYW43455 implementation is a hardware/firmware reference, not the Windows-facing architecture. Port code in small reviewable units and preserve upstream copyright/license notices for copied or derived material.

Do not copy ReactOS-only debug, build, device-interface, or Native-802.11 glue into this project unless it is explicitly adapted for Windows and justified.
