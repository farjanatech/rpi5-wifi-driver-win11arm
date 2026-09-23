[CmdletBinding()]
param([switch]$ScanLibraryOnly)
Set-StrictMode -Version Latest
$ErrorActionPreference='Stop'

# Definitions only: importing this helper never opens the driver or scans.
function ConvertTo-Rpi5ScanRequest {
    param([string]$Country)
    if($Country -cnotmatch '^[A-Z]{2}$'){throw 'Confirm the two-letter country where this Pi is physically located.'}
    $data=[byte[]]::new(8)
    [BitConverter]::GetBytes([uint32]1).CopyTo($data,0)
    [Text.Encoding]::ASCII.GetBytes($Country).CopyTo($data,4)
    return ,$data
}
function ConvertFrom-Rpi5ScanReport {
    param([byte[]]$Data)
    if($null -eq $Data -or $Data.Length -ne 3616){throw 'Unsupported scan report length.'}
    $version=[BitConverter]::ToUInt32($Data,0)
    $generation=[BitConverter]::ToUInt32($Data,4)
    $state=[BitConverter]::ToUInt32($Data,8)
    $count=[BitConverter]::ToUInt32($Data,16)
    $flags=[BitConverter]::ToUInt32($Data,20)
    $countryRaw=[BitConverter]::ToUInt32($Data,24)
    if($version -ne 1 -or $state -gt 5 -or ($state -ne 0 -and $generation -eq 0) -or
        $count -gt 64 -or $flags -gt 1 -or $countryRaw -gt 65535){
        throw 'Unsupported scan report fields.'
    }
    $country=''
    if($countryRaw){
        $country=[string][char]($countryRaw -band 255)+[char](($countryRaw -shr 8) -band 255)
        if($country -cnotmatch '^[A-Z]{2}$'){throw 'Invalid scan country.'}
    }
    $entries=@()
    $utf8=[Text.UTF8Encoding]::new($false,$true)
    for($index=0;$index -lt $count;$index++){
        $offset=32+56*$index
        $length=[BitConverter]::ToUInt32($Data,$offset)
        if($length -gt 32){throw 'Invalid scan SSID length.'}
        $raw=[byte[]]::new($length)
        if($length){[Array]::Copy($Data,$offset+4,$raw,0,$length)}
        $ssid=$null;$display='<Hidden network: enter its SSID manually>';$textSafe=$false
        if($length){
            try{
                $candidate=$utf8.GetString($raw)
                $roundtrip=$utf8.GetBytes($candidate)
                $textSafe=([BitConverter]::ToString($raw) -ceq [BitConverter]::ToString($roundtrip))
                foreach($character in $candidate.ToCharArray()){
                    if([Globalization.CharUnicodeInfo]::GetUnicodeCategory($character) -in @(
                        [Globalization.UnicodeCategory]::Control,[Globalization.UnicodeCategory]::Format,
                        [Globalization.UnicodeCategory]::LineSeparator,[Globalization.UnicodeCategory]::ParagraphSeparator)){
                        $textSafe=$false
                    }
                }
                if($textSafe){$ssid=$candidate;$display=$candidate}
                else{$display='<SSID contains non-displayable characters>'}
            }catch{$display='<SSID is not valid UTF-8>';$textSafe=$false}
        }
        $mac=[byte[]]::new(6);[Array]::Copy($Data,$offset+36,$mac,0,6)
        if(($mac[0] -band 1) -or [BitConverter]::ToString($mac) -eq '00-00-00-00-00-00'){
            throw 'Invalid scan BSSID.'
        }
        $rssi=[BitConverter]::ToInt32($Data,$offset+44)
        $security=[BitConverter]::ToUInt32($Data,$offset+48)
        $channel=[BitConverter]::ToUInt32($Data,$offset+52)
        $band='Unknown'
        if($channel -ge 1 -and $channel -le 14){$band='2.4 GHz'}
        elseif($channel -ge 32 -and $channel -le 196){$band='5 GHz'}
        # Driver ABI flags: OPEN=1, WPA2_PSK_CCMP=2, UNSUPPORTED=4,
        # HIDDEN=8, MALFORMED_IE=16, PMF_REQUIRED=32. Fail closed.
        $compatible=($security -eq 2)
        $securityText='Unsupported / unknown'
        if($compatible){$securityText='WPA2-Personal / AES'}
        elseif($security -band 32){$securityText='MFP required (unsupported)'}
        elseif($security -band 16){$securityText='Malformed security information'}
        elseif($security -band 1){$securityText='Open (unsupported)'}
        $entries += [pscustomobject]@{
            Ssid=$ssid;DisplaySsid=$display;SsidBytes=$raw;Bssid=([BitConverter]::ToString($mac).Replace('-',':'));
            Chanspec=[BitConverter]::ToUInt16($Data,$offset+42);Channel=$channel;Band=$band;
            RssiDbm=$(if($rssi -lt 0 -and $rssi -ge -127){$rssi}else{$null});
            SecurityFlags=$security;Security=$securityText;
            Connectable=($textSafe -and $compatible -and $band -ne 'Unknown')
        }
    }
    [pscustomobject]@{Version=$version;Generation=$generation;State=$state;
        Status=[BitConverter]::ToUInt32($Data,12);Count=$count;Truncated=($flags -band 1) -ne 0;
        Country=$country;FirmwareError=[BitConverter]::ToInt32($Data,28);Entries=$entries}
}
function Initialize-Rpi5ScanControl {
    if('Rpi5WifiScanControl' -as [type]){return}
    Add-Type -TypeDefinition @'
using System;
using System.ComponentModel;
using System.Runtime.InteropServices;
using Microsoft.Win32.SafeHandles;
[assembly: DefaultDllImportSearchPaths(DllImportSearchPath.System32)]
public static class Rpi5WifiScanControl {
    [DllImport("kernel32.dll",CharSet=CharSet.Unicode,SetLastError=true)]
    static extern SafeFileHandle CreateFile(string name,uint access,uint share,IntPtr security,uint creation,uint flags,IntPtr template);
    [DllImport("kernel32.dll",SetLastError=true)]
    static extern bool DeviceIoControl(SafeFileHandle handle,uint code,byte[] input,int inputSize,byte[] output,int outputSize,out int returned,IntPtr overlapped);
    public static byte[] Call(uint code,byte[] input) {
        bool read=code==0x126018u;
        if(!read && code!=0x12A014u && code!=0x12A01Cu)throw new ArgumentException("Unsupported scan operation.");
        using(var handle=CreateFile(@"\\.\Rpi5CywControl",read?0x80000000u:0x40000000u,3,IntPtr.Zero,3,0,IntPtr.Zero)) {
            if(handle.IsInvalid)throw new Win32Exception(Marshal.GetLastWin32Error());
            byte[] output=new byte[read?3616:0];int returned;
            if(!DeviceIoControl(handle,code,input,input==null?0:input.Length,output,output.Length,out returned,IntPtr.Zero))
                throw new Win32Exception(Marshal.GetLastWin32Error());
            if(returned!=output.Length)throw new InvalidOperationException("Incomplete scan response.");
            return output;
        }
    }
}
'@
}
function Invoke-Rpi5ScanControl {
    param([uint32]$Code,[byte[]]$InputData)
    Initialize-Rpi5ScanControl
    return ,([Rpi5WifiScanControl]::Call($Code,$InputData))
}
if($ScanLibraryOnly){return}
Write-Output 'This file provides the app scan helpers. Open RPi5-WiFi-App.cmd; no scan was started.'
