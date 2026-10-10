[CmdletBinding()]
param(
    [ValidateSet('x64','arm64')][string]$Architecture='arm64',
    [Parameter(Mandatory=$true)][string]$DriverPackageDirectory,
    [Parameter(Mandatory=$true)][string]$GuiPath,
    [Parameter(Mandatory=$true)][string]$OutputDirectory
)
Set-StrictMode -Version Latest
$ErrorActionPreference='Stop'
$root=Split-Path -Parent $PSScriptRoot
$driver=[IO.Path]::GetFullPath((Join-Path $root $DriverPackageDirectory))
$gui=[IO.Path]::GetFullPath((Join-Path $root $GuiPath))
$out=[IO.Path]::GetFullPath((Join-Path $root $OutputDirectory))
New-Item -ItemType Directory -Path $out -Force | Out-Null
$required=@('rpi5cyw.inf','rpi5cyw.sys','rpi5cyw.cat','rpi5cyw-test.cer','cyfmac43455-sdio.bin','cyfmac43455-sdio.clm_blob','brcmfmac43455-sdio.txt')
foreach($name in $required){if(-not (Test-Path (Join-Path $driver $name) -PathType Leaf)){throw "Missing setup payload: $name"}}
if(-not (Test-Path $gui -PathType Leaf)){throw 'Native GUI payload is missing.'}
$programFilesX86=[Environment]::GetFolderPath([Environment+SpecialFolder]::ProgramFilesX86)
$vswhere=Join-Path $programFilesX86 'Microsoft Visual Studio\Installer\vswhere.exe'
$install=& $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
$vcvars=if($Architecture -eq 'arm64'){Join-Path $install 'VC\Auxiliary\Build\vcvarsamd64_arm64.bat'}else{Join-Path $install 'VC\Auxiliary\Build\vcvars64.bat'}
function Q([string]$p){return $p.Replace('\','\\').Replace('"','\"')}
$rc=Join-Path $out 'SetupPayload.rc'
@"
#include "resource.h"
#include "SetupPayloadIds.h"
#include "ProductVersion.rc"
IDI_SETUP ICON "$(Q (Join-Path $root 'native\Assets\RPi5-WiFi.ico'))"
1 24 "$(Q (Join-Path $root 'native\RPi5WiFiSetup.manifest'))"
IDR_PAYLOAD_GUI RCDATA "$(Q $gui)"
IDR_PAYLOAD_INF RCDATA "$(Q (Join-Path $driver 'rpi5cyw.inf'))"
IDR_PAYLOAD_SYS RCDATA "$(Q (Join-Path $driver 'rpi5cyw.sys'))"
IDR_PAYLOAD_CAT RCDATA "$(Q (Join-Path $driver 'rpi5cyw.cat'))"
IDR_PAYLOAD_CERT RCDATA "$(Q (Join-Path $driver 'rpi5cyw-test.cer'))"
IDR_PAYLOAD_FW RCDATA "$(Q (Join-Path $driver 'cyfmac43455-sdio.bin'))"
IDR_PAYLOAD_CLM RCDATA "$(Q (Join-Path $driver 'cyfmac43455-sdio.clm_blob'))"
IDR_PAYLOAD_NVRAM RCDATA "$(Q (Join-Path $driver 'brcmfmac43455-sdio.txt'))"
IDR_PAYLOAD_LICENSE RCDATA "$(Q (Join-Path $root 'LICENSE'))"
IDR_PAYLOAD_NOTICES RCDATA "$(Q (Join-Path $root 'THIRD_PARTY_NOTICES.md'))"
"@ | Set-Content -LiteralPath $rc -Encoding ASCII
Push-Location (Join-Path $root 'native')
try {
    $res=Join-Path $out 'RPi5WiFiSetup.res'
    $exe=Join-Path $out 'RPi5-WiFi-Setup.exe'
    $command='call "{0}" && rc.exe /nologo /I "{1}" /fo"{2}" "{3}" && cl.exe /nologo /std:c++20 /EHsc /O2 /W4 /WX /DUNICODE /D_UNICODE RPi5WiFiSetup.cpp RPi5WiFiPlatform.cpp "{2}" /Fe:"{4}" /link /SUBSYSTEM:WINDOWS /MANIFEST:NO newdev.lib setupapi.lib crypt32.lib shell32.lib ole32.lib user32.lib gdi32.lib' -f $vcvars,(Join-Path $root 'native'),$res,$rc,$exe
    cmd.exe /d /s /c $command
    if($LASTEXITCODE -ne 0){throw "Native setup build failed with exit code $LASTEXITCODE."}
} finally { Pop-Location }
