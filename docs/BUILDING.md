# Building

## Toolchain

Use Visual Studio 2022 with the Windows Driver Kit (WDK) and ARM64 driver build
support. The driver is built for Windows 11 ARM64.

## Visual Studio

Open `rpi5-cyw43455.sln`, select `Release | ARM64`, and build the solution.

## Command line

From a Visual Studio/WDK developer environment:

```powershell
msbuild .\rpi5-cyw43455.sln /m /t:Rebuild /p:Configuration=Release /p:Platform=ARM64
```

The release CI also builds both TX scheduling modes and verifies optimized
`/O2 /Ot` compilation, host/ASAN regression tests, test-signing, and packaging.

The driver links against `ndis.lib` and directly owns the Raspberry Pi 5
`ACPI\RPI1060` SDIO2 controller. It does not use `sdbus.lib`, WDF, or
WiFiCx.

## Signing and release policy

Development packages are test-signed and require Windows Test Signing on the
test Raspberry Pi. The installer does not enable Test Signing or modify BCD/UEFI.

The hardware-tested kernel source is frozen at
`c16aa318da490350126739add45223a186ab0a47`. New driver changes should be made
on an experimental branch and promoted to `main` only after full CI and
Raspberry Pi hardware validation.

## Damian Edition

Use the **Build Damian Edition Wi-Fi GUI and installer** workflow on
`main` (also enabled for `damian-edition/**` branches). It checks the pinned firmware contract,
runs C++ hardware-policy and native GUI/setup self-tests, builds the unchanged
kernel with TX scheduling and glom enabled, test-signs the RPI1060 package, and
produces ARM64 setup/GUI binaries, a driver ZIP, provenance and checksums.
The full driver workflow also runs both TX modes and existing ASAN regressions.

Local source gates (no Pi or driver installation required):

```powershell
python tests/damian_contract_tests.py
python tests/stability_recovery_scope_tests.py
pwsh -NoProfile -File tests/damian_installer_tests.ps1
pwsh -NoProfile -File tests/installer_utility_tests.ps1
```

With Visual C++ tools, `scripts/build-native-gui.ps1 -Architecture x64
-OutputDirectory artifacts/native-x64` builds the host-test GUI. Run it with
`--self-test` to test profile crypto and the scan ABI without touching hardware.
Compile `tests/platform_tests.cpp` with C++20 to test device matching independently.
`package-damian.ps1` wraps existing CI signing and pinned firmware fetching.
For a release, require both workflows to pass at the same commit, download that
commit's native artifact, verify SHA256SUMS, and publish a separate prerelease
without changing GitHub Latest. See `docs/DAMIAN-EDITION.md` for hardware gates.
