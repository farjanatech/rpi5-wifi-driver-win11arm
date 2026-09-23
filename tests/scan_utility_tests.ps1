Set-StrictMode -Version Latest
$ErrorActionPreference='Stop'
$root=Split-Path -Parent $PSScriptRoot
$scanPath=Join-Path $root 'utility\RPi5-WiFi-Scan.ps1'
$appPath=Join-Path $root 'utility\RPi5-WiFi-App.ps1'
foreach($path in @($scanPath,$appPath)){
    $tokens=$null;$parseErrors=$null
    [void][Management.Automation.Language.Parser]::ParseFile($path,[ref]$tokens,[ref]$parseErrors)
    if($parseErrors.Count){throw "PowerShell parser error: $path"}
}
# Safe import must not instantiate a form, load CNG/control native interop,
# read a private profile, open the driver, or launch an elevated process.
. $appPath -LibraryOnly
function Assert-ScanTest {param([bool]$Value,[string]$Message) if(-not $Value){throw $Message}}
function Assert-ScanThrow {
    param([scriptblock]$Action,[string]$Message)
    $rejected=$false;try{$null=& $Action}catch{$rejected=$true}
    Assert-ScanTest $rejected $Message
}
function Get-ScanFixture {
    param([byte[]]$Ssid=([Text.Encoding]::UTF8.GetBytes('test-network')),[uint32]$Security=2)
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
    [BitConverter]::GetBytes([uint32]36).CopyTo($bytes,84)
    return ,$bytes
}
$request=ConvertTo-Rpi5ScanRequest 'BD'
Assert-ScanTest ($request -is [byte[]] -and $request.Length -eq 8 -and
    [BitConverter]::ToUInt32($request,0) -eq 1 -and $request[4] -eq 66 -and $request[5] -eq 68 -and
    $request[6] -eq 0 -and $request[7] -eq 0) 'Scan request ABI differs.'
foreach($country in @('','B','bd','USA','B1','BD;')){Assert-ScanThrow {ConvertTo-Rpi5ScanRequest $country} 'Invalid/unconfirmed country accepted.'}
$fixture=Get-ScanFixture
$report=ConvertFrom-Rpi5ScanReport $fixture
Assert-ScanTest ($report.Version -eq 1 -and $report.Generation -eq 12 -and $report.State -eq 3 -and
    $report.Country -eq 'BD' -and $report.Count -eq 1 -and -not $report.Truncated) 'Scan header decoding failed.'
$entry=$report.Entries[0]
Assert-ScanTest ($entry.Ssid -ceq 'test-network' -and $entry.Connectable -and $entry.Band -eq '5 GHz' -and
    $entry.Channel -eq 36 -and $entry.RssiDbm -eq -45 -and $entry.Bssid -eq '02:03:04:05:06:07' -and
    $entry.Security -eq 'WPA2-Personal / AES') 'Compatible scan entry decoding failed.'
