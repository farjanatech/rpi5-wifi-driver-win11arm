[CmdletBinding()]
param(
    [ValidateSet('Menu','Connect','Status','FullTest','Diagnostics')][string]$Mode='Menu',
    [string]$ConfigPath,
    [switch]$SkipUpload,
    [switch]$NoPause
)

Set-StrictMode -Version Latest
$ErrorActionPreference='Stop'
$InformationPreference='Continue'
$script:ToolVersion='0.7.1.14'
$script:SkipUploadRequested=[bool]$SkipUpload
$script:PayloadBase64='__PAYLOAD_BASE64__'
$script:DiagKey='HKLM:\SOFTWARE\Rpi5CywDirectDiag'
$script:MonotonicCounters=@(
    'TxPackets','RxPackets','TxQueueFull','TxBacklogAccepted','TxBacklogPromoted','TxBacklogFull',
    'TxGlomAttempts','TxGlomChains','TxGlomFrames','TxGlomBusyFallbacks','TxGlomErrors',
    'TxGlomPayloadBytes','TxGlomPaddedBytes','TxGlomExtendedDataSingles','TxGlomExtendedControlSingles',
    'TxCreditWaits','TxPressurePasses','TxPressureFrames','TxPressureDeadlineYields',
    'Cmd53ReadCount','Cmd53WriteCount','FifoBlockCommands','FifoBlockBytes','FifoBlockFailures',
    'Cmd53WaitSleeps','Cmd53Timeouts','TransportStatusReads','TransportStatusAcks',
    'TransportTxStatusChecks','TransportMailboxReads','TransportServiceErrors',
    'WorkerFailureCount','DisconnectCount','RxGlomErrors'
)
$script:StateCounters=@(
    'DiagVersion','NetworkPhase','NetworkStatus','TxQueueHighWater','TxQueueFrames',
    'TxBacklogCurrent','TxBacklogHighWater','TxBacklogMaxDelayMs','TxGlomEnabled',
    'TxCreditSequence','TxCreditMaximum','TransportGlobalFlow','TransportPriorityBlocked',
    'FifoTransportFailed','BusActualKhz','BusWidth'
)

