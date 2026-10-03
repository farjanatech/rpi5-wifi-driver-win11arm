Set-StrictMode -Version Latest
$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot '..\utility\Test-RPi5-WiFi-Lan.ps1') -LibraryOnly
function Assert-LanTest([bool]$Condition,[string]$Message){if(-not $Condition){throw $Message}}
function Assert-LanRejection([scriptblock]$Action){$rejected=$false;try{& $Action | Out-Null}catch{$rejected=$true};Assert-LanTest $rejected 'Expected invalid evidence to be rejected.'}
foreach($address in @('10.1.2.3','172.16.1.1','172.31.255.1','192.168.1.2')){Assert-LanAddress $address}
foreach($address in @('example.com','127.0.0.1','8.8.8.8','172.32.0.1','10.1.2.999','192.168.01.2','10.1.2.3 -R','::1','')) {
    Assert-LanRejection {Assert-LanAddress $address}
}
$arguments=Get-LanArguments '192.168.1.10' '192.168.1.20' 5201 60 1 $false
Assert-LanTest (($arguments -join ' ') -ceq '-4 -c 192.168.1.10 -B 192.168.1.20 -p 5201 -t 60 -O 5 -P 1 -J') 'Bound IPv4 single-stream command.'
$reverse=Get-LanArguments '192.168.1.10' '192.168.1.20' 5201 60 4 $true
Assert-LanTest ($reverse[-1] -ceq '-R' -and ($reverse -join ' ') -match '-P 4') 'Reverse command.'
Assert-LanRejection {Get-LanArguments '192.168.1.20' '192.168.1.20' 5201 60 1 $false}
Assert-LanRejection {Get-LanArguments '192.168.1.10' '192.168.1.20' 5201 0 1 $false}
Assert-LanRejection {Get-LanArguments '192.168.1.10' '192.168.1.20' 5201 60 2 $false}
$route=@([pscustomobject]@{IPAddress='192.168.1.20';InterfaceIndex=7},[pscustomobject]@{NextHop='0.0.0.0';InterfaceIndex=7})
Assert-LanRoute $route '192.168.1.20' 7
Assert-LanRejection {Assert-LanRoute $route '192.168.1.21' 7}
Assert-LanRejection {Assert-LanRoute $route '192.168.1.20' 8}
$route[1].NextHop='192.168.1.1';Assert-LanRejection {Assert-LanRoute $route '192.168.1.20' 7}
Assert-LanRejection {Assert-LanRoute @() '192.168.1.20' 7}
function Get-LanFixture {
    '{"start":{"test_start":{"protocol":"TCP","duration":60,"omit":5,"num_streams":1,"reverse":0},"connected":[{"local_host":"192.168.1.20","remote_host":"192.168.1.10"}]},"end":{"sum_received":{"bits_per_second":24000000,"bytes":180000000,"seconds":60}}}' | ConvertFrom-Json
}
$v=Get-LanFixture
$r=ConvertFrom-LanResult ($v | ConvertTo-Json -Depth 8) '192.168.1.10' '192.168.1.20' 60 1 $false
Assert-LanTest ($r.ReceiverMbps -eq 24 -and $r.ReceiverBytes -eq 180000000) 'Receiver result math.'
foreach($key in @('protocol','duration','omit','num_streams','reverse')) {
    $x=Get-LanFixture;$x.start.test_start.$key=999
    Assert-LanRejection {ConvertFrom-LanResult ($x | ConvertTo-Json -Depth 8) '192.168.1.10' '192.168.1.20' 60 1 $false}
}
foreach($key in @('bits_per_second','bytes','seconds')) {
    $x=Get-LanFixture;$x.end.sum_received.$key=0
    Assert-LanRejection {ConvertFrom-LanResult ($x | ConvertTo-Json -Depth 8) '192.168.1.10' '192.168.1.20' 60 1 $false}
}
foreach($badSeconds in @(1,1000)) {
    $x=Get-LanFixture;$x.end.sum_received.seconds=$badSeconds
    Assert-LanRejection {ConvertFrom-LanResult ($x | ConvertTo-Json -Depth 8) '192.168.1.10' '192.168.1.20' 60 1 $false}
}
$x=Get-LanFixture;$x.start.connected[0].local_host='10.0.0.1'
Assert-LanRejection {ConvertFrom-LanResult ($x | ConvertTo-Json -Depth 8) '192.168.1.10' '192.168.1.20' 60 1 $false}
Assert-LanRejection {ConvertFrom-LanResult '{"error":"connection failed"}' '192.168.1.10' '192.168.1.20' 60 1 $false}
Assert-LanRejection {ConvertFrom-LanResult '{}' '192.168.1.10' '192.168.1.20' 60 1 $false}
# Test the actual process wrapper, not just its parser. Loopback ICMP only;
# no Wi-Fi hardware, iperf3 installation, Internet endpoint or private data.
$work=Join-Path ([IO.Path]::GetTempPath()) ('rpi-lan-fixture-'+[guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $work | Out-Null
try {
    $exe=Join-Path $work 'fixture.exe'
    Add-Type -OutputAssembly $exe -OutputType ConsoleApplication -TypeDefinition @'
using System;
using System.Threading;
public class LanFixture {
    public static int Main(string[] args) {
        if(args.Length>0 && args[0]=="hang") { Thread.Sleep(30000); return 0; }
        Console.Error.Write(new string('e',32768));
        Console.WriteLine("{\"fixture\":true}");
        return args.Length>0 && args[0]=="fail" ? 2 : 0;
    }
}
'@
    $prefix=Join-Path $work 'normal'
    $raw=Invoke-LanProcess $exe @('normal') '127.0.0.1' 10 $prefix
    Assert-LanTest (($raw | ConvertFrom-Json).fixture -eq $true) 'Concurrent stdout/stderr capture.'
    Assert-LanTest ((Get-Content "$prefix.stderr.txt" -Raw).Length -ge 32768) 'No pipe truncation/deadlock.'
    Assert-LanRejection {Invoke-LanProcess $exe @('fail') '127.0.0.1' 10 (Join-Path $work 'fail')}
    Assert-LanRejection {Invoke-LanProcess $exe @('hang') '127.0.0.1' 1 (Join-Path $work 'hang')}
    Assert-LanRejection {Invoke-LanProcess (Join-Path $work 'absent.exe') @('normal') '127.0.0.1' 1 (Join-Path $work 'absent')}
} finally {Remove-Item -LiteralPath $work -Recurse -Force}
Write-Output 'PASS: LAN-only addresses/routes, binding, receiver validation, corrupt/incomplete evidence, process exits, async pipes and watchdog.'