foreach($pair in @(@(0,2),@(4,0),@(8,6),@(16,65),@(20,2),@(24,65536),@(32,33))){
    $malformed=[byte[]]$fixture.Clone();[BitConverter]::GetBytes([uint32]$pair[1]).CopyTo($malformed,[int]$pair[0])
    Assert-ScanThrow {ConvertFrom-Rpi5ScanReport $malformed} 'Malformed scan ABI accepted.'
}
foreach($length in @(0,32,3615,3617)){Assert-ScanThrow {ConvertFrom-Rpi5ScanReport ([byte[]]::new($length))} 'Invalid scan output size accepted.'}
foreach($first in @(1,255)){
    $malformed=[byte[]]$fixture.Clone();$malformed[68]=[byte]$first
    Assert-ScanThrow {ConvertFrom-Rpi5ScanReport $malformed} 'Multicast BSSID accepted.'
}
$malformed=[byte[]]$fixture.Clone();[Array]::Clear($malformed,68,6)
Assert-ScanThrow {ConvertFrom-Rpi5ScanReport $malformed} 'Zero BSSID accepted.'
foreach($security in @(0,1,3,4,6,8,10,16,18,32,34,64,66,255)){
    $blocked=ConvertFrom-Rpi5ScanReport (Get-ScanFixture -Security $security)
    Assert-ScanTest (-not $blocked.Entries[0].Connectable) 'Unsupported/malformed/security-policy network became selectable.'
}
foreach($raw in @([byte[]]@(),[byte[]]@(0xc3,0x28),[byte[]]@(65,0,66),[byte[]]@(65,10,66),
    [Text.Encoding]::UTF8.GetBytes(('a'+[char]0x202e+'b')))){
    $blocked=ConvertFrom-Rpi5ScanReport (Get-ScanFixture -Ssid $raw)
    Assert-ScanTest (-not $blocked.Entries[0].Connectable -and $null -eq $blocked.Entries[0].Ssid) 'Unsafe/hidden SSID text became a connect target.'
}
$utf8Ssid=[string][char]0x00e9+'-network'
$unicode=ConvertFrom-Rpi5ScanReport (Get-ScanFixture -Ssid ([Text.Encoding]::UTF8.GetBytes($utf8Ssid)))
Assert-ScanTest ($unicode.Entries[0].Ssid -ceq $utf8Ssid -and $unicode.Entries[0].Connectable) 'Exact UTF-8 SSID was damaged.'
$full=Get-ScanFixture -Ssid ([Text.Encoding]::ASCII.GetBytes(('x'*32)))
Assert-ScanTest (ConvertFrom-Rpi5ScanReport $full).Entries[0].Connectable '32-byte SSID was rejected.'
$unknownChannel=[byte[]]$fixture.Clone();[BitConverter]::GetBytes([uint32]15).CopyTo($unknownChannel,84)
Assert-ScanTest (-not (ConvertFrom-Rpi5ScanReport $unknownChannel).Entries[0].Connectable) 'Invalid/unknown channel silently assigned a band.'
$channel24=[byte[]]$fixture.Clone();[BitConverter]::GetBytes([uint32]6).CopyTo($channel24,84)
Assert-ScanTest ((ConvertFrom-Rpi5ScanReport $channel24).Entries[0].Band -eq '2.4 GHz') '2.4 GHz channel decoded incorrectly.'
$duplicate=[byte[]]$fixture.Clone();[BitConverter]::GetBytes([uint32]2).CopyTo($duplicate,16)
[Array]::Copy($fixture,32,$duplicate,88,56);$duplicate[129]=8
Assert-ScanTest ((ConvertFrom-Rpi5ScanReport $duplicate).Entries.Count -eq 2) 'Duplicate SSIDs/BSSIDs incorrectly merged or forced.'
$truncated=[byte[]]$fixture.Clone();[BitConverter]::GetBytes([uint32]1).CopyTo($truncated,20)
Assert-ScanTest (ConvertFrom-Rpi5ScanReport $truncated).Truncated 'Truncation hidden.'

$idle=[pscustomobject]@{Phase=500;Status=0;Authenticated=$false}
Assert-ScanTest (Test-Rpi5AppIdle $idle) 'Idle disconnected driver not recognized.'
foreach($sample in @($null,[pscustomobject]@{Phase=600;Status=0;Authenticated=$true},
    [pscustomobject]@{Phase=520;Status=0;Authenticated=$false},[pscustomobject]@{Phase=500;Status=1;Authenticated=$false})){
    Assert-ScanTest (-not (Test-Rpi5AppIdle $sample)) 'Unsafe scan/connect UI gate opened.'
}
Assert-ScanTest ((Get-Rpi5AppOperationOutcome Scan $idle 0 $report 12) -eq 'Wait') 'Stale prior scan completed the new operation.'
Assert-ScanTest ((Get-Rpi5AppOperationOutcome Scan $idle 0 $report 11) -eq 'Complete') 'Fresh successful scan ignored.'
Assert-ScanTest ((Get-Rpi5AppOperationOutcome Scan $idle 90 $null 12) -eq 'Timeout') 'Scan observation not bounded.'
$failed=[pscustomobject]@{Generation=13;State=4;Status=3221225473}
Assert-ScanTest ((Get-Rpi5AppOperationOutcome Scan $idle 1 $failed 12) -eq 'Failed') 'Scan failure hidden.'
$pending=[pscustomobject]@{Generation=13;State=2;Status=259}
Assert-ScanTest ((Get-Rpi5AppOperationOutcome Scan $idle 1 $pending 12) -eq 'Wait') 'Pending scan treated as failure.'
$linked=[pscustomobject]@{Phase=600;Status=0;Authenticated=$true}
Assert-ScanTest ((Get-Rpi5AppOperationOutcome Connect $linked 1 $null 0) -eq 'Complete') 'Authenticated connection not recognized.'
Assert-ScanTest ((Get-Rpi5AppOperationOutcome Connect $idle 180 $null 0) -eq 'Timeout') 'Connect wait is unbounded.'
Assert-ScanTest ((Get-Rpi5AppOperationOutcome Disconnect $linked 20 $null 0) -eq 'Timeout') 'Disconnect wait is unbounded.'
Assert-ScanTest ((Get-Rpi5AppOperationOutcome Disconnect $idle 1 $null 0) -eq 'Complete') 'Disconnection not recognized.'

