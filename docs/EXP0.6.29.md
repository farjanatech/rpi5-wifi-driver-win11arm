# exp0.6.29 — C# connector and guarded faster firmware startup

Experimental Windows 11 ARM64 Raspberry Pi 5 candidate. The previous .28 Pi
test connected through the separate scanning app, completed 128/128 downloads
at 26.06 Mbps and retained under-load queue pressure. This release does not
claim a browsing-speed improvement or zero-delay startup before hardware testing.

## One EXE connector

Download `RPi5-WiFi-Connector.exe` from this release. This is a self-contained C#
WinForms ARM64 executable: no PowerShell launcher and no separate .NET install.
Its embedded runtime extracts native runtime files when required; "one EXE"
describes distribution, not a promise of no local application data.

The matching driver must already be installed. The EXE works with .28 scanning
and status ABIs as well, but .28 retains the long firmware initialization.

1. On the **Pi**, extract the .29 driver ZIP into a fresh folder, run
   `Install-RPi5-WiFi-Driver.cmd`, then restart once. Keep the complete .28 package.
2. Open the C# EXE and approve administrator access. Confirm the actual country
   (`BD` for a Pi in Bangladesh), scan when ready, select a WPA2-Personal/AES
   SSID, enter the password and connect. No scan occurs during an active link.
3. Click **Save & enable at Windows startup** and approve the confirmation.
   This saves one encrypted network key and copies the EXE to a protected
   `%ProgramData%\RPi5WiFiConnector` directory. No credentials go in task arguments.
4. On later boots, the SYSTEM task starts without a configured delay, before
   sign-in when Task Scheduler runs. It waits for actual firmware readiness,
   then submits one connection request. An existing authenticated connection is
   left alone. Driver/OS/association/DHCP still take time: **instant is not promised**.

You can change country/SSID/password in the app and save again. **Disable
startup** leaves the saved profile/current connection intact. **Forget saved
network** disables this task and deletes its saved key, not the current link.
Closing the app does not disconnect. A known pending scan is cancelled on close.
Existing PowerShell startup profiles/tasks are never silently replaced; disable
an enabled old task with its original utility before enabling the new one.

WPA3-only, enterprise authentication and the native Windows taskbar Wi-Fi menu
are not implemented. This is not a continuous reconnect/reset service.

## Credential and startup security

The app stores a DPAPI machine-encrypted derived WPA2 key, not the plaintext
password. The directory, EXE and profile are restricted to SYSTEM/Administrators;
machine encryption alone is not a user-isolation boundary. Administrators can
access saved credentials. No private profile or key is shipped in releases.
The app rejects redirected/untrusted startup paths and unrelated tasks with its
task name. It does not change boot security, UEFI, DNS, routes or the driver store.
Startup status contains only outcome/time, not the password, SSID or key.

The driver package remains test-signed. The C# EXE is not Microsoft/WHQL-certified;
Windows may show an unknown-publisher warning. Check release hashes; do not
disable system protection to run it.

## Startup-only driver change

Before RAM writes, while the firmware CPU is held, require an explicit host
clock and a full-speed SDIO card, select the existing default 4-bit/25MHz setup,
then validate 16 chip-ID reads. Only verified mode uses the existing bounded
fast polling. Failed preflight can restore and verify the original slow mode;
unsafe recovery/voltage state or cancellation stops startup.

Keep 64-byte F1 chunks and verify **every uploaded firmware byte**. Restore and
verify identification mode before the original NVRAM/vector/CPU-start sequence.
The post-firmware operating-bus negotiation, connected packet scheduler, queues,
TX/RX ownership, authentication, firmware/CLM/calibration bytes remain unchanged.
RAM transfer or readback corruption fails closed; it is not automatically ignored
or retried indefinitely. New diagnostics record startup bus, fallback, status and
upload/readback elapsed time (not complete Windows boot time).

GitHub checks build ARM64, exercise actual startup code with mocked bus failures,
preserve the old traffic source outside exact startup/diagnostic splices, test C#
protocol/key/state logic, validate the boot-task XML without installing it, and
render the real GUI offline. These cannot prove Pi startup speed or reboot
autoconnection. Keep Ethernet and .28 available for recovery. Reinstalling .28
and restarting restores the prior driver; disable the new app's startup task if
you no longer want it. Do not reflash UEFI or reinstall Windows.
