Set-StrictMode -Version Latest
$ErrorActionPreference='Stop'
$root=Split-Path -Parent $PSScriptRoot
$scanPath=Join-Path $root 'utility\RPi5-WiFi-Scan.ps1'

$tokens=$null;$parseErrors=$null
[void][Management.Automation.Language.Parser]::ParseFile($scanPath,[ref]$tokens,[ref]$parseErrors)
if($parseErrors.Count){throw "PowerShell parser error: $scanPath"}

# Library import must define helpers only; it must not open the driver or scan.
. $scanPath -ScanLibraryOnly

function Assert-ScanTest {param([bool]$Value,[string]$Message) if(-not $Value){throw $Message}}
function Assert-ScanThrow {
    param([scriptblock]$Action,[string]$Message)
    $rejected=$false;try{$null=& $Action}catch{$rejected=$true}
    Assert-ScanTest $rejected $Message
}
function Get-ScanFixture {
    param([byte[]]$Ssid=([Text.Encoding]::UTF8.GetBytes('test-network')),[uint32]$Security=2,[uint32]$Channel=36)
    $bytes=[byte[]]::new(3616)
    [BitConverter]::GetBytes([uint32]1).CopyTo($bytes,0)
    [BitConverter]::GetBytes([uint32]12).CopyTo($bytes,4)
    [BitConverter]::GetBytes([uint32]3).CopyTo($bytes,8)
    [BitConverter]::GetBytes([uint32]1).CopyTo($bytes,16)
    [Text.Encoding]::ASCII.GetBytes('BD').CopyTo($bytes,24)
    [BitConverter]::GetBytes([uint32]$Ssid.Length).CopyTo($bytes,32)
    if($Ssid.Length){$Ssid.CopyTo($bytes,36)}
    ([byte[]]@(2,3,4,5,6,7)).CopyTo($bytes,68)
    [BitConverter]::GetBytes([uint16]0xd024).CopyTo($bytes,74)
    [BitConverter]::GetBytes([int32]-45).CopyTo($bytes,76)
    [BitConverter]::GetBytes($Security).CopyTo($bytes,80)
    [BitConverter]::GetBytes($Channel).CopyTo($bytes,84)
    return ,$bytes
}

$request=ConvertTo-Rpi5ScanRequest 'BD'
Assert-ScanTest ($request -is [byte[]] -and $request.Length -eq 8 -and
    [BitConverter]::ToUInt32($request,0) -eq 1 -and $request[4] -eq 66 -and $request[5] -eq 68) 'Scan request ABI differs.'
foreach($country in @('','B','bd','USA','B1','BD;')){
    Assert-ScanThrow {ConvertTo-Rpi5ScanRequest $country} 'Invalid/unconfirmed country accepted.'
}

$fixture=Get-ScanFixture
$report=ConvertFrom-Rpi5ScanReport $fixture
Assert-ScanTest ($report.Version -eq 1 -and $report.Generation -eq 12 -and $report.State -eq 3 -and
    $report.Country -eq 'BD' -and $report.Count -eq 1 -and -not $report.Truncated) 'Scan header decoding failed.'
$entry=$report.Entries[0]
Assert-ScanTest ($entry.Ssid -ceq 'test-network' -and $entry.Connectable -and $entry.Band -eq '5 GHz' -and
    $entry.Channel -eq 36 -and $entry.RssiDbm -eq -45 -and $entry.Bssid -eq '02:03:04:05:06:07' -and
    $entry.Security -eq 'WPA2-Personal / AES') 'Compatible 5 GHz scan entry decoding failed.'

$channel24=ConvertFrom-Rpi5ScanReport (Get-ScanFixture -Channel 6)
Assert-ScanTest ($channel24.Entries[0].Band -eq '2.4 GHz' -and $channel24.Entries[0].Connectable) '2.4 GHz channel decoded incorrectly.'

$unknown=ConvertFrom-Rpi5ScanReport (Get-ScanFixture -Channel 15)
Assert-ScanTest (-not $unknown.Entries[0].Connectable -and $unknown.Entries[0].Band -eq 'Unknown') 'Invalid channel silently assigned a band.'

foreach($security in @(0,1,3,4,6,8,10,16,18,32,34,64,66,255)){
    $blocked=ConvertFrom-Rpi5ScanReport (Get-ScanFixture -Security $security)
    Assert-ScanTest (-not $blocked.Entries[0].Connectable) 'Unsupported security became selectable.'
}

foreach($length in @(0,32,3615,3617)){
    Assert-ScanThrow {ConvertFrom-Rpi5ScanReport ([byte[]]::new($length))} 'Invalid scan output size accepted.'
}

$malformed=[byte[]]$fixture.Clone();[Array]::Clear($malformed,68,6)
Assert-ScanThrow {ConvertFrom-Rpi5ScanReport $malformed} 'Zero BSSID accepted.'

$utf8Ssid=[string][char]0x00e9+'-network'
$unicode=ConvertFrom-Rpi5ScanReport (Get-ScanFixture -Ssid ([Text.Encoding]::UTF8.GetBytes($utf8Ssid)))
Assert-ScanTest ($unicode.Entries[0].Ssid -ceq $utf8Ssid -and $unicode.Entries[0].Connectable) 'Exact UTF-8 SSID was damaged.'

$unsafe=ConvertFrom-Rpi5ScanReport (Get-ScanFixture -Ssid ([byte[]]@(0xc3,0x28)))
Assert-ScanTest (-not $unsafe.Entries[0].Connectable -and $null -eq $unsafe.Entries[0].Ssid) 'Invalid UTF-8 SSID became a connect target.'

$scanSource=Get-Content -LiteralPath $scanPath -Raw
foreach($forbidden in @('RPi5-WiFi-App','WiFi.private.json','Register-ScheduledTask','Set-RPi5-WiFi-Autoconnect','Invoke-Expression')){
    Assert-ScanTest (-not $scanSource.Contains($forbidden)) 'Scan helper retained legacy GUI/persistence coupling.'
}
foreach($code in @('0x12A014','0x126018','0x12A01C')){
    Assert-ScanTest $scanSource.Contains($code) 'Scan control ABI code missing.'
}

Write-Output 'PASS: standalone scan request/report ABI, 2.4/5 GHz decoding, strict SSID/security validation, and no legacy GUI coupling.'