# Immediate-close cancellation: a successful start captures the fresh
# generation before the caller can return to the UI message loop.
$scanContext=@{Operation='';Watch=$null;PreviousGeneration=[uint32]12;ScanGeneration=$null;ScanSubmitted=$false}
$lifecycle=@{Starts=0;Reads=0;Cancels=0;CancelledGeneration=$null}
$startScan={param($Data)
    $lifecycle.Starts++
    Assert-ScanTest ($scanContext.Operation -eq 'Scan' -and $Data.Length -eq 8) 'Operation not owned before scan submission.'
}
$readScan={$lifecycle.Reads++;[pscustomobject]@{Generation=[uint32]13;State=1;Status=259}}
$cancelScan={param($Data) $lifecycle.Cancels++;$lifecycle.CancelledGeneration=[BitConverter]::ToUInt32($Data,0)}
Submit-Rpi5AppScan $scanContext BD $startScan $readScan
Assert-ScanTest ($scanContext.ScanGeneration -eq 13 -and $lifecycle.Starts -eq 1 -and $lifecycle.Reads -eq 1) 'Fresh scan generation was not captured synchronously.'
Assert-ScanTest (Invoke-Rpi5AppScanCancel $scanContext $readScan $cancelScan) 'Immediate-close scan was not cancelled.'
Assert-ScanTest ($lifecycle.Cancels -eq 1 -and $lifecycle.CancelledGeneration -eq 13 -and $lifecycle.Reads -eq 1) 'Known-generation cancellation guessed or performed extra reads.'
$scanContext.ScanGeneration=$null;$scanContext.Operation=''
Assert-ScanThrow {Submit-Rpi5AppScan $scanContext BD $startScan {throw 'mock report failure'}} 'Failed immediate report read ignored.'
Assert-ScanTest ($scanContext.Operation -eq 'Scan') 'Observation failure lost scan ownership before cancellation.'
Assert-ScanTest (Invoke-Rpi5AppScanCancel $scanContext $readScan $cancelScan) 'Bounded recovery read could not cancel fresh scan.'
Assert-ScanTest ($lifecycle.Cancels -eq 2 -and $lifecycle.CancelledGeneration -eq 13) 'Recovery cancelled the wrong scan.'
Assert-ScanTest (-not (Invoke-Rpi5AppScanCancel $scanContext {[pscustomobject]@{Generation=12;State=3}} $cancelScan)) 'Stale prior scan was cancelled.'
Assert-ScanTest (-not (Invoke-Rpi5AppScanCancel $scanContext {throw 'unavailable'} $cancelScan)) 'Missing scan generation was guessed.'
Assert-ScanTest ($lifecycle.Cancels -eq 2) 'Cancellation retried without a verified generation.'
$scanContext.PreviousGeneration=[uint32]::MaxValue;$scanContext.ScanGeneration=[uint32]1
Assert-ScanTest (Invoke-Rpi5AppScanCancel $scanContext $readScan $cancelScan) 'Wrapped generation one was rejected.'
Assert-ScanTest ($lifecycle.CancelledGeneration -eq 1) 'Wrapped cancellation generation damaged.'
$scanContext.ScanGeneration=[uint32]0
Assert-ScanTest (-not (Invoke-Rpi5AppScanCancel $scanContext $readScan $cancelScan)) 'Reserved generation zero sent to driver.'
$readsBefore=$lifecycle.Reads;$cancelsBefore=$lifecycle.Cancels
Assert-ScanThrow {Submit-Rpi5AppScan $scanContext BD {param($Data)
    Assert-ScanTest ($Data.Length -eq 8) 'Rejected-start mock received an invalid request.'
    throw 'busy start rejected'
} $readScan} 'Rejected start ignored.'
Assert-ScanTest (-not $scanContext.ScanSubmitted) 'Rejected start claimed driver scan ownership.'
Assert-ScanTest (-not (Invoke-Rpi5AppScanCancel $scanContext $readScan $cancelScan)) 'Rejected start tried cancelling another client scan.'
Assert-ScanTest ($lifecycle.Reads -eq $readsBefore -and $lifecycle.Cancels -eq $cancelsBefore) 'Rejected start made recovery reads/cancellation.'
$fakePassword=[pscustomobject]@{Text='dummy-old-password'}
$fakePassword | Add-Member ScriptMethod Clear {$this.Text=''}
$selectionContext=@{Ssid=[pscustomobject]@{Text='previous-network'};Password=$fakePassword;Message=''}
Select-Rpi5AppNetwork $selectionContext ([pscustomobject]@{Connectable=$false;Ssid=$null})
Assert-ScanTest ($selectionContext.Ssid.Text -eq '' -and $selectionContext.Password.Text -eq '') 'Unsupported selection retained previous SSID/password.'
Select-Rpi5AppNetwork $selectionContext $entry
Assert-ScanTest ($selectionContext.Ssid.Text -ceq 'test-network' -and $selectionContext.Password.Text -eq '') 'Compatible selection changed target or retained password.'

