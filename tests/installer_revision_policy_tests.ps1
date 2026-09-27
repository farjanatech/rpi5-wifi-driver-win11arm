Set-StrictMode -Version Latest
$ErrorActionPreference='Stop'
$root=Split-Path -Parent $PSScriptRoot
$baseline='445212a69ff805d3501de1adda91baf90a5df1c1'
$allowed=@('installer/Install-RPi5-WiFi-Driver.ps1','tests/installer_utility_tests.ps1',
    'tests/installer_revision_policy_tests.ps1','scripts/package-compatibility.ps1',
    '.github/workflows/compatibility-package.yml','README.md','docs/INSTALLER-UEFI-REVISION.md')
$changed=@(& git -C $root diff --name-only $baseline HEAD)
if($LASTEXITCODE -ne 0){throw 'Cannot compare installer-only scope.'}
foreach($path in $changed){if($path -notin $allowed){throw "Unexpected change: $path"}}
# Kernel, firmware pins, INF, connector, utilities, boot and security-related
# implementation are not rebuilt or modified by this installer-only package.
& git -C $root diff --exit-code $baseline HEAD -- src package connector utility diagnostics scripts/fetch-firmware.ps1 rpi5-cyw43455.vcxproj
if($LASTEXITCODE -ne 0){throw 'Protected driver, firmware or connection source changed.'}
$scriptPath=Join-Path $root 'installer/Install-RPi5-WiFi-Driver.ps1'
$source=(Get-Content -LiteralPath $scriptPath -Raw).Replace("`r`n","`n")
$original=(@(& git -C $root show ($baseline+':installer/Install-RPi5-WiFi-Driver.ps1')) -join "`n")
if($LASTEXITCODE -ne 0){throw 'Cannot inspect baseline installer.'}
$tokens=$null;$parseErrors=$null
$oldAst=[Management.Automation.Language.Parser]::ParseInput($original,[ref]$tokens,[ref]$parseErrors)
if($parseErrors.Count){throw 'Baseline parse failed.'}
$newAst=[Management.Automation.Language.Parser]::ParseInput($source,[ref]$tokens,[ref]$parseErrors)
if($parseErrors.Count){throw 'Updated installer parse failed.'}
$oldGate=@'
        $bios = Get-CimInstance Win32_BIOS
        $biosText = "$($bios.SMBIOSBIOSVersion) $($bios.BIOSVersion -join ' ')"
        $compatibleRevision = Get-Rpi5CompatibleUefiRevision -BiosText $biosText
        if (-not $compatibleRevision) {
            throw 'Supported direct-SDIO UEFI was not detected. Requires exp.0.3 (bda4c47) or exp.0.5 (838d87d). Unknown revisions are not accepted; do not bypass this check.'
        }
        Write-InstallMessage "Supported direct-SDIO UEFI revision $compatibleRevision detected."
'@
$oldGate=$oldGate.Replace("`r`n","`n")
$newFunctions=@($newAst.FindAll({param($node) $node -is [Management.Automation.Language.FunctionDefinitionAst]},$true))
foreach($function in $oldAst.FindAll({param($node) $node -is [Management.Automation.Language.FunctionDefinitionAst]},$true)) {
    if($function.Name -in @('Get-Rpi5CompatibleUefiRevision','Get-Rpi5TargetDevice')){continue}
    $expected=$function.Extent.Text
    if($function.Name -eq 'Invoke-Rpi5DriverInstall') {
        if(-not $expected.Contains($oldGate)){throw 'Baseline UEFI block changed unexpectedly.'}
        $expected=$expected.Replace($oldGate,'        Write-InstallMessage (Get-Rpi5UefiNotice)')
    }
    $actual=@($newFunctions | Where-Object Name -eq $function.Name)
    if($actual.Count -ne 1 -or $actual[0].Extent.Text -cne $expected){throw "Protected installer behavior changed: $($function.Name)"}
}
. $scriptPath -LibraryOnly

