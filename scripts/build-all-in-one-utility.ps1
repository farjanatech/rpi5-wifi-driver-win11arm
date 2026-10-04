param(
    [Parameter(Mandatory=$true)][string]$OutputDirectory
)
Set-StrictMode -Version Latest
$ErrorActionPreference='Stop'
$root=Split-Path -Parent $PSScriptRoot
$template=Join-Path $root 'utility\RPi5-WiFi-AllInOne.Template.ps1'
if(-not (Test-Path -LiteralPath $template -PathType Leaf)){throw 'All-in-one template is missing.'}
[void](New-Item -ItemType Directory -Path $OutputDirectory -Force)

$helpers=@(
    'utility\RPi5-WiFi-Operations.ps1',
    'utility\Connect-RPi5-WiFi.ps1',
    'utility\Test-RPi5-WiFi-Performance.ps1',
    'utility\Measure-RPi5-WiFi-Load.ps1',
    'utility\RPi5-WiFi-DownloadTiming.ps1',
    'utility\RPi5-WiFi-MeasurementClock.ps1',
    'utility\Get-RPi5-WiFi-Radio.ps1',
    'utility\Get-RPi5-WiFi-Timing.ps1',
    'utility\Get-RPi5-WiFi-Transport.ps1',
    'diagnostics\Collect-RPi5-WiFi-Diagnostics.ps1'
)
$temp=Join-Path $env:TEMP ('rpi5-all-in-one-build-'+[guid]::NewGuid().ToString('N'))
[void](New-Item -ItemType Directory -Path $temp)
try{
    foreach($relative in $helpers){
        $source=Join-Path $root $relative
        if(-not (Test-Path -LiteralPath $source -PathType Leaf)){throw "Missing embedded helper: $relative"}
        Copy-Item -LiteralPath $source -Destination (Join-Path $temp (Split-Path -Leaf $source))
    }
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    $payloadZip=Join-Path $env:TEMP ('rpi5-all-in-one-payload-'+[guid]::NewGuid().ToString('N')+'.zip')
    try{
        [IO.Compression.ZipFile]::CreateFromDirectory($temp,$payloadZip,[IO.Compression.CompressionLevel]::Optimal,$false)
        $payload=[Convert]::ToBase64String([IO.File]::ReadAllBytes($payloadZip))
    }finally{Remove-Item -LiteralPath $payloadZip -Force -ErrorAction SilentlyContinue}
    $text=Get-Content -LiteralPath $template -Raw -Encoding UTF8
    if($text.IndexOf('__PAYLOAD_BASE64__',[StringComparison]::Ordinal) -lt 0){throw 'Template payload marker is missing.'}
    $text=$text.Replace('__PAYLOAD_BASE64__',$payload)
    $ps1=Join-Path $OutputDirectory 'RPi5-WiFi-AllInOne.ps1'
    $text|Set-Content -LiteralPath $ps1 -Encoding UTF8
    @'
@echo off
setlocal
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0RPi5-WiFi-AllInOne.ps1"
set "RC=%ERRORLEVEL%"
endlocal & exit /b %RC%
'@ | Set-Content -LiteralPath (Join-Path $OutputDirectory 'RPi5-WiFi-AllInOne.cmd') -Encoding ASCII
    Write-Output "Built single user utility at $ps1 with $($helpers.Count) embedded helpers."
}finally{
    Remove-Item -LiteralPath $temp -Recurse -Force -ErrorAction SilentlyContinue
}
