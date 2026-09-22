[CmdletBinding()]
param([switch]$LibraryOnly)
Set-StrictMode -Version Latest
$ErrorActionPreference='Stop'
function ConvertFrom-Rpi5RadioReport {
    param([byte[]]$Data)
    if ($Data.Length -ne 80 -or [BitConverter]::ToUInt32($Data,0) -ne 1) { throw 'Unsupported radio report; exp0.6.17 or newer is required.' }
    $mask=[BitConverter]::ToUInt32($Data,8)
    $result=[ordered]@{CapturedUtc=[datetime]::UtcNow.ToString('o'); Generation=[BitConverter]::ToUInt32($Data,4);
        ValidMask=$mask; Status=('0x{0:X8}' -f [BitConverter]::ToUInt32($Data,12));
        Band='Unknown';Channel='Unknown';SignalDbm='Unknown';PowerSave='Unknown';MinimumPowerConsumption='Unknown'}
    if ($mask -band 1) {
        $band=[BitConverter]::ToUInt32($Data,44)
        if ($band -in @(2400,5000)) { $result.Band=if($band -eq 2400){'2.4 GHz'}else{'5 GHz'} }
        $result.Channel=[BitConverter]::ToUInt32($Data,32)
    }
    if ($mask -band 2) { $result.SignalDbm=[BitConverter]::ToInt32($Data,48) }
    if ($mask -band 4) { $result.PowerSave=@('Off','PM1','PM2')[[BitConverter]::ToUInt32($Data,52)] }
    if ($mask -band 8) { $result.MinimumPowerConsumption=if([BitConverter]::ToUInt32($Data,56) -eq 0){'Off'}else{'On'} }
    foreach($i in 0..3) {
        $name=@('Channel','RSSI','PM','MPC')[$i]
        $result[($name+'Status')]='0x{0:X8}' -f [BitConverter]::ToUInt32($Data,16+4*$i)
        $result[($name+'FirmwareError')]=[BitConverter]::ToInt32($Data,60+4*$i)
    }
    $result.HardwareChannel=[BitConverter]::ToUInt32($Data,32)
    $result.TargetChannel=[BitConverter]::ToUInt32($Data,36)
    $result.ScanChannel=[BitConverter]::ToUInt32($Data,40)
    [pscustomobject]$result
}
if ($LibraryOnly) { return }
$identity=[Security.Principal.WindowsIdentity]::GetCurrent()
if (-not ([Security.Principal.WindowsPrincipal]$identity).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    Start-Process -FilePath (Join-Path $PSHOME 'powershell.exe') -Verb RunAs -ArgumentList @('-NoProfile','-NoExit','-ExecutionPolicy','Bypass','-File',('"{0}"' -f $PSCommandPath))
    return
}
if ($env:PROCESSOR_ARCHITECTURE -ne 'ARM64') { throw 'Run this only on the Raspberry Pi 5, after Wi-Fi connects.' }
Add-Type -TypeDefinition @'
using System;
using System.ComponentModel;
using System.Runtime.InteropServices;
using Microsoft.Win32.SafeHandles;
[assembly: DefaultDllImportSearchPaths(DllImportSearchPath.System32)]
public static class Rpi5RadioControl {
    [DllImport("kernel32.dll",CharSet=CharSet.Unicode,SetLastError=true)]
    static extern SafeFileHandle CreateFile(string n,uint a,uint s,IntPtr p,uint c,uint f,IntPtr t);
    [DllImport("kernel32.dll",SetLastError=true)]
    static extern bool DeviceIoControl(SafeFileHandle h,uint c,IntPtr i,int il,byte[] o,int ol,out int count,IntPtr p);
    public static byte[] Call(bool refresh) {
        using(var h=CreateFile(@"\\.\Rpi5CywControl",refresh?0x40000000u:0x80000000u,3,IntPtr.Zero,3,0,IntPtr.Zero)) {
            if(h.IsInvalid)throw new Win32Exception(Marshal.GetLastWin32Error());
            byte[] b=new byte[80];int count;
            if(!DeviceIoControl(h,refresh?0x12A00Cu:0x126010u,IntPtr.Zero,0,b,b.Length,out count,IntPtr.Zero))
                throw new Win32Exception(Marshal.GetLastWin32Error());
            Array.Resize(ref b,count);return b;
        }
    }
}
'@
$previous=[Rpi5RadioControl]::Call($false)
if($previous.Length -ne 80){throw 'Driver has no radio-report support.'}
$generation=[BitConverter]::ToUInt32($previous,4)
[void][Rpi5RadioControl]::Call($true)
$timer=[Diagnostics.Stopwatch]::StartNew()
do {
    Start-Sleep -Milliseconds 250
    $data=[Rpi5RadioControl]::Call($false)
    if($data.Length -eq 80 -and [BitConverter]::ToUInt32($data,4) -ne $generation) {
        ConvertFrom-Rpi5RadioReport $data | Format-List | Out-String -Width 200 | Write-Output
        Write-Output 'Read-only point-in-time snapshot; unknown values are not guesses. No band, country or power setting was changed.'
        return
    }
} while($timer.Elapsed.TotalSeconds -lt 25)
throw 'Radio query did not complete in 25 seconds. No retry or settings change was performed.'
