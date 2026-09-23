Set-StrictMode -Version Latest
$ErrorActionPreference='Stop'
$root=Split-Path -Parent $PSScriptRoot
foreach($file in @('RPi5-WiFi-Operations.ps1','Connect-RPi5-WiFi.ps1','Set-RPi5-WiFi-Autoconnect.ps1',
    'Test-RPi5-WiFi-Performance.ps1','Check-RPi5-WiFi-Readiness.ps1')) {
    $tokens=$null;$errors=$null
    [void][Management.Automation.Language.Parser]::ParseFile((Join-Path $root "utility\$file"),[ref]$tokens,[ref]$errors)
    if($errors.Count){throw "Parser error in $file"}
}
. (Join-Path $root 'utility\Check-RPi5-WiFi-Readiness.ps1') -LibraryOnly
function Assert-Rpi5Test { param([bool]$Condition,[string]$Message) if(-not $Condition){throw $Message} }
function Get-Rpi5FakeMutex {
    param($Acl,[bool]$Allow=$true,[bool]$Abandon=$false)
    $fake=[pscustomobject]@{Acl=$Acl;Allow=$Allow;Abandon=$Abandon;Waited=0;Released=0;Disposed=0}
    $fake | Add-Member ScriptMethod GetAccessControl {return $this.Acl}
    $fake | Add-Member ScriptMethod WaitOne {param($Milliseconds) $this.Waited=$Milliseconds;
        if($this.Abandon){throw [Threading.AbandonedMutexException]::new()};return $this.Allow}
    $fake | Add-Member ScriptMethod ReleaseMutex {$this.Released++}
    $fake | Add-Member ScriptMethod Dispose {$this.Disposed++}
    return $fake
}
$fixture=@{Mutex=$null}
$factory={param($Acl) $fixture.Mutex=Get-Rpi5FakeMutex $Acl;return $fixture.Mutex}
$lease=Enter-Rpi5Operation -WaitSeconds 2 -CreateMutex $factory
Assert-Rpi5Test ($lease.Owned -and $fixture.Mutex.Waited -eq 2000) 'Lock acquisition not bounded.'
Exit-Rpi5Operation $lease;Exit-Rpi5Operation $lease
Assert-Rpi5Test ($fixture.Mutex.Released -eq 1 -and $fixture.Mutex.Disposed -eq 1) 'Lease released more than once.'
$rejected=$false
try { $null=Enter-Rpi5Operation -CreateMutex {param($Acl) $fixture.Mutex=Get-Rpi5FakeMutex $Acl $false;return $fixture.Mutex} }
catch {$rejected=$true}
Assert-Rpi5Test ($rejected -and $fixture.Mutex.Released -eq 0 -and $fixture.Mutex.Disposed -eq 1) 'Busy lease incorrectly released or accepted.'
$lease=Enter-Rpi5Operation -CreateMutex {param($Acl) $fixture.Mutex=Get-Rpi5FakeMutex $Acl $true $true;return $fixture.Mutex}
Assert-Rpi5Test ($lease.Owned -and $lease.Abandoned) 'Abandoned ownership was not acknowledged.'
Exit-Rpi5Operation $lease
foreach($address in @('169.254.1.2','127.0.0.1','0.0.0.0','224.0.0.1','::1','invalid')) {
    Assert-Rpi5Test (-not (Test-Rpi5UsableAddress ([pscustomobject]@{IPAddress=$address;AddressState='Preferred'}))) 'Unusable address accepted.'
}
Assert-Rpi5Test (Test-Rpi5UsableAddress ([pscustomobject]@{IPAddress='192.168.0.2';AddressState='Preferred'})) 'Usable IPv4 rejected.'
$clock=@{Now=0;Reads=0}
$observe={ $clock.Reads++; [pscustomobject]@{Status=0;Authenticated=$false;Phase=520;Ready=$false} }
$rejected=$false
try {$null=Wait-Rpi5ConnectionState -Target Ready -Seconds 3 -Observe $observe -Now {$clock.Now} -Delay {$clock.Now++}}
catch {$rejected=$true}
Assert-Rpi5Test ($rejected -and $clock.Now -eq 3 -and $clock.Reads -eq 4) 'Readiness timeout is unbounded.'
$boundary=@{Disconnects=0;Waits=0}
$disconnect={$boundary.Disconnects++};$wait={$boundary.Waits++}
Invoke-Rpi5FreshConnectionBoundary ([pscustomobject]@{Status=0;Phase=500;Authenticated=$false}) $disconnect $wait
Assert-Rpi5Test ($boundary.Disconnects -eq 0) 'Already-idle device unnecessarily disconnected.'
Invoke-Rpi5FreshConnectionBoundary ([pscustomobject]@{Status=0;Phase=600;Authenticated=$true}) $disconnect $wait
Assert-Rpi5Test ($boundary.Disconnects -eq 1 -and $boundary.Waits -eq 1) 'Warm connection lacked one observed disconnect.'
$rejected=$false
try {Invoke-Rpi5FreshConnectionBoundary ([pscustomobject]@{Status=0;Phase=600;Authenticated=$true}) $disconnect {throw 'timeout'}} catch {$rejected=$true}
Assert-Rpi5Test ($rejected -and $boundary.Disconnects -eq 2) 'Failed disconnect was retried or ignored.'

