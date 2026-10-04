Set-StrictMode -Version Latest
$ErrorActionPreference='Stop'
$root=Split-Path -Parent $PSScriptRoot
$temp=Join-Path ([IO.Path]::GetTempPath()) ('rpi5-all-in-one-test-'+[guid]::NewGuid().ToString('N'))
[void](New-Item -ItemType Directory -Path $temp)
try {
    & (Join-Path $root 'scripts\build-all-in-one-utility.ps1') -OutputDirectory $temp
    $ps1=Join-Path $temp 'RPi5-WiFi-AllInOne.ps1'
    $cmd=Join-Path $temp 'RPi5-WiFi-AllInOne.cmd'
    if(-not (Test-Path -LiteralPath $ps1 -PathType Leaf) -or
       -not (Test-Path -LiteralPath $cmd -PathType Leaf)){throw 'Generated all-in-one entry point is incomplete.'}
    $visible=@(Get-ChildItem -LiteralPath $temp -File)
    if($visible.Count -ne 2){throw "Builder exposed $($visible.Count) files instead of one utility + launcher."}

    $tokens=$null;$errors=$null
    [void][Management.Automation.Language.Parser]::ParseFile($ps1,[ref]$tokens,[ref]$errors)
    if($errors.Count){throw "Generated all-in-one parser errors: $($errors|Out-String)"}
    $source=Get-Content -LiteralPath $ps1 -Raw -Encoding UTF8
    foreach($required in @(
        '$script:ToolVersion=''0.7.1.14''',
        'https://speed.cloudflare.com/__up',
        'Sustained upload stage: $streams stream(s), 16 MiB per stream.',
        'upload-scaling-result.json',
        'Connect + sustained 1/2/4-stream download/upload + diagnostics',
        'Driver runtime=byte-for-byte v0.7.1.11 source; v0.7.1.14 changes measurement only.'
    )){
        if($source -notmatch [regex]::Escape($required)){throw "Generated utility missing: $required"}
    }
    if($source.Contains('__PAYLOAD_BASE64__')){throw 'Embedded payload marker was not replaced.'}
    if($source -match 'WiFi\.private\.json|Password\s*='){throw 'Generated utility must not embed a saved credential/profile.'}

    $match=[regex]::Match($source,'\$script:PayloadBase64=''([A-Za-z0-9+/=]+)''')
    if(-not $match.Success){throw 'Embedded helper payload was not found.'}
    $payloadZip=Join-Path $temp 'payload.zip'
    [IO.File]::WriteAllBytes($payloadZip,[Convert]::FromBase64String($match.Groups[1].Value))
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    $zip=[IO.Compression.ZipFile]::OpenRead($payloadZip)
    try {$names=@($zip.Entries|ForEach-Object Name|Sort-Object)}
    finally {$zip.Dispose()}
    $expected=@(
        'Collect-RPi5-WiFi-Diagnostics.ps1',
        'Connect-RPi5-WiFi.ps1',
        'Get-RPi5-WiFi-Radio.ps1',
        'Get-RPi5-WiFi-Transport.ps1',
        'RPi5-WiFi-Operations.ps1'
    )|Sort-Object
    if(($names -join '|') -cne ($expected -join '|')){
        throw "Unexpected embedded helper set: $($names -join ', ')"
    }

    foreach($measurementMarker in @(
        'download-1stream','download-2stream','download-4stream',
        'upload-1stream','upload-2stream','upload-4stream',
        'Get-Rpi5FreshSnapshot','TransportTxStatusChecks','Cmd53WriteCount'
    )){
        if(-not $source.Contains($measurementMarker)){throw "Generated utility missing measurement marker: $measurementMarker"}
    }
    Write-Output 'PASS: one user utility embeds only required helpers and measures sustained 1/2/4-stream download/upload with fresh per-stage driver snapshots.'
} finally {
    Remove-Item -LiteralPath $temp -Recurse -Force -ErrorAction SilentlyContinue
}
