Set-StrictMode -Version Latest
$ErrorActionPreference='Stop'
$baseline='a80eab1c44d822c9c8fa10be96e7b769bd0d0b20'
$root=Split-Path -Parent $PSScriptRoot
function Get-ScopeSource {
    param([string]$Path,[switch]$BaselineSource)
    if ($BaselineSource) {
        $lines = & git -C $root show "${baseline}:$Path"
        if ($LASTEXITCODE -ne 0) { throw "Cannot read baseline $Path" }
        return ($lines -join "`n").TrimEnd()
    }
    return (Get-Content -LiteralPath (Join-Path $root $Path) -Raw).Replace("`r`n","`n").TrimEnd()
}
function ConvertTo-ScopeToken {
    param([string]$Text)
    return [regex]::Replace([regex]::Replace($Text,'/\*.*?\*/|//[^\r\n]*','',[Text.RegularExpressions.RegexOptions]::Singleline),'\s+','')
}
$changed=@(& git -C $root diff --name-only $baseline -- src)
if ($LASTEXITCODE -ne 0) { throw 'Cannot compare source scope.' }
$allowed=@('src/cyw43455/connection.h','src/cyw43455/join_preference.h','src/driver/driver.c','src/driver/driver.h')
foreach($file in $changed) { if($file -notin $allowed) { throw "Unexpected driver-source change versus exp0.6.18: $file" } }
foreach($file in @('src/sdio/sdio.c','tests/sdio_host_tests.c','src/cyw43455/network.c','src/cyw43455/tx_queue.h','src/cyw43455/tx_types.h','src/cyw43455/tx_dispatch.h','src/cyw43455/control.h','src/cyw43455/radio.h','utility/Get-RPi5-WiFi-Radio.ps1','utility/Get-RPi5-WiFi-Radio.cmd','utility/Test-RPi5-WiFi-Performance.ps1','utility/Measure-RPi5-WiFi-Load.ps1','utility/Set-RPi5-WiFi-Autoconnect.ps1','utility/WiFi.config.example.json','scripts/fetch-firmware.ps1')) {
    if((Get-ScopeSource $file) -cne (Get-ScopeSource $file -BaselineSource)){throw "Preserved baseline changed: $file"}
}
$expected=Get-ScopeSource 'src/driver/driver.h' -BaselineSource
$actual=(Get-ScopeSource 'src/driver/driver.h') -replace '(?m)^\s*ULONG JoinPreferenceAccepted, JoinPreferenceError;\n','' -replace '(?m)^\s*NTSTATUS JoinPreferenceStatus;\n',''
if((ConvertTo-ScopeToken $actual) -cne (ConvertTo-ScopeToken $expected)){throw 'Unexpected adapter/cap change.'}
$expected=(Get-ScopeSource 'src/driver/driver.c' -BaselineSource).Replace('SET_DWORD(L"DiagVersion", 18);','SET_DWORD(L"DiagVersion", 20);').Replace('Data.Ulong = 0x00060012;','Data.Ulong = 0x00060014;')
$actual=(Get-ScopeSource 'src/driver/driver.c') -replace '(?m)^\s*SET_DWORD\(L"JoinPreference\w+", Adapter->JoinPreference\w+\);\n','' -replace '(?m)^\s*SET_DWORD\(L"RuntimeCmd52\w+", 0\);\n',''
if((ConvertTo-ScopeToken $actual) -cne (ConvertTo-ScopeToken $expected)){throw 'Unexpected driver-core behavior.'}
$expected=Get-ScopeSource 'src/cyw43455/connection.h' -BaselineSource
$actual=(Get-ScopeSource 'src/cyw43455/connection.h').Replace('#include "join_preference.h"','').Replace('STEP(18,CywApplyJoinPreference(A));','').Replace('A->JoinPreferenceAccepted=0;A->JoinPreferenceError=0;A->JoinPreferenceStatus=(NTSTATUS)0x103;','')
if((ConvertTo-ScopeToken $actual) -cne (ConvertTo-ScopeToken $expected)){throw 'Unexpected country/security/join change.'}
$expected=Get-ScopeSource 'utility/Connect-RPi5-WiFi.ps1' -BaselineSource
$actual=(Get-ScopeSource 'utility/Connect-RPi5-WiFi.ps1') -replace '(?ms)^function Get-Rpi5JoinPreferenceSummary \{.*?^\}\n',''
$actual=$actual.Replace("        18 { 'automatic-band-preference' }"+"`n",'')
$actual=$actual.Replace('$joinSnapshot = Get-ItemProperty -LiteralPath ''HKLM:\SOFTWARE\Rpi5CywDirectDiag'' -ErrorAction SilentlyContinue'+"`n",'')
$actual=$actual.Replace('Write-Output (Get-Rpi5JoinPreferenceSummary $joinSnapshot)'+"`n",'')
if($actual -cne $expected){throw 'Connection utility changed beyond status display.'}
$preference=Get-ScopeSource 'src/cyw43455/join_preference.h'
if($preference -match 'CywCmdInt|CywFirmwareCommand|CywSendFrame|CywTxPump|while\s*\('){throw 'Preference must not lock bands, send frames, or loop.'}
Write-Output 'PASS: exact .18 transport/scheduler/cap restored; only pre-join preference, diagnostics and status display added.'
