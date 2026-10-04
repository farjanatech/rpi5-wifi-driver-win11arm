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
$script:ToolVersion='0.7.1.13'
$script:SkipUploadRequested=[bool]$SkipUpload
$script:PayloadBase64='__PAYLOAD_BASE64__'

function Test-Rpi5Administrator {
    $p=[Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()
    return $p.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
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
    } else {
        & (Join-Path $PSHOME 'powershell.exe') @all
        $code=$LASTEXITCODE
    }
    if($code -ne 0){throw "$Name failed with exit code $code."}
}
function Get-Rpi5DiagSnapshot {
    try { return Get-ItemProperty -LiteralPath 'HKLM:\SOFTWARE\Rpi5CywDirectDiag' -ErrorAction Stop }
    catch { return $null }
}
function Get-Rpi5PropertyInt64 {
    param($Object,[string]$Name)
    if($null -eq $Object -or -not $Object.PSObject.Properties[$Name]){return $null}
    try{return [int64]$Object.$Name}catch{return $null}
}
function Get-Rpi5Delta {
    param($Before,$After,[string]$Name)
    $a=Get-Rpi5PropertyInt64 $Before $Name;$b=Get-Rpi5PropertyInt64 $After $Name
    if($null -eq $a -or $null -eq $b -or $b -lt $a){return $null}
    return $b-$a
}
function Get-Rpi5Median {
    param([double[]]$Values)
    if(-not $Values -or !$Values.Count){return $null}
    $s=@($Values|Sort-Object);$m=[int]($s.Count/2)
    if($s.Count%2){return [double]$s[$m]}
    return ([double]$s[$m-1]+[double]$s[$m])/2
}
function Invoke-Rpi5UploadBenchmark {
    param([string]$OutputDirectory)
    $adapters=@(Get-NetAdapter -ErrorAction Stop|Where-Object InterfaceDescription -like '*CYW43455*'|Where-Object Status -eq 'Up')
    if($adapters.Count -ne 1){throw 'Expected exactly one running CYW43455 adapter.'}
    $adapter=$adapters[0]
    $ips=@(Get-NetIPAddress -InterfaceIndex $adapter.ifIndex -AddressFamily IPv4 -ErrorAction Stop|Where-Object {$_.IPAddress -notlike '169.254.*'})
    if($ips.Count -lt 1){throw 'CYW43455 has no usable IPv4 address.'}
    $ip=[string]$ips[0].IPAddress
    $otherDefaults=@(Get-NetRoute -PolicyStore ActiveStore -AddressFamily IPv4 -DestinationPrefix '0.0.0.0/0' -ErrorAction Stop|Where-Object {$_.InterfaceIndex -ne $adapter.ifIndex -and $_.NextHop -ne '0.0.0.0'})
    if($otherDefaults.Count){throw 'Another IPv4 default route is active. Disconnect Ethernet/VPN before testing.'}

    $curl=(Get-Command curl.exe -ErrorAction Stop).Source
    $payload=Join-Path $OutputDirectory 'upload-payload-4MiB.bin'
    $stream=[IO.File]::Open($payload,[IO.FileMode]::Create,[IO.FileAccess]::Write,[IO.FileShare]::None)
    try{$stream.SetLength(4MB)}finally{$stream.Dispose()}
    $beforeStats=Get-NetAdapterStatistics -Name $adapter.Name
    $beforeDiag=Get-Rpi5DiagSnapshot
    $samples=[Collections.Generic.List[object]]::new()
    try {
        1..4|ForEach-Object{
            $arguments=@('--silent','--show-error','--fail','--max-time','45','--interface',$ip,
                '--request','POST','--header','Content-Type: application/octet-stream',
                '--data-binary',("@"+$payload),'--output','NUL',
                '--write-out','%{http_code}|%{time_total}|%{speed_upload}',
                'https://speed.cloudflare.com/__up')
            $watch=[Diagnostics.Stopwatch]::StartNew()
            $raw=& $curl @arguments 2>&1
            $code=$LASTEXITCODE;$watch.Stop()
            if($code -ne 0){throw "Upload request $_ failed: $raw"}
            $parts=([string]$raw).Trim().Split('|')
            if($parts.Count -ne 3 -or $parts[0] -ne '200'){throw "Unexpected upload response: $raw"}
            $seconds=0.0;$bytesPerSecond=0.0
            if(-not [double]::TryParse($parts[1],[Globalization.NumberStyles]::Float,[Globalization.CultureInfo]::InvariantCulture,[ref]$seconds) -or
               -not [double]::TryParse($parts[2],[Globalization.NumberStyles]::Float,[Globalization.CultureInfo]::InvariantCulture,[ref]$bytesPerSecond) -or
               $seconds -le 0 -or $bytesPerSecond -le 0){throw "Invalid upload timing: $raw"}
            $mbps=$bytesPerSecond*8/1000000
            $samples.Add([pscustomobject]@{Sample=$_;Bytes=4MB;Seconds=$seconds;Mbps=$mbps;HttpStatus=200;WallSeconds=$watch.Elapsed.TotalSeconds})
        }
    } finally {Remove-Item -LiteralPath $payload -Force -ErrorAction SilentlyContinue}
    $afterStats=Get-NetAdapterStatistics -Name $adapter.Name
    $afterDiag=Get-Rpi5DiagSnapshot
    $rates=[double[]]@($samples|ForEach-Object Mbps)
    $counterNames=@('TxPackets','TxQueueFull','TxBacklogFull','TxBacklogAccepted','TxBacklogPromoted',
        'TxGlomChains','TxGlomFrames','TxGlomErrors','TxCreditWaits','Cmd53Timeouts','WorkerFailureCount','DisconnectCount')
    $deltas=[ordered]@{}
    foreach($name in $counterNames){$deltas[$name]=Get-Rpi5Delta $beforeDiag $afterDiag $name}
    $result=[pscustomobject]@{
        UtilityVersion=$script:ToolVersion
        Service='Cloudflare public speed test upload endpoint'
        Endpoint='https://speed.cloudflare.com/__up'
        InterfaceAlias=$adapter.Name
        InterfaceIndex=$adapter.ifIndex
        IPv4=$ip
        Samples=$samples
        MedianMbps=(Get-Rpi5Median $rates)
        AverageMbps=($rates|Measure-Object -Average).Average
        MinimumMbps=($rates|Measure-Object -Minimum).Minimum
        MaximumMbps=($rates|Measure-Object -Maximum).Maximum
        AdapterSentBytesDelta=([int64]$afterStats.SentBytes-[int64]$beforeStats.SentBytes)
        DriverDeltas=[pscustomobject]$deltas
    }
    $samples|Export-Csv -LiteralPath (Join-Path $OutputDirectory 'upload-samples.csv') -NoTypeInformation -Encoding UTF8
    $result|ConvertTo-Json -Depth 8|Set-Content -LiteralPath (Join-Path $OutputDirectory 'upload-result.json') -Encoding UTF8
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
        $summary.Add('Driver baseline=v0.7.1.11 runtime; no new TX tuning in this package.')
        $summary.Add('Upload service=https://speed.cloudflare.com/__up (synthetic zero-filled data; Cloudflare receives the test IP).')

        $connectArgs=@('-IfNeeded')
        if($ConfigPath){$connectArgs+=@('-ConfigPath',[IO.Path]::GetFullPath($ConfigPath))}
        Invoke-Rpi5Tool $Root 'Connect-RPi5-WiFi.ps1' $connectArgs (Join-Path $work 'connect.txt')
        $summary.Add('Connection=Completed')

        Invoke-Rpi5Tool $Root 'Test-RPi5-WiFi-Performance.ps1' @('-NoPause','-SkipConnect','-SkipDiagnostics','-OutputRoot',$work) (Join-Path $work 'download-test-console.txt')
        $summary.Add('DownloadTest=Completed')

        if($script:SkipUploadRequested){$summary.Add('UploadTest=Skipped by user')}
        else{
            Write-Information ''
            Write-Information 'Running upload measurement: 4 x 4 MiB synthetic payloads to Cloudflare Speed Test.'
            $upload=Invoke-Rpi5UploadBenchmark $work
            $summary.Add(('UploadMedianMbps={0:N2}' -f $upload.MedianMbps))
            $summary.Add(('UploadAverageMbps={0:N2}' -f $upload.AverageMbps))
            $summary.Add(('UploadQueueRejectsDelta={0}' -f $upload.DriverDeltas.TxQueueFull))
            $summary.Add(('UploadBacklogFullDelta={0}' -f $upload.DriverDeltas.TxBacklogFull))
            $summary.Add(('UploadGlomChainsDelta={0}' -f $upload.DriverDeltas.TxGlomChains))
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
    Write-Information '3. Connect + full download/upload test + diagnostics  [RECOMMENDED]'
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
            $a=@();if($ConfigPath){$a+=@('-ConfigPath',[IO.Path]::GetFullPath($ConfigPath))}
            Invoke-Rpi5Tool $root 'Connect-RPi5-WiFi.ps1' $a
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
