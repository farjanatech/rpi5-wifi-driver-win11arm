Set-StrictMode -Version Latest
$ErrorActionPreference='Stop'
$root=Split-Path -Parent $PSScriptRoot
$path=Join-Path $root 'utility\Get-RPi5-WiFi-Radio.ps1'
$tokens=$null;$errors=$null
[void][Management.Automation.Language.Parser]::ParseFile($path,[ref]$tokens,[ref]$errors)
if($errors.Count){throw 'Radio utility syntax error.'}
. $path -RadioLibraryOnly
$data=[byte[]]::new(80)
foreach($pair in @(@(0,1),@(4,7),@(8,15),@(32,36),@(36,36),@(44,5000))){[BitConverter]::GetBytes([uint32]$pair[1]).CopyTo($data,$pair[0])}
[BitConverter]::GetBytes([int32]-55).CopyTo($data,48)
$r=ConvertFrom-Rpi5RadioReport $data
if($r.Band -ne '5 GHz' -or $r.Channel -ne 36 -or $r.SignalDbm -ne -55 -or $r.PowerSave -ne 'Off' -or $r.MinimumPowerConsumption -ne 'Off'){throw 'Radio report values wrong.'}
[BitConverter]::GetBytes([uint32]2400).CopyTo($data,44)
if((ConvertFrom-Rpi5RadioReport $data).Band -ne '2.4 GHz'){throw '2.4 GHz mislabelled.'}
foreach($i in 1..2){[BitConverter]::GetBytes([uint32]$i).CopyTo($data,52);if((ConvertFrom-Rpi5RadioReport $data).PowerSave -ne "PM$i"){throw 'Power mode mislabelled.'}}
[BitConverter]::GetBytes([uint32]0).CopyTo($data,8)
$r=ConvertFrom-Rpi5RadioReport $data
if($r.Band -ne 'Unknown' -or $r.Channel -ne 'Unknown' -or $r.SignalDbm -ne 'Unknown' -or $r.PowerSave -ne 'Unknown'){throw 'Invalid readbacks presented as valid.'}
foreach($bad in @([byte[]]::new(0),[byte[]]::new(80),[byte[]]::new(79))){
    $rejected=$false;try{ConvertFrom-Rpi5RadioReport $bad | Out-Null}catch{$rejected=$true}
    if(!$rejected){throw 'Invalid/old ABI accepted.'}
}
$v2=[byte[]]::new(256)
foreach($pair in @(@(0,2),@(4,8),@(8,15),@(32,36),@(36,36),@(44,5000),@(80,511),@(128,144),
    @(132,0x30201002),@(136,0x5040),@(140,0xd024),@(144,1000),@(148,7),@(152,800),@(156,9),@(160,10),
    @(164,4),@(168,200),@(172,0xc032),@(176,72000),@(180,65000),@(184,123),@(188,11),@(192,456),@(196,12),@(200,13),
    @(204,20),@(208,21),@(212,22),@(216,23),@(220,24),@(224,25),@(228,26),@(232,27),@(236,6000))) {
    [BitConverter]::GetBytes([uint32]$pair[1]).CopyTo($v2,$pair[0])
}
[BitConverter]::GetBytes([int32]-55).CopyTo($v2,48)
$r=ConvertFrom-Rpi5RadioReport $v2
if($r.Version -ne 2 -or $r.CurrentTxRateMbps -ne 72 -or $r.LastTxRateMbps -ne 72 -or $r.LastRxRateMbps -ne 65 -or
    $r.Bssid -ne '02:10:20:30:40:50' -or $r.ChanspecRaw -ne '0xD024' -or $r.FirmwareRxBad -ne 7 -or $r.FirmwareTxBad -ne 9 -or
    $r.StationTxFailures -ne 11 -or $r.StationUserTxRetries -ne 23 -or $r.StationRxRetried -ne 27 -or
    !$r.WmmNegotiated -or !$r.AmpduCapable){throw 'Extended radio field offsets/units incorrect.'}
$roundTrip=$r | ConvertTo-Json -Depth 5 | ConvertFrom-Json
if($roundTrip.FirmwareTxBad -ne 9 -or $roundTrip.Bssid -ne $r.Bssid){throw 'JSON loses evidence.'}
foreach($bit in @(1,2,4,8,16,32,64,128,256)) {
    [BitConverter]::GetBytes([uint32]$bit).CopyTo($v2,80)
    $r=ConvertFrom-Rpi5RadioReport $v2
    if(!($bit -band 1) -and $r.CurrentTxRateMbps -ne 'Unknown'){throw 'Unavailable rate guessed.'}
    if(!($bit -band 2) -and $r.Bssid -ne 'Unknown'){throw 'Unavailable identity guessed.'}
    if(!($bit -band 8) -and $r.FirmwareTxBad -ne 'Unknown'){throw 'Unavailable counter treated as zero.'}
    if(!($bit -band 64) -and $r.StationUserTxRetries -ne 'Unknown'){throw 'Unavailable retries treated as zero.'}
}
[BitConverter]::GetBytes([uint32]0).CopyTo($v2,80)
$r=ConvertFrom-Rpi5RadioReport $v2
if($r.ExtendedValidMask -ne 0 -or $r.StationVersion -ne 'Unknown' -or $r.FirmwareRxBad -ne 'Unknown'){throw 'Unsupported station not unknown.'}
foreach($pair in @(@(0,1),@(0,3),@(80,512),@(52,3),@(56,2))) {
    $bad=[byte[]]$v2.Clone();[BitConverter]::GetBytes([uint32]$pair[1]).CopyTo($bad,$pair[0])
    $rejected=$false;try{ConvertFrom-Rpi5RadioReport $bad | Out-Null}catch{$rejected=$true}
    if(!$rejected){throw 'Malformed extended ABI accepted.'}
}
$source=Get-Content -LiteralPath $path -Raw
foreach($bad in @('Set-ItemProperty','bcdedit','pnputil','Disable-NetAdapter','WiFi.private.json')){if($source.Contains($bad)){throw 'Radio reader changes settings or accesses credentials.'}}
foreach($code in @('0x12A00C','0x126010')){if(!$source.Contains($code)){throw 'Radio ABI mismatch.'}}
$perf=Get-Content -LiteralPath (Join-Path $root 'utility\Test-RPi5-WiFi-Performance.ps1') -Raw
if($perf.IndexOf("Write-Report 'Read-only radio") -gt $perf.IndexOf("Repeated-download START")){throw 'Radio query overlaps load stage.'}
if(!$perf.Contains('if (-not $radioReady)') -or !$perf.Contains("'-File',`$radioTool")){throw 'Missing pre-load completion gate or incorrectly quoted tool path.'}
. (Join-Path $root 'utility\Test-RPi5-WiFi-Performance.ps1') -LibraryOnly
$probe=Invoke-Rpi5BoundedProcess (Join-Path $PSHOME 'powershell.exe') @('-NoProfile','-ExecutionPolicy','Bypass','-File',$path,'-LibraryOnly') 15
if($probe.TimedOut -or $probe.ExitCode -ne 0){throw 'Radio script could not be invoked as a bounded child process.'}
if(!$source.Contains('[switch]$AsJson') -or !$source.Contains('new byte[256]')){throw 'Missing v2 JSON/full-size request support.'}
Write-Output 'PASS: v1/v2 radio reports, signed RSSI, PHY-rate units, BSSID/chanspec, firmware counters, unknown/partial evidence, malformed ABI and read-only JSON support.'
