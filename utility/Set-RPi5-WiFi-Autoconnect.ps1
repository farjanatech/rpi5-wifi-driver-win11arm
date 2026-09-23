[CmdletBinding()]
param([switch]$Disable, [switch]$LibraryOnly, [switch]$RefreshExisting)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'RPi5-WiFi-Operations.ps1')

function Set-Rpi5PrivateDirectory {
    [CmdletBinding(SupportsShouldProcess)]
    param([string]$Path)
    $acl = [Security.AccessControl.DirectorySecurity]::new()
    $acl.SetAccessRuleProtection($true, $false)
    foreach ($sid in @('S-1-5-18', 'S-1-5-32-544')) {
        $identity = [Security.Principal.SecurityIdentifier]::new($sid)
        $rule = [Security.AccessControl.FileSystemAccessRule]::new($identity,
            'FullControl', 'ContainerInherit, ObjectInherit', 'None', 'Allow')
        $acl.AddAccessRule($rule)
    }
    $acl.SetOwner([Security.Principal.SecurityIdentifier]::new('S-1-5-32-544'))
    if ($PSCmdlet.ShouldProcess($Path, 'Restrict Wi-Fi configuration access')) {
        Set-Acl -LiteralPath $Path -AclObject $acl
    } else { throw 'Private directory permissions were not applied.' }
}
if ($LibraryOnly) { return }
$principal = [Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()
if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    $arguments = @('-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', ('"{0}"' -f $PSCommandPath))
    if ($Disable) { $arguments += '-Disable' }
    if ($RefreshExisting) { $arguments += '-RefreshExisting' }
    $child = Start-Process -FilePath (Join-Path $PSHOME 'powershell.exe') -Verb RunAs -ArgumentList $arguments -WindowStyle Hidden -Wait -PassThru
    if ($child.ExitCode -ne 0) { throw 'Autoconnect setup failed. Run this script in Administrator PowerShell to see the reason.' }
    Write-Output 'Autoconnect setting updated. See AUTO-CONNECT.md for the editable configuration location.'
    return
}
# Never install this SYSTEM task on the development PC.
if ([Runtime.InteropServices.RuntimeInformation]::OSArchitecture -ne [Runtime.InteropServices.Architecture]::Arm64 -or
    -not @(Get-CimInstance Win32_PnPEntity | Where-Object { $_.PNPDeviceID -match '^ACPI\\RPI0011(?:\\|$)' }).Count) {
    throw 'Run only on the Raspberry Pi 5 with the direct-SDIO device.'
}
$taskName = 'RPi5WiFi-AutoConnect'
$setupLease=$null
try {
$setupLease=Enter-Rpi5Operation
if ($Disable) {
    if (Get-ScheduledTask -TaskName $taskName -TaskPath '\' -ErrorAction SilentlyContinue) {
        Stop-ScheduledTask -TaskName $taskName -TaskPath '\' -ErrorAction SilentlyContinue
        Unregister-ScheduledTask -TaskName $taskName -TaskPath '\' -Confirm:$false
    }
    Write-Output 'Startup task removed. Wi-Fi driver and private configuration were kept.'
    return
}
. (Join-Path $PSScriptRoot 'RPi5-WiFi-Operations.ps1')
$destination = Join-Path $env:ProgramData 'RPi5WiFi'
$existingTask = Get-ScheduledTask -TaskName $taskName -TaskPath '\' -ErrorAction SilentlyContinue
if ($RefreshExisting -and -not $existingTask) {
    Write-Output 'Autoconnect was not enabled; no startup task or profile was created.'
    return
}
if ($RefreshExisting) {
    if (-not (Test-Rpi5OwnedStartupTask $existingTask $destination (Join-Path $PSHOME 'powershell.exe'))) {
        throw 'Existing startup task is not the expected RPi5 Wi-Fi task. It was not changed.'
    }
    Test-Rpi5ProtectedDirectory $destination
}
. (Join-Path $PSScriptRoot 'Connect-RPi5-WiFi.ps1') -LibraryOnly
if (-not $RefreshExisting) {
    $sourceConfig = Join-Path $PSScriptRoot 'WiFi.private.json'
    $wifiProfile = Read-Rpi5WifiConfig $sourceConfig
    $wifiProfile.Password = $null; $wifiProfile = $null
}
if (Test-Path -LiteralPath $destination) {
    # Reject junctions and unexpected contents before a SYSTEM task trusts this directory.
    $existing = Get-Item -LiteralPath $destination -Force
    if (-not $existing.PSIsContainer -or ($existing.Attributes -band [IO.FileAttributes]::ReparsePoint)) { throw 'Unsafe autoconnect directory.' }
    foreach ($entry in Get-ChildItem -LiteralPath $destination -Force) {
        if ($entry.PSIsContainer -or ($entry.Attributes -band [IO.FileAttributes]::ReparsePoint) -or
            $entry.Name -notin @('Connect-RPi5-WiFi.ps1', 'RPi5-WiFi-Operations.ps1', 'WiFi.private.json',
                'Startup-receipt.json', 'Startup-receipt.pending.json')) { throw 'Unexpected contents in autoconnect directory.' }
    }
} else { [void](New-Item -ItemType Directory -Path $destination) }
Set-Rpi5PrivateDirectory $destination
if ($existingTask) { Stop-ScheduledTask -TaskName $taskName -TaskPath '\' -ErrorAction SilentlyContinue }
$copyNames=@('RPi5-WiFi-Operations.ps1','Connect-RPi5-WiFi.ps1')
if (-not $RefreshExisting) { $copyNames += 'WiFi.private.json' }
foreach ($name in $copyNames) {
    $target = Join-Path $destination $name
    Copy-Item -LiteralPath (Join-Path $PSScriptRoot $name) -Destination $target -Force
    # Remove inherited or explicit permissions copied from an earlier installation.
    $acl = [Security.AccessControl.FileSecurity]::new()
    $acl.SetAccessRuleProtection($true, $false)
    foreach ($sid in @('S-1-5-18', 'S-1-5-32-544')) {
        $acl.AddAccessRule([Security.AccessControl.FileSystemAccessRule]::new(
            [Security.Principal.SecurityIdentifier]::new($sid), 'FullControl', 'Allow'))
    }
    $acl.SetOwner([Security.Principal.SecurityIdentifier]::new('S-1-5-32-544'))
    Set-Acl -LiteralPath $target -AclObject $acl
}
if ($RefreshExisting) {
    Write-Output 'Existing startup connector refreshed. Private profile, startup receipt and task settings were preserved.'
    return
}
$scriptPath = Join-Path $destination 'Connect-RPi5-WiFi.ps1'
$configPath = Join-Path $destination 'WiFi.private.json'
$arguments = '-NoProfile -NonInteractive -ExecutionPolicy Bypass -File "{0}" -Startup -ConfigPath "{1}"' -f $scriptPath, $configPath
$action = New-ScheduledTaskAction -Execute (Join-Path $PSHOME 'powershell.exe') -Argument $arguments -WorkingDirectory $destination
$trigger = New-ScheduledTaskTrigger -AtStartup
$settings = New-ScheduledTaskSettingsSet -StartWhenAvailable -MultipleInstances IgnoreNew -ExecutionTimeLimit (New-TimeSpan -Minutes 40) -AllowStartIfOnBatteries -DontStopIfGoingOnBatteries
$taskPrincipal = New-ScheduledTaskPrincipal -UserId 'SYSTEM' -LogonType ServiceAccount -RunLevel Highest
[void](Register-ScheduledTask -TaskName $taskName -TaskPath '\' -Action $action -Trigger $trigger -Settings $settings -Principal $taskPrincipal -Force)
Write-Output "Enabled at Windows startup. Edit as Administrator: $configPath"
Write-Output 'Configuration contains a plaintext password, restricted to SYSTEM and Administrators. Keep it private.'
Write-Output 'Task runs once per boot; it does not repeatedly reset a failing connection. Reboot or start the task manually to connect.'
} finally { Exit-Rpi5Operation $setupLease }
