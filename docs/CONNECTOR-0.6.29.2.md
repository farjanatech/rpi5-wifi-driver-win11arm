# Connector 0.6.29.2 — standalone size, startup controls and clear SSID selection

This is an app-only update. Keep driver 0.6.29.0, its exp0.6.29.1 installer,
and working UEFI exp.0.5. No driver uninstall/reinstall or firmware replacement.

## Fixes

- Missing startup tasks can be returned by .NET COM interop as
  `FileNotFoundException` (`0x80070002`), not just `COMException`. The old
  catch missed that case, blocking startup status, save, disable and forget.
  Only the task lookup treats the exact missing-task HRESULTs as absent.
  Access denied, scheduler errors and unrelated missing files still fail.
- The chosen SSID remains marked **Selected** and highlighted while typing
  the password, after clicking another field, and after refreshing scan results.
  Typing an SSID updates those marks. Multiple rows with the same SSID may be
  marked because they are the same logical network on different bands/APs;
  choosing a row does not force that BSSID or band.
- Selecting another row for the same SSID no longer clears the password.
  Selecting a different network still clears it.
- Startup status distinguishes a missing profile, a disabled saved profile,
  and enabled autoconnect. Disable and Forget are enabled only when applicable.
- Connected scanning remains deliberately disabled to preserve the existing
  driver behaviour. The app now explains **Disconnect first**. It does not
  perform background scans, change the country or reset an existing connection.

## Standalone package

The C# Windows Forms EXE still includes its ARM64 .NET Desktop Runtime. No
separate runtime installation or additional DLL downloads are required.
Microsoft-supported single-file compression and English resource filtering
reduce size; no unsafe WinForms trimming or executable packer is used. The
custom multi-resolution icon is retained. Actual size is recorded in BUILD.txt
and the release assets, not promised to fit an arbitrary 10–20 MB target.

Compression requires decompression during app startup; it does not change the
kernel driver's connected data path. GitHub smoke tests exercise the compressed
standalone x64 build as well as the regular test host. ARM64 output is built
and inspected; physical Pi startup and boot autoconnect still need confirmation.

## Use on the Pi

1. Close the previous connector and open the new EXE (administrator permission
   is required). Keep your current driver and UEFI.
2. Confirm the actual country (BD if in Bangladesh). When disconnected and
   ready, Scan, choose the SSID, enter its WPA2/AES password, and Connect.
3. Click **Save & enable at Windows startup** to opt into autoconnect and
   confirm. Check that the app reports startup enabled. This also refreshes
   the protected EXE copy used by the scheduled task. Passwords are not shipped
   with the release; the saved key remains machine-encrypted and ACL-protected.
4. **Disable startup** keeps the saved profile. **Forget saved network** disables
   startup and removes the saved key. Neither disconnects the current link.
   Use **Disconnect** to disconnect or scan again.

If the older PowerShell autoconnect task is enabled, the app will still ask you
to disable it using its original utility first; it never silently takes over
an unrelated task. A currently running startup task must finish before its
protected EXE is replaced.

## Verification

GitHub checks cover real missing-task COM mapping, an inert uniquely named task
that is created/read/disabled/deleted, DPAPI/ACL guards, existing credential and
driver protocol tests, and real UI button handlers with synthetic devices and
profiles. Tests never connect a GitHub runner to a Wi-Fi network. The development
PC does not execute/install the connector, driver, startup task or certificate.

References: [COM exception mapping](https://learn.microsoft.com/en-us/dotnet/standard/exceptions/handling-com-interop-exceptions),
[single-file compression and startup tradeoff](https://learn.microsoft.com/en-us/dotnet/core/deploying/single-file/overview#compress-assemblies-in-single-file-apps),
[WinForms trimming limitations](https://learn.microsoft.com/en-us/dotnet/core/deploying/trimming/incompatibilities#windows-forms).
