[CmdletBinding()]
param([switch]$StatusOnly, [switch]$Disconnect, [switch]$LibraryOnly)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function Test-Rpi5ConnectionInput {
    param([string]$Country, [string]$Ssid)
    return $Country -cmatch '^[A-Z]{2}$' -and
        [Text.Encoding]::UTF8.GetByteCount($Ssid) -in 1..32
}
if ($LibraryOnly) { return }
$identity = [Security.Principal.WindowsIdentity]::GetCurrent()
$principal = [Security.Principal.WindowsPrincipal]::new($identity)
if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    throw 'Run this utility as Administrator on the Raspberry Pi, not the build PC.'
}
# Deliberately no transcript, saved password, command-line credential or profile.
Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
using System.ComponentModel;
using Microsoft.Win32.SafeHandles;
public static class Rpi5WifiControl {
    [DllImport("kernel32.dll", CharSet=CharSet.Unicode, SetLastError=true)]
    static extern SafeFileHandle CreateFile(string name, uint access, uint share,
        IntPtr security, uint creation, uint flags, IntPtr template);
    [DllImport("kernel32.dll", SetLastError=true)]
    static extern bool DeviceIoControl(SafeFileHandle handle, uint code,
        byte[] input, int inputSize, byte[] output, int outputSize,
        out int returned, IntPtr overlapped);
    public static byte[] Call(uint code, byte[] input) {
        using(var h=CreateFile(@"\\.\Rpi5CywControl",0xC0000000,3,IntPtr.Zero,3,0,IntPtr.Zero)) {
            if(h.IsInvalid) throw new Win32Exception(Marshal.GetLastWin32Error());
            byte[] output=new byte[32]; int count;
            if(!DeviceIoControl(h,code,input,input==null?0:input.Length,output,output.Length,out count,IntPtr.Zero))
                throw new Win32Exception(Marshal.GetLastWin32Error());
            return output;
        }
    }
}
'@

if ($Disconnect) {
    [void][Rpi5WifiControl]::Call(0x12A008, $null)
    Write-Host 'Disconnect requested.'
} elseif (-not $StatusOnly) {
    Write-Host 'Experimental WPA2-Personal / AES only. Keep your working Ethernet connection available.'
    Write-Host 'Use the country where the Pi is physically located. No UEFI or boot settings are changed.'
    $country = (Read-Host 'Two-letter country code, e.g. BD').Trim().ToUpperInvariant()
    $ssid = Read-Host 'Exact Wi-Fi network name (SSID)'
    if (-not (Test-Rpi5ConnectionInput $country $ssid)) { throw 'Invalid country or SSID length (1-32 UTF-8 bytes).' }
    $secure = Read-Host 'WPA2 password (8-63 printable ASCII characters)' -AsSecureString
    $pointer = [IntPtr]::Zero
    $passwordBytes = $null; $pmk = $null; $request = $null; $derive = $null
    try {
        if ($secure.Length -lt 8 -or $secure.Length -gt 63) { throw 'Password must be 8-63 characters.' }
        $pointer = [Runtime.InteropServices.Marshal]::SecureStringToGlobalAllocUnicode($secure)
        $passwordBytes = [byte[]]::new($secure.Length)
        for ($i = 0; $i -lt $secure.Length; $i++) {
            $character = [Runtime.InteropServices.Marshal]::ReadInt16($pointer, $i * 2)
            if ($character -lt 32 -or $character -gt 126) { throw 'This first utility supports printable ASCII passwords only.' }
            $passwordBytes[$i] = [byte]$character
        }
        $ssidBytes = [Text.Encoding]::UTF8.GetBytes($ssid)
        $derive = [Security.Cryptography.Rfc2898DeriveBytes]::new($passwordBytes, $ssidBytes, 4096)
        $pmk = $derive.GetBytes(32)
        $request = [byte[]]::new(76)
        [BitConverter]::GetBytes([uint32]1).CopyTo($request, 0)
        [BitConverter]::GetBytes([uint32]$ssidBytes.Length).CopyTo($request, 4)
        [Text.Encoding]::ASCII.GetBytes($country).CopyTo($request, 8)
        $ssidBytes.CopyTo($request, 12); $pmk.CopyTo($request, 44)
        [void][Rpi5WifiControl]::Call(0x12A000, $request)
        Write-Host 'Connection requested. Success requires authenticated link AND an IP address.'
    } finally {
        if ($derive) { $derive.Dispose() }
        foreach ($buffer in @($passwordBytes, $pmk, $request)) {
            if ($null -ne $buffer) { [Array]::Clear($buffer, 0, $buffer.Length) }
        }
        if ($pointer -ne [IntPtr]::Zero) { [Runtime.InteropServices.Marshal]::ZeroFreeGlobalAllocUnicode($pointer) }
        $secure.Dispose()
    }
}

$limit = if ($StatusOnly -or $Disconnect) { 1 } else { 60 }
for ($attempt = 0; $attempt -lt $limit; $attempt++) {
    $state = [Rpi5WifiControl]::Call(0x126004, $null)
    $phase = [BitConverter]::ToUInt32($state, 4)
    $errorCode = [BitConverter]::ToUInt32($state, 8)
    $connected = [BitConverter]::ToUInt32($state, 28) -eq 1
    Write-Host ('Phase={0} Status=0x{1:X8} AuthenticatedLink={2}' -f $phase, $errorCode, $connected)
    if ($connected -or $errorCode -ne 0) { break }
    if ($attempt + 1 -lt $limit) { Start-Sleep -Seconds 2 }
}
Get-NetAdapter | Where-Object InterfaceDescription -like '*CYW43455*' |
    Get-NetIPConfiguration | Format-List InterfaceAlias, IPv4Address, IPv4DefaultGateway
Write-Host 'If association failed, collect diagnostics. Do not change UEFI or reinstall Windows.'
