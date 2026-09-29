Set-StrictMode -Version Latest
$ErrorActionPreference='Stop'
$root=Split-Path -Parent $PSScriptRoot
$baseline='3f750ced3ca10cb04d7d061ac6418adc8ed2acc9'
$allowed=@(
  '.github/workflows/build-arm64-driver.yml','.github/workflows/native-connector.yml',
  'native-connector/WiFiManager.cpp','package/rpi5cyw.inf','scripts/fetch-firmware.ps1','installer/Install-RPi5-WiFi-Driver.ps1',
  'tests/firmware_package_tests.ps1','tests/fw229_scope_tests.ps1','scripts/package-ci.ps1',
  'docs/PORTABLE-0.7.0.1-FW229.md','.github/workflows/release-fw229.yml'
)
$changed=@(& git -C $root diff --name-only $baseline HEAD)
if($LASTEXITCODE -ne 0){throw 'Cannot compare fw229 candidate scope.'}
foreach($path in $changed){if($path -notin $allowed){throw "Unexpected fw229 candidate change: $path"}}
$fw=Get-Content -LiteralPath (Join-Path $root 'scripts/fetch-firmware.ps1') -Raw
if($fw -notmatch '7\.45\.229' -or $fw -notmatch 'CF79E8E8727D103A94CD243F1D98770FA29F5DA25DF251D0D31B3696F3B4AC6A'){throw 'Known-working firmware pin missing.'}
if($fw -notmatch '2DBD7D22FC9AF0EB560CEAB45B19646D211BC7B34A1DD00C6BFAC5DD6BA25E8A'){throw 'Matching 7.45.229 CLM pin missing.'}
$inf=Get-Content -LiteralPath (Join-Path $root 'package/rpi5cyw.inf') -Raw
if($inf -notmatch '09/29/2026,0\.7\.0\.3'){throw 'fw229 candidate INF version missing.'}
$installer=Get-Content -LiteralPath (Join-Path $root 'installer/Install-RPi5-WiFi-Driver.ps1') -Raw
if($installer -notmatch "0\.7\.0\.3-fw229"){throw 'fw229 installer identity missing.'}
$native=Get-Content -LiteralPath (Join-Path $root 'native-connector/WiFiManager.cpp') -Raw
if($native -match 'Username|Enterprise'){throw 'Native GUI must remain password-only.'}
if($native -notmatch '2\.4 GHz' -or $native -notmatch '5 GHz' -or $native -notmatch 'IOCTL_SCAN_START'){throw 'Native scan/band controls missing.'}
Write-Output 'PASS: fw229 candidate changes are isolated to firmware/package/native GUI/release plumbing.'