$boot=[datetime]::UtcNow.AddHours(-1).ToString('o')
$receipt=[pscustomobject]@{SchemaVersion=1;UtilityVersion='0.6.26';BootUtc=$boot;
    StartedUtc=[datetime]::UtcNow.AddMinutes(-5).ToString('o');CompletedUtc=[datetime]::UtcNow.AddMinutes(-4).ToString('o');
    Outcome='Ready';Authenticated=$true;IPv4Ready=$true;RouteReady=$true;Status=0;UntrustedExtra='not-to-be-exported'}
$startup=Get-Rpi5StartupAssessment $receipt $boot
Assert-Rpi5Test ($startup.Outcome -eq 'Passed' -and ($startup | ConvertTo-Json -Depth 5) -notmatch 'not-to-be-exported') 'Receipt result/allowlist invalid.'
Assert-Rpi5Test ((Get-Rpi5StartupAssessment $receipt 'other-boot').Outcome -eq 'NotTested') 'Old boot accepted.'
Assert-Rpi5Test ((Get-Rpi5StartupAssessment $receipt $boot 'old-version').Outcome -eq 'NotTested') 'Old version accepted.'
$task=[pscustomobject]@{Present=$true;State='Ready';LastResult=0;LastRunUtc=[datetime]::UtcNow.AddMinutes(-6).ToString('o')}
Assert-Rpi5Test ((Test-Rpi5StartupTaskReceipt $startup $task).Outcome -eq 'Passed') 'Matching task rejected.'
$task.LastRunUtc=[datetime]::UtcNow.AddMinutes(-3).ToString('o')
Assert-Rpi5Test ((Test-Rpi5StartupTaskReceipt $startup $task).Outcome -eq 'NotTested') 'Newer task run ignored.'
$task.LastRunUtc=[datetime]::UtcNow.AddMinutes(-6).ToString('o');$task.State='Running'
Assert-Rpi5Test ((Test-Rpi5StartupTaskReceipt $startup $task).Outcome -eq 'NotTested') 'Running task accepted as completed.'
$task.State='Ready';$task.LastResult=1
Assert-Rpi5Test ((Test-Rpi5StartupTaskReceipt $startup $task).Outcome -eq 'NotTested') 'Failed task accepted.'
$receipt.IPv4Ready=$false
Assert-Rpi5Test ((Get-Rpi5StartupAssessment $receipt $boot).Outcome -eq 'Failed') 'No-IP startup passed.'
$receipt.IPv4Ready='True'
Assert-Rpi5Test ((Get-Rpi5StartupAssessment $receipt $boot).Outcome -eq 'NotTested') 'Wrong receipt field type accepted.'
$ownedDirectory='C:\ProgramData\RPi5WiFi'
$ownedPowerShell='C:\Windows\System32\WindowsPowerShell\v1.0\powershell.exe'
$ownedTask=[pscustomobject]@{Principal=[pscustomobject]@{UserId='SYSTEM'};
    Actions=@([pscustomobject]@{Execute=$ownedPowerShell;WorkingDirectory=$ownedDirectory;
        Arguments='-NoProfile -NonInteractive -ExecutionPolicy Bypass -File "C:\ProgramData\RPi5WiFi\Connect-RPi5-WiFi.ps1" -Startup -ConfigPath "C:\ProgramData\RPi5WiFi\WiFi.private.json"'})}
