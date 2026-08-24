Raspberry Pi 5 Wi-Fi One-Click Diagnostics v0.2.1
=================================================

Run this only inside Windows 11 ARM64 on the Raspberry Pi 5 being tested.

1. Extract the entire ZIP.
2. Double-click Run-RPi5-WiFi-Diagnostics.cmd.
3. Approve the Windows Administrator prompt.
4. Wait for the green completion message.
5. Attach the RPI5-CYW43455-DIRECT-SDIO-DIAGNOSTICS-*.zip created on
   the Windows desktop.

The utility is read-only. It does not install or remove drivers, change BCD,
change Secure Boot, change the registry, restart Windows, or modify firmware.
It only creates temporary text reports and the final ZIP.

It can be run before or after installing the experimental driver. It collects:
- Windows, firmware, ARM64 and boot-security state;
- ACPI RPI0011 Wi-Fi, RPI000F fan and RPI0010 temperature device state;
- direct-SDIO probe stages, commands, responses and controller diagnostics;
- each bounded CMD5 attempt at 400, 200 and 100 kHz, including command-reset,
  raw response, interrupt, clock, power and line-state snapshots;
- target driver/service/network-adapter state and signatures;
- targeted SetupAPI and recent relevant event-log excerpts;
- dump-file metadata only (never dump contents).

Privacy:
- common user/computer/profile identifiers and MAC addresses are redacted;
- saved Wi-Fi profiles, passwords, browser data and file contents are not read;
- review the text files inside the final ZIP before sharing if desired.

This experimental probe does not yet provide working Wi-Fi. A successful CMD52
result proves only basic host-to-CYW43455 SDIO command communication.
