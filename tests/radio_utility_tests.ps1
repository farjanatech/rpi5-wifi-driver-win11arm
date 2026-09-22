Set-StrictMode -Version Latest
$ErrorActionPreference='Stop'
$root=Split-Path -Parent $PSScriptRoot
$path=Join-Path $root 'utility\Get-RPi5-WiFi-Radio.ps1'
$tokens=$null;$errors=$null
[void][Management.Automation.Language.Parser]::ParseFile($path,[ref]$tokens,[ref]$errors)
if($errors.Count){throw 'Radio utility syntax error.'}
. $path -LibraryOnly
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
$source=Get-Content -LiteralPath $path -Raw
foreach($bad in @('Set-ItemProperty','bcdedit','pnputil','Disable-NetAdapter','WiFi.private.json')){if($source.Contains($bad)){throw 'Radio reader changes settings or accesses credentials.'}}
foreach($code in @('0x12A00C','0x126010')){if(!$source.Contains($code)){throw 'Radio ABI mismatch.'}}
$perf=Get-Content -LiteralPath (Join-Path $root 'utility\Test-RPi5-WiFi-Performance.ps1') -Raw
if($perf.IndexOf("Write-Report 'Read-only radio") -gt $perf.IndexOf("Repeated-download START")){throw 'Radio query overlaps load stage.'}
if(!$perf.Contains('if (-not $radioReady)') -or !$perf.Contains("'-File',`$radioTool")){throw 'Missing pre-load completion gate or incorrectly quoted tool path.'}
. (Join-Path $root 'utility\Test-RPi5-WiFi-Performance.ps1') -LibraryOnly
$probe=Invoke-Rpi5BoundedProcess (Join-Path $PSHOME 'powershell.exe') @('-NoProfile','-ExecutionPolicy','Bypass','-File',$path,'-LibraryOnly') 15
if($probe.TimedOut -or $probe.ExitCode -ne 0){throw 'Radio script could not be invoked as a bounded child process.'}
Write-Output 'PASS: radio report parsing, signed RSSI, unknown/partial readbacks, backward ABI failure and pre-load-only integration.'
