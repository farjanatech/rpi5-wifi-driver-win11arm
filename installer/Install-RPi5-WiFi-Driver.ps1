[CmdletBinding()]
param(
    [switch]$LibraryOnly,
    [switch]$NoPause
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$InformationPreference = 'Continue'
$script:InstallerVersion = '0.3.0'
$script:RequiredUefiRevision = '5a5013a'

function Test-Rpi5Administrator {
    $identity = [Security.Principal.WindowsIdentity]::GetCurrent()
    $principal = [Security.Principal.WindowsPrincipal]::new($identity)
    return $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
}

function Test-Rpi5ManifestName {
    param([Parameter(Mandatory=$true)][string]$Name)
    if ([IO.Path]::IsPathRooted($Name)) { return $false }
    if ([IO.Path]::GetFileName($Name) -ne $Name) { return $false }
    if ($Name -in '.', '..', 'SHA256SUMS.txt') { return $false }
    return $true
}

function Test-Rpi5PackageManifest {
    param([Parameter(Mandatory=$true)][string]$Directory)

    $manifestPath = Join-Path $Directory 'SHA256SUMS.txt'
    if (-not (Test-Path -LiteralPath $manifestPath -PathType Leaf)) {
        throw 'SHA256SUMS.txt is missing. Extract the complete driver ZIP before installing.'
    }

    $verified = 0
    foreach ($line in Get-Content -LiteralPath $manifestPath) {
        $match = [regex]::Match($line, '^([0-9A-Fa-f]{64})\s+(.+)$')
        if (-not $match.Success) { continue }
        $name = $match.Groups[2].Value
        if (-not (Test-Rpi5ManifestName -Name $name)) { throw "Unsafe manifest filename: $name" }
        $path = Join-Path $Directory $name
        if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { throw "Package file is missing: $name" }
        $actual = (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash
        if ($actual -ne $match.Groups[1].Value.ToUpperInvariant()) { throw "Package hash mismatch: $name" }
        $verified++
    }
    if ($verified -lt 5) { throw 'The package manifest did not contain enough verified files.' }
    return $verified
}

function Get-Rpi5TargetDevice {
    $hardwarePattern = '^ACPI\\RPI0011(?:\\|$)'
    $pnp = Get-PnpDevice -PresentOnly:$false -ErrorAction SilentlyContinue |
        Where-Object { $_.InstanceId -match $hardwarePattern } |
        Select-Object -First 1
    if ($pnp) {
        return [pscustomobject]@{ Method='Get-PnpDevice'; InstanceId=$pnp.InstanceId; Status=$pnp.Status }
    }

    $entity = Get-CimInstance Win32_PnPEntity -ErrorAction SilentlyContinue |
        Where-Object { $_.PNPDeviceID -match $hardwarePattern } |
        Select-Object -First 1
    if ($entity) {
        return [pscustomobject]@{ Method='Win32_PnPEntity'; InstanceId=$entity.PNPDeviceID; Status=$entity.Status }
    }

    # Some Pi 5 Windows builds expose an unbound ACPI node only through this
    # inventory class. Accept it only when the exact matching UEFI is running.
    $bios = Get-CimInstance Win32_BIOS -ErrorAction SilentlyContinue
    $biosText = "$($bios.SMBIOSBIOSVersion) $($bios.BIOSVersion -join ' ')"
    if ($biosText -match [regex]::Escape($script:RequiredUefiRevision)) {
        $signedNode = Get-CimInstance Win32_PnPSignedDriver -ErrorAction SilentlyContinue |
            Where-Object { $_.DeviceID -match $hardwarePattern } |
            Select-Object -First 1
        if ($signedNode) {
            return [pscustomobject]@{ Method='Win32_PnPSignedDriver+matching-UEFI'; InstanceId=$signedNode.DeviceID; Status='Unbound' }
        }
    }
    return $null
}

function Invoke-Rpi5DriverInstall {
    [CmdletBinding()]
    param([switch]$DoNotPause)

    if (-not (Test-Rpi5Administrator)) {
        if (-not $PSCommandPath) { throw 'Run this installer from its saved script file.' }
        Write-Information 'Requesting Administrator permission...'
        $quotedScript = $PSCommandPath.Replace('"', '""')
        $arguments = "-NoProfile -ExecutionPolicy Bypass -File `"$quotedScript`""
        if ($DoNotPause) { $arguments += ' -NoPause' }
        try {
            $elevated = Start-Process -FilePath 'powershell.exe' -Verb RunAs -ArgumentList $arguments -Wait -PassThru
            return $elevated.ExitCode
        } catch {
            Write-Information "Administrator permission was not granted: $($_.Exception.Message)"
            return 1
        }
    }

    $desktop = [Environment]::GetFolderPath([Environment+SpecialFolder]::DesktopDirectory)
    if (-not $desktop -or -not (Test-Path -LiteralPath $desktop -PathType Container)) {
        $desktop = Split-Path -Parent $PSCommandPath
    }
    $stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
    $logPath = Join-Path $desktop "RPI5-WIFI-DRIVER-INSTALL-$stamp.txt"

    function Write-InstallMessage {
        param([Parameter(Mandatory=$true)][string]$Message)
        Write-Information $Message
        "[$(Get-Date -Format 'yyyy-MM-dd HH:mm:ss')] $Message" |
            Add-Content -LiteralPath $logPath -Encoding UTF8
    }

    try {
        Write-InstallMessage "RPi5 direct-SDIO driver one-click installer v$script:InstallerVersion"
        Write-InstallMessage 'This installer will not change UEFI, Secure Boot, Test Signing or BCD.'

        if ([Runtime.InteropServices.RuntimeInformation]::OSArchitecture -ne [Runtime.InteropServices.Architecture]::Arm64) {
            throw 'This package can only be installed on Windows ARM64 running on Raspberry Pi 5.'
        }
        $computer = Get-CimInstance Win32_ComputerSystem
        if ("$($computer.Manufacturer) $($computer.Model)" -notmatch 'Raspberry Pi.*5') {
            throw "This computer is not identified as a Raspberry Pi 5: $($computer.Manufacturer) $($computer.Model)"
        }

        $directory = Split-Path -Parent $PSCommandPath
        $cer = Join-Path $directory 'rpi5cyw-test.cer'
        $inf = Join-Path $directory 'rpi5cyw.inf'
        $sys = Join-Path $directory 'rpi5cyw.sys'
        $cat = Join-Path $directory 'rpi5cyw.cat'
        foreach ($required in @($cer,$inf,$sys,$cat)) {
            if (-not (Test-Path -LiteralPath $required -PathType Leaf)) {
                throw "Required package file is missing: $(Split-Path -Leaf $required)"
            }
        }

        $verifiedFiles = Test-Rpi5PackageManifest -Directory $directory
        Write-InstallMessage "Verified $verifiedFiles package files against SHA256SUMS.txt."

        $bios = Get-CimInstance Win32_BIOS
        $biosText = "$($bios.SMBIOSBIOSVersion) $($bios.BIOSVersion -join ' ')"
        if ($biosText -notmatch [regex]::Escape($script:RequiredUefiRevision)) {
            throw "Matching UEFI revision $script:RequiredUefiRevision was not detected. Refusing installation."
        }
        Write-InstallMessage "Matching UEFI revision $script:RequiredUefiRevision detected."

        $device = Get-Rpi5TargetDevice
        if (-not $device) {
            throw 'ACPI\RPI0011 was not found through PnP or CIM. Refusing to force-install the driver.'
        }
        Write-InstallMessage "Target device found: $($device.InstanceId) via $($device.Method), status=$($device.Status)."

        $secureBoot = $null
        try {
            $secureBoot = Confirm-SecureBootUEFI -ErrorAction Stop
        } catch {
            Write-InstallMessage "Secure Boot status is unavailable from this UEFI ($($_.Exception.GetType().Name))."
        }
        if ($secureBoot -eq $true) { throw 'Secure Boot is enabled. Refusing to install a test-signed kernel driver.' }

        $boot = (& bcdedit.exe /enum '{current}' 2>&1 | Out-String)
        if ($LASTEXITCODE -ne 0 -or $boot -notmatch '(?im)^\s*testsigning\s+Yes\s*$') {
            throw 'Windows Test Signing is not enabled. This installer will not change the boot configuration.'
        }
        Write-InstallMessage 'Windows Test Signing is enabled.'

        $certificate = [Security.Cryptography.X509Certificates.X509Certificate2]::new($cer)
        $now = Get-Date
        if ($now -lt $certificate.NotBefore -or $now -gt $certificate.NotAfter) {
            throw "The included test certificate is not valid at the current system time ($now)."
        }
        foreach ($signedFile in @($sys,$cat)) {
            $signature = Get-AuthenticodeSignature -LiteralPath $signedFile
            if (-not $signature.SignerCertificate -or
                $signature.SignerCertificate.Thumbprint -ne $certificate.Thumbprint) {
                throw "Signer mismatch for $(Split-Path -Leaf $signedFile). Refusing to trust the package."
            }
        }
        Write-InstallMessage "Verified driver/catalog signer thumbprint $($certificate.Thumbprint)."

        & certutil.exe -addstore -f Root $cer | Out-String | Add-Content -LiteralPath $logPath -Encoding UTF8
        if ($LASTEXITCODE -ne 0) { throw 'Failed to trust the test certificate in LocalMachine Root.' }
        & certutil.exe -addstore -f TrustedPublisher $cer | Out-String | Add-Content -LiteralPath $logPath -Encoding UTF8
        if ($LASTEXITCODE -ne 0) { throw 'Failed to trust the test certificate in LocalMachine TrustedPublisher.' }
        Write-InstallMessage 'Trusted the verified test certificate for this experimental driver.'

        & pnputil.exe /add-driver $inf /install | Out-String | Add-Content -LiteralPath $logPath -Encoding UTF8
        if ($LASTEXITCODE -ne 0) { throw "PnPUtil rejected the driver package with exit code $LASTEXITCODE." }
        & pnputil.exe /scan-devices | Out-String | Add-Content -LiteralPath $logPath -Encoding UTF8
        if ($LASTEXITCODE -ne 0) { throw "PnP device rescan failed with exit code $LASTEXITCODE." }
        Write-InstallMessage 'Driver package installation and PnP rescan completed.'

        Start-Sleep -Seconds 2
        $service = Get-Service -Name rpi5cyw -ErrorAction SilentlyContinue
        if ($service) {
            Write-InstallMessage "Driver service status: $($service.Status)."
        } else {
            Write-InstallMessage 'Driver service was not visible after installation; diagnostics will record the failure.'
        }

        $collector = Join-Path $directory 'Collect-RPi5-WiFi-Diagnostics.ps1'
        if (Test-Path -LiteralPath $collector -PathType Leaf) {
            Write-InstallMessage 'Collecting post-install diagnostics...'
            & powershell.exe -NoProfile -ExecutionPolicy Bypass -File $collector -NoPause
            if ($LASTEXITCODE -ne 0) {
                Write-InstallMessage "Diagnostic collector returned exit code $LASTEXITCODE."
            }
        } else {
            Write-InstallMessage 'Diagnostic collector was not found in the package.'
        }

        Write-InstallMessage 'Installation attempt completed. Send the Desktop diagnostic ZIP for analysis.'
        Write-Information "Install log: $logPath"
        return 0
    } catch {
        $message = "INSTALLATION STOPPED: $($_.Exception.Message)"
        Write-Information $message
        try {
            "[$(Get-Date -Format 'yyyy-MM-dd HH:mm:ss')] $message" | Add-Content -LiteralPath $logPath -Encoding UTF8
        } catch {
            Write-Warning "Could not update the install log: $($_.Exception.Message)"
        }
        Write-Information "Install log: $logPath"
        return 1
    } finally {
        if (-not $DoNotPause) { [void](Read-Host 'Press Enter to close') }
    }
}

if (-not $LibraryOnly) {
    $exitCode = Invoke-Rpi5DriverInstall -DoNotPause:$NoPause
    exit $exitCode
}