Assert-Rpi5Test (Test-Rpi5OwnedStartupTask $ownedTask $ownedDirectory $ownedPowerShell) 'Owned task rejected.'
$ownedTask.Actions[0].Arguments+=' -Unexpected'
Assert-Rpi5Test (-not (Test-Rpi5OwnedStartupTask $ownedTask $ownedDirectory $ownedPowerShell)) 'Unexpected task command accepted for refresh.'
Assert-Rpi5Test (-not (Test-Rpi5OwnedStartupTask $null $ownedDirectory $ownedPowerShell)) 'Absent startup task accepted.'

$checks=[ordered]@{}
foreach($name in @('Gateway latency/loss (8 requests)','Internet IPv4 latency/loss (8 requests)',
    'Configured DNS server, A query','Download hostname, configured DNS server, A query',
    'HTTPS headers; certificate verification enabled','1 MiB bounded HTTPS download; bytes/sec is application throughput')) {
    $checks[$name]=[pscustomobject]@{ProcessCompleted=$true}
}
$perf=[pscustomobject]@{Outcome='Completed';Summary=[pscustomobject]@{Completed=128;Failed=0};
    DiagnosticsCollected=$true;LoadProbeFailures=0;QueueRejectsDelta=0;Checks=$checks}
$ok=[pscustomobject]@{Outcome='Passed'};$ready=[pscustomobject]@{Ready=$true}
Assert-Rpi5Test ((Get-Rpi5ReadinessVerdict $ok $ok $perf $ready) -eq 'Ready') 'Complete readiness rejected.'
$perf.LoadProbeFailures=2
Assert-Rpi5Test ((Get-Rpi5ReadinessVerdict $ok $ok $perf $ready) -eq 'ReadyWithWarnings') 'NoResources hidden.'
$perf.LoadProbeFailures=$null
Assert-Rpi5Test ((Get-Rpi5ReadinessVerdict $ok $ok $perf $ready) -eq 'ReadyWithWarnings') 'Missing sampler evidence accepted.'
$perf.LoadProbeFailures=0;$perf.QueueRejectsDelta=1
Assert-Rpi5Test ((Get-Rpi5ReadinessVerdict $ok $ok $perf $ready) -eq 'ReadyWithWarnings') 'Queue rejects hidden.'
$perf.QueueRejectsDelta=0;$checks['Configured DNS server, A query'].ProcessCompleted=$false
Assert-Rpi5Test ((Get-Rpi5ReadinessVerdict $ok $ok $perf $ready) -eq 'ReadyWithWarnings') 'Failed preflight hidden.'
$checks['Configured DNS server, A query'].ProcessCompleted=$true
$perf.Outcome='Inconclusive'
Assert-Rpi5Test ((Get-Rpi5ReadinessVerdict $ok $ok $perf $ready) -eq 'Inconclusive') 'Server denial mislabelled.'
$perf.Outcome='Completed';$perf.Summary=$null
Assert-Rpi5Test ((Get-Rpi5ReadinessVerdict $ok $ok $perf $ready) -eq 'Failed') 'Exit-zero without load summary accepted.'

