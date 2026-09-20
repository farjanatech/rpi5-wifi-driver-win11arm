[CmdletBinding()]
param([switch]$StatusOnly, [switch]$Disconnect, [switch]$LibraryOnly)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function Test-Rpi5ConnectionInput {
    param([string]$Country, [string]$Ssid)
    return $Country -cmatch '^[A-Z]{2}$' -and
        [Text.Encoding]::UTF8.GetByteCount($Ssid) -in 1..32
}
# Deliberately no transcript, saved password, command-line credential or profile.
Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
using System.ComponentModel;
using Microsoft.Win32.SafeHandles;
public static class Rpi5WifiControl {
    [DllImport("bcrypt.dll", CharSet=CharSet.Unicode)]
    static extern int BCryptOpenAlgorithmProvider(out IntPtr handle, string algorithm, string implementation, uint flags);
    [DllImport("bcrypt.dll")]
    static extern int BCryptDeriveKeyPBKDF2(IntPtr handle, byte[] password, uint passwordSize,
        byte[] salt, uint saltSize, ulong iterations, byte[] output, uint outputSize, uint flags);
    [DllImport("bcrypt.dll")]
    static extern int BCryptCloseAlgorithmProvider(IntPtr handle, uint flags);
    public static byte[] Derive(byte[] password, byte[] ssid) {
        IntPtr algorithm;
        if(BCryptOpenAlgorithmProvider(out algorithm,"SHA1",null,8)!=0)
            throw new System.Security.Cryptography.CryptographicException("Cannot open WPA2 HMAC-SHA1 provider.");
        byte[] key=new byte[32];
        try {
            if(BCryptDeriveKeyPBKDF2(algorithm,password,(uint)password.Length,ssid,(uint)ssid.Length,4096,key,32,0)!=0) {
                Array.Clear(key,0,key.Length);
                throw new System.Security.Cryptography.CryptographicException("WPA2 key derivation failed.");
            }
            return key;
        } finally { BCryptCloseAlgorithmProvider(algorithm,0); }
    }
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
if ($LibraryOnly) { return }
$identity = [Security.Principal.WindowsIdentity]::GetCurrent()
$principal = [Security.Principal.WindowsPrincipal]::new($identity)
if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    throw 'Run this utility as Administrator on the Raspberry Pi, not the build PC.'
}

if ($Disconnect) {
    [void][Rpi5WifiControl]::Call(0x12A008, $null)
    Write-Output 'Disconnect requested.'
} elseif (-not $StatusOnly) {
    for ($readyAttempt = 0; $readyAttempt -lt 60; $readyAttempt++) {
        $readyState = [Rpi5WifiControl]::Call(0x126004, $null)
        $readyPhase = [BitConverter]::ToUInt32($readyState, 4)
        $readyError = [BitConverter]::ToUInt32($readyState, 8)
        if ($readyError -ne 0) { throw ('Firmware startup failed: phase {0}, status 0x{1:X8}. Run diagnostics.' -f $readyPhase, $readyError) }
        if ($readyPhase -ge 500) { break }
        Write-Output "Firmware initialization: phase $readyPhase. Waiting before requesting credentials..."
        Start-Sleep -Seconds 3
    }
    if ($readyPhase -lt 500) { throw 'Firmware did not become ready within three minutes. Run diagnostics.' }
    Write-Output 'Experimental WPA2-Personal / AES only. Keep your working Ethernet connection available.'
    Write-Output 'Use the country where the Pi is physically located. No UEFI or boot settings are changed.'
    $country = (Read-Host 'Two-letter country code, e.g. BD').Trim().ToUpperInvariant()
    $ssid = Read-Host 'Exact Wi-Fi network name (SSID)'
    if (-not (Test-Rpi5ConnectionInput $country $ssid)) { throw 'Invalid country or SSID length (1-32 UTF-8 bytes).' }
    $secure = Read-Host 'WPA2 password (8-63 printable ASCII characters)' -AsSecureString
    $pointer = [IntPtr]::Zero
    $passwordBytes = $null; $pmk = $null; $request = $null
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
        # Windows CNG allows SSIDs shorter than eight bytes; .NET Framework's
        # Rfc2898DeriveBytes constructor rejects those otherwise-valid salts.
        $pmk = [Rpi5WifiControl]::Derive($passwordBytes, $ssidBytes)
        $request = [byte[]]::new(76)
        [BitConverter]::GetBytes([uint32]1).CopyTo($request, 0)
        [BitConverter]::GetBytes([uint32]$ssidBytes.Length).CopyTo($request, 4)
        [Text.Encoding]::ASCII.GetBytes($country).CopyTo($request, 8)
        $ssidBytes.CopyTo($request, 12); $pmk.CopyTo($request, 44)
        [void][Rpi5WifiControl]::Call(0x12A000, $request)
        Write-Output 'Connection requested. Success requires authenticated link AND an IP address.'
    } finally {
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
    $eventType = [BitConverter]::ToUInt32($state, 20)
    $reason = [BitConverter]::ToUInt32($state, 24)
    Write-Output ('Phase={0} Status=0x{1:X8} AuthenticatedLink={2} Event={3} Reason={4}' -f $phase, $errorCode, $connected, $eventType, $reason)
    if ($connected -or $errorCode -ne 0) { break }
    if ($attempt + 1 -lt $limit) { Start-Sleep -Seconds 2 }
}
Get-NetAdapter | Where-Object InterfaceDescription -like '*CYW43455*' |
    Get-NetIPConfiguration | Format-List InterfaceAlias, IPv4Address, IPv4DefaultGateway
Write-Output 'If association failed, collect diagnostics. Do not change UEFI or reinstall Windows.'
