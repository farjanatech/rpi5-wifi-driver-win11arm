Set-StrictMode -Version Latest
$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot '../installer/Install-RPi5-WiFi-Driver.ps1') -LibraryOnly
$script:pnp=@()
$script:cim=@()
function Get-PnpDevice { param([switch]$PresentOnly,[string]$ErrorAction)
    if(-not $PresentOnly){throw 'A present-only inventory is required.'}
    return $script:pnp
}
function Get-CimInstance { param([string]$ClassName,[string]$ErrorAction)
    if($ClassName -ne 'Win32_PnPEntity'){throw 'Unexpected inventory.'}
    return $script:cim
}
function Node([string]$Id,[bool]$Present,[string]$Service) {
    return [pscustomobject]@{PNPDeviceID=$Id;InstanceId=$Id;Present=$Present;Service=$Service;Status='OK'}
}
$script:cim=@((Node 'ACPI\RPI0011\0' $true 'Pi5Rp1Irq'))
if(Get-Rpi5TargetDevice){throw 'Damian IRQ was accepted as Wi-Fi.'}
Assert-Rpi5NoLegacyCollision
$script:cim+=Node 'ACPI\RPI1060\0' $false 'rpi5cyw'
if(Get-Rpi5TargetDevice){throw 'Phantom Wi-Fi node was accepted.'}
$script:cim=@((Node 'ACPI\RPI1060\0' $true ''))
if(-not (Get-Rpi5TargetDevice)){throw 'Unbound present Wi-Fi target was rejected.'}
$script:pnp=@((Node 'ACPI\RPI10600\0' $true ''))
$script:cim=@()
if(Get-Rpi5TargetDevice){throw 'ID prefix collision was accepted.'}
$script:pnp=@((Node 'ACPI\RPI1060\0' $true 'rpi5cyw'))
if(-not (Get-Rpi5TargetDevice)){throw 'Present Wi-Fi target was rejected.'}
$script:cim=@((Node 'ACPI\RPI0011\0' $true 'rpi5cyw'))
$blocked=$false
try { Assert-Rpi5NoLegacyCollision } catch { $blocked=$true }
if(-not $blocked){throw 'Legacy binding to Damian IRQ was not blocked.'}
Write-Output 'PASS: installer rejects IRQ, stale/prefix targets and legacy driver collisions.'
