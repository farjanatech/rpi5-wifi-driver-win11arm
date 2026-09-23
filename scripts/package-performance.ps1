Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$stage = Join-Path $root 'artifacts\rpi5-wifi-performance-0.6.24'
if (Test-Path -LiteralPath $stage) { throw 'Performance staging directory already exists; use a clean CI checkout.' }
[void](New-Item -ItemType Directory -Path $stage)
# Explicit allowlist: no driver binaries, certificate, private profile or logs.
$files = @{
    'utility\Test-RPi5-WiFi-Performance.cmd'='Test-RPi5-WiFi-Performance.cmd'
    'utility\Test-RPi5-WiFi-Performance.ps1'='Test-RPi5-WiFi-Performance.ps1'
    'utility\Measure-RPi5-WiFi-Load.ps1'='Measure-RPi5-WiFi-Load.ps1'
    'utility\Get-RPi5-WiFi-Timing.ps1'='Get-RPi5-WiFi-Timing.ps1'
    'utility\Get-RPi5-WiFi-Transport.ps1'='Get-RPi5-WiFi-Transport.ps1'
    'utility\Get-RPi5-WiFi-Radio.ps1'='Get-RPi5-WiFi-Radio.ps1'
    'utility\Get-RPi5-WiFi-Radio.cmd'='Get-RPi5-WiFi-Radio.cmd'
    'utility\Connect-RPi5-WiFi.ps1'='Connect-RPi5-WiFi.ps1'
    'diagnostics\Collect-RPi5-WiFi-Diagnostics.ps1'='Collect-RPi5-WiFi-Diagnostics.ps1'
    'diagnostics\Run-RPi5-WiFi-Diagnostics.cmd'='Run-RPi5-WiFi-Diagnostics.cmd'
    'docs\PERFORMANCE-0.6.24.md'='README.md'
    'LICENSE'='LICENSE'
}
foreach ($file in $files.Keys) { Copy-Item -LiteralPath (Join-Path $root $file) -Destination (Join-Path $stage $files[$file]) }
@"
utility_version=0.6.24
minimum_driver_for_transport_history=exp0.6.24
minimum_driver_for_radio=exp0.6.17
minimum_driver_for_timing=exp0.6.22
minimum_driver_for_extended_firmware_evidence=exp0.6.23
repository=$env:GITHUB_REPOSITORY
commit=$env:GITHUB_SHA
workflow_run=$env:GITHUB_SERVER_URL/$env:GITHUB_REPOSITORY/actions/runs/$env:GITHUB_RUN_ID
"@ | Set-Content -LiteralPath (Join-Path $stage 'SOURCE_REVISION.txt') -Encoding UTF8
Get-ChildItem -LiteralPath $stage -File | Sort-Object Name | ForEach-Object {
    '{0}  {1}' -f (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash,$_.Name
} | Set-Content -LiteralPath (Join-Path $stage 'SHA256SUMS.txt') -Encoding ASCII
$zip = Join-Path $root 'artifacts\RPi5-WiFi-Performance-0.6.24.zip'
Compress-Archive -Path (Join-Path $stage '*') -DestinationPath $zip
'{0}  {1}' -f (Get-FileHash -LiteralPath $zip -Algorithm SHA256).Hash,(Split-Path -Leaf $zip) |
    Set-Content -LiteralPath (Join-Path $root 'artifacts\PERFORMANCE-SHA256SUMS.txt') -Encoding ASCII
Write-Output "Packaged standalone measurement utility: $zip"
