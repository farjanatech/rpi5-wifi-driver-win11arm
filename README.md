# Raspberry Pi 5 CYW43455 Wi-Fi Driver for Windows 11 ARM64

**Current release: v0.7.1.20**

Hardware-tested Windows 11 ARM64 driver for the Raspberry Pi 5 onboard **Broadcom / Infineon CYW43455** Wi-Fi controller.

## Latest Features

| Feature | Current v0.7.1.20 |
|---|---|
| **Platform** | Raspberry Pi 5 running Windows 11 ARM64 |
| **Wi-Fi hardware** | Onboard CYW43455 connected through direct SDIO access |
| **Driver architecture** | ARM64 kernel-mode **NDIS 6.30 miniport** |
| **Windows network interface** | Presented to Windows as an **Ethernet-style NDIS network adapter** |
| **2.4 GHz Wi-Fi** | Supported and selectable through the driver band policy |
| **5 GHz Wi-Fi** | Supported; 5 GHz is preferred when a suitable signal is available |
| **Wi-Fi scanning** | Scans nearby SSIDs and reports band, channel, RSSI and security |
| **Connection security** | WPA2-Personal / AES |
| **Native Wi-Fi Manager** | Lightweight native Win32 C++ `RPi5-WiFi.exe` |
| **Connection controls** | Scan, Connect and Disconnect through the driver's custom IOCTL interface |
| **Saved Wi-Fi profiles** | Save, update and delete Wi-Fi profiles |
| **Credential protection** | Stores the derived WPA2 PMK protected with Windows DPAPI; plaintext Wi-Fi passwords are not stored |
| **Auto-connect after reboot** | Native Wi-Fi Manager can automatically connect to the strongest enabled saved SSID at Windows startup |
| **Installer** | Native standalone `RPi5-WiFi-Setup.exe` installs the driver, firmware files and Wi-Fi Manager and creates a Desktop shortcut |
| **UEFI independence** | No dependency on a specific UEFI project, build or version; a compatible ACPI hardware description is required |
| **Runtime stability** | v0.7.1.20 includes corrected SDIO completion/deadline handling and one bounded runtime adapter recovery path |
| **Hardware-tested performance** | Approximately **80 Mbps download / 80 Mbps upload** on the tested Raspberry Pi 5 setup |
| **Release signing** | Test-signed driver; Windows Test Signing is required |

## Driver Architecture

The driver uses a direct SDIO architecture rather than the normal Windows WLAN stack.

```text
UEFI / platform firmware
        |
        v
ACPI tables
        |
        v
ACPI\RPI0011
(platform SDIO device + compatible hardware resources)
        |
        v
Windows Plug and Play
        |
        v
rpi5cyw.inf
(matches ACPI\RPI0011 on Windows ARM64)
        |
        v
rpi5cyw.sys
(ARM64 kernel-mode NDIS miniport)
        |
        +----------------------+
        |                      |
        v                      v
Direct SDIO path        Custom control device
to CYW43455             \\.\Rpi5CywControl
        |                      |
        |                      v
        |                RPi5-WiFi.exe
        |                Scan / Connect /
        |                Profiles / Auto-connect
        |
        v
CYW43455 Wi-Fi firmware + radio
        |
        v
NDIS Ethernet-style network adapter
        |
        v
Windows TCP/IP stack
        |
        v
Applications / Internet
```

## UEFI / ACPI Requirement

The driver does **not** require a particular UEFI implementation or UEFI version.

For Windows to load the driver, the platform firmware must expose the compatible ACPI device:

```text
ACPI\RPI0011
```

That ACPI node must provide the compatible Raspberry Pi 5 SDIO hardware/resource structure that the driver uses to reach the onboard CYW43455.

The binding sequence is:

```text
UEFI exposes ACPI\RPI0011
        ->
Windows PnP enumerates the device
        ->
rpi5cyw.inf matches ACPI\RPI0011
        ->
Windows loads rpi5cyw.sys
        ->
Driver initializes the SDIO controller and CYW43455
        ->
NDIS network adapter becomes available
        ->
RPi5-WiFi.exe controls scan/connect through IOCTLs
```

If `ACPI\RPI0011` is **not exposed**, the driver package may remain staged in the Windows Driver Store, but the driver cannot bind to the hardware until a compatible firmware/UEFI exposes that node.

## Release

[Download RPi5-WiFi-Setup.exe](https://github.com/farjanatech/rpi5-wifi-driver-win11arm/releases/download/v0.7.1.20/RPi5-WiFi-Setup.exe)
