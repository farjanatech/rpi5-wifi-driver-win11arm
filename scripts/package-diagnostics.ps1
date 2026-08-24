Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent $PSScriptRoot
$source = Join-Path $root 'diagnostics'
$stage = Join-Path $root 'artifacts\rpi5-wifi-one-click-diagnostics'
if (Test-Path -LiteralPath $stage) { Remove-Item -LiteralPath $stage -Recurse -Force }
New-Item -ItemType Directory -Path $stage -Force | Out-Null

foreach ($name in @('Collect-RPi5-WiFi-Diagnostics.ps1','Run-RPi5-WiFi-Diagnostics.cmd','README.txt')) {
    $path = Join-Path $source $name
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { throw "Missing diagnostics source: $name" }
    Copy-Item -LiteralPath $path -Destination (Join-Path $stage $name) -Force
}
Copy-Item -LiteralPath (Join-Path $root 'LICENSE') -Destination (Join-Path $stage 'LICENSE') -Force

@"
repository=$env:GITHUB_REPOSITORY
commit=$env:GITHUB_SHA
workflow_run=$env:GITHUB_SERVER_URL/$env:GITHUB_REPOSITORY/actions/runs/$env:GITHUB_RUN_ID
utility_version=0.2.0
matching_acpi_id=ACPI\RPI0011
"@ | Set-Content -LiteralPath (Join-Path $stage 'SOURCE_REVISION.txt') -Encoding UTF8

Get-ChildItem -LiteralPath $stage -File | Sort-Object Name | ForEach-Object {
    $hash = Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256
    "$($hash.Hash)  $($_.Name)"
} | Set-Content -LiteralPath (Join-Path $stage 'SHA256SUMS.txt') -Encoding ASCII

Write-Host "Packaged one-click diagnostics at $stage"
