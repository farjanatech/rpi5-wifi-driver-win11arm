Set-StrictMode -Version Latest
$ErrorActionPreference='Stop'
$root=Split-Path -Parent $PSScriptRoot
$baseline='46ddbfd2de6ed2c2be42765d45b050f708721815'
$allowed=@(
  'README.md','.github/workflows/build-arm64-driver.yml','.github/workflows/connector.yml',
  'connector/MainForm.cs','connector/Operations.cs','connector/Protocol.cs','connector/RPi5.WiFi.Connector.csproj',
  'connector/SavedNetwork.cs','connector/SelfTests.cs','connector/StartupSettings.cs','connector/UiTests.cs',
  'docs/PORTABLE-0.7.0.md','scripts/fetch-firmware.ps1','scripts/package-ci.ps1',
  'src/cyw43455/band_policy.h','src/cyw43455/band_selection.h','src/cyw43455/connection.h',
  'src/cyw43455/join_preference.h','src/cyw43455/network.c','src/cyw43455/network_protocol.h','src/cyw43455/us_region.h',
  'src/cyw43455/scan_control.h','src/cyw43455/scan_sequence.h',
  'tests/band_policy_tests.c','tests/firmware_package_tests.ps1','tests/network_protocol_tests.c','tests/scan_control_tests.c','tests/us_region_tests.c','tests/portable_scope_tests.ps1'
)
$changed=@(& git -C $root diff --name-only $baseline HEAD)
if($LASTEXITCODE -ne 0){throw 'Cannot compare portable branch scope.'}
foreach($path in $changed){if($path -notin $allowed){throw "Unexpected portable-branch change: $path"}}
$network=Get-Content -LiteralPath (Join-Path $root 'src/cyw43455/network.c') -Raw
$scan=Get-Content -LiteralPath (Join-Path $root 'src/cyw43455/scan_control.h') -Raw
$protocol=Get-Content -LiteralPath (Join-Path $root 'src/cyw43455/network_protocol.h') -Raw
$connection=Get-Content -LiteralPath (Join-Path $root 'src/cyw43455/connection.h') -Raw
$installer=Get-Content -LiteralPath (Join-Path $root 'installer/Install-RPi5-WiFi-Driver.ps1') -Raw
if($network -match 'CywUsValidConnect|us_region\.h' -or $scan -match 'CywUsCountryAllowed'){throw 'US-only production admission remains wired.'}
if($protocol -notmatch 'CYW_BAND_PREF_AUTO' -or $protocol -notmatch 'CywCountryAuto'){throw 'Portable country/band ABI is missing.'}
if($connection -notmatch 'CywCountryValueUsable' -or $connection -notmatch 'CountrySetMode=5'){throw 'Firmware-owned regulatory mode is not wired.'}
if($installer -match 'Requires exp\.|Unknown revisions are not accepted|Get-Rpi5CompatibleUefiRevision'){throw 'Installer still hard-gates a UEFI revision.'}
if($installer -notmatch 'ACPI\\\\RPI0011'){throw 'Capability target ACPI\\RPI0011 is no longer required.'}
Write-Output 'PASS: portable branch is capability-gated, not UEFI-version-gated; US-only host admission removed; firmware regulatory authority preserved.'
