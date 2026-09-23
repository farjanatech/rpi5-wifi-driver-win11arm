# exp0.6.29.1 — UEFI compatibility and supplied connector icon

This maintenance package supports UEFI exp.0.5 (`838d87d`) in the guarded
installer, alongside exp.0.3 (`bda4c47`). The old .29 installer accepted only
exp.0.3, which is why installing it after the UEFI upgrade was blocked.
Unknown revisions, including exp.0.4, are not admitted. Do not bypass the checks.

The signed driver, INF, catalog, test certificate, Wi-Fi firmware and utilities
are reused unchanged from the published exp0.6.29 ZIP. Device Manager therefore
still reports **driver 0.6.29.0**. Package/installer and connector versions are
**0.6.29.1**. This is not a new networking-performance experiment.

The connector embeds the user-supplied multi-resolution ICO in the EXE's native
Windows resources and its form/title-bar icon. No external ICO is required.
Scanning, connection, credentials, saved profiles and startup logic are unchanged.
The EXE is still a self-contained ARM64 app, not a driver installer and not
native Windows taskbar Wi-Fi integration. It remains unsigned.

## What to run on the Pi

1. Keep working UEFI exp.0.5. Do not reflash firmware, reinstall Windows or
   uninstall a working Wi-Fi driver just for this update.
2. If driver installation was blocked, extract the complete
   `RPi5-WiFi-Windows11-ARM64-exp0.6.29.1.zip` into a new folder on the Pi and run
   `Install-RPi5-WiFi-Driver.cmd`. Approve administrator access and restart once
   if required. Do not mix these files into the older hash-checked package.
3. Download the new `RPi5-WiFi-Connector.exe`, close the old connector window and
   open the new EXE on Windows ARM64. If .29 already works, this EXE alone is
   sufficient for the icon update; no driver reinstall is needed.
4. Use the existing saved network, or confirm the Pi's actual country, scan,
   select your network and connect. Saving/enabling startup remains opt-in.
   A pre-existing startup task uses its protected app copy until explicitly
   refreshed by saving/enabling startup through the new app. No password is
   included in this release, and profiles are not overwritten by the installer.

If Explorer still displays a cached old icon, use the newly downloaded file in
a fresh folder and reopen Explorer. Do not delete system icon caches or disable
security protections. The release test verifies all nine supplied ICO frames
inside the actual ARM64 executable without running that executable.

## Verification and limits

GitHub tests cover old/new allowed UEFI hashes, unknown/near-match rejection,
both installer detection paths, existing package/security checks, connector
protocol/credential/startup tests, embedded icon loading and PE resource bytes.
Packaging verifies the exact SHA256-pinned .29 release before replacing only
installer/documentation metadata and regenerating the package manifest.

No kernel driver, certificate, connector or startup task is installed/executed
on the development PC. Build/test success is not a new physical-Pi performance
claim. Keep Ethernet/recovery access available. Secure Boot, Test Signing, BCD,
DNS, routes, UEFI, fan behaviour and the connected driver data path are unchanged.
