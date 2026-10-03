[CmdletBinding()]
param(
    [switch]$LibraryOnly,
    [string]$IperfPath,
    [string]$ServerAddress,
    [string]$LocalAddress,
    [int]$InterfaceIndex,
    [ValidatePattern('^[A-Za-z0-9_-]{1,48}$')][string]$BuildLabel='unlabelled',
    [ValidateRange(30,300)][int]$Seconds=60,
    [ValidateRange(1,5)][int]$Repetitions=3,
    [ValidateSet(1,4)][int]$Streams=1,
    [ValidateRange(1024,65535)][int]$Port=5201,
    [string]$OutputParent=[Environment]::GetFolderPath('Desktop')
)
Set-StrictMode -Version Latest
$ErrorActionPreference='Stop'

function Assert-LanAddress([string]$Address) {
    $ip=$null
    if($Address -notmatch '^\d{1,3}(\.\d{1,3}){3}$' -or
       -not [Net.IPAddress]::TryParse($Address,[ref]$ip) -or
       $ip.AddressFamily -ne [Net.Sockets.AddressFamily]::InterNetwork -or
       $ip.ToString() -cne $Address){throw 'Use a canonical numeric IPv4 address, not a hostname.'}
    $b=$ip.GetAddressBytes()
    if(-not ($b[0] -eq 10 -or ($b[0] -eq 172 -and $b[1] -ge 16 -and $b[1] -le 31) -or
        ($b[0] -eq 192 -and $b[1] -eq 168))){throw 'This LAN-only runner requires private IPv4 addresses.'}
}
function Assert-LanRoute([object[]]$Route,[string]$Source,[int]$Index) {
    $addresses=@($Route | Where-Object {$null -ne $_.PSObject.Properties['IPAddress']})
    $routes=@($Route | Where-Object {$null -ne $_.PSObject.Properties['NextHop']})
    if($addresses.Count -ne 1 -or $routes.Count -ne 1 -or
       $addresses[0].IPAddress -ne $Source -or $addresses[0].InterfaceIndex -ne $Index -or
       $routes[0].InterfaceIndex -ne $Index -or $routes[0].NextHop -ne '0.0.0.0') {
        throw 'Target must use the selected source/interface and an on-link LAN route. No gateway/VPN fallback is allowed.'
    }
}
function Get-LanArguments([string]$Server,[string]$Source,[int]$TestPort,[int]$Duration,[int]$Parallel,[bool]$Reverse) {
    Assert-LanAddress $Server;Assert-LanAddress $Source
    if($Server -eq $Source -or $TestPort -lt 1024 -or $TestPort -gt 65535 -or
       $Duration -lt 30 -or $Duration -gt 300 -or $Parallel -notin @(1,4)){throw 'Invalid LAN test parameters.'}
    $arguments=@('-4','-c',$Server,'-B',$Source,'-p',"$TestPort",'-t',"$Duration",'-O','5','-P',"$Parallel",'-J')
    if($Reverse){$arguments+='-R'}
    return $arguments
}
function ConvertFrom-LanResult([string]$Json,[string]$Server,[string]$Source,[int]$Duration,[int]$Parallel,[bool]$Reverse) {
    $v=$Json | ConvertFrom-Json
    if($null -ne $v.PSObject.Properties['error']){throw "iperf3 error: $($v.error)"}
    $test=$v.start.test_start;$rx=$v.end.sum_received
    if($test.protocol -ne 'TCP' -or $test.duration -ne $Duration -or $test.omit -ne 5 -or
       $test.num_streams -ne $Parallel -or [int]$test.reverse -ne [int]$Reverse){throw 'iperf3 test parameters do not match the requested run.'}
    $connections=@($v.start.connected)
    if($connections.Count -ne $Parallel){throw 'Missing iperf3 connection records.'}
    foreach($c in $connections){if($c.local_host -ne $Source -or $c.remote_host -ne $Server){throw 'iperf3 used the wrong endpoint/source.'}}
    $receivedBytes=[uint64]$rx.bytes;$rate=[double]$rx.bits_per_second;$receiverSeconds=[double]$rx.seconds
    if([double]::IsNaN($rate) -or [double]::IsInfinity($rate) -or $rate -le 0 -or
       [double]::IsNaN($receiverSeconds) -or [double]::IsInfinity($receiverSeconds) -or
       $receiverSeconds -lt 0.9*$Duration -or $receiverSeconds -gt $Duration+15 -or $receivedBytes -eq 0){throw 'Incomplete or invalid receiver result.'}
    [pscustomobject]@{ReceiverMbps=$rate/1000000.0;ReceiverBytes=$receivedBytes;ReceiverSeconds=$receiverSeconds}
}
function Save-LanSnapshot([string]$Destination) {
    $out=[ordered]@{CapturedUtc=[DateTime]::UtcNow.ToString('o');Status='Unavailable';TxSnapshot=$null;Counters=$null;Error=$null}
    try {
        $raw=Get-ItemProperty -LiteralPath 'HKLM:\SOFTWARE\Rpi5CywDirectDiag'
        $c=[ordered]@{}
        foreach($name in @('DisconnectCount','WorkerFailureCount','FifoBlockFailures','FifoTransportFailed',
            'RxGlomErrors','Cmd53Timeouts','InterruptStormFallback','WorkerStartCount','WorkerRestartCount',
            'TxQueueFull','TxCreditWaits','TxQueueMaxDelayMs','TxQueueHighWater','TxQueueLimit',
            'TxPackets','RxPackets','PowerTransitionCount','NdisPauseCount','NdisRestartCount')) {
            $c[$name]=if($null -ne $raw.PSObject.Properties[$name]){$raw.$name}else{$null}
        }
        $out.Counters=[pscustomobject]$c
        $out.TxSnapshot=ConvertTo-CywTxCreditArchive $raw
        $out.Status='Captured'
    } catch {$out.Error=$_.Exception.Message}
    # Baseline 0.7.1.4 may have no TX blob. Missing/stale data is never a pass.
    $out | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $Destination -Encoding UTF8
    return [pscustomobject]$out
}
function Invoke-LanProcess([string]$Executable,[string[]]$Arguments,[string]$Server,[int]$LimitSeconds,[string]$Prefix) {
    $p=New-Object Diagnostics.Process;$ping=New-Object Net.NetworkInformation.Ping
    $rows=New-Object 'Collections.Generic.List[object]'
    $p.StartInfo.FileName=$Executable;$p.StartInfo.Arguments=$Arguments -join ' '
    $p.StartInfo.UseShellExecute=$false;$p.StartInfo.CreateNoWindow=$true
    $p.StartInfo.RedirectStandardOutput=$true;$p.StartInfo.RedirectStandardError=$true
    $clock=[Diagnostics.Stopwatch]::StartNew();$timedOut=$false;$nextPing=0.0;$started=$false
    try {
        if(-not $p.Start()){throw 'iperf3 did not start.'}
        $started=$true
        $stdout=$p.StandardOutput.ReadToEndAsync();$stderr=$p.StandardError.ReadToEndAsync()
        while(-not $p.HasExited) {
            if($clock.Elapsed.TotalSeconds -ge $LimitSeconds){$timedOut=$true;$p.Kill();break}
            if($clock.Elapsed.TotalSeconds -ge $nextPing) {
                $nextPing=$clock.Elapsed.TotalSeconds+1
                $status='Exception';$rtt=$null
                try{$reply=$ping.Send($Server,500);$status=$reply.Status.ToString();if($status -eq 'Success'){$rtt=$reply.RoundtripTime}}catch{$status='Exception'}
                $rows.Add([pscustomobject]@{ElapsedSeconds=$clock.Elapsed.TotalSeconds;Status=$status;RttMs=$rtt})
            }
            Start-Sleep -Milliseconds 100
        }
        $p.WaitForExit();$stdout.Result | Set-Content -LiteralPath "$Prefix.json" -Encoding UTF8
        $stderr.Result | Set-Content -LiteralPath "$Prefix.stderr.txt" -Encoding UTF8
        $rows | ConvertTo-Json -Depth 3 | Set-Content -LiteralPath "$Prefix.ping.json" -Encoding UTF8
        if($timedOut){throw 'iperf3 exceeded the bounded timeout; no success result.'}
        if($p.ExitCode -ne 0){throw "iperf3 exit code $($p.ExitCode); see raw stderr/JSON."}
        return $stdout.Result
    } finally {
        if($started -and -not $p.HasExited){$p.Kill();$p.WaitForExit()}
        $p.Dispose();$ping.Dispose();$clock.Stop()
    }
}
if(-not $LibraryOnly) {
    Assert-LanAddress $ServerAddress;Assert-LanAddress $LocalAddress
    if($ServerAddress -eq $LocalAddress -or $InterfaceIndex -le 0){throw 'Choose the Pi Wi-Fi address/interface and a different wired LAN server.'}
    if(-not $IperfPath -or -not (Test-Path -LiteralPath $IperfPath -PathType Leaf)){throw 'Supply a trusted local iperf3 executable; this tool never downloads one.'}
    $exe=(Resolve-Path -LiteralPath $IperfPath).Path
    . (Join-Path $PSScriptRoot 'Get-RPi5-WiFi-TxCredit.ps1') -LibraryOnly
    $route=@(Find-NetRoute -RemoteIPAddress $ServerAddress)
    Assert-LanRoute $route $LocalAddress $InterfaceIndex
    $adapters=@(Get-NetAdapter | Where-Object {$_.ifIndex -eq $InterfaceIndex})
    if($adapters.Count -ne 1 -or $adapters[0].Status -ne 'Up'){throw 'Selected adapter is not uniquely present and Up.'}
    $folder=Join-Path $OutputParent ("RPI5-LAN-{0}-{1}-{2}" -f $BuildLabel,(Get-Date -Format 'yyyyMMdd-HHmmss'),[guid]::NewGuid().ToString('N').Substring(0,6))
    New-Item -ItemType Directory -Path $folder -ErrorAction Stop | Out-Null
    $sys=Join-Path $env:windir 'System32\drivers\rpi5cyw.sys'
    $metadata=[ordered]@{BuildLabel=$BuildLabel;CreatedUtc=[DateTime]::UtcNow.ToString('o');Server=$ServerAddress;
        Source=$LocalAddress;InterfaceIndex=$InterfaceIndex;AdapterDescription=$adapters[0].InterfaceDescription;
        Seconds=$Seconds;OmitSeconds=5;Repetitions=$Repetitions;Streams=$Streams;Port=$Port;
        IperfSha256=(Get-FileHash -LiteralPath $exe -Algorithm SHA256).Hash;
        DriverFileSha256=if(Test-Path -LiteralPath $sys){(Get-FileHash -LiteralPath $sys -Algorithm SHA256).Hash}else{$null};
        DriverIdentityNote='On-disk hash is not proof the same image is loaded. Reboot after switching packages and retain installation/full diagnostics.';
        LatencyNote='ICMP uses the default route checked before/after each workload; iperf3 is explicitly source-bound. Ping samples include warmup.';
        CounterNote='Registry snapshots are periodic, not workload boundaries. General DWORDs are not an atomic set. No merge gate is evaluated.'}
    $metadata | ConvertTo-Json -Depth 4 | Set-Content (Join-Path $folder 'metadata.json') -Encoding UTF8
    $results=New-Object 'Collections.Generic.List[object]';$failed=$false
    try {
        for($trial=1;$trial -le $Repetitions;$trial++) {
            $directions=if($trial%2){@('upload','download')}else{@('download','upload')}
            foreach($direction in $directions) {
                Assert-LanRoute @(Find-NetRoute -RemoteIPAddress $ServerAddress) $LocalAddress $InterfaceIndex
                $prefix=Join-Path $folder ("trial-{0}-{1}" -f $trial,$direction)
                $before=Save-LanSnapshot "$prefix.before.json"
                $arguments=Get-LanArguments $ServerAddress $LocalAddress $Port $Seconds $Streams ($direction -eq 'download')
                $arguments -join ' ' | Set-Content "$prefix.command.txt" -Encoding ASCII
                try {
                    $raw=Invoke-LanProcess $exe $arguments $ServerAddress ($Seconds+35) $prefix
                    Assert-LanRoute @(Find-NetRoute -RemoteIPAddress $ServerAddress) $LocalAddress $InterfaceIndex
                    $value=ConvertFrom-LanResult $raw $ServerAddress $LocalAddress $Seconds $Streams ($direction -eq 'download')
                } finally {$after=Save-LanSnapshot "$prefix.after.json"}
                $window='Unavailable'
                try {
                    if($before.Status -ne 'Captured' -or $after.Status -ne 'Captured'){throw 'Extended TX snapshot unavailable.'}
                    $report=Get-CywTxCreditReport -After $after.TxSnapshot -Before $before.TxSnapshot
                    $report | ConvertTo-Json -Depth 6 | Set-Content "$prefix.tx-interval.json" -Encoding UTF8
                    $window='Advancing TX snapshots; not exactly workload-aligned'
                } catch {$_.Exception.Message | Set-Content "$prefix.snapshot-warning.txt" -Encoding UTF8}
                $results.Add([pscustomobject]@{Trial=$trial;Direction=$direction;ReceiverMbps=$value.ReceiverMbps;
                    ReceiverBytes=$value.ReceiverBytes;ReceiverSeconds=$value.ReceiverSeconds;TxWindow=$window})
                Start-Sleep -Seconds 2
            }
        }
    } catch {
        $failed=$true;$_ | Out-String | Set-Content (Join-Path $folder 'failure.txt') -Encoding UTF8
    } finally {
        [pscustomobject]@{Completed=(-not $failed -and $results.Count -eq 2*$Repetitions);
            MergeReady=$false;HardwareGates='NotEvaluated';Results=@($results.ToArray())} |
            ConvertTo-Json -Depth 6 | Set-Content (Join-Path $folder 'summary.json') -Encoding UTF8
        Compress-Archive -LiteralPath $folder -DestinationPath ($folder+'.zip') -ErrorAction Stop
        Write-Output ("LAN evidence: "+$folder+'.zip')
    }
    if($failed){throw 'LAN test did not complete; retain the failure evidence. No passing result is asserted.'}
}
