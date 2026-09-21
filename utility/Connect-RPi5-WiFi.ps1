[CmdletBinding()]
param([switch]$StatusOnly, [switch]$Disconnect, [switch]$LibraryOnly,
      [string]$ConfigPath, [switch]$Startup)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function Test-Rpi5ConnectionInput {
    param([string]$Country, [string]$Ssid)
    return $Country -cmatch '^[A-Z]{2}$' -and
        [Text.Encoding]::UTF8.GetByteCount($Ssid) -in 1..32
}
function Read-Rpi5WifiConfig {
    param([string]$Path)
    # Do not emit parser exceptions: malformed JSON can include the password.
    try {
        $file = Get-Item -LiteralPath $Path -ErrorAction Stop
        if ($file.PSIsContainer -or $file.Length -gt 8192) { throw 'size' }
        $wifiProfile = Get-Content -LiteralPath $Path -Raw -Encoding UTF8 | ConvertFrom-Json -ErrorAction Stop
        if ($null -eq $wifiProfile -or $wifiProfile -is [array]) { throw 'shape' }
        foreach ($name in @('Country', 'SSID', 'Password')) {
            if (-not $wifiProfile.PSObject.Properties[$name] -or $wifiProfile.$name -isnot [string]) { throw 'field' }
        }
        if (@($wifiProfile.PSObject.Properties).Count -ne 3) { throw 'extra fields' }
        $wifiProfile.Country = $wifiProfile.Country.Trim().ToUpperInvariant()
        if (-not (Test-Rpi5ConnectionInput $wifiProfile.Country $wifiProfile.SSID) -or
            $wifiProfile.SSID.Contains([string][char]0) -or
            $wifiProfile.Password -cnotmatch '\A[\x20-\x7E]{8,63}\z') { throw 'bounds' }
        return $wifiProfile
    } catch {
        throw 'Wi-Fi configuration is missing or invalid. Use only Country (two letters), SSID (1-32 UTF-8 bytes), and Password (8-63 printable ASCII characters). Contents are not logged.'
    }
}
function Resolve-Rpi5Country {
    param([string]$InputCountry, [string]$SavedCountry)
    $choice = $InputCountry.Trim().ToUpperInvariant()
    if (-not $choice -and $SavedCountry -cmatch '^[A-Z]{2}$') { $choice = $SavedCountry }
    if ($choice -cnotmatch '^[A-Z]{2}$') { throw 'Enter the two-letter country where the Pi is physically located.' }
    return $choice
}
function Get-Rpi5ConnectStepName {
    param([uint32]$Step)
    switch ($Step) {
        1 { 'radio-down' } 2 { 'country-set' } 3 { 'country-readback' }
        4 { 'infrastructure' } 5 { 'authentication-mode' } 6 { 'AES-cipher' }
        7 { 'WPA2-mode' } 8 { 'mfp' } 9 { 'sup_wpa' } 10 { 'wpaie' }
        11 { 'PMK' } 12 { 'radio-up' } 13 { 'join-SSID' }
        14 { 'country-initial-read' } 15 { 'regulatory-data-status' } 16 { 'country-full-auto-revision' }
        17 { 'supported-country-query' }
        default { 'not-recorded' }
    }
}
function Get-Rpi5DriverFailure {
    param([byte[]]$State)
    if ($State.Length -lt 32) { return 'Incomplete driver status. Run diagnostics.' }
    $phase = [BitConverter]::ToUInt32($State, 4)
    $step = if ($State.Length -ge 48 -and [BitConverter]::ToUInt32($State, 0) -ge 2) {
        Get-Rpi5ConnectStepName ([BitConverter]::ToUInt32($State, 32))
    } else { 'not-recorded' }
    $stage = if ($phase -ge 500) { 'Connection setup/runtime' } else { 'Firmware startup' }
    return ('{0} failed: phase {1}, step {2}, status 0x{3:X8}, firmware command {4}, firmware error {5}. Run diagnostics.' -f
        $stage, $phase, $step, [BitConverter]::ToUInt32($State, 8),
        [BitConverter]::ToUInt32($State, 12), [BitConverter]::ToInt32($State, 16))
}
function Get-Rpi5StartupSample {
    param([byte[]]$State)
    if ($State.Length -lt 32) { throw 'Incomplete driver status.' }
    $phase = [BitConverter]::ToUInt32($State, 4)
    $status = [BitConverter]::ToUInt32($State, 8)
    $extended = $State.Length -ge 96 -and [BitConverter]::ToUInt32($State, 0) -ge 3
    $total = 0; $uploaded = 0; $verified = 0
    $text = "Phase=$phase (live byte counters unavailable from this driver)"
    if ($extended) {
        $total = [BitConverter]::ToUInt32($State, 48)
        $uploaded = [BitConverter]::ToUInt32($State, 52)
        $verified = [BitConverter]::ToUInt32($State, 56)
        $percent = if ($total -gt 0) { [Math]::Round(100.0 * $uploaded / $total, 1) } else { 0 }
        $text = ('Phase={0} Upload={1}/{2} bytes ({3}%) Verified={4}/{2} bytes RAM=0x{5:X8} Length={6} Write={7} TransferStage={8} TransferStatus=0x{9:X8} CMD={10} Argument=0x{11:X8} Response=0x{12:X8} Interrupt=0x{13:X8}' -f
            $phase, $uploaded, $total, $percent, $verified,
            [BitConverter]::ToUInt32($State, 60), [BitConverter]::ToUInt32($State, 64),
            [BitConverter]::ToUInt32($State, 68), [BitConverter]::ToUInt32($State, 76),
            [BitConverter]::ToUInt32($State, 72), [BitConverter]::ToUInt32($State, 80),
            [BitConverter]::ToUInt32($State, 84), [BitConverter]::ToUInt32($State, 88),
            [BitConverter]::ToUInt32($State, 92))
    }
    [pscustomobject]@{ Phase=$phase; Status=$status; Key="$phase/$total/$uploaded/$verified"; Text=$text }
}
function Get-Rpi5StartupDecision {
    param($Sample, [double]$ElapsedSeconds, [double]$IdleSeconds)
    if ($Sample.Status -ne 0) { return 'error' }
    if ($Sample.Phase -ge 500) { return 'ready' }
    if ($IdleSeconds -ge 120) { return 'no-progress' }
    if ($ElapsedSeconds -ge 1800) { return 'wait-limit' }
    return 'wait'
}
# No transcript or command-line credential. An optional user-owned JSON file
# supplies credentials, but this connector never writes them or includes them in errors.
Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
using System.ComponentModel;
using Microsoft.Win32.SafeHandles;
[assembly: DefaultDllImportSearchPaths(DllImportSearchPath.System32)]
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
        using(var h=CreateFile(@"\\.\Rpi5CywControl",code==0x126004?0x80000000u:0x40000000u,3,IntPtr.Zero,3,0,IntPtr.Zero)) {
            if(h.IsInvalid) throw new Win32Exception(Marshal.GetLastWin32Error());
            byte[] output=new byte[96]; int count;
            if(!DeviceIoControl(h,code,input,input==null?0:input.Length,output,output.Length,out count,IntPtr.Zero))
                throw new Win32Exception(Marshal.GetLastWin32Error());
            if(code==0x126004 && (count<32 || (BitConverter.ToUInt32(output,0)>=2 && count<48) ||
                (BitConverter.ToUInt32(output,0)>=3 && count<96)))
                throw new InvalidOperationException("Incomplete driver status response.");
            return output;
        }
    }
}
'@
if ($LibraryOnly) { return }
if (-not $ConfigPath -and -not $StatusOnly -and -not $Disconnect) {
    $candidate = Join-Path $PSScriptRoot 'WiFi.private.json'
    if (Test-Path -LiteralPath $candidate) { $ConfigPath = $candidate }
}
if ($ConfigPath) {
    if ($ConfigPath.Contains('"')) { throw 'Invalid configuration path.' }
    $ConfigPath = [IO.Path]::GetFullPath($ConfigPath)
}
$identity = [Security.Principal.WindowsIdentity]::GetCurrent()
$principal = [Security.Principal.WindowsPrincipal]::new($identity)
if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    $launchArguments = @('-NoProfile', '-NoExit', '-ExecutionPolicy', 'Bypass', '-File', ('"{0}"' -f $PSCommandPath))
    if ($StatusOnly) { $launchArguments += '-StatusOnly' }
    if ($Disconnect) { $launchArguments += '-Disconnect' }
    if ($ConfigPath) { $launchArguments += @('-ConfigPath', ('"{0}"' -f $ConfigPath)) }
    if ($Startup) { throw 'Startup mode requires the installed SYSTEM task.' }
    # Interactive connection must show its prompts/results; only the SYSTEM
    # startup task is noninteractive and runs without a user desktop window.
    Start-Process -FilePath (Join-Path $PSHOME 'powershell.exe') -Verb RunAs -ArgumentList $launchArguments
    return
}
if ($Startup -and -not $ConfigPath) { throw 'Startup mode requires a configuration file.' }
if ($Startup) {
    $deviceWatch = [Diagnostics.Stopwatch]::StartNew()
    while ($true) {
        try { [void][Rpi5WifiControl]::Call(0x126004, $null); break }
        catch { if ($deviceWatch.Elapsed.TotalSeconds -ge 180) { throw 'Driver control device unavailable after 180 seconds.' } }
        Start-Sleep -Seconds 5
    }
}

