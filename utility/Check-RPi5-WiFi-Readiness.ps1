[CmdletBinding()]
param([switch]$LibraryOnly,[switch]$NoPause,[switch]$VerifyReconnect,[string]$ConfigPath)
Set-StrictMode -Version Latest
$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot 'RPi5-WiFi-Operations.ps1')

function Get-Rpi5ReadinessVerdict {
    param($Startup,$Reconnect,$Performance,$After)
    if($null -eq $Performance -or $Performance.Outcome -eq 'Failed' -or
        $null -eq $After -or -not $After.Ready -or $Reconnect.Outcome -eq 'Failed') { return 'Failed' }
    if($Performance.Outcome -eq 'Inconclusive') { return 'Inconclusive' }
    if($Performance.Outcome -ne 'Completed' -or $null -eq $Performance.Summary -or
        $Performance.Summary.Completed -le 0 -or $Performance.Summary.Failed -ne 0) { return 'Failed' }
    $checksComplete=$null -ne $Performance.Checks
    foreach($name in @('Gateway latency/loss (8 requests)','Internet IPv4 latency/loss (8 requests)',
        'Configured DNS server, A query','Download hostname, configured DNS server, A query',
        'HTTPS headers; certificate verification enabled','1 MiB bounded HTTPS download; bytes/sec is application throughput')) {
        if(-not $checksComplete -or -not $Performance.Checks.Contains($name) -or
            -not $Performance.Checks[$name].ProcessCompleted){$checksComplete=$false}
    }
    if($Startup.Outcome -ne 'Passed' -or $Reconnect.Outcome -ne 'Passed' -or
        -not $Performance.DiagnosticsCollected -or $Performance.Summary.Completed -ne 128 -or
        $null -eq $Performance.LoadProbeFailures -or $Performance.LoadProbeFailures -ne 0 -or
        $null -eq $Performance.QueueRejectsDelta -or $Performance.QueueRejectsDelta -ne 0 -or
        -not $checksComplete) { return 'ReadyWithWarnings' }
    return 'Ready'
}
function Test-Rpi5StartupTaskReceipt {
    param($Startup,$Task)
    if($Startup.Outcome -ne 'Passed') { return $Startup }
    try {
        if(-not $Task.Present -or $Task.State -eq 'Running' -or $Task.LastResult -ne 0 -or
            [datetime]::Parse($Task.LastRunUtc).ToUniversalTime() -gt
            [datetime]::Parse($Startup.Receipt.StartedUtc).ToUniversalTime()) { throw 'Stale task evidence.' }
        return $Startup
    } catch { return [pscustomobject]@{Outcome='NotTested';Reason='Startup receipt does not match latest completed successful task run';Receipt=$null} }
}
if($LibraryOnly) { return }
$principal=[Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()
if(-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    $arguments=@('-NoProfile','-ExecutionPolicy','Bypass','-File',('"{0}"' -f $PSCommandPath))
    if($NoPause){$arguments+='-NoPause'}
    if($VerifyReconnect){$arguments+='-VerifyReconnect'}
    if($ConfigPath){
        if($ConfigPath.Contains('"')){throw 'Invalid configuration path.'}
        $arguments+=@('-ConfigPath',('"{0}"' -f [IO.Path]::GetFullPath($ConfigPath)))
    }
    Start-Process -FilePath (Join-Path $PSHOME 'powershell.exe') -Verb RunAs -ArgumentList $arguments
    return
}
$lease=$null; $readiness=$null; $directory=$null
try {
    if([Runtime.InteropServices.RuntimeInformation]::OSArchitecture -ne [Runtime.InteropServices.Architecture]::Arm64 -or
        -not @(Get-CimInstance Win32_PnPEntity | Where-Object PNPDeviceID -like 'ACPI\RPI0011\*').Count) {
        throw 'Run this readiness check on the Raspberry Pi 5, not the development PC.'
    }
    $lease=Enter-Rpi5Operation
    & (Join-Path $PSScriptRoot 'Connect-RPi5-WiFi.ps1') -LibraryOnly
    $startup=Read-Rpi5StartupAssessment
    $initial=$null
    try { $initial=Get-Rpi5ConnectionReadiness } catch { Write-Warning 'Initial live status unavailable; this is not a successful startup observation.' }
    $taskEvidence=[pscustomobject]@{Present=$false;State='Unknown';LastResult=$null;LastRunUtc=$null}
    $task=Get-ScheduledTask -TaskName 'RPi5WiFi-AutoConnect' -TaskPath '\' -ErrorAction SilentlyContinue
    if($task) {
        $taskEvidence.Present=$true; $taskEvidence.State=[string]$task.State
        $taskInfo=Get-ScheduledTaskInfo -TaskName 'RPi5WiFi-AutoConnect' -TaskPath '\' -ErrorAction SilentlyContinue
        if($taskInfo){$taskEvidence.LastResult=$taskInfo.LastTaskResult;$taskEvidence.LastRunUtc=$taskInfo.LastRunTime.ToUniversalTime().ToString('o')}
    }
    $startup=Test-Rpi5StartupTaskReceipt $startup $taskEvidence
    # Resolve only metadata here. Only Connect reads a chosen private profile.
    if(-not $ConfigPath) {
        $packageProfile=Join-Path $PSScriptRoot 'WiFi.private.json'
        if(Test-Path -LiteralPath $packageProfile){$ConfigPath=$packageProfile}
        else {
            $installedDirectory=Join-Path $env:ProgramData 'RPi5WiFi'
            if(Test-Rpi5OwnedStartupTask $task $installedDirectory (Join-Path $PSHOME 'powershell.exe')) {
                try {
                    Test-Rpi5ProtectedDirectory $installedDirectory
                    $installedProfile=Get-Item -LiteralPath (Join-Path $installedDirectory 'WiFi.private.json') -Force
                    if(-not $installedProfile.PSIsContainer -and -not ($installedProfile.Attributes -band [IO.FileAttributes]::ReparsePoint)) {
                        $ConfigPath=$installedProfile.FullName
                    }
                } catch { Write-Warning 'Installed private profile is not available safely; it will not be used.' }
            }
        }
    }
    $reconnect=[pscustomobject]@{Outcome='NotTested';Reason='Use -VerifyReconnect to request one brief disconnect/connect cycle'}
    $connectAttempted=$false
    if($VerifyReconnect) {
        if(-not $ConfigPath) { $reconnect.Reason='No existing private profile; no disconnect was requested' }
        else {
            Write-Output 'Reconnect verification requested: one brief disconnect/connect cycle, no automatic repeat or device reset.'
            $connectAttempted=$true
            try {
                $connection=& (Join-Path $PSScriptRoot 'Connect-RPi5-WiFi.ps1') -ConfigPath $ConfigPath -PassThru | ForEach-Object {
                    if($_ -isnot [string] -and $_.PSObject.Properties['Kind'] -and $_.Kind -eq 'RPi5ConnectionResult') {$_}
                    else { Out-Host -InputObject $_ }
                } | Select-Object -Last 1
                if($null -eq $connection -or -not $connection.Connection.Ready){throw 'Connection not verified.'}
                if($null -ne $initial -and $initial.Authenticated) {
                    $reconnect.Outcome='Passed';$reconnect.Reason='One requested reconnect reached authenticated IPv4/default-route readiness'
                } else {$reconnect.Reason='Connection established, but no initial authenticated link existed to test reconnection'}
            } catch { $reconnect.Outcome='Failed';$reconnect.Reason='The single requested reconnect did not complete; inspect diagnostics' }
        }
    }
    Write-Output 'Running the existing bounded performance test once. Disconnect wired Ethernet/VPN for an exclusive Wi-Fi result.'
    $performance=& (Join-Path $PSScriptRoot 'Test-RPi5-WiFi-Performance.ps1') -NoPause -PassThru -SkipConnect:$connectAttempted -ConfigPath $ConfigPath | ForEach-Object {
        if($_ -isnot [string] -and $_.PSObject.Properties['Kind'] -and $_.Kind -eq 'RPi5PerformanceResult') {$_}
        else { Out-Host -InputObject $_ }
    } | Select-Object -Last 1
    $after=$null
    try {$after=Get-Rpi5ConnectionReadiness} catch { Write-Warning 'Final live status unavailable.' }
    $readiness=[pscustomobject]@{SchemaVersion=1;UtilityVersion='0.6.26';CapturedUtc=[datetime]::UtcNow.ToString('o');
        Outcome=(Get-Rpi5ReadinessVerdict $startup $reconnect $performance $after);
        Startup=$startup;StartupTask=$taskEvidence;InitialConnection=$initial;Reconnect=$reconnect;
        FinalConnection=$after;Performance=$performance;
        Notice='One short test is not sustained reliability proof. Startup and reconnect are separate observations; NotTested is not a pass. No reboot, sleep test, device reset, firmware or permanent network change was performed.'}
    $desktop=[Environment]::GetFolderPath('Desktop');if(-not $desktop){$desktop=$env:TEMP}
    $directory=Join-Path $desktop ('RPI5-WIFI-READINESS-'+(Get-Date -Format yyyyMMdd-HHmmss)+'-'+[guid]::NewGuid().ToString('N').Substring(0,6))
    [void](New-Item -ItemType Directory -Path $directory)
    $readiness | ConvertTo-Json -Depth 9 | Set-Content -LiteralPath (Join-Path $directory 'readiness.json') -Encoding UTF8
    if($null -ne $performance -and $performance.ZipPath -and (Test-Path -LiteralPath $performance.ZipPath)) {
        Copy-Item -LiteralPath $performance.ZipPath -Destination (Join-Path $directory 'performance.zip')
    }
    Compress-Archive -Path (Join-Path $directory '*') -DestinationPath "$directory.zip"
    Write-Output "Readiness: $($readiness.Outcome). Startup: $($startup.Outcome). Reconnect: $($reconnect.Outcome)."
    Write-Output "Share this one report ZIP: $directory.zip"
    Write-Output 'Review local device/network details before sharing. No private profile or password was copied.'
} finally {
    Exit-Rpi5Operation $lease
    if(-not $NoPause){[void](Read-Host 'Press Enter to close')}
}
if($null -eq $readiness -or $readiness.Outcome -in @('Failed','Inconclusive')) { throw 'Readiness is not confirmed; review the saved report.' }
