[CmdletBinding()]
param([switch]$LibraryOnly,[switch]$RadioLibraryOnly,[switch]$AsJson)
Set-StrictMode -Version Latest
$ErrorActionPreference='Stop'
function ConvertFrom-Rpi5RadioReport {
    param([byte[]]$Data)
    if ($null -eq $Data -or $Data.Length -notin @(80,256)) { throw 'Unsupported radio report length.' }
    $version=[BitConverter]::ToUInt32($Data,0)
    if (($Data.Length -eq 80 -and $version -ne 1) -or ($Data.Length -eq 256 -and $version -ne 2)) { throw 'Unsupported radio report version.' }
    $mask=[BitConverter]::ToUInt32($Data,8)
    $result=[ordered]@{CapturedUtc=[datetime]::UtcNow.ToString('o'); Version=$version; Generation=[BitConverter]::ToUInt32($Data,4);
        ValidMask=$mask; Status=('0x{0:X8}' -f [BitConverter]::ToUInt32($Data,12));
        Band='Unknown';Channel='Unknown';SignalDbm='Unknown';PowerSave='Unknown';MinimumPowerConsumption='Unknown'}
    if ($mask -band 1) {
        $band=[BitConverter]::ToUInt32($Data,44)
        if ($band -in @(2400,5000)) { $result.Band=if($band -eq 2400){'2.4 GHz'}else{'5 GHz'} }
        $result.Channel=[BitConverter]::ToUInt32($Data,32)
    }
    if ($mask -band 2) { $result.SignalDbm=[BitConverter]::ToInt32($Data,48) }
    if ($mask -band 4) {
        $pm=[BitConverter]::ToUInt32($Data,52)
        if($pm -gt 2){throw 'Malformed PM field.'}
        $result.PowerSave=@('Off','PM1','PM2')[$pm]
    }
    if ($mask -band 8) {
        $mpc=[BitConverter]::ToUInt32($Data,56)
        if($mpc -gt 1){throw 'Malformed MPC field.'}
        $result.MinimumPowerConsumption=if($mpc -eq 0){'Off'}else{'On'}
    }
    foreach($i in 0..3) {
        $name=@('Channel','RSSI','PM','MPC')[$i]
        $result[($name+'Status')]='0x{0:X8}' -f [BitConverter]::ToUInt32($Data,16+4*$i)
        $result[($name+'FirmwareError')]=[BitConverter]::ToInt32($Data,60+4*$i)
    }
    $result.HardwareChannel=[BitConverter]::ToUInt32($Data,32)
    $result.TargetChannel=[BitConverter]::ToUInt32($Data,36)
    $result.ScanChannel=[BitConverter]::ToUInt32($Data,40)
    $result.ExtendedValidMask=0
    foreach($name in @('CurrentTxRateMbps','Bssid','ChanspecRaw','FirmwareRxGood','FirmwareRxBad','FirmwareTxGood','FirmwareTxBad','FirmwareRxOther',
        'StationVersion','StationLength','StationFlags','WmmNegotiated','AmpduCapable','LastTxRateMbps','LastRxRateMbps',
        'StationTxPackets','StationTxFailures','StationRxUnicast','StationRxMulticast','StationRxDecryptFailures',
        'StationTxRetryCountRaw','StationTxRetryExhausted','StationUserTxSuccess','StationUserTxRetries',
        'StationFirmwareTxSuccess','StationFirmwareTxRetries','StationFirmwareTxRetryExhausted','StationRxRetried','StationTxFallbackKbps')) {
        $result[$name]='Unknown'
    }
    if($version -eq 2) {
        $extended=[BitConverter]::ToUInt32($Data,80);$result.ExtendedValidMask=$extended
        if($extended -band 0xfffffe00L){throw 'Unsupported extended valid mask.'}
        $result.ExtendedStatus='0x{0:X8}' -f [BitConverter]::ToUInt32($Data,84)
        foreach($i in 0..4) {
            $name=@('Rate','Bssid','Chanspec','PacketCounters','Station')[$i]
            $result[($name+'Status')]='0x{0:X8}' -f [BitConverter]::ToUInt32($Data,88+4*$i)
            $result[($name+'FirmwareError')]=[BitConverter]::ToInt32($Data,108+4*$i)
        }
        if($extended -band 1){$result.CurrentTxRateMbps=[BitConverter]::ToUInt32($Data,128)/2.0}
        if($extended -band 2){$result.Bssid=(($Data[132..137] | ForEach-Object {$_.ToString('X2')}) -join ':')}
        if($extended -band 4){$result.ChanspecRaw='0x{0:X4}' -f [BitConverter]::ToUInt32($Data,140)}
        if($extended -band 8) {
            foreach($i in 0..4){$result[@('FirmwareRxGood','FirmwareRxBad','FirmwareTxGood','FirmwareTxBad','FirmwareRxOther')[$i]]=[BitConverter]::ToUInt32($Data,144+4*$i)}
        }
        if($extended -band 16) {
            $result.StationVersion=[BitConverter]::ToUInt32($Data,164)
            $result.StationLength=[BitConverter]::ToUInt32($Data,168)
            $flags=[BitConverter]::ToUInt32($Data,172);$result.StationFlags='0x{0:X8}' -f $flags
            $result.WmmNegotiated=[bool]($flags -band 2);$result.AmpduCapable=[bool]($flags -band 0x8000)
        }
        if($extended -band 128){$result.LastTxRateMbps=[BitConverter]::ToUInt32($Data,176)/1000.0}
        if($extended -band 256){$result.LastRxRateMbps=[BitConverter]::ToUInt32($Data,180)/1000.0}
        if($extended -band 32) {
            foreach($i in 0..4){$result[@('StationTxPackets','StationTxFailures','StationRxUnicast','StationRxMulticast','StationRxDecryptFailures')[$i]]=[BitConverter]::ToUInt32($Data,184+4*$i)}
        }
        if($extended -band 64) {
            foreach($i in 0..8){$result[@('StationTxRetryCountRaw','StationTxRetryExhausted','StationUserTxSuccess','StationUserTxRetries',
                'StationFirmwareTxSuccess','StationFirmwareTxRetries','StationFirmwareTxRetryExhausted','StationRxRetried','StationTxFallbackKbps')[$i]]=[BitConverter]::ToUInt32($Data,204+4*$i)}
            if($result.StationTxFallbackKbps -eq 0 -or $result.StationTxFallbackKbps -gt 10000000){$result.StationTxFallbackKbps='Unknown'}
        }
    }
    $result.Notes='Read-only sequential firmware observations, not an atomic radio snapshot. PHY rates are not throughput; AMPDU capability is not proof of active aggregation. Compare counters only for the same BSSID/session; decreases may be reset/wrap, not zero loss. Unsupported fields remain Unknown. Raw chanspec is not decoded without a verified encoding.'
    [pscustomobject]$result
}
if ($LibraryOnly -or $RadioLibraryOnly) { return }
$identity=[Security.Principal.WindowsIdentity]::GetCurrent()
if (-not ([Security.Principal.WindowsPrincipal]$identity).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    if($AsJson){throw 'Run the JSON radio reader from an elevated performance utility.'}
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
            byte[] b=new byte[256];int count;
            if(!DeviceIoControl(h,refresh?0x12A00Cu:0x126010u,IntPtr.Zero,0,b,b.Length,out count,IntPtr.Zero))
                throw new Win32Exception(Marshal.GetLastWin32Error());
            Array.Resize(ref b,count);return b;
        }
    }
}
'@
$previous=[Rpi5RadioControl]::Call($false)
if($previous.Length -notin @(80,256)){throw 'Driver has no radio-report support.'}
$generation=[BitConverter]::ToUInt32($previous,4)
[void][Rpi5RadioControl]::Call($true)
$timer=[Diagnostics.Stopwatch]::StartNew()
do {
    Start-Sleep -Milliseconds 250
    $data=[Rpi5RadioControl]::Call($false)
    if($data.Length -in @(80,256) -and [BitConverter]::ToUInt32($data,4) -ne $generation) {
        $report=ConvertFrom-Rpi5RadioReport $data
        if($AsJson){$report | ConvertTo-Json -Depth 5 | Write-Output}
        else {
            $report | Format-List | Out-String -Width 200 | Write-Output
            Write-Output 'Read-only point-in-time snapshot; unknown values are not guesses. No band, country or power setting was changed.'
        }
        return
    }
} while($timer.Elapsed.TotalSeconds -lt 25)
throw 'Radio query did not complete in 25 seconds. No retry or settings change was performed.'