# Mock derivation and control send exercise the production request constructor;
# no driver handle, firmware, credentials on disk, native form, or real password.
$capture=@{Derived=0;Sent=0;Pmk=[byte[]](1..32);Request=$null}
$derive={param([byte[]]$Password,[byte[]]$Name)
    $capture.Derived++
    Assert-ScanTest ($Password.Length -eq 8 -and [Text.Encoding]::UTF8.GetString($Name) -ceq 'test') 'Derivation inputs damaged.'
    return ,$capture.Pmk
}
$send={param($Data)
    $capture.Sent++;$capture.Request=$Data
    Assert-ScanTest ($Data.Length -eq 76 -and [BitConverter]::ToUInt32($Data,0) -eq 1 -and
        [BitConverter]::ToUInt32($Data,4) -eq 4 -and $Data[8] -eq 66 -and $Data[9] -eq 68 -and
        $Data[10] -eq 0 -and $Data[11] -eq 0 -and $Data[44] -eq 1 -and $Data[75] -eq 32) 'Existing WPA2 connect ABI changed.'
}
$password=[Text.Encoding]::ASCII.GetBytes('mockpass')
Invoke-Rpi5AppConnect BD test $password $derive $send
Assert-ScanTest ($capture.Derived -eq 1 -and $capture.Sent -eq 1) 'Connect was retried or not issued once.'
foreach($buffer in @($password,$capture.Pmk,$capture.Request)){
    Assert-ScanTest (@($buffer | Where-Object {$_ -ne 0}).Count -eq 0) 'Sensitive byte buffer was not cleared after success.'
}
$capture.Pmk=[byte[]](1..32);$password=[Text.Encoding]::ASCII.GetBytes('mockpass')
Assert-ScanThrow {Invoke-Rpi5AppConnect BD test $password $derive {param($Data) $capture.Request=$Data;throw 'mock send failure'}} 'Send failure ignored.'
foreach($buffer in @($password,$capture.Pmk,$capture.Request)){
    Assert-ScanTest (@($buffer | Where-Object {$_ -ne 0}).Count -eq 0) 'Sensitive byte buffer was not cleared after failure.'
}
$before=$capture.Derived
foreach($bad in @('',('x'*33),('a'+[char]0+'b'),('a'+[char]0x202e+'b'))){
    $password=[Text.Encoding]::ASCII.GetBytes('mockpass')
    Assert-ScanThrow {Invoke-Rpi5AppConnect BD $bad $password $derive $send} 'Invalid SSID accepted.'
    Assert-ScanTest (@($password | Where-Object {$_ -ne 0}).Count -eq 0) 'Invalid-input password not cleared.'
}
Assert-ScanTest ($capture.Derived -eq $before) 'Invalid SSID reached key derivation.'
$password=[byte[]]@(1,2,3,4,5,6,7,8)
Assert-ScanThrow {Invoke-Rpi5AppConnect BD test $password $derive $send} 'Non-printable password accepted.'
Assert-ScanTest ($capture.Derived -eq $before) 'Invalid password reached key derivation.'