# All inventory is mocked. Never call hardware enumeration or installation.
$script:FakeBios=$null;$script:FailBios=$false;$script:BiosCalls=0
$script:FakePnp=@();$script:FakeEntity=@();$script:FakeSigned=@()
function Get-PnpDevice {
    [Diagnostics.CodeAnalysis.SuppressMessageAttribute('PSAvoidOverwritingBuiltInCmdlets','',Justification='Test-only inventory mock prevents runner hardware access and is removed in finally.')]
    [CmdletBinding()]
    param([switch]$PresentOnly)
    if($PresentOnly){throw 'Unexpected change to enumeration policy.'}
    return $script:FakePnp
}
function Get-CimInstance {
    [Diagnostics.CodeAnalysis.SuppressMessageAttribute('PSAvoidOverwritingBuiltInCmdlets','',Justification='Test-only inventory mock prevents runner hardware access and is removed in finally.')]
    [CmdletBinding()]
    param([string]$ClassName)
    switch($ClassName) {
        'Win32_BIOS' {
            $script:BiosCalls++
            if($script:FailBios){throw 'Synthetic unavailable BIOS inventory.'}
            return $script:FakeBios
        }
        'Win32_PnPEntity' {return $script:FakeEntity}
        'Win32_PnPSignedDriver' {return $script:FakeSigned}
        default {throw "Unexpected hardware access: $ClassName"}
    }
}
try {
    foreach($revision in @('bda4c47','838d87d','6023be0','custom-future-UEFI-2030',
        'unknown','',"custom`r`nrevision",('x'*600))) {
        $script:FakeBios=[pscustomobject]@{SMBIOSBIOSVersion=$revision;BIOSVersion=@($revision)}
        $notice=Get-Rpi5UefiNotice
        if($notice -notmatch '^WARNING: UEFI revision:' -or $notice -notmatch 'informational only' -or
            $notice -notmatch 'compatibility is not guaranteed' -or $notice -match '[\r\n]') {
            throw 'Firmware notice must not claim compatibility or inject log lines.'
        }
    }
    foreach($bios in @($null,[pscustomobject]@{},[pscustomobject]@{BIOSVersion=$null})) {
        $script:FakeBios=$bios
        if((Get-Rpi5UefiNotice) -notmatch 'unavailable'){throw 'Missing BIOS metadata must be nonblocking.'}
    }
    $script:FailBios=$true
    if((Get-Rpi5UefiNotice) -notmatch 'unavailable'){throw 'BIOS query failure must be nonblocking.'}
    $before=$script:BiosCalls
    foreach($id in @('ACPI\RPI0011\1','ACPI\RPI00110\1','PCI\RPI0011\1','ACPI\RPI0001\1','XACPI\RPI0011\1')) {
        for($method=0;$method -lt 3;$method++) {
            $script:FakePnp=@();$script:FakeEntity=@();$script:FakeSigned=@()
            switch($method) {
                0 {$script:FakePnp=@([pscustomobject]@{InstanceId=$id;Status='OK'})}
                1 {$script:FakeEntity=@([pscustomobject]@{PNPDeviceID=$id;Status='OK'})}
                2 {$script:FakeSigned=@([pscustomobject]@{DeviceID=$id})}
            }
            $device=Get-Rpi5TargetDevice
            if($id -eq 'ACPI\RPI0011\1') {
                if($null -eq $device -or $device.InstanceId -ne $id){throw "Exact target rejected via method $method"}
            } elseif($null -ne $device){throw "Wrong target accepted: $id"}
        }
    }
    $script:FakePnp=@();$script:FakeEntity=@();$script:FakeSigned=@()
    if($null -ne (Get-Rpi5TargetDevice)){throw 'Missing target must remain blocked.'}
    if($script:BiosCalls -ne $before){throw 'Device lookup must not depend on BIOS revision or availability.'}
} finally {
    Remove-Item Function:\Get-PnpDevice,Function:\Get-CimInstance
}
Write-Output 'PASS: arbitrary/missing/erroring BIOS is informational; exact ACPI target required in all three paths; main install/security behavior otherwise identical to alpha.3-US.'
