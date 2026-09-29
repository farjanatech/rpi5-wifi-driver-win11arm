# Portable 0.7.0: country-neutral Wi-Fi manager and UEFI capability policy

This branch removes the experimental **US-only host admission** and the old exact-UEFI revision dependency. It is intended for Raspberry Pi 5 Windows 11 ARM64 systems whose UEFI exposes the direct-SDIO interface required by the driver.

## UEFI policy

The installer does **not** require a named UEFI release or commit. BIOS/UEFI version text is informational only. Installation still requires the actual Raspberry Pi 5 direct-SDIO ACPI device (`ACPI\\RPI0011`) because that hardware interface, resources and controller ownership are what the driver binds to.

A future or custom UEFI can therefore work without an installer update if it continues to expose the same compatible interface. A matching version string alone is never treated as proof of compatibility.

## Country / regulatory behavior

The new WiFi Manager defaults to **Automatic** regulatory mode. In this mode it does not send a guessed country code and does not substitute US or another country. It reads and uses the regulatory domain already active in the firmware/CLM and scans/joins only channels that firmware permits.

Advanced two-letter ISO country requests remain supported by the protocol. Those requests still require firmware acceptance and matching readback. The driver does not patch CLM data, radio power tables, DFS rules or firmware regulatory enforcement.

This means the application can be used in locations whose ISO code is not listed by this firmware, but only with the firmware's existing regulatory domain and channel set. It intentionally does not force an unsupported country into the radio.

## WiFi Manager

`RPi5-WiFi-Connector.exe` is renamed in the UI to **WiFi Manager** and remains a single compressed ARM64 executable. It provides:

- nearby SSID scan;
- Auto / 2.4 GHz / 5 GHz connection preference;
- WPA2-Personal password entry;
- protected optional reconnect-at-boot profile;
- explicit disconnect / forget controls;
- a dark compact interface based on the supplied design reference.

The username field is visible but disabled because the current driver implements WPA2-Personal/AES, not 802.1X/EAP Enterprise authentication. WPA3 and Open network modes are also not claimed by this branch.

For an explicit 2.4 or 5 GHz request the driver verifies the associated channel. It does not silently publish a connection on the opposite band. If the firmware does not implement the needed join preference, explicit band selection fails instead of pretending the request was honored.

## Firmware

The firmware and CLM bytes remain pinned and unmodified. This branch changes host policy and UI behavior, not regulatory data. Keep a previously working package for rollback and validate on physical Pi hardware before treating the branch as production-ready.
