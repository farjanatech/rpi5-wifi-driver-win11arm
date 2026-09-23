Set-StrictMode -Version Latest
$ErrorActionPreference='Stop'
if ($env:GITHUB_ACTIONS -ne 'true') { throw 'Packaging must run on GitHub, not the development PC.' }
$root=Split-Path -Parent $PSScriptRoot
$baseUrl='https://github.com/farjanatech/rpi5-wifi-driver-win11arm/releases/download/driver-exp0.6.29/RPi5-WiFi-Windows11-ARM64-exp0.6.29-d184bc6.zip'
$baseHash='93D9ADC72CB5F4962233DD98052089606D69AC38A92871FC525EABDA96CFD69A'
$work=Join-Path $env:RUNNER_TEMP ('rpi5-compatibility-'+[guid]::NewGuid().ToString('N'))
[void](New-Item -ItemType Directory -Path $work)
$archive=Join-Path $work 'baseline.zip'
Invoke-WebRequest -Uri $baseUrl -OutFile $archive
if ((Get-FileHash -LiteralPath $archive).Hash -cne $baseHash) { throw 'Baseline release ZIP hash mismatch.' }
Add-Type -AssemblyName System.IO.Compression.FileSystem
$zip=[IO.Compression.ZipFile]::OpenRead($archive)
try {
    foreach ($entry in $zip.Entries) {
        if ($entry.FullName -match '[/\\:]' -or $entry.FullName -in @('.','..')) { throw 'Baseline must contain flat, safe filenames.' }
    }
} finally { $zip.Dispose() }
$stage=Join-Path $work 'package'
Expand-Archive -LiteralPath $archive -DestinationPath $stage
. (Join-Path $root 'installer/Install-RPi5-WiFi-Driver.ps1') -LibraryOnly
[void](Test-Rpi5PackageManifest $stage -RequiredNames @('rpi5cyw.sys','rpi5cyw.inf','rpi5cyw.cat','rpi5cyw-test.cer'))
$original=@{}
foreach ($file in Get-ChildItem -LiteralPath $stage -File) { $original[$file.Name]=(Get-FileHash -LiteralPath $file.FullName).Hash }
foreach ($name in @('Install-RPi5-WiFi-Driver.cmd','Install-RPi5-WiFi-Driver.ps1')) {
    Copy-Item -LiteralPath (Join-Path $root ('installer/'+$name)) -Destination (Join-Path $stage $name)
}
Copy-Item -LiteralPath (Join-Path $root 'docs/EXP0.6.29.1.md') -Destination (Join-Path $stage 'EXP0.6.29.1.md')
Copy-Item -LiteralPath (Join-Path $root 'docs/EXP0.6.29.1.md') -Destination (Join-Path $stage 'README-TESTING.txt')
@"

maintenance_package_version=0.6.29.1
installer_commit=$env:GITHUB_SHA
installer_workflow=$env:GITHUB_SERVER_URL/$env:GITHUB_REPOSITORY/actions/runs/$env:GITHUB_RUN_ID
signed_driver_reused_from=driver-exp0.6.29
original_zip_sha256=$baseHash
supported_uefi_revisions=bda4c47,838d87d
"@ | Add-Content -LiteralPath (Join-Path $stage 'SOURCE_REVISION.txt') -Encoding UTF8
$allowedChanges=@('Install-RPi5-WiFi-Driver.cmd','Install-RPi5-WiFi-Driver.ps1','README-TESTING.txt','SOURCE_REVISION.txt','SHA256SUMS.txt')
foreach ($name in $original.Keys) {
    if ($name -notin $allowedChanges -and (Get-FileHash -LiteralPath (Join-Path $stage $name)).Hash -cne $original[$name]) {
        throw "Protected original file changed: $name"
    }
}
foreach ($name in @('rpi5cyw.sys','rpi5cyw.inf','rpi5cyw.cat','rpi5cyw-test.cer')) {
    Write-Output "UNCHANGED $name $($original[$name])"
}
$manifest=@(Get-ChildItem -LiteralPath $stage -File | Where-Object Name -ne 'SHA256SUMS.txt' | Sort-Object Name | ForEach-Object {
    '{0}  {1}' -f (Get-FileHash -LiteralPath $_.FullName).Hash,$_.Name
})
$manifest | Set-Content -LiteralPath (Join-Path $stage 'SHA256SUMS.txt') -Encoding ASCII
$verified=Test-Rpi5PackageManifest $stage -RequiredNames @('rpi5cyw.sys','rpi5cyw.inf','rpi5cyw.cat','rpi5cyw-test.cer','Install-RPi5-WiFi-Driver.ps1','EXP0.6.29.1.md')
Write-Output "PASS: verified $verified payload files; existing signed driver, certificate, firmware and utilities preserved."
$output=Join-Path $root 'artifacts/compatibility'
[void](New-Item -ItemType Directory -Path $output -Force)
$result=Join-Path $output 'RPi5-WiFi-Windows11-ARM64-exp0.6.29.1.zip'
Compress-Archive -Path (Join-Path $stage '*') -DestinationPath $result
'{0}  {1}' -f (Get-FileHash -LiteralPath $result).Hash,(Split-Path -Leaf $result) | Set-Content -LiteralPath (Join-Path $output 'SHA256SUMS.txt') -Encoding ASCII
"commit=$env:GITHUB_SHA`npackage=0.6.29.1`ndriver=0.6.29.0`noriginal_zip_sha256=$baseHash" | Set-Content -LiteralPath (Join-Path $output 'BUILD.txt') -Encoding ASCII
