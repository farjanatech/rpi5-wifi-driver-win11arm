param([switch]$NativeSmoke)
Set-StrictMode -Version Latest
$ErrorActionPreference='Stop'
$clockPath=Join-Path (Split-Path -Parent $PSScriptRoot) 'utility\RPi5-WiFi-MeasurementClock.ps1'

function Get-MeasurementClockFixture {
    # Each fixture has isolated script-scope state. Native compilation is
    # forbidden here, including at import; the real API is an explicit CI test.
    return New-Module -ArgumentList $clockPath -ScriptBlock {
        param($Path)
        Set-StrictMode -Version Latest
        $script:NativeAttempts=0
        $script:ReaderCalls=0
        $script:ReadValues=@()
        function Add-Type {
            [Diagnostics.CodeAnalysis.SuppressMessageAttribute('PSAvoidOverwritingBuiltInCmdlets','',Justification='This isolated clock fixture must prevent native compilation and API loading.')]
            [CmdletBinding()]
            param([string]$TypeDefinition)
            $null=$TypeDefinition
            $script:NativeAttempts++
            throw 'Native API must not be used by a mocked fixture.'
        }
        . $Path
        function Initialize-TestClock {
            param([object[]]$Values)
            $script:ReadValues=$Values
            Initialize-Rpi5MeasurementClock -ReadClock {
                $index=$script:ReaderCalls
                $script:ReaderCalls++
                if($index -ge $script:ReadValues.Count) { throw 'Unexpected extra clock read.' }
                $value=$script:ReadValues[$index]
                if($value -is [Exception]) { throw $value }
                return $value
            }
        }
        # New-Module exports functions into its caller by default. Keep mocks
        # private so Add-Type cannot contaminate the later real-API smoke.
        Export-ModuleMember -Function @()
    }
}

$fixture=Get-MeasurementClockFixture
if($fixture.ExportedFunctions.Count -ne 0) { throw 'Clock fixture exported a mock into the caller.' }
& $fixture {
    if($script:NativeAttempts -ne 0 -or $script:ReaderCalls -ne 0) { throw 'Import queried a native clock.' }
    if($null -ne (Get-Rpi5MeasurementTimestamp)) { throw 'Uninitialized clock is not unknown.' }
    if($script:NativeAttempts -ne 0) { throw 'Get implicitly initialized the native API.' }
    $output=@(Initialize-TestClock @([uint64]0,[uint64]0,[uint64]156250,[uint64]156250))
    if($output.Count -ne 0 -or $script:ReaderCalls -ne 1) { throw 'Initialization output/probe count incorrect.' }
    foreach($expected in @([int64]0,[int64]156250,[int64]156250)) {
        $actual=Get-Rpi5MeasurementTimestamp
        if($actual -isnot [int64] -or $actual -ne $expected) { throw 'Raw zero/equal tick/scalar Int64 changed.' }
    }
}

$fixture=Get-MeasurementClockFixture
& $fixture {
    Initialize-TestClock @([uint64]9007199254740993,[uint64]9007199254740994,[uint64][int64]::MaxValue)
    if((Get-Rpi5MeasurementTimestamp) -ne [int64]9007199254740994) { throw 'Counter lost integer precision.' }
    if((Get-Rpi5MeasurementTimestamp) -ne [int64]::MaxValue) { throw 'Int64 maximum was not preserved.' }
}

foreach($bad in @($null,[int64]-1,[uint64]::MaxValue,[double]100.5,[decimal]100,'100',
                  $true,[pscustomobject]@{Value=100},[Exception]::new('Mock read failure'))) {
    $fixture=Get-MeasurementClockFixture
    & $fixture {
        param($BadValue)
        Initialize-TestClock @([uint64]100,$BadValue,[uint64]200)
        if($null -ne (Get-Rpi5MeasurementTimestamp)) { throw 'Invalid reading was converted to a measured timestamp.' }
        if($null -ne (Get-Rpi5MeasurementTimestamp) -or $script:ReaderCalls -ne 2) {
            throw 'Failed clock resumed or queried again.'
        }
    } $bad
}

$fixture=Get-MeasurementClockFixture
& $fixture {
    Initialize-TestClock @([uint64]100,[uint64]99,[uint64]101)
    if($null -ne (Get-Rpi5MeasurementTimestamp)) { throw 'Backward/wrapped clock was accepted.' }
    Initialize-Rpi5MeasurementClock -ReadClock { throw 'A latched failure must not reinitialize.' }
    if($null -ne (Get-Rpi5MeasurementTimestamp) -or $script:ReaderCalls -ne 2) { throw 'Backward failure was not latched.' }
}