$appSource=Get-Content -LiteralPath $appPath -Raw
$scanSource=Get-Content -LiteralPath $scanPath -Raw
foreach($forbidden in @('Start-Transcript','Read-Rpi5WifiConfig','WiFi.private.json','Set-Content','Add-Content','Out-File',
    'Register-ScheduledTask','Set-RPi5-WiFi-Autoconnect','Invoke-Expression','0x12A00C','Get-Rpi5ConnectionReadiness')){
    Assert-ScanTest (-not $appSource.Contains($forbidden)) 'App added persistence, private profile access, radio refresh or hidden connection work.'
}
Assert-ScanTest ($appSource.Contains('UseSystemPasswordChar=$true') -and $appSource.Contains('$app.Password.Clear()')) 'Password masking/clearing missing.'
Assert-ScanTest ($appSource.Contains('Enter-Rpi5Operation -WaitSeconds 0') -and $appSource.Contains('Exit-Rpi5Operation')) 'Nonblocking operation serialization missing.'
Assert-ScanTest ($appSource.Contains('-ScanLibraryOnly') -and $scanSource.Contains('param([switch]$ScanLibraryOnly)')) 'Dot-source LibraryOnly parameter would overwrite app launch mode.'
foreach($code in @('0x12A014','0x126018','0x12A01C')){Assert-ScanTest ($scanSource.Contains($code) -and $appSource.Contains($code)) 'Scan control ABI mismatch.'}
$tokens=$null;$parseErrors=$null
$ast=[Management.Automation.Language.Parser]::ParseFile($appPath,[ref]$tokens,[ref]$parseErrors)
$tick=$ast.Find({param($node) $node -is [Management.Automation.Language.FunctionDefinitionAst] -and $node.Name -eq 'Show-Rpi5AppStatus'},$true)
Assert-ScanTest ($null -ne $tick -and -not $tick.Extent.Text.Contains('0x12A014') -and -not $tick.Extent.Text.Contains('0x12A000')) 'Status timer initiates scan/connect.'
$launcher=Get-Content -LiteralPath (Join-Path $root 'utility\RPi5-WiFi-App.cmd') -Raw
Assert-ScanTest ($launcher.Contains('-STA') -and $launcher.Contains('System32\WindowsPowerShell\v1.0\powershell.exe')) 'Native PowerShell STA launcher missing.'
Assert-ScanTest ($appSource.Contains('if(-not $PreviewPath){. (Join-Path $PSScriptRoot ''Connect-RPi5-WiFi.ps1'') -LibraryOnly}') -and
    $appSource.IndexOf('if($PreviewPath){') -lt $appSource.IndexOf('$timer.add_Tick') -and
    $appSource.IndexOf('if($PreviewPath){') -lt $appSource.IndexOf('$form.add_Shown') -and
    $appSource.Contains('$form.DrawToBitmap') -and $appSource.Contains('Offline preview must target a PNG')) 'Offline preview can reach native control initialization/events or lacks bounded output.'
Write-Output 'PASS: scan ABI, strict SSID/security selection, stale-generation/timeout gates, existing connect ABI and credential cleanup; no GUI or hardware executed.'
