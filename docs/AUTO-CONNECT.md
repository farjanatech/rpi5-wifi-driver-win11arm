# Editable Wi-Fi configuration

Use only on the Raspberry Pi, after installing the driver and restarting.
This optional feature automates connecting; it does not fix packet loss or prove Internet access.

1. Put your private `WiFi.private.json` beside `Connect-RPi5-WiFi.cmd` in the extracted package.
   Alternatively, copy `WiFi.config.example.json` to that name and fill its three fields.
2. Run `Connect-RPi5-WiFi.cmd` to use the file without prompts. Without the private file,
   the usual interactive prompts remain available.
3. For connection at every Windows startup, run `Enable-RPi5-WiFi-Autoconnect.cmd` once
   and approve Administrator access. It copies the connector and configuration to
   `C:\ProgramData\RPi5WiFi` and registers `RPi5WiFi-AutoConnect` as SYSTEM.
4. To change Wi-Fi later, open Notepad **as Administrator**, then edit
   `C:\ProgramData\RPi5WiFi\WiFi.private.json`. Preserve JSON quotes and escape any
   password backslash as `\\` and quote as `\"`. Save and reboot, or run
   `Start-ScheduledTask -TaskName RPi5WiFi-AutoConnect` in Administrator PowerShell.
   Editing the package copy alone does not change the installed startup profile.
5. To remove the startup task, run `Disable-RPi5-WiFi-Autoconnect.cmd`.
   This preserves the driver and configuration. Delete the private file manually
   when no longer needed. Re-enabling copies the package profile again.

Country must be where the Pi is physically located. BD is Bangladesh.
Only WPA2-Personal/AES and passwords of 8–63 printable ASCII characters are supported.
SSID is case-sensitive, 1–32 UTF-8 bytes; no separate username is needed.

The editable JSON contains the password in **plaintext**. The installed directory
is limited to SYSTEM and Administrators, but administrators can still read it.
Keep the original file and any USB copy private; never upload it to GitHub,
include it in diagnostics, or send a screenshot of its contents. The public package
contains only a blank example. The diagnostic collector does not copy this file.
No credentials appear in task command-line arguments or connector output.

The startup task runs once per boot, waits up to 180 seconds for the driver and uses
the existing bounded firmware wait (up to 30 minutes while progressing). It will
not loop indefinitely, change UEFI/security settings, or reconnect continuously
after sleep/dropouts. Firmware or association failures still require diagnosis.