function Test-Rpi5Administrator {
    $principal=[Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()
    return $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
}
function Invoke-Rpi5SelfElevated {
    $launchArgs=@('-NoProfile','-ExecutionPolicy','Bypass','-File',('"{0}"' -f $PSCommandPath),'-Mode',$Mode)
    if($ConfigPath){
        if($ConfigPath.Contains('"')){throw 'Invalid configuration path.'}
        $launchArgs+=@('-ConfigPath',('"{0}"' -f [IO.Path]::GetFullPath($ConfigPath)))
    }
    if($script:SkipUploadRequested){$launchArgs+='-SkipUpload'}
    if($NoPause){$launchArgs+='-NoPause'}
    Start-Process -FilePath (Join-Path $PSHOME 'powershell.exe') -Verb RunAs -ArgumentList $launchArgs
}
function Expand-Rpi5EmbeddedPayload {
    $root=Join-Path $env:TEMP ('RPi5WiFi-AllInOne-'+[guid]::NewGuid().ToString('N'))
    [void](New-Item -ItemType Directory -Path $root)
    $zip=Join-Path $root 'payload.zip'
    [IO.File]::WriteAllBytes($zip,[Convert]::FromBase64String($script:PayloadBase64))
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    [IO.Compression.ZipFile]::ExtractToDirectory($zip,$root)
    Remove-Item -LiteralPath $zip -Force
    return $root
}
function Invoke-Rpi5Tool {
    param([string]$Root,[string]$Name,[string[]]$Arguments=@(),[string]$LogPath)
    $path=Join-Path $Root $Name
    if(-not (Test-Path -LiteralPath $path -PathType Leaf)){throw "Embedded helper is missing: $Name"}
    $all=@('-NoProfile','-ExecutionPolicy','Bypass','-File',$path)+$Arguments
    if($LogPath){
        & (Join-Path $PSHOME 'powershell.exe') @all 2>&1 | Tee-Object -FilePath $LogPath
        $code=$LASTEXITCODE
    }else{
        & (Join-Path $PSHOME 'powershell.exe') @all
        $code=$LASTEXITCODE
    }
    if($code -ne 0){throw "$Name failed with exit code $code."}
}
function Get-Rpi5DiagSnapshot {
    try{return Get-ItemProperty -LiteralPath $script:DiagKey -ErrorAction Stop}catch{return $null}
}
function Get-Rpi5Int64 {
    param($Object,[string]$Name)
    if($null -eq $Object -or -not $Object.PSObject.Properties[$Name]){return $null}
    try{return [int64]$Object.$Name}catch{return $null}
}
function Get-Rpi5CounterSet {
    param($Snapshot)
    $values=[ordered]@{}
    foreach($name in @($script:MonotonicCounters+$script:StateCounters)){
        $values[$name]=Get-Rpi5Int64 $Snapshot $name
    }
    $values['SnapshotTimeUtc']=Get-Rpi5Int64 $Snapshot 'SnapshotTimeUtc'
    return [pscustomobject]$values
}
function Get-Rpi5CounterDelta {
    param($Before,$After)
    $values=[ordered]@{}
    foreach($name in $script:MonotonicCounters){
        $a=Get-Rpi5Int64 $Before $name
        $b=Get-Rpi5Int64 $After $name
        $values[$name]=if($null -ne $a -and $null -ne $b -and $b -ge $a){$b-$a}else{$null}
    }
    return [pscustomobject]$values
}
function Get-Rpi5FreshSnapshot {
    param([string]$Root,[string]$OutputDirectory,[string]$Label)
    $old=Get-Rpi5DiagSnapshot
    $oldStamp=Get-Rpi5Int64 $old 'SnapshotTimeUtc'
    $radio=Join-Path $Root 'Get-RPi5-WiFi-Radio.ps1'
    $radioOutput=& (Join-Path $PSHOME 'powershell.exe') -NoProfile -ExecutionPolicy Bypass -File $radio -AsJson 2>&1
    $radioCode=$LASTEXITCODE
    $radioOutput|Set-Content -LiteralPath (Join-Path $OutputDirectory ($Label+'-radio.json')) -Encoding UTF8
    if($radioCode -ne 0){throw "Read-only snapshot trigger failed for $Label with exit code $radioCode."}
    for($attempt=0;$attempt -lt 20;$attempt++){
        $current=Get-Rpi5DiagSnapshot
        $stamp=Get-Rpi5Int64 $current 'SnapshotTimeUtc'
        if($null -ne $stamp -and ($null -eq $oldStamp -or $stamp -gt $oldStamp)){
            $current|Format-List *|Out-String -Width 500|
                Set-Content -LiteralPath (Join-Path $OutputDirectory ($Label+'-driver.txt')) -Encoding UTF8
            return $current
        }
        Start-Sleep -Milliseconds 100
    }
    throw "Driver snapshot did not refresh after the read-only radio query for $Label."
}
function Get-Rpi5ActiveNetwork {
    $adapters=@(Get-NetAdapter -ErrorAction Stop|
        Where-Object InterfaceDescription -like '*CYW43455*'|
        Where-Object Status -eq 'Up')
    if($adapters.Count -ne 1){throw 'Expected exactly one running CYW43455 adapter.'}
    $adapter=$adapters[0]
    $ips=@(Get-NetIPAddress -InterfaceIndex $adapter.ifIndex -AddressFamily IPv4 -ErrorAction Stop|
        Where-Object {$_.IPAddress -notlike '169.254.*'})
    if($ips.Count -lt 1){throw 'CYW43455 has no usable IPv4 address.'}
    $ip=[string]$ips[0].IPAddress
    $otherDefaults=@(Get-NetRoute -PolicyStore ActiveStore -AddressFamily IPv4 -DestinationPrefix '0.0.0.0/0' -ErrorAction Stop|
        Where-Object {$_.InterfaceIndex -ne $adapter.ifIndex -and $_.NextHop -ne '0.0.0.0'})
    if($otherDefaults.Count){throw 'Another IPv4 default route is active. Disconnect Ethernet/VPN before testing.'}
    return [pscustomobject]@{Adapter=$adapter;IPv4=$ip}
}
function Get-Rpi5Median {
    param([double[]]$Values)
    if(-not $Values -or !$Values.Count){return $null}
    $sorted=@($Values|Sort-Object);$middle=[int]($sorted.Count/2)
    if($sorted.Count%2){return [double]$sorted[$middle]}
    return ([double]$sorted[$middle-1]+[double]$sorted[$middle])/2
}
function ConvertFrom-Rpi5CurlRow {
    param($Raw,[ValidateSet('Upload','Download')][string]$Direction,[long]$Bytes,[int]$Stream)
    $text=(@($Raw)-join [Environment]::NewLine).Trim()
    $parts=$text.Split('|')
    if($parts.Count -ne 6 -or $parts[0] -ne '200'){throw ("Unexpected {0} response for stream {1}: {2}" -f $Direction,$Stream,$text)}
    $numbers=[double[]]::new(5)
    for($index=0;$index -lt 5;$index++){
        if(-not [double]::TryParse($parts[$index+1],[Globalization.NumberStyles]::Float,
            [Globalization.CultureInfo]::InvariantCulture,[ref]$numbers[$index]) -or $numbers[$index] -lt 0){
            throw ("Invalid {0} timing for stream {1}: {2}" -f $Direction,$Stream,$text)
        }
    }
    $total=$numbers[0];$reportedBytesPerSecond=$numbers[1];$connect=$numbers[2];$tls=$numbers[3];$pretransfer=$numbers[4]
    if($total -le 0 -or $reportedBytesPerSecond -le 0){throw "Invalid $Direction throughput for stream $Stream."}
    $bodySeconds=$total-$pretransfer
    $payloadMbps=if($bodySeconds -gt 0){[double]$Bytes*8/$bodySeconds/1000000}else{$null}
    return [pscustomobject]@{
        Stream=$Stream;HttpStatus=200;Bytes=$Bytes;TotalSeconds=$total;
        CurlMbps=$reportedBytesPerSecond*8/1000000;ConnectSeconds=$connect;
        TlsSeconds=$tls;PreTransferSeconds=$pretransfer;BodySeconds=$bodySeconds;
        PayloadMbps=$payloadMbps
    }
}
function Invoke-Rpi5TransferStage {
    param(
        [string]$Root,[string]$OutputDirectory,[ValidateSet('Upload','Download')][string]$Direction,
        [ValidateRange(1,4)][int]$Streams,[ValidateRange(1048576,134217728)][long]$BytesPerStream,
        [string]$PayloadPath,[string]$Label
    )
    $network=Get-Rpi5ActiveNetwork
    $curl=(Get-Command curl.exe -ErrorAction Stop).Source
    if($Direction -eq 'Upload' -and (-not $PayloadPath -or -not (Test-Path -LiteralPath $PayloadPath -PathType Leaf))){
        throw 'Upload payload is missing.'
    }
    $before=Get-Rpi5FreshSnapshot $Root $OutputDirectory ($Label+'-before')
    $beforeStats=Get-NetAdapterStatistics -Name $network.Adapter.Name
    $planned=[datetime]::UtcNow.AddSeconds(3)
    $jobs=[Collections.Generic.List[object]]::new()
    try{
        for($stream=1;$stream -le $Streams;$stream++){
            $jobs.Add((Start-Job -ScriptBlock {
                param($CurlPath,$IpAddress,$TransferDirection,$TransferBytes,$Payload,$StartAt,$StreamNumber)
                while([datetime]::UtcNow -lt $StartAt){Start-Sleep -Milliseconds 20}
                $started=[datetime]::UtcNow
                if($TransferDirection -eq 'Upload'){
                    $url='https://speed.cloudflare.com/__up'
                    $curlArgs=@('--silent','--show-error','--fail','--max-time','90','--interface',$IpAddress,
                        '--request','POST','--header','Content-Type: application/octet-stream',
                        '--data-binary',('@'+$Payload),'--output','NUL',
                        '--write-out','%{http_code}|%{time_total}|%{speed_upload}|%{time_connect}|%{time_appconnect}|%{time_pretransfer}',
                        $url)
                }else{
                    $url='https://speed.cloudflare.com/__down?bytes='+$TransferBytes
                    $curlArgs=@('--silent','--show-error','--fail','--max-time','90','--interface',$IpAddress,
                        '--output','NUL',
                        '--write-out','%{http_code}|%{time_total}|%{speed_download}|%{time_connect}|%{time_appconnect}|%{time_starttransfer}',
                        $url)
                }
                $raw=& $CurlPath @curlArgs 2>&1
                $code=$LASTEXITCODE
                $ended=[datetime]::UtcNow
                [pscustomobject]@{Stream=$StreamNumber;ExitCode=$code;Raw=@($raw);
                    StartedUtc=$started;EndedUtc=$ended}
            } -ArgumentList $curl,$network.IPv4,$Direction,$BytesPerStream,$PayloadPath,$planned,$stream))
        }
        [void](Wait-Job -Job @($jobs) -Timeout 120)
        $unfinished=@($jobs|Where-Object State -ne 'Completed')
        if($unfinished.Count){throw "$Direction stage $Label did not complete within 120 seconds."}
        $jobRows=@($jobs|Receive-Job)
    }finally{
        foreach($job in $jobs){
            if($job.State -eq 'Running'){Stop-Job -Job $job -ErrorAction SilentlyContinue}
            Remove-Job -Job $job -Force -ErrorAction SilentlyContinue
        }
    }
    $samples=[Collections.Generic.List[object]]::new()
    foreach($jobRow in $jobRows){
        if([int]$jobRow.ExitCode -ne 0){throw "$Direction stage $Label stream $($jobRow.Stream) failed: $(@($jobRow.Raw)-join ' ')"}
        $parsed=ConvertFrom-Rpi5CurlRow $jobRow.Raw $Direction $BytesPerStream ([int]$jobRow.Stream)
        $samples.Add([pscustomobject]@{
            Stream=$parsed.Stream;Bytes=$parsed.Bytes;TotalSeconds=$parsed.TotalSeconds;
            CurlMbps=$parsed.CurlMbps;ConnectSeconds=$parsed.ConnectSeconds;TlsSeconds=$parsed.TlsSeconds;
            PreTransferSeconds=$parsed.PreTransferSeconds;BodySeconds=$parsed.BodySeconds;PayloadMbps=$parsed.PayloadMbps;
            StartedUtc=([datetime]$jobRow.StartedUtc).ToString('o');EndedUtc=([datetime]$jobRow.EndedUtc).ToString('o')
        })
    }
    $afterStats=Get-NetAdapterStatistics -Name $network.Adapter.Name
    $after=Get-Rpi5FreshSnapshot $Root $OutputDirectory ($Label+'-after')
    $starts=@($jobRows|ForEach-Object {[datetime]$_.StartedUtc}|Sort-Object)
    $ends=@($jobRows|ForEach-Object {[datetime]$_.EndedUtc}|Sort-Object)
    $wall=($ends[-1]-$starts[0]).TotalSeconds
    if($wall -le 0){throw "Invalid stage wall time for $Label."}
    $totalBytes=[long]$BytesPerStream*$Streams
    $curlRates=[double[]]@($samples|ForEach-Object CurlMbps)
    $payloadRates=[double[]]@($samples|Where-Object {$null -ne $_.PayloadMbps}|ForEach-Object PayloadMbps)
    $result=[pscustomobject]@{
        Label=$Label;Direction=$Direction;Streams=$Streams;BytesPerStream=$BytesPerStream;TotalPayloadBytes=$totalBytes;
        PlannedStartUtc=$planned.ToString('o');StageStartUtc=$starts[0].ToString('o');StageEndUtc=$ends[-1].ToString('o');
        StageWallSeconds=$wall;AggregateMbps=[double]$totalBytes*8/$wall/1000000;
        SumCurlMbps=($curlRates|Measure-Object -Sum).Sum;MedianStreamCurlMbps=(Get-Rpi5Median $curlRates);
        SumPayloadMbps=if($payloadRates.Count){($payloadRates|Measure-Object -Sum).Sum}else{$null};
        AdapterSentBytesDelta=([int64]$afterStats.SentBytes-[int64]$beforeStats.SentBytes);
        AdapterReceivedBytesDelta=([int64]$afterStats.ReceivedBytes-[int64]$beforeStats.ReceivedBytes);
        Before=(Get-Rpi5CounterSet $before);After=(Get-Rpi5CounterSet $after);
        DriverDeltas=(Get-Rpi5CounterDelta $before $after);Samples=@($samples);
        SnapshotTrigger='read-only radio query before and after stage; driver deltas include small boundary-query control traffic'
    }
    $samples|Export-Csv -LiteralPath (Join-Path $OutputDirectory ($Label+'-samples.csv')) -NoTypeInformation -Encoding UTF8
    $result|ConvertTo-Json -Depth 10|Set-Content -LiteralPath (Join-Path $OutputDirectory ($Label+'-result.json')) -Encoding UTF8
    return $result
}
function Invoke-Rpi5DownloadBenchmark {
    param([string]$Root,[string]$OutputDirectory)
    $stages=[Collections.Generic.List[object]]::new()
    foreach($streams in @(1,2,4)){
        $bytesPerStream=[long](64MB/$streams)
        Write-Information "Sustained download stage: $streams stream(s), total 64 MiB."
        $stages.Add((Invoke-Rpi5TransferStage $Root $OutputDirectory 'Download' $streams $bytesPerStream $null ("download-$($streams)stream")))
    }
    $result=[pscustomobject]@{
        UtilityVersion=$script:ToolVersion;Service='Cloudflare public speed test download endpoint';
        Endpoint='https://speed.cloudflare.com/__down';Stages=@($stages);
        BestAggregateMbps=($stages|Measure-Object AggregateMbps -Maximum).Maximum
    }
    $result|ConvertTo-Json -Depth 12|Set-Content -LiteralPath (Join-Path $OutputDirectory 'sustained-download-result.json') -Encoding UTF8
    return $result
}
function Invoke-Rpi5UploadBenchmark {
    param([string]$Root,[string]$OutputDirectory)
    $payload=Join-Path $OutputDirectory 'upload-payload-16MiB.bin'
    $stream=[IO.File]::Open($payload,[IO.FileMode]::Create,[IO.FileAccess]::Write,[IO.FileShare]::Read)
    try{$stream.SetLength(16MB)}finally{$stream.Dispose()}
    $stages=[Collections.Generic.List[object]]::new()
    try{
        foreach($streams in @(1,2,4)){
            Write-Information "Sustained upload stage: $streams stream(s), 16 MiB per stream."
            $stages.Add((Invoke-Rpi5TransferStage $Root $OutputDirectory 'Upload' $streams 16MB $payload ("upload-$($streams)stream")))
        }
    }finally{Remove-Item -LiteralPath $payload -Force -ErrorAction SilentlyContinue}
    $one=@($stages|Where-Object Streams -eq 1)[0]
    foreach($stage in $stages){
        $stage|Add-Member -NotePropertyName ScalingVsOneStream -NotePropertyValue (
            if($one.AggregateMbps -gt 0){[double]$stage.AggregateMbps/[double]$one.AggregateMbps}else{$null})
    }
    $result=[pscustomobject]@{
        UtilityVersion=$script:ToolVersion;Service='Cloudflare public speed test upload endpoint';
        Endpoint='https://speed.cloudflare.com/__up';Stages=@($stages);
        BestAggregateMbps=($stages|Measure-Object AggregateMbps -Maximum).Maximum;
        FourStreamScalingVsOneStream=(@($stages|Where-Object Streams -eq 4)[0]).ScalingVsOneStream
    }
    $result|ConvertTo-Json -Depth 12|Set-Content -LiteralPath (Join-Path $OutputDirectory 'upload-scaling-result.json') -Encoding UTF8
    return $result
}
function Get-Rpi5DriverSummary {
    $diag=Get-Rpi5DiagSnapshot
    if($null -eq $diag){return 'Driver diagnostics unavailable.'}
    $names=@('DiagVersion','NetworkPhase','NetworkStatus','TxQueueFull','TxBacklogFull','TxBacklogHighWater',
        'TxGlomEnabled','TxGlomChains','TxGlomErrors','WorkerFailureCount','DisconnectCount','Cmd53Timeouts','FifoTransportFailed')
    $parts=[Collections.Generic.List[string]]::new()
    foreach($name in $names){
        $value=if($diag.PSObject.Properties[$name]){$diag.$name}else{'<missing>'}
        $parts.Add("$name=$value")
    }
    return ($parts -join '; ')
}
function Invoke-Rpi5FullTest {
    param([string]$Root)
    $desktop=[Environment]::GetFolderPath('Desktop');if(-not $desktop){$desktop=$env:TEMP}
    $stamp=Get-Date -Format 'yyyyMMdd-HHmmss'
    $work=Join-Path $desktop ("RPI5-WIFI-ALL-IN-ONE-$stamp")
    [void](New-Item -ItemType Directory -Path $work)
    $summary=[Collections.Generic.List[string]]::new()
    try{
        $summary.Add("RPi5 Wi-Fi All-In-One v$script:ToolVersion")
        $summary.Add("StartedUtc=$([datetime]::UtcNow.ToString('o'))")
        $summary.Add('Driver runtime=byte-for-byte v0.7.1.11 source; v0.7.1.14 changes measurement only.')
        $summary.Add('Measurement service=Cloudflare public speed-test endpoints; public test IP is visible to Cloudflare.')

        $connectArgs=@('-IfNeeded')
        if($ConfigPath){$connectArgs+=@('-ConfigPath',[IO.Path]::GetFullPath($ConfigPath))}
        Invoke-Rpi5Tool $Root 'Connect-RPi5-WiFi.ps1' $connectArgs (Join-Path $work 'connect.txt')
        $summary.Add('Connection=Completed')

        $download=Invoke-Rpi5DownloadBenchmark $Root $work
        $summary.Add(('DownloadBestAggregateMbps={0:N2}' -f $download.BestAggregateMbps))
        foreach($stage in $download.Stages){
            $summary.Add(('Download{0}StreamMbps={1:N2};QueueFullDelta={2};GlomChainsDelta={3};CreditWaitsDelta={4};F2WritesDelta={5}' -f
                $stage.Streams,$stage.AggregateMbps,$stage.DriverDeltas.TxQueueFull,$stage.DriverDeltas.TxGlomChains,
                $stage.DriverDeltas.TxCreditWaits,$stage.DriverDeltas.Cmd53WriteCount))
        }

        if($script:SkipUploadRequested){$summary.Add('UploadTest=Skipped by user')}
        else{
            $upload=Invoke-Rpi5UploadBenchmark $Root $work
            $summary.Add(('UploadBestAggregateMbps={0:N2}' -f $upload.BestAggregateMbps))
            $summary.Add(('UploadFourStreamScalingVsOne={0:N2}' -f $upload.FourStreamScalingVsOneStream))
            foreach($stage in $upload.Stages){
                $summary.Add(('Upload{0}StreamMbps={1:N2};QueueFullDelta={2};BacklogFullDelta={3};GlomChainsDelta={4};CreditWaitsDelta={5};TxStatusChecksDelta={6};F2WritesDelta={7}' -f
                    $stage.Streams,$stage.AggregateMbps,$stage.DriverDeltas.TxQueueFull,$stage.DriverDeltas.TxBacklogFull,
                    $stage.DriverDeltas.TxGlomChains,$stage.DriverDeltas.TxCreditWaits,
                    $stage.DriverDeltas.TransportTxStatusChecks,$stage.DriverDeltas.Cmd53WriteCount))
            }
        }

        Invoke-Rpi5Tool $Root 'Collect-RPi5-WiFi-Diagnostics.ps1' @('-NoPause','-OutputDirectory',$work) (Join-Path $work 'diagnostics-console.txt')
        $summary.Add('Diagnostics=Completed')
        $summary.Add('FinalDriver='+(Get-Rpi5DriverSummary))
        $summary.Add("FinishedUtc=$([datetime]::UtcNow.ToString('o'))")
        $summary|Set-Content -LiteralPath (Join-Path $work 'ALL-IN-ONE-SUMMARY.txt') -Encoding UTF8
        $zip="$work.zip"
        Compress-Archive -Path (Join-Path $work '*') -DestinationPath $zip -CompressionLevel Optimal
        Remove-Item -LiteralPath $work -Recurse -Force
        Write-Information ''
        Write-Information "DONE. Share only this ZIP: $zip"
        Write-Information 'It contains local network/device diagnostics but no saved Wi-Fi password.'
    }catch{
        $summary.Add("FAILED=$($_.Exception.Message)")
        $summary|Set-Content -LiteralPath (Join-Path $work 'ALL-IN-ONE-SUMMARY.txt') -Encoding UTF8 -ErrorAction SilentlyContinue
        throw
    }
}
function Show-Rpi5Menu {
    Write-Information ''
    Write-Information "RPi5 Wi-Fi All-In-One v$script:ToolVersion"
    Write-Information '1. Connect / reconnect Wi-Fi'
    Write-Information '2. Show connection status'
    Write-Information '3. Connect + sustained 1/2/4-stream download/upload + diagnostics  [RECOMMENDED]'
    Write-Information '4. Collect diagnostics only'
    Write-Information '5. Exit'
    switch(Read-Host 'Choose 1-5'){
        '1'{return 'Connect'}
        '2'{return 'Status'}
        '3'{return 'FullTest'}
        '4'{return 'Diagnostics'}
        default{return 'Exit'}
    }
}

if(-not (Test-Rpi5Administrator)){Invoke-Rpi5SelfElevated;return}
if([Runtime.InteropServices.RuntimeInformation]::OSArchitecture -ne [Runtime.InteropServices.Architecture]::Arm64){
    throw 'This utility is only for Windows ARM64 on Raspberry Pi 5.'
}
$root=$null
try{
    $root=Expand-Rpi5EmbeddedPayload
    $selected=$Mode
    if($selected -eq 'Menu'){$selected=Show-Rpi5Menu}
    switch($selected){
        'Connect'{
            $connectOnly=@();if($ConfigPath){$connectOnly+=@('-ConfigPath',[IO.Path]::GetFullPath($ConfigPath))}
            Invoke-Rpi5Tool $root 'Connect-RPi5-WiFi.ps1' $connectOnly
        }
        'Status'{Invoke-Rpi5Tool $root 'Connect-RPi5-WiFi.ps1' @('-StatusOnly')}
        'FullTest'{Invoke-Rpi5FullTest $root}
        'Diagnostics'{
            $desktop=[Environment]::GetFolderPath('Desktop');if(-not $desktop){$desktop=$env:TEMP}
            Invoke-Rpi5Tool $root 'Collect-RPi5-WiFi-Diagnostics.ps1' @('-NoPause','-OutputDirectory',$desktop)
        }
        default{}
    }
}finally{
    if($root -and (Test-Path -LiteralPath $root)){Remove-Item -LiteralPath $root -Recurse -Force -ErrorAction SilentlyContinue}
    if(-not $NoPause -and $Mode -ne 'Menu'){[void](Read-Host 'Press Enter to close')}
}
