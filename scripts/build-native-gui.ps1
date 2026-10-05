[CmdletBinding()]
param(
    [ValidateSet('x64','arm64')][string]$Architecture='arm64',
    [Parameter(Mandatory=$true)][string]$OutputDirectory
)
Set-StrictMode -Version Latest
$ErrorActionPreference='Stop'
$root=Split-Path -Parent $PSScriptRoot
$out=[IO.Path]::GetFullPath((Join-Path $root $OutputDirectory))
New-Item -ItemType Directory -Path $out -Force | Out-Null
$vswhere="$env:ProgramFiles(x86)\Microsoft Visual Studio\Installer\vswhere.exe"
$install=& $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if(-not $install){throw 'Visual C++ tools were not found.'}
$vcvars=if($Architecture -eq 'arm64'){Join-Path $install 'VC\Auxiliary\Build\vcvarsamd64_arm64.bat'}else{Join-Path $install 'VC\Auxiliary\Build\vcvars64.bat'}
if(-not (Test-Path $vcvars)){throw "Missing cross compiler environment: $vcvars"}
Push-Location (Join-Path $root 'native')
try {
    $res=Join-Path $out 'RPi5WiFi.res'
    $exe=Join-Path $out 'RPi5-WiFi.exe'
    $command='call "{0}" && rc.exe /nologo /fo"{1}" RPi5WiFi.rc && cl.exe /nologo /std:c++20 /EHsc /O2 /W4 /WX /DUNICODE /D_UNICODE RPi5WiFi.cpp RPi5WiFiCommon.cpp "{1}" /Fe:"{2}" /link /SUBSYSTEM:WINDOWS /MANIFEST:NO bcrypt.lib crypt32.lib advapi32.lib shell32.lib ole32.lib comctl32.lib' -f $vcvars,$res,$exe
    cmd.exe /d /s /c $command
    if($LASTEXITCODE -ne 0){throw "Native GUI build failed with exit code $LASTEXITCODE."}
} finally { Pop-Location }
