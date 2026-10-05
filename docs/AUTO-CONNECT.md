# Native Wi-Fi profiles and auto-connect

The current user-facing Wi-Fi application is the native C++ `RPi5-WiFi.exe`
installed by `RPi5-WiFi-Setup.exe`. The older C# connector and PowerShell GUI
are no longer part of `main`.

## Save a Wi-Fi profile

1. Open the **RPi5 Wi-Fi** Desktop shortcut.
2. Confirm the two-letter country code for the Pi's physical location.
3. Click **Scan / Refresh**.
4. Select the desired SSID. The selected row is highlighted green and the app
   shows the selected SSID, band, channel and RSSI above the password field.
5. Enter the WPA2-Personal/AES password.
6. Enable **Auto-connect after reboot** if desired.
7. Click **Save / Update Profile**.

The application stores the derived WPA2 PMK, not the plaintext password. The
saved key is protected with Windows DPAPI using machine scope and the profile
database is restricted to SYSTEM and Administrators.

Profiles are stored under:

`C:\ProgramData\RPi5 WiFi\profiles.bin`

## Auto-connect after restart

The installer creates the scheduled task:

`RPi5 WiFi AutoConnect`

At Windows startup the same native executable runs with `--autoconnect`. It
waits for the driver control device to become available, scans nearby networks,
and connects to the strongest supported saved profile that has auto-connect
enabled. It does not modify UEFI, BCD, Secure Boot or Windows Test Signing.

## Delete or change a profile

Open **RPi5 Wi-Fi**, select the saved profile, and use **Delete Profile**.
To change its password or auto-connect setting, select the network/profile,
enter the new password when needed, change the checkbox, and click
**Save / Update Profile**.

## Current connection limits

- WPA2-Personal/AES is supported.
- SSID must be 1-32 UTF-8 bytes.
- WPA2 password must be 8-63 printable ASCII characters.
- The country code must match the Pi's actual physical location.
- The driver remains an Ethernet-style NDIS adapter; scan/connect operations
  are performed through the driver's control interface rather than Windows WLAN.
