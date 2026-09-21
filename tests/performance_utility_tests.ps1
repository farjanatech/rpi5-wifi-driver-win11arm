Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$path = Join-Path (Split-Path -Parent $PSScriptRoot) 'utility\Test-RPi5-WiFi-Performance.ps1'
$tokens=$null; $errors=$null
[void][Management.Automation.Language.Parser]::ParseFile($path,[ref]$tokens,[ref]$errors)
if ($errors.Count) { throw ($errors | Out-String) }
. $path -LibraryOnly
$d=[pscustomobject]@{ DiagVersion=10; BusModeStage=6; BusWidth=4; BusActualKhz=25000; BusVerifyReads=16; BusUpgradeStatus=0; BusVerifyStatus=0 }
if ((Get-Rpi5BusAssessment $d) -notlike 'BUS VERIFIED:*') { throw 'Valid bus rejected.' }
$d.BusWidth=1
if ((Get-Rpi5BusAssessment $d) -notlike 'BUS NOT VERIFIED:*') { throw 'Slow fallback misreported as performance success.' }
if ((Get-Rpi5BusAssessment $null) -notlike '*Restart*') { throw 'Missing runtime not diagnosed.' }
if (-not (Test-Rpi5ExclusiveRoute @([pscustomobject]@{InterfaceIndex=6}) 6)) { throw 'Correct route rejected.' }
if (Test-Rpi5ExclusiveRoute @([pscustomobject]@{InterfaceIndex=6},[pscustomobject]@{InterfaceIndex=15}) 6) { throw 'Competing route accepted.' }
if (Test-Rpi5ExclusiveRoute @() 6) { throw 'Absent route accepted.' }
$stamp=134344762239466275L
if (Test-Rpi5NewerSnapshot $null $stamp) { throw 'Absent snapshot accepted.' }
if (Test-Rpi5NewerSnapshot ([pscustomobject]@{}) $stamp) { throw 'Missing timestamp accepted.' }
if (Test-Rpi5NewerSnapshot ([pscustomobject]@{SnapshotTimeUtc=$stamp}) $stamp) { throw 'Same end-of-test snapshot accepted.' }
if (Test-Rpi5NewerSnapshot ([pscustomobject]@{SnapshotTimeUtc=$stamp-1}) $stamp) { throw 'Older/mid-test snapshot accepted.' }
if (-not (Test-Rpi5NewerSnapshot ([pscustomobject]@{SnapshotTimeUtc=$stamp+1}) $stamp)) { throw 'New post-test snapshot rejected.' }
$r=Invoke-Rpi5BoundedProcess (Join-Path $PSHOME 'powershell.exe') @('-NoProfile','-Command','Write-Output 123; exit 7') 10
if ($r.ExitCode -ne 7 -or $r.TimedOut -or $r.Output -notmatch '123') { throw 'Native output/exit capture failed.' }
$r=Invoke-Rpi5BoundedProcess (Join-Path $PSHOME 'powershell.exe') @('-NoProfile','-Command','Start-Sleep -Seconds 20') 1
if (-not $r.TimedOut) { throw 'Process deadline failed.' }
$source=Get-Content -LiteralPath $path -Raw
if ($source -match '(?i)Start-Transcript|Set-DnsClient|Disable-NetAdapter|--insecure|bcdedit') { throw 'Unexpected mutation or credential capture.' }
Write-Output 'PASS: performance assessment, competing routes, native output, deadline, no unsafe network edits.'
