# Portable 0.7.0.1-fw229 — firmware compatibility A/B candidate

This candidate keeps the Portable 0.7.0 host driver behavior but restores the exact CYW43455 firmware/CLM pair from the previous working connector generation.

## Purpose

The physical Portable 0.7.0 test with firmware 7.45.265 completed firmware upload, SDIO runtime setup, CLM load and country handling, then failed at connection step 9 (`sup_wpa`) with firmware error -23 (unsupported). The host connection sequence at that point is unchanged from the previous working source.

To isolate the regression, this candidate changes the firmware package rather than bypassing the `sup_wpa` error.

## Restored firmware set

- CYW43455 firmware **7.45.229**, 631,467 bytes, SHA-256 `CF79E8E8727D103A94CD243F1D98770FA29F5DA25DF251D0D31B3696F3B4AC6A`
- matching CLM, 7,163 bytes, SHA-256 `2DBD7D22FC9AF0EB560CEAB45B19646D211BC7B34A1DD00C6BFAC5DD6BA25E8A`
- Pi 5 NVRAM/calibration remains SHA-256 `CA709BE81A78BDB6932936374F39943ACBD7AF07FAE6151011127599A3CE9E3D`

The firmware, CLM and calibration bytes are not edited.

## Driver and UEFI policy

Driver package version is **0.7.0.3** so Windows can distinguish/install this A/B candidate over Portable 0.7.0. UEFI revision strings remain informational. The installer still requires the compatible `ACPI\RPI0011` direct-SDIO interface rather than a fixed UEFI release.

Portable automatic regulatory mode remains unchanged: it uses the firmware/CLM domain already active and does not force another unsupported country.

## Lightweight WiFi Manager

The release uses a new native Win32 ARM64 manager instead of the self-contained .NET executable. It has no .NET runtime dependency and provides only the connection controls needed here:

- nearby SSID scan;
- Auto / 2.4 GHz / 5 GHz choice;
- SSID;
- WPA2 password;
- Connect and Disconnect;
- clear connection/driver status.

There is no username field because the driver implements WPA2-Personal/AES, not Enterprise/802.1X.

## Performance utility

The complete driver package continues to include `Test-RPi5-WiFi-Performance.cmd` and its timing/radio/transport dependencies. Performance testing should be run only after the adapter successfully authenticates.

## Test interpretation

If this candidate gets past `sup_wpa` and associates using the same Pi/router/SSID, the result strongly isolates firmware 7.45.265 as the compatibility regression. If it still fails at the same step, investigate a remaining host/package difference instead of assuming firmware is the cause.