if ($Disconnect) {
    [void][Rpi5WifiControl]::Call(0x12A008, $null)
    Write-Output 'Disconnect requested.'
} elseif (-not $StatusOnly) {
    $startupWatch = [Diagnostics.Stopwatch]::StartNew()
    $previousKey = ''; $lastAdvance = 0.0
    while ($true) {
        $readyState = [Rpi5WifiControl]::Call(0x126004, $null)
        $sample = Get-Rpi5StartupSample $readyState
        $elapsed = $startupWatch.Elapsed.TotalSeconds
        if ($sample.Key -ne $previousKey) { $lastAdvance = $elapsed; $previousKey = $sample.Key }
        $idle = $elapsed - $lastAdvance
        Write-Output ('{0} Elapsed={1}s NoProgressObserved={2}s' -f $sample.Text, [int]$elapsed, [int]$idle)
        $decision = Get-Rpi5StartupDecision $sample $elapsed $idle
        if ($decision -eq 'error') { throw (Get-Rpi5DriverFailure $readyState) }
        if ($decision -eq 'ready') { break }
        if ($decision -eq 'no-progress') {
            throw 'No startup byte/phase progress observed for 120 seconds. This does not prove a hardware hang. Driver is not stopped. Collect diagnostics now before rebooting.'
        }
        if ($decision -eq 'wait-limit') {
            throw 'Utility reached its 30-minute observation limit. Driver is not stopped. Collect diagnostics before rebooting.'
        }
        Start-Sleep -Seconds 5
    }
    Write-Output 'Experimental WPA2-Personal / AES only. Keep your working Ethernet connection available.'
    Write-Output 'Use the country where the Pi is physically located. No UEFI or boot settings are changed.'
    $countryKey = 'HKCU:\Software\Farjanatech\RPi5WiFi'
    $savedCountry = ''
    try {
        $savedCountry = Get-ItemPropertyValue -LiteralPath $countryKey -Name ConfirmedCountry -ErrorAction Stop
    } catch { $savedCountry = '' }
    if ($savedCountry -cnotmatch '^[A-Z]{2}$') { $savedCountry = '' }
    $countryPrompt = if ($savedCountry) {
        "Country where this Pi is physically located [Enter confirms $savedCountry, or type another]"
    } else { 'Two-letter country code, e.g. BD (no automatic USA fallback)' }
    if ($ConfigPath) {
        $wifiProfile = Read-Rpi5WifiConfig $ConfigPath
        $country = $wifiProfile.Country; $ssid = $wifiProfile.SSID
        $secure = [Security.SecureString]::new()
        foreach ($character in $wifiProfile.Password.ToCharArray()) { $secure.AppendChar($character) }
        $secure.MakeReadOnly(); $wifiProfile.Password = $null; $wifiProfile = $null
        Write-Output 'Using the editable local configuration. Credentials are not printed.'
    } else {
        $country = Resolve-Rpi5Country (Read-Host $countryPrompt) $savedCountry
        $ssid = Read-Host 'Exact Wi-Fi network name (SSID)'
        $secure = Read-Host 'WPA2 password (8-63 printable ASCII characters)' -AsSecureString
    }
    if (-not (Test-Rpi5ConnectionInput $country $ssid)) { throw 'Invalid country or SSID length (1-32 UTF-8 bytes).' }
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
        try {
            if (-not (Test-Path -LiteralPath $countryKey)) { [void](New-Item -Path $countryKey -Force) }
            [void](New-ItemProperty -LiteralPath $countryKey -Name ConfirmedCountry -Value $country -PropertyType String -Force)
        } catch { Write-Warning 'Could not remember the country; the connection request was still submitted.' }
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
    if ($StatusOnly) { Write-Output (Get-Rpi5StartupSample $state).Text }
    $phase = [BitConverter]::ToUInt32($state, 4)
    $errorCode = [BitConverter]::ToUInt32($state, 8)
    $connected = [BitConverter]::ToUInt32($state, 28) -eq 1
    $eventType = [BitConverter]::ToUInt32($state, 20)
    $reason = [BitConverter]::ToUInt32($state, 24)
    Write-Output ('Phase={0} Status=0x{1:X8} AuthenticatedLink={2} Event={3} Reason={4}' -f $phase, $errorCode, $connected, $eventType, $reason)
    if ($errorCode -ne 0) { Write-Output (Get-Rpi5DriverFailure $state) }
    if ([BitConverter]::ToUInt32($state, 0) -ge 2 -and $state.Length -ge 48) {
        $applied = [BitConverter]::ToUInt32($state, 40)
        if ($applied -ne 0) {
            $appliedText = [string][char]($applied -band 255) + [char](($applied -shr 8) -band 255)
            Write-Output ('Verified country={0} revision={1}' -f $appliedText, [BitConverter]::ToUInt32($state, 44))
        }
    }
    if ($connected -or $errorCode -ne 0) { break }
    if ($attempt + 1 -lt $limit) { Start-Sleep -Seconds 2 }
}
Get-NetAdapter | Where-Object InterfaceDescription -like '*CYW43455*' |
    Get-NetIPConfiguration | Format-List InterfaceAlias, IPv4Address, IPv4DefaultGateway
Write-Output 'If association failed, collect diagnostics. Do not change UEFI or reinstall Windows.'
if ($ConfigPath -and -not $StatusOnly -and -not $Disconnect -and (-not $connected -or $errorCode -ne 0)) {
    throw 'Configured connection did not authenticate. Run diagnostics; automatic mode does not imply working Internet.'
}
