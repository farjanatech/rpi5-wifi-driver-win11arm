[CmdletBinding()]
param(
    [switch]$LibraryOnly,
    [switch]$NoPause,
    [string]$OutputDirectory
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$InformationPreference = 'Continue'
$script:UtilityVersion = '0.6.21'

function Invoke-Rpi5ReadOnlyCapture {
    param([Parameter(Mandatory=$true)][scriptblock]$Command)
    $lines = [Collections.Generic.List[string]]::new()
    $failure = $null
    try {
        & $Command 2>&1 | Out-String -Width 500 -Stream |
            ForEach-Object { $lines.Add([string]$_) }
    } catch {
        $failure = $_.Exception.Message
        $lines.Add("COLLECTION ERROR: $($_.Exception.GetType().FullName): $failure")
    }
    [pscustomobject]@{ Text=($lines -join [Environment]::NewLine); Failure=$failure }
}

function Get-Rpi5ProbeResult {
    param([AllowNull()]$Diagnostic, [string]$ServiceStatus, [datetime]$BootTime)
    if ($null -eq $Diagnostic) { return 'No saved driver diagnostics.' }
    if ($ServiceStatus -ne 'Running') {
        return 'Driver is not running. Saved probe results may be stale; check device enable/problem status.'
    }
    $stamp = Get-Rpi5PropertyValue $Diagnostic 'SnapshotTimeUtc' -Default 0
    try { $snapshot = [datetime]::FromFileTimeUtc([int64]$stamp) }
    catch { return 'Invalid diagnostic timestamp; cannot verify this test is current.' }
    if ($snapshot -lt $BootTime.ToUniversalTime()) {
        return 'Saved results predate this boot or have no timestamp; rerun with the updated driver.'
    }
    $phase = Get-Rpi5PropertyValue $Diagnostic 'ProbePhase' -Default 0
    $networkPhase = Get-Rpi5PropertyValue $Diagnostic 'NetworkPhase' -Default 0
    if ($networkPhase -ge 400) {
        $networkStatus = ConvertTo-Rpi5Hex32 (Get-Rpi5PropertyValue $Diagnostic 'NetworkStatus')
        $connectStep = Get-Rpi5PropertyValue $Diagnostic 'ConnectStep' -Default 0
        return "Integrated candidate: NetworkPhase=$networkPhase status=$networkStatus ConnectStep=$connectStep. Authenticated link, IP address and traffic still require separate confirmation."
    }
    $status = ConvertTo-Rpi5Hex32 (Get-Rpi5PropertyValue $Diagnostic 'LastStatus' -Default $null)
    $restored = ConvertTo-Rpi5Hex32 (Get-Rpi5PropertyValue $Diagnostic 'ProbeRestoreStatus' -Default $null)
    $reads = Get-Rpi5PropertyValue $Diagnostic 'Cmd53ReadCount' -Default 0
    $chip = Get-Rpi5PropertyValue $Diagnostic 'ChipId' -Default 0
    $inventory = Get-Rpi5PropertyValue $Diagnostic 'CoreInventoryComplete' -Default 0
    $writes = Get-Rpi5PropertyValue $Diagnostic 'Cmd53WriteCount' -Default -1
    if ($phase -eq 350 -and $status -eq '0x00000000' -and
        $restored -eq '0x00000000' -and $reads -gt 16 -and $chip -eq 0x4345 -and
        $inventory -eq 1 -and $writes -eq 0) {
        return 'PASS: core inventory and restoration completed. RAM size, firmware upload and Wi-Fi are NOT validated.'
    }
    if ($phase -eq 250 -and $status -eq '0x00000000' -and
        $restored -eq '0x00000000' -and $reads -eq 16 -and $chip -eq 0x4345) {
        return 'PASS: 16 matching CMD53 chip-ID reads and restoration completed. This is not working Wi-Fi.'
    }
    return "Probe not complete: phase=$phase status=$status restoration=$restored. Inspect controller registers."
}

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

function Get-Rpi5DeviceByAcpiId {
    param([Parameter(Mandatory=$true)][string]$AcpiId)

    $pattern = '^ACPI\\' + [regex]::Escape($AcpiId) + '(?:\\|$)'
    $device = Get-PnpDevice -PresentOnly:$false -ErrorAction SilentlyContinue |
        Where-Object { $_.InstanceId -match $pattern } |
        Select-Object -First 1
    if ($device) { return $device }

    $cimDevice = Get-CimInstance Win32_PnPEntity -ErrorAction SilentlyContinue |
        Where-Object { $_.PNPDeviceID -match $pattern } |
        Select-Object -First 1
    if (-not $cimDevice) { return $null }

    return [pscustomobject]@{
        InstanceId = $cimDevice.PNPDeviceID
        Status = $cimDevice.Status
        Class = $cimDevice.PNPClass
        FriendlyName = $cimDevice.Name
        Problem = $cimDevice.ConfigManagerErrorCode
        ConfigManagerErrorCode = $cimDevice.ConfigManagerErrorCode
        DiscoverySource = 'Win32_PnPEntity fallback'
    }
}

function Get-Rpi5SetupApiExcerpt {
    param([Parameter(Mandatory=$true)][string]$Path)
    if (-not (Test-Path -LiteralPath $Path)) { return 'SetupAPI device log was not found.' }

    $setupMatches = Select-String -LiteralPath $Path -Pattern 'RPI0011|rpi5cyw|CYW43455|Direct SDIO' -Context 10,18 -ErrorAction SilentlyContinue
    if (-not $setupMatches) { return 'No RPI0011/rpi5cyw entries were found in SetupAPI device log.' }
    return (($setupMatches | Select-Object -Last 40 | ForEach-Object { $_.ToString() }) -join [Environment]::NewLine)
}

function Invoke-Rpi5WiFiDiagnostic {
    [CmdletBinding()]
    param(
        [string]$Destination,
        [switch]$DoNotPause
    )

    if (-not (Test-Rpi5Administrator)) {
        if (-not $PSCommandPath) { throw 'Run this collector from its saved script file.' }
        Write-Information 'Administrator permission is required to read driver and event diagnostics.'
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
        Write-Information "Collecting $Name ..."
        $capture = Invoke-Rpi5ReadOnlyCapture -Command $Command
        if ($capture.Failure) { $collectionErrors.Add("$Name`: $($capture.Failure)") }
        Protect-Rpi5DiagnosticText -Text $capture.Text |
            Set-Content -LiteralPath (Join-Path $work $Name) -Encoding UTF8
    }

    try {
        Write-Information "RPi5 Wi-Fi diagnostics utility v$script:UtilityVersion"
        Write-Information 'This utility only reads system state and writes a diagnostic ZIP.'

        $bootText = (& bcdedit.exe /enum '{current}' 2>&1 | Out-String -Width 500)
        $testSigning = if ($bootText -match '(?im)^\s*testsigning\s+Yes\s*$') { 'Enabled' } else { 'Disabled or not reported' }
        $secureBoot = Get-Rpi5SecureBootState
        $architecture = [Runtime.InteropServices.RuntimeInformation]::OSArchitecture.ToString()
        $targetDevice = Get-Rpi5DeviceByAcpiId -AcpiId 'RPI0011'
        $fanDevice = Get-Rpi5DeviceByAcpiId -AcpiId 'RPI000F'
        $temperatureDevice = Get-Rpi5DeviceByAcpiId -AcpiId 'RPI0010'
        $diagKey = 'HKLM:\SOFTWARE\Rpi5CywDirectDiag'
        $diag = if (Test-Path -LiteralPath $diagKey) { Get-ItemProperty -LiteralPath $diagKey -ErrorAction SilentlyContinue } else { $null }
        $stage = Get-Rpi5PropertyValue -Object $diag -Name 'Stage'
        $lastStatusValue = Get-Rpi5PropertyValue -Object $diag -Name 'LastStatus' -Default $null
        $lastStatus = ConvertTo-Rpi5Hex32 -Value $lastStatusValue
        $cmd5AttemptCount = Get-Rpi5PropertyValue -Object $diag -Name 'Cmd5AttemptCount' -Default 0
        $cmd5ValidAttempt = Get-Rpi5PropertyValue -Object $diag -Name 'Cmd5ValidAttempt' -Default 0
        $cmd5SuccessAttempt = Get-Rpi5PropertyValue -Object $diag -Name 'Cmd5SuccessAttempt' -Default 0
        $driverService = Get-Service -Name rpi5cyw -ErrorAction SilentlyContinue
        $serviceStatus = [string](Get-Rpi5PropertyValue $driverService 'Status' -Default 'Unavailable')
        $bootTime = (Get-CimInstance Win32_OperatingSystem).LastBootUpTime
        $probeResult = Get-Rpi5ProbeResult $diag $serviceStatus $bootTime

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
            "DriverService=$serviceStatus"
            "ProbePhase=$(Get-Rpi5PropertyValue $diag 'ProbePhase')"
            "SnapshotTimeUtc=$(Get-Rpi5PropertyValue $diag 'SnapshotTimeUtc')"
            "ChipId=$(ConvertTo-Rpi5Hex32 (Get-Rpi5PropertyValue $diag 'ChipId' -Default $null))"
            "Cmd53ReadCount=$(Get-Rpi5PropertyValue $diag 'Cmd53ReadCount')"
            "LastStatus=$lastStatus"
            "Cmd5AttemptCount=$cmd5AttemptCount"
            "Cmd5ValidAttempt=$cmd5ValidAttempt"
            "Cmd5SuccessAttempt=$cmd5SuccessAttempt"
            "Result=$probeResult"
            ''
            'Probe-only releases remain disconnected. Integrated candidates need authenticated link, IP and traffic validation.'
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
            @($fanDevice, $temperatureDevice, $targetDevice) |
                Where-Object { $null -ne $_ } |
                Format-List Status,Class,FriendlyName,InstanceId,Problem,ConfigManagerErrorCode,DiscoverySource
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
                    'Capabilities','Capabilities2','PresentState','ClockControl','PowerControl',
                    'HostControl','HostControl2','TimeoutControl','SoftwareReset',
                    'LastCommand','LastArgument','LastInterruptStatus','LastResponse',
                    'LastCommandResetStatus','Cmd5AttemptCount','Cmd5ValidAttempt','Cmd5SuccessAttempt',
                    'Cmd5ProbeResponse','SdioOcr','SdioFunctions','RelativeAddress',
                    'CccrRevision','IoEnable','IoReady','F1InterfaceCode','F2InterfaceCode',
                    'BusModeStage','BusTargetKhz','BusActualKhz','BusWidth','BusCardInterface',
                    'BusCardSpeed','BusVerifyReads','BusUpgradeStatus','BusRecoveryStatus','BusVerifyStatus',
                    'DiagVersion','ProbePhase','Function1Ready','ChipClockCsr','ChipIdRaw',
                    'ChipId','ChipRevision','Cmd53ReadCount','Cmd53BytesTransferred',
                    'Cmd53ResetStatus','ProbeRestoreStatus','Cmd53WriteCount',
                    'EromAddress','EromWords','CoreCount','ChipCommonBase','SdioCoreBase',
                    'D11CoreBase','Cr4CoreBase','Cr4WrapperBase','Cr4Capabilities',
                    'Cr4IoControl','Cr4ResetControl','RamBankCount','RamBase','CoreInventoryComplete',
                    'NetworkPhase','NetworkStatus','FirmwareCommand','FirmwareError','FirmwareBytes',
                    'FirmwareReplyLength','FirmwareReplyDeclaredLength','FirmwareReplyPayloadLength',
                    'FirmwareRequestCapacity','FirmwareValueLength',
                    'FirmwareTotalBytes','FirmwareUploadedBytes','RamTransferStatus','RamTransferStage',
                    'ConnectStep','CountryRequested','CountryApplied','CountryRevision',
                    'CountryBefore','CountryBeforeRevision','CountrySetMode','CountryExplicitError',
                    'ClmLoadStatus','ClmQueryStatus',
                    'CountryListStatus','CountryListError','CountryListCount',
                    'CountryListMembership','CountryListReplyLength',
                    'RamTransferAddress','RamTransferLength','RamTransferWrite',
                    'RamSize','LinkEvent','LinkReason','TxPackets','RxPackets'
                )
                foreach ($name in $names) {
                    $value = Get-Rpi5PropertyValue -Object $diag -Name $name
                    if ($name -match 'Status|Capabilities|Command|Argument|Response|Ocr|Phys|Length|Version|Address|Revision|Enable|Ready|Code') {
                        "{0}={1} ({2})" -f $name,$value,(ConvertTo-Rpi5Hex32 -Value $value)
                    } else {
                        "{0}={1}" -f $name,$value
                    }
                }
                ''
                'Bounded CMD5 attempts (zero values after Cmd5AttemptCount were not executed):'
                for ($attemptNumber = 1; $attemptNumber -le 18; $attemptNumber++) {
                    $prefix = "Cmd5Attempt$attemptNumber"
                    $clock = Get-Rpi5PropertyValue -Object $diag -Name "${prefix}ClockKhz" -Default 0
                    $argument = Get-Rpi5PropertyValue -Object $diag -Name "${prefix}Argument" -Default 0
                    $status = Get-Rpi5PropertyValue -Object $diag -Name "${prefix}Status" -Default 0
                    $resetStatus = Get-Rpi5PropertyValue -Object $diag -Name "${prefix}ResetStatus" -Default 0
                    $responseValid = Get-Rpi5PropertyValue -Object $diag -Name "${prefix}ResponseValid" -Default 0
                    $interrupt = Get-Rpi5PropertyValue -Object $diag -Name "${prefix}InterruptStatus" -Default 0
                    $response = Get-Rpi5PropertyValue -Object $diag -Name "${prefix}Response" -Default 0
                    $presentBefore = Get-Rpi5PropertyValue -Object $diag -Name "${prefix}PresentStateBefore" -Default 0
                    $presentAfter = Get-Rpi5PropertyValue -Object $diag -Name "${prefix}PresentStateAfter" -Default 0
                    $clockBefore = Get-Rpi5PropertyValue -Object $diag -Name "${prefix}ClockControlBefore" -Default 0
                    $clockAfter = Get-Rpi5PropertyValue -Object $diag -Name "${prefix}ClockControlAfter" -Default 0
                    $powerBefore = Get-Rpi5PropertyValue -Object $diag -Name "${prefix}PowerControlBefore" -Default 0
                    $powerAfter = Get-Rpi5PropertyValue -Object $diag -Name "${prefix}PowerControlAfter" -Default 0
                    $hostBefore = Get-Rpi5PropertyValue -Object $diag -Name "${prefix}HostControlBefore" -Default 0
                    $hostAfter = Get-Rpi5PropertyValue -Object $diag -Name "${prefix}HostControlAfter" -Default 0
                    $host2Before = Get-Rpi5PropertyValue -Object $diag -Name "${prefix}HostControl2Before" -Default 0
                    $host2After = Get-Rpi5PropertyValue -Object $diag -Name "${prefix}HostControl2After" -Default 0
                    $timeoutBefore = Get-Rpi5PropertyValue -Object $diag -Name "${prefix}TimeoutControlBefore" -Default 0
                    $timeoutAfter = Get-Rpi5PropertyValue -Object $diag -Name "${prefix}TimeoutControlAfter" -Default 0
                    $cmdLineBefore = if (([uint32]$presentBefore -band 0x01000000) -ne 0) { 'High' } else { 'Low' }
                    $cmdLineAfter = if (([uint32]$presentAfter -band 0x01000000) -ne 0) { 'High' } else { 'Low' }
                    ('Attempt={0} ClockKhz={1} Argument={2} ResponseValid={3} Status={4} ResetStatus={5} Interrupt={6} Response={7} CmdLineBefore={8} CmdLineAfter={9} PresentBefore={10} PresentAfter={11} ClockBefore={12} ClockAfter={13} PowerBefore={14} PowerAfter={15} HostBefore={16} HostAfter={17} Host2Before={18} Host2After={19} TimeoutBefore={20} TimeoutAfter={21}' -f
                        $attemptNumber,$clock,(ConvertTo-Rpi5Hex32 $argument),$responseValid,
                        (ConvertTo-Rpi5Hex32 $status),(ConvertTo-Rpi5Hex32 $resetStatus),
                        (ConvertTo-Rpi5Hex32 $interrupt),(ConvertTo-Rpi5Hex32 $response),
                        $cmdLineBefore,$cmdLineAfter,(ConvertTo-Rpi5Hex32 $presentBefore),(ConvertTo-Rpi5Hex32 $presentAfter),
                        (ConvertTo-Rpi5Hex32 $clockBefore),(ConvertTo-Rpi5Hex32 $clockAfter),
                        (ConvertTo-Rpi5Hex32 $powerBefore),(ConvertTo-Rpi5Hex32 $powerAfter),
                        (ConvertTo-Rpi5Hex32 $hostBefore),(ConvertTo-Rpi5Hex32 $hostAfter),
                        (ConvertTo-Rpi5Hex32 $host2Before),(ConvertTo-Rpi5Hex32 $host2After),
                        (ConvertTo-Rpi5Hex32 $timeoutBefore),(ConvertTo-Rpi5Hex32 $timeoutAfter))
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
        Write-Capture '16-ip-routing-dns.txt' {
            # Read-only snapshots, no DNS changes, DHCP renew or external probes.
            # Capture all IPv4 routes to expose competing default interfaces.
            Get-NetRoute -AddressFamily IPv4 -ErrorAction Stop |
                Sort-Object InterfaceIndex,DestinationPrefix |
                Format-Table InterfaceIndex,DestinationPrefix,NextHop,RouteMetric,State -AutoSize
            $wifiAdapters = @(Get-NetAdapter -IncludeHidden -ErrorAction Stop |
                Where-Object { $_.PnPDeviceID -match 'RPI0011' -or $_.InterfaceDescription -match 'CYW43455|Direct SDIO' })
            foreach ($wifiAdapter in $wifiAdapters) {
                $index = $wifiAdapter.ifIndex
                Get-NetIPInterface -InterfaceIndex $index -ErrorAction Stop |
                    Format-List InterfaceAlias,InterfaceIndex,AddressFamily,ConnectionState,Dhcp,NlMtu,InterfaceMetric
                Get-NetIPAddress -InterfaceIndex $index -ErrorAction Stop |
                    Format-List IPAddress,PrefixLength,AddressFamily,AddressState,PrefixOrigin,SuffixOrigin
                Get-DnsClientServerAddress -InterfaceIndex $index -ErrorAction Stop |
                    Format-List InterfaceIndex,AddressFamily,ServerAddresses
                Get-NetNeighbor -InterfaceIndex $index -ErrorAction Stop |
                    Format-Table IPAddress,LinkLayerAddress,State -AutoSize
            }
        }
        Write-Capture '17-optional-windows-statistics.txt' {
            # Not implemented by every experimental miniport. Never discard
            # configuration/route evidence when this optional query fails.
            try {
                Get-NetAdapter -IncludeHidden -ErrorAction Stop |
                    Where-Object { $_.PnPDeviceID -match 'RPI0011' -or $_.InterfaceDescription -match 'CYW43455|Direct SDIO' } |
                    ForEach-Object { Get-NetAdapterStatistics -Name $_.Name -ErrorAction Stop } |
                    Format-List *
            } catch {
                "Optional Windows adapter statistics unavailable: $($_.Exception.Message)"
                'Use TxPackets/RxPackets and queue counters in 05-driver-registry.txt instead.'
            }
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
        Write-Capture '15-live-driver-status.txt' {
            $liveUtility = Join-Path $PSScriptRoot 'Connect-RPi5-WiFi.ps1'
            if (Test-Path -LiteralPath $liveUtility) {
                # StatusOnly issues only the read-only status IOCTL; no connect,
                # disconnect, SDIO operation or reboot is requested.
                & (Join-Path $PSHOME 'powershell.exe') -NoProfile -ExecutionPolicy Bypass -File $liveUtility -StatusOnly
                if ($LASTEXITCODE -ne 0) { "Live status utility exit code: $LASTEXITCODE" }
            } else { 'Live status utility missing; saved registry snapshots may be older than current progress.' }
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
        Write-Information ''
        Write-Information 'Diagnostics completed successfully.'
        Write-Information "ZIP: $zip"
        Write-Information "SHA256: $zipHash"
        Write-Information 'Attach this ZIP for analysis. It contains no saved Wi-Fi passwords or dump contents.'
        return 0
    } catch {
        Write-Information ''
        Write-Information "Diagnostics failed: $($_.Exception.Message)"
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
