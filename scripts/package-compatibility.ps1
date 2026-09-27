Set-StrictMode -Version Latest
$ErrorActionPreference='Stop'
if ($env:GITHUB_ACTIONS -ne 'true') { throw 'Packaging must run on GitHub, not the development PC.' }
$root=Split-Path -Parent $PSScriptRoot
$baseUrl='https://github.com/farjanatech/rpi5-wifi-driver-win11arm/releases/download/driver-perf0.7.0-alpha.3-us/RPi5-WiFi-Windows11-ARM64-perf0.7.0-alpha.3-us.zip'
$baseHash='50659B0F0708F18960768E521A1DAF56274271D14E21584360E9933B379D5498'
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
Copy-Item -LiteralPath (Join-Path $root 'installer/Install-RPi5-WiFi-Driver.cmd') -Destination (Join-Path $stage 'Install-RPi5-WiFi-Driver.cmd')
# The public CMD deliberately launches the legacy packaged filename, not the
# source filename. Replace that exact entry point rather than adding a sibling.
Copy-Item -LiteralPath (Join-Path $root 'installer/Install-RPi5-WiFi-Driver.ps1') -Destination (Join-Path $stage 'install-test-driver.ps1')
$launcher=Get-Content -LiteralPath (Join-Path $stage 'Install-RPi5-WiFi-Driver.cmd') -Raw
if ($launcher -notmatch [regex]::Escape('-File "%~dp0install-test-driver.ps1"')) { throw 'Packaged CMD does not launch the verified installer.' }
if ((Get-FileHash -LiteralPath (Join-Path $stage 'install-test-driver.ps1')).Hash -cne
    (Get-FileHash -LiteralPath (Join-Path $root 'installer/Install-RPi5-WiFi-Driver.ps1')).Hash) { throw 'Packaged installer differs from tested source.' }
. (Join-Path $stage 'install-test-driver.ps1') -LibraryOnly
if ($script:InstallerVersion -ne '0.7.0.2-uefi1') { throw 'Packaged installer version mismatch.' }
Write-Output 'PASS: public CMD points to the tested installer-only UEFI policy update.'
Copy-Item -LiteralPath (Join-Path $root 'docs/INSTALLER-UEFI-REVISION.md') -Destination (Join-Path $stage 'INSTALLER-UEFI-REVISION.md')
Copy-Item -LiteralPath (Join-Path $root 'docs/INSTALLER-UEFI-REVISION.md') -Destination (Join-Path $stage 'README-TESTING.txt')
@"

maintenance_package_version=perf0.7.0-alpha.3-us.1
installer_version=0.7.0.2-uefi1
installer_commit=$env:GITHUB_SHA
installer_workflow=$env:GITHUB_SERVER_URL/$env:GITHUB_REPOSITORY/actions/runs/$env:GITHUB_RUN_ID
signed_driver_reused_from=driver-perf0.7.0-alpha.3-us
original_zip_sha256=$baseHash
uefi_revision_policy=informational-only; no revision allowlist
"@ | Add-Content -LiteralPath (Join-Path $stage 'SOURCE_REVISION.txt') -Encoding UTF8
$allowedChanges=@('Install-RPi5-WiFi-Driver.cmd','install-test-driver.ps1','README-TESTING.txt','SOURCE_REVISION.txt','SHA256SUMS.txt')
foreach ($name in $original.Keys) {
    if ($name -notin $allowedChanges -and (Get-FileHash -LiteralPath (Join-Path $stage $name)).Hash -cne $original[$name]) {
        throw "Protected original file changed: $name"
    }
}
foreach ($name in @('rpi5cyw.sys','rpi5cyw.inf','rpi5cyw.cat','rpi5cyw-test.cer','cyfmac43455-sdio.bin','cyfmac43455-sdio.clm_blob','brcmfmac43455-sdio.txt')) {
    Write-Output "UNCHANGED $name $($original[$name])"
}
$manifest=@(Get-ChildItem -LiteralPath $stage -File | Where-Object Name -ne 'SHA256SUMS.txt' | Sort-Object Name | ForEach-Object {
    '{0}  {1}' -f (Get-FileHash -LiteralPath $_.FullName).Hash,$_.Name
})
$manifest | Set-Content -LiteralPath (Join-Path $stage 'SHA256SUMS.txt') -Encoding ASCII
$verified=Test-Rpi5PackageManifest $stage -RequiredNames @('rpi5cyw.sys','rpi5cyw.inf','rpi5cyw.cat','rpi5cyw-test.cer','Install-RPi5-WiFi-Driver.cmd','install-test-driver.ps1','INSTALLER-UEFI-REVISION.md',
    'cyfmac43455-sdio.bin','cyfmac43455-sdio.clm_blob','brcmfmac43455-sdio.txt')
Write-Output "PASS: verified $verified payload files; existing signed driver, certificate, firmware and utilities preserved."
$output=Join-Path $root 'artifacts/compatibility'
[void](New-Item -ItemType Directory -Path $output -Force)
$result=Join-Path $output 'RPi5-WiFi-Windows11-ARM64-perf0.7.0-alpha.3-us.1.zip'
Compress-Archive -Path (Join-Path $stage '*') -DestinationPath $result
'{0}  {1}' -f (Get-FileHash -LiteralPath $result).Hash,(Split-Path -Leaf $result) | Set-Content -LiteralPath (Join-Path $output 'SHA256SUMS.txt') -Encoding ASCII
"commit=$env:GITHUB_SHA`npackage=perf0.7.0-alpha.3-us.1`ndriver=0.7.0.2`noriginal_zip_sha256=$baseHash" | Set-Content -LiteralPath (Join-Path $output 'BUILD.txt') -Encoding ASCII