# Exercise the actual connector's warm/IfNeeded path with synthetic control
# replies. No real device IOCTL, network operation, task or registry write.
Add-Type -TypeDefinition @'
using System;
public static class Rpi5WifiControl {
    public static uint Phase=600; public static bool Connected=true;
    public static int Disconnects,Connects; public static bool BusyConnect;
    public static byte[] Derive(byte[] password,byte[] ssid) {return new byte[32];}
    public static byte[] Call(uint code,byte[] input) {
        if(code==0x12A008){Disconnects++;Phase=500;Connected=false;}
        if(code==0x12A000){Connects++;if(BusyConnect)throw new InvalidOperationException("busy");Phase=600;Connected=true;}
        byte[] result=new byte[96];BitConverter.GetBytes((uint)3).CopyTo(result,0);
        BitConverter.GetBytes(Phase).CopyTo(result,4);BitConverter.GetBytes(Connected?(uint)1:0).CopyTo(result,28);
        return result;
    }
}
'@
function Get-NetAdapter { [pscustomobject]@{ifIndex=6;InterfaceDescription='CYW43455 mock';Status='Up'} }
function Get-NetIPAddress {
    param($InterfaceIndex,$AddressFamily,$ErrorAction)
    $null=@($InterfaceIndex,$AddressFamily,$ErrorAction)
    [pscustomobject]@{IPAddress='192.168.0.2';AddressState='Preferred'}
}
function Get-NetRoute {
    param($InterfaceIndex,$AddressFamily,$DestinationPrefix,$ErrorAction)
    $null=@($InterfaceIndex,$AddressFamily,$DestinationPrefix,$ErrorAction)
    [pscustomobject]@{NextHop='192.168.0.1'}
}
function Get-NetIPConfiguration {
    param([Parameter(ValueFromPipeline)]$InputObject)
    process {if($InputObject){[pscustomobject]@{InterfaceAlias='mock';IPv4Address='192.168.0.2';IPv4DefaultGateway='192.168.0.1'}}}
}
function Get-ItemProperty {
    [Diagnostics.CodeAnalysis.SuppressMessageAttribute('PSAvoidOverwritingBuiltInCmdlets','',Justification='This synthetic connector test must prevent real registry reads.')]
    [CmdletBinding()]
    param($LiteralPath)
    $null=$LiteralPath;return $null
}
function Get-ItemPropertyValue {
    [Diagnostics.CodeAnalysis.SuppressMessageAttribute('PSAvoidOverwritingBuiltInCmdlets','',Justification='This synthetic connector test must prevent real registry reads.')]
    [CmdletBinding()]
    param($LiteralPath,$Name)
    $null=@($LiteralPath,$Name);throw 'No remembered country in mock.'
}
function New-ItemProperty {
    [Diagnostics.CodeAnalysis.SuppressMessageAttribute('PSAvoidOverwritingBuiltInCmdlets','',Justification='This synthetic connector test must block all real registry writes.')]
    [CmdletBinding(SupportsShouldProcess)]
    param($LiteralPath,$Name,$Value,$PropertyType,[switch]$Force)
    $null=@($LiteralPath,$Name,$Value,$PropertyType,$Force)
    if($PSCmdlet.ShouldProcess('mock','Reject registry write')){throw 'No registry writes in mock.'}
}
# Creating the remembered-country key is prevented too; filesystem fixtures
# below use .NET APIs rather than this command.
function New-Item {
    [Diagnostics.CodeAnalysis.SuppressMessageAttribute('PSAvoidOverwritingBuiltInCmdlets','',Justification='This synthetic connector test must block creation of real registry keys.')]
    [CmdletBinding(SupportsShouldProcess)]
    param($Path,[switch]$Force)
    $null=@($Path,$Force)
    if($PSCmdlet.ShouldProcess('mock','Reject registry write')){throw 'No registry writes in mock.'}
}
$temporary=Join-Path ([IO.Path]::GetTempPath()) ('rpi5-readiness-test-'+[guid]::NewGuid().ToString('N'))
[void][IO.Directory]::CreateDirectory($temporary)
$profilePath=Join-Path $temporary 'WiFi.private.json'
[IO.File]::WriteAllText($profilePath,'{"Country":"BD","SSID":"synthetic-test","Password":"synthetic-only"}')
try {
    $connector=Join-Path $root 'utility\Connect-RPi5-WiFi.ps1'
    $output=@(& $connector -IfNeeded -ConfigPath $profilePath -PassThru)
    Assert-Rpi5Test ([Rpi5WifiControl]::Connects -eq 0 -and [Rpi5WifiControl]::Disconnects -eq 0) 'Healthy IfNeeded mutated connection.'
    Assert-Rpi5Test (@($output | Where-Object {$_ -isnot [string] -and $_.Outcome -eq 'AlreadyReady'}).Count -eq 1) 'Healthy result missing.'
    $output=@(& $connector -ConfigPath $profilePath -PassThru)
    Assert-Rpi5Test ([Rpi5WifiControl]::Connects -eq 1 -and [Rpi5WifiControl]::Disconnects -eq 1) 'Warm path did not disconnect once before connect.'
    Assert-Rpi5Test (($output | Out-String) -notmatch 'synthetic-only') 'Credential leaked to output.'
    [Rpi5WifiControl]::BusyConnect=$true;$rejected=$false
    try {$null=& $connector -ConfigPath $profilePath -PassThru} catch {$rejected=$true}
    Assert-Rpi5Test ($rejected -and [Rpi5WifiControl]::Connects -eq 2 -and [Rpi5WifiControl]::Disconnects -eq 2) 'Busy connect was retried or false success.'
} finally {
    [IO.File]::Delete($profilePath);[IO.Directory]::Delete($temporary)
}
Write-Output 'PASS: bounded leases, fresh warm connection, healthy no-op, IP readiness, startup receipts, truthful report warnings and no repeated recovery.'
