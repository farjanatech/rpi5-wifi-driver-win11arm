[CmdletBinding()]
param(
    [switch]$LibraryOnly,
    [switch]$NoPause,
    [string]$OutputDirectory
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$script:UtilityVersion = '0.2.0'

function Test-Rpi5Administrator {
    $identity = [Security.Principal.WindowsIdentity]::GetCurrent()
    $principal = [Security.Principal.WindowsPrincipal]::new($identity)
    return $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
}

function Protect-Rpi5DiagnosticText {
    param([AllowNull()][string]$Text)

    if ($null -eq $Text) { return '' }
    $result = $Text
    $replacements = [ordered]@{}
    if ($env:USERPROFILE) { $replacements[$env:USERPROFILE] = '<REDACTED_USER_PROFILE>' }
    if ($env:USERNAME) { $replacements[$env:USERNAME] = '<REDACTED_USER>' }
    if ($env:COMPUTERNAME) { $replacements[$env:COMPUTERNAME] = '<REDACTED_COMPUTER>' }

    foreach ($entry in $replacements.GetEnumerator()) {
        $result = [regex]::Replace(
            $result,
            [regex]::Escape([string]$entry.Key),
            [string]$entry.Value,
            [Text.RegularExpressions.RegexOptions]::IgnoreCase)
    }

    # Adapter addresses are not required for this host-controller probe.
    $result = [regex]::Replace(
        $result,
        '(?i)(?<![0-9a-f])(?:[0-9a-f]{2}[:-]){5}[0-9a-f]{2}(?![0-9a-f])',
        '<REDACTED_MAC>')
    return $result
}

function ConvertTo-Rpi5Hex32 {
    param([AllowNull()]$Value)
    if ($null -eq $Value) { return '<missing>' }
    try {
        $number = [uint32]([int64]$Value -band 0xFFFFFFFFL)
        return ('0x{0:X8}' -f $number)
    } catch {
        return '<invalid>'
    }
}

function Get-Rpi5PropertyValue {
    param(
        [AllowNull()]$Object,
        [Parameter(Mandatory=$true)][string]$Name,
        $Default = '<missing>'
    )
    if ($null -eq $Object) { return $Default }
    $property = $Object.PSObject.Properties[$Name]
    if ($null -eq $property -or $null -eq $property.Value) { return $Default }
    return $property.Value
}

function Get-Rpi5SecureBootState {
    try {
        if (Confirm-SecureBootUEFI -ErrorAction Stop) { return 'Enabled' }
        return 'Disabled'
    } catch {
        return "Unavailable ($($_.Exception.GetType().Name))"
    }
}

function Get-Rpi5SetupApiExcerpt {
    param([Parameter(Mandatory=$true)][string]$Path)
    if (-not (Test-Path -LiteralPath $Path)) { return 'SetupAPI device log was not found.' }

    $matches = Select-String -LiteralPath $Path -Pattern 'RPI0011|rpi5cyw|CYW43455|Direct SDIO' -Context 10,18 -ErrorAction SilentlyContinue
    if (-not $matches) { return 'No RPI0011/rpi5cyw entries were found in SetupAPI device log.' }
    return (($matches | Select-Object -Last 40 | ForEach-Object { $_.ToString() }) -join [Environment]::NewLine)
}

function Invoke-Rpi5WiFiDiagnostic {
    [CmdletBinding()]
    param(
        [string]$Destination,
        [switch]$DoNotPause
    )

    if (-not (Test-Rpi5Administrator)) {
        if (-not $PSCommandPath) { throw 'Run this collector from its saved script file.' }
        Write-Host 'Administrator permission is required to read driver and event diagnostics.' -ForegroundColor Yellow
        $quotedScript = $PSCommandPath.Replace('"', '""')
        $arguments = "-NoProfile -ExecutionPolicy Bypass -File `"$quotedScript`""
        if ($Destination) {
            $quotedDestination = $Destination.Replace('"', '""')
            $arguments += " -OutputDirectory `"$quotedDestination`""
        }
        if ($DoNotPause) { $arguments += ' -NoPause' }
        $elevated = Start-Process -FilePath 'powershell.exe' -Verb RunAs -ArgumentList $arguments -Wait -PassThru
        return $elevated.ExitCode
    }

    $stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
    $tempBase = [IO.Path]::GetFullPath($env:TEMP).TrimEnd('\')
    $work = Join-Path $tempBase "RPI5-WIFI-DIAG-$stamp-$PID"
    New-Item -ItemType Directory -Path $work -Force | Out-Null

    if (-not $Destination) {
        $Destination = [Environment]::GetFolderPath([Environment+SpecialFolder]::DesktopDirectory)
    }
    if (-not $Destination -or -not (Test-Path -LiteralPath $Destination -PathType Container)) {
        $Destination = Split-Path -Parent $PSCommandPath
    }
    $zip = Join-Path $Destination "RPI5-CYW43455-DIRECT-SDIO-DIAGNOSTICS-$stamp.zip"
    $collectionErrors = [Collections.Generic.List[string]]::new()

    function Write-Capture {
        param(
            [Parameter(Mandatory=$true)][string]$Name,
            [Parameter(Mandatory=$true)][scriptblock]$Command
        )
        Write-Host "Collecting $Name ..."
        try {
            $text = (& $Command 2>&1 | Out-String -Width 500)
        } catch {
            $text = "COLLECTION ERROR: $($_.Exception.GetType().FullName): $($_.Exception.Message)"
            $collectionErrors.Add("$Name`: $($_.Exception.Message)")
        }
        Protect-Rpi5DiagnosticText -Text $text |
            Set-Content -LiteralPath (Join-Path $work $Name) -Encoding UTF8
    }

    try {
        Write-Host "RPi5 Wi-Fi diagnostics utility v$script:UtilityVersion" -ForegroundColor Cyan
        Write-Host 'This utility only reads system state and writes a diagnostic ZIP.'

        $bootText = (& bcdedit.exe /enum '{current}' 2>&1 | Out-String -Width 500)
        $testSigning = if ($bootText -match '(?im)^\s*testsigning\s+Yes\s*$') { 'Enabled' } else { 'Disabled or not reported' }
        $secureBoot = Get-Rpi5SecureBootState
        $architecture = [Runtime.InteropServices.RuntimeInformation]::OSArchitecture.ToString()
        $targetDevice = Get-PnpDevice -PresentOnly:$false -ErrorAction SilentlyContinue |
            Where-Object { $_.InstanceId -match '^ACPI\\RPI0011(?:\\|$)' } |
            Select-Object -First 1
        $fanDevice = Get-PnpDevice -PresentOnly:$false -ErrorAction SilentlyContinue |
            Where-Object { $_.InstanceId -match '^ACPI\\RPI000F(?:\\|$)' } |
            Select-Object -First 1
        $temperatureDevice = Get-PnpDevice -PresentOnly:$false -ErrorAction SilentlyContinue |
            Where-Object { $_.InstanceId -match '^ACPI\\RPI0010(?:\\|$)' } |
            Select-Object -First 1
        $diagKey = 'HKLM:\SOFTWARE\Rpi5CywDirectDiag'
        $diag = if (Test-Path -LiteralPath $diagKey) { Get-ItemProperty -LiteralPath $diagKey -ErrorAction SilentlyContinue } else { $null }
        $stage = Get-Rpi5PropertyValue -Object $diag -Name 'Stage'
        $lastStatusValue = Get-Rpi5PropertyValue -Object $diag -Name 'LastStatus' -Default $null
        $lastStatus = ConvertTo-Rpi5Hex32 -Value $lastStatusValue
        $probeResult = 'Driver diagnostic registry data is not present.'
        if ($null -ne $diag) {
            $stageNumber = 0
            [void][int]::TryParse([string]$stage, [ref]$stageNumber)
            if ($stageNumber -ge 90 -and $lastStatus -eq '0x00000000') {
                $probeResult = 'CMD52 path reached successfully. This is not working Wi-Fi.'
            } else {
                $probeResult = 'Probe did not complete CMD52 reads. Inspect controller-registers.txt and driver-registry.txt.'
            }
        }

        $summary = @(
            'Raspberry Pi 5 CYW43455 direct-SDIO diagnostic summary'
            "CollectorVersion=$script:UtilityVersion"
            "CollectedUtc=$((Get-Date).ToUniversalTime().ToString('o'))"
            "OSArchitecture=$architecture"
            "SecureBoot=$secureBoot"
            "TestSigning=$testSigning"
            "ACPI_RPI0011=$([bool]$targetDevice)"
            "RPI0011_Status=$(Get-Rpi5PropertyValue -Object $targetDevice -Name 'Status' -Default '<not found>')"
            "Fan_ACPI_RPI000F=$([bool]$fanDevice)"
            "Temperature_ACPI_RPI0010=$([bool]$temperatureDevice)"
            "DriverDiagnosticsPresent=$([bool]$diag)"
            "Stage=$stage"
            "LastStatus=$lastStatus"
            "Result=$probeResult"
            ''
            'The probe intentionally remains media-disconnected and does not provide working Wi-Fi.'
        ) -join [Environment]::NewLine
        Protect-Rpi5DiagnosticText -Text $summary |
            Set-Content -LiteralPath (Join-Path $work '00-SUMMARY.txt') -Encoding UTF8

        Write-Capture '01-windows-and-firmware.txt' {
            $os = Get-CimInstance Win32_OperatingSystem
            $bios = Get-CimInstance Win32_BIOS
            $computer = Get-CimInstance Win32_ComputerSystem
            [pscustomobject]@{
                Caption = $os.Caption
                Version = $os.Version
                BuildNumber = $os.BuildNumber
                OSArchitecture = $os.OSArchitecture
                Manufacturer = $computer.Manufacturer
                Model = $computer.Model
                SystemType = $computer.SystemType
                BiosVersion = ($bios.BIOSVersion -join '; ')
                SMBIOSBIOSVersion = $bios.SMBIOSBIOSVersion
                FirmwareType = $env:firmware_type
                CollectorProcessArchitecture = [Runtime.InteropServices.RuntimeInformation]::ProcessArchitecture
            } | Format-List
        }
        Write-Capture '02-boot-security.txt' {
            "SecureBoot=$secureBoot"
            "TestSigning=$testSigning"
            ''
            $bootText
        }
        Write-Capture '03-rpi-acpi-devices.txt' {
            Get-PnpDevice -PresentOnly:$false -ErrorAction SilentlyContinue |
                Where-Object { $_.InstanceId -match '^ACPI\\RPI00(?:0F|10|11)(?:\\|$)' } |
                Format-List Status,Class,FriendlyName,InstanceId,Problem,ConfigManagerErrorCode
        }
        Write-Capture '04-rpi0011-properties.txt' {
            if ($targetDevice) {
                $targetDevice | Format-List *
                Get-PnpDeviceProperty -InstanceId $targetDevice.InstanceId -ErrorAction SilentlyContinue |
                    Sort-Object KeyName | Format-Table KeyName,Type,Data -AutoSize
            } else {
                'ACPI\RPI0011 was not found. The matching experimental UEFI may not be active.'
            }
        }
        Write-Capture '05-driver-registry.txt' {
            reg.exe query 'HKLM\SOFTWARE\Rpi5CywDirectDiag' /s
        }
        Write-Capture '06-controller-registers.txt' {
            if ($diag) {
                $names = @(
                    'Stage','LastStatus','RegPhysHi','RegPhysLo','RegLength','HostVersion',
                    'Capabilities','Capabilities2','LastCommand','LastArgument','LastInterruptStatus',
                    'LastResponse','Cmd5ProbeResponse','SdioOcr','SdioFunctions','RelativeAddress',
                    'CccrRevision','IoEnable','IoReady','F1InterfaceCode','F2InterfaceCode'
                )
                foreach ($name in $names) {
                    $value = Get-Rpi5PropertyValue -Object $diag -Name $name
                    if ($name -match 'Status|Capabilities|Command|Argument|Response|Ocr|Phys|Length|Version|Address|Revision|Enable|Ready|Code') {
                        "{0}={1} ({2})" -f $name,$value,(ConvertTo-Rpi5Hex32 -Value $value)
                    } else {
                        "{0}={1}" -f $name,$value
                    }
                }
            } else {
                'Driver diagnostic registry key was not found.'
            }
        }
        Write-Capture '07-driver-service.txt' {
            sc.exe query rpi5cyw
            sc.exe qc rpi5cyw
            reg.exe query 'HKLM\SYSTEM\CurrentControlSet\Services\rpi5cyw' /s
        }
        Write-Capture '08-installed-driver.txt' {
            Get-CimInstance Win32_PnPSignedDriver -ErrorAction SilentlyContinue |
                Where-Object { $_.DeviceID -match 'RPI0011' -or $_.DriverName -match 'rpi5cyw' -or $_.DeviceName -match 'CYW43455|Direct SDIO' } |
                Format-List DeviceName,DeviceID,DriverName,DriverVersion,DriverProviderName,InfName,IsSigned,Signer
            pnputil.exe /enum-drivers /class Net
        }
        Write-Capture '09-network-adapter.txt' {
            Get-NetAdapter -IncludeHidden -ErrorAction SilentlyContinue |
                Where-Object { $_.PnPDeviceID -match 'RPI0011' -or $_.InterfaceDescription -match 'CYW43455|Direct SDIO' } |
                Format-List Name,InterfaceDescription,Status,LinkSpeed,MediaConnectionState,DriverInformation,DriverFileName,PnPDeviceID
        }
        Write-Capture '10-package-signatures-and-hashes.txt' {
            $packageRoot = Split-Path -Parent $PSCommandPath
            Get-ChildItem -LiteralPath $packageRoot -File -ErrorAction SilentlyContinue |
                Where-Object { $_.Name -match '^rpi5cyw\.(sys|cat|inf)$|^rpi5cyw-test\.cer$' } |
                ForEach-Object {
                    $hash = Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256
                    $signature = Get-AuthenticodeSignature -LiteralPath $_.FullName
                    [pscustomobject]@{
                        File = $_.Name
                        SHA256 = $hash.Hash
                        SignatureStatus = $signature.Status
                        SignerThumbprint = if ($signature.SignerCertificate) { $signature.SignerCertificate.Thumbprint } else { '<none>' }
                    }
                } | Format-Table -AutoSize
        }
        Write-Capture '11-setupapi-rpi5-excerpt.txt' {
            Get-Rpi5SetupApiExcerpt -Path (Join-Path $env:windir 'INF\setupapi.dev.log')
        }
        Write-Capture '12-relevant-system-events.txt' {
            $start = (Get-Date).AddHours(-24)
            Get-WinEvent -FilterHashtable @{ LogName='System'; StartTime=$start } -ErrorAction SilentlyContinue |
                Where-Object {
                    $_.Message -match 'RPI0011|rpi5cyw|CYW43455|Direct SDIO' -or
                    ($_.ProviderName -match 'Kernel-PnP|NDIS|Service Control Manager|ACPI' -and $_.Level -le 3)
                } |
                Select-Object -First 300 TimeCreated,Id,LevelDisplayName,ProviderName,Message |
                Format-List
        }
        Write-Capture '13-problem-devices.txt' {
            Get-PnpDevice -PresentOnly:$false -ErrorAction SilentlyContinue |
                Where-Object {
                    ($_.InstanceId -match '^ACPI\\RPI0011(?:\\|$)' -or $_.FriendlyName -match 'CYW43455|Direct SDIO') -and
                    ($_.Status -ne 'OK' -or $_.Problem -ne 0)
                } | Format-List *
        }
        Write-Capture '14-crash-and-reliability-inventory.txt' {
            'Only dump metadata is collected; dump contents are not included.'
            Get-ChildItem -LiteralPath (Join-Path $env:windir 'Minidump') -File -ErrorAction SilentlyContinue |
                Select-Object Name,Length,LastWriteTime | Sort-Object LastWriteTime -Descending | Select-Object -First 20 |
                Format-Table -AutoSize
            Get-Item -LiteralPath (Join-Path $env:windir 'MEMORY.DMP') -ErrorAction SilentlyContinue |
                Select-Object Name,Length,LastWriteTime | Format-Table -AutoSize
        }

        if ($collectionErrors.Count -gt 0) {
            Protect-Rpi5DiagnosticText -Text ($collectionErrors -join [Environment]::NewLine) |
                Set-Content -LiteralPath (Join-Path $work 'COLLECTION-ERRORS.txt') -Encoding UTF8
        } else {
            'No collector exceptions were recorded.' |
                Set-Content -LiteralPath (Join-Path $work 'COLLECTION-ERRORS.txt') -Encoding UTF8
        }

        @"
Utility=RPi5 Wi-Fi One-Click Diagnostics
Version=$script:UtilityVersion
Mode=Read-only system inspection
Source=https://github.com/farjanatech/rpi5-wifi-driver-win11arm
ExpectedACPI=ACPI\RPI0011
"@ | Set-Content -LiteralPath (Join-Path $work 'UTILITY-INFO.txt') -Encoding UTF8

        Get-ChildItem -LiteralPath $work -File | Sort-Object Name | ForEach-Object {
            $hash = Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256
            "$($hash.Hash)  $($_.Name)"
        } | Set-Content -LiteralPath (Join-Path $work 'SHA256SUMS.txt') -Encoding ASCII

        Compress-Archive -Path (Join-Path $work '*') -DestinationPath $zip -CompressionLevel Optimal -Force
        $zipHash = (Get-FileHash -LiteralPath $zip -Algorithm SHA256).Hash
        Write-Host ''
        Write-Host 'Diagnostics completed successfully.' -ForegroundColor Green
        Write-Host "ZIP: $zip" -ForegroundColor Green
        Write-Host "SHA256: $zipHash"
        Write-Host 'Attach this ZIP for analysis. It contains no saved Wi-Fi passwords or dump contents.'
        return 0
    } catch {
        Write-Host ''
        Write-Host "Diagnostics failed: $($_.Exception.Message)" -ForegroundColor Red
        return 1
    } finally {
        $resolvedWork = [IO.Path]::GetFullPath($work)
        $tempPrefix = $tempBase + [IO.Path]::DirectorySeparatorChar
        if ($resolvedWork.StartsWith($tempPrefix, [StringComparison]::OrdinalIgnoreCase) -and
            (Split-Path -Leaf $resolvedWork) -match '^RPI5-WIFI-DIAG-\d{8}-\d{6}-\d+$') {
            Remove-Item -LiteralPath $resolvedWork -Recurse -Force -ErrorAction SilentlyContinue
        }
        if (-not $DoNotPause) {
            [void](Read-Host 'Press Enter to close')
        }
    }
}

if (-not $LibraryOnly) {
    $exitCode = Invoke-Rpi5WiFiDiagnostic -Destination $OutputDirectory -DoNotPause:$NoPause
    exit $exitCode
}