$fixture=Get-MeasurementClockFixture
& $fixture {
    # Multiple pipeline objects must not masquerade as one native reading.
    Initialize-Rpi5MeasurementClock -ReadClock { [uint64]100; [uint64]101 }
    if($null -ne (Get-Rpi5MeasurementTimestamp)) { throw 'Multiple reader outputs were accepted.' }
}

$fixture=Get-MeasurementClockFixture
& $fixture {
    Initialize-TestClock @($null,[uint64]100)
    if($null -ne (Get-Rpi5MeasurementTimestamp) -or $script:ReaderCalls -ne 1) { throw 'Failed initial probe was not latched.' }
}

$fixture=Get-MeasurementClockFixture
& $fixture {
    # Force a failed initial read even if a caller already loaded the type.
    Initialize-Rpi5MeasurementClock -ReadClock { $script:ReaderCalls++; throw 'Mock initial read failed.' }
    Initialize-Rpi5MeasurementClock -ReadClock { throw 'Initialization must not retry.' }
    if($null -ne (Get-Rpi5MeasurementTimestamp) -or $script:ReaderCalls -ne 1) {
        throw 'Initial read failure was retried or reported as zero.'
    }
}

# Also exercise a failed Add-Type when no real type exists yet. Do not invoke
# the real API implicitly when this test is rerun in an initialized process.
if(-not ('Rpi5WifiMeasurementClockNativeV1' -as [type])) {
    $fixture=Get-MeasurementClockFixture
    & $fixture {
        Initialize-Rpi5MeasurementClock
        Initialize-Rpi5MeasurementClock
        if($null -ne (Get-Rpi5MeasurementTimestamp) -or $script:NativeAttempts -ne 1) {
            throw 'Native initialization failure was retried or reported as zero.'
        }
    }
}

$fixture=Get-MeasurementClockFixture
& $fixture {
    param($Path)
    Initialize-TestClock @([uint64]100,[uint64]110)
    . $Path
    $output=@(Initialize-Rpi5MeasurementClock -ReadClock { throw 'Existing clock must not be replaced.' })
    if($output.Count -ne 0 -or (Get-Rpi5MeasurementTimestamp) -ne 110 -or $script:ReaderCalls -ne 2) {
        throw 'Re-import/re-initialization replaced or reset the clock.'
    }
} $clockPath

if($NativeSmoke) {
    if($env:GITHUB_ACTIONS -ne 'true') { throw 'Native smoke is only authorized in GitHub Actions.' }
    $native=New-Module -ArgumentList $clockPath -ScriptBlock {
        param($Path)
        . $Path
        Export-ModuleMember -Function @()
    }
    if($native.ExportedFunctions.Count -ne 0) { throw 'Native clock fixture exported helper functions.' }
    & $native {
        $binding=Get-Command -Name Add-Type -ErrorAction Stop
        if($binding.CommandType -ne 'Cmdlet' -or $binding.ModuleName -ne 'Microsoft.PowerShell.Utility') {
            throw ('Native clock smoke resolved unexpected Add-Type: {0} from {1}.' -f
                $binding.CommandType,$binding.ModuleName)
        }
        $output=@(Initialize-Rpi5MeasurementClock)
        $first=Get-Rpi5MeasurementTimestamp
        $second=Get-Rpi5MeasurementTimestamp
        if($output.Count -ne 0 -or $first -isnot [int64] -or $second -isnot [int64] -or
           $first -lt 0 -or $second -lt $first) {
            $firstType=if($null -eq $first){'<null>'}else{$first.GetType().FullName}
            $secondType=if($null -eq $second){'<null>'}else{$second.GetType().FullName}
            # Initialization intentionally returns unknown to production callers
            # on failure. CI must fail loudly and retain the exception chain.
            $failure=if($Error.Count -gt 0){$Error[0].Exception.ToString()}else{'No error record.'}
            throw ('Native boot interrupt-time smoke failed: output={0}, first={1} ({2}), second={3} ({4}); Add-Type={5}/{6}; latest exception: {7}' -f
                $output.Count,$first,$firstType,$second,$secondType,$binding.CommandType,$binding.ModuleName,$failure)
        }
        # Adjacent calls can share the same clock tick. No sleep or resolution
        # change is needed, and strict increase would be an invalid requirement.
    }
}
Write-Output 'Measurement clock tests passed (mocked; optional native smoke is GitHub-only).'
