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
$allowed=@('src/sdio/sdio.c','src/sdio/cmd52_wait.h','src/driver/driver.c','src/driver/driver.h')
foreach($file in $changed) { if($file -notin $allowed) { throw "Unexpected driver-source change versus exp0.6.18: $file" } }
foreach($file in @('src/cyw43455/network.c','src/cyw43455/tx_queue.h','src/cyw43455/tx_types.h','src/cyw43455/tx_dispatch.h','src/cyw43455/control.h','src/cyw43455/connection.h','src/cyw43455/radio.h','utility/Get-RPi5-WiFi-Radio.ps1','utility/Get-RPi5-WiFi-Radio.cmd','utility/Test-RPi5-WiFi-Performance.ps1','utility/Measure-RPi5-WiFi-Load.ps1','utility/Connect-RPi5-WiFi.ps1','utility/Set-RPi5-WiFi-Autoconnect.ps1','utility/WiFi.config.example.json','scripts/fetch-firmware.ps1')) {
    if((Get-ScopeSource $file) -cne (Get-ScopeSource $file -BaselineSource)){throw "Preserved baseline changed: $file"}
}
$expected=Get-ScopeSource 'src/driver/driver.h' -BaselineSource
$actual=(Get-ScopeSource 'src/driver/driver.h') -replace '(?m)^\s*ULONG RuntimeCmd52Commands, RuntimeCmd52FastPolls, RuntimeCmd52WaitSleeps, RuntimeCmd52Timeouts;\n',''
if((ConvertTo-ScopeToken $actual) -cne (ConvertTo-ScopeToken $expected)){throw 'Unexpected adapter/cap change.'}
$expected=(Get-ScopeSource 'src/driver/driver.c' -BaselineSource).Replace('SET_DWORD(L"DiagVersion", 18);','SET_DWORD(L"DiagVersion", 19);').Replace('Data.Ulong = 0x00060012;','Data.Ulong = 0x00060013;')
$actual=(Get-ScopeSource 'src/driver/driver.c') -replace '(?m)^\s*SET_DWORD\(L"RuntimeCmd52\w+", Adapter->RuntimeCmd52\w+\);\n',''
if((ConvertTo-ScopeToken $actual) -cne (ConvertTo-ScopeToken $expected)){throw 'Unexpected driver-core behavior.'}
$expected=Get-ScopeSource 'src/sdio/sdio.c' -BaselineSource
$oldLoop=@'
    for (Timeout = 0; Timeout < 10000; Timeout++)
    {
        InterruptStatus = SdioRead32(Adapter, SDHCI_INT_STATUS);
        if ((InterruptStatus & (SDHCI_INT_CMD_COMPLETE | SDHCI_INT_ERROR | SDHCI_INT_CMD_ERROR_MASK)) != 0)
        {
            break;
        }
        KeStallExecutionProcessor(100);
    }
'@
$newLoop=@'
    if (CommandIndex == SDCMD_IO_RW_DIRECT && KeGetCurrentIrql() == PASSIVE_LEVEL &&
        Adapter->BusModeStage == 6 && Adapter->BusWidth == 4 &&
        Adapter->BusActualKhz > 400 && Adapter->BusActualKhz <= 25000)
        Status = SdioWaitRuntimeCmd52(Adapter, &InterruptStatus);
    else {
        for (Timeout = 0; Timeout < 10000; Timeout++)
        {
            InterruptStatus = SdioRead32(Adapter, SDHCI_INT_STATUS);
            if ((InterruptStatus & (SDHCI_INT_CMD_COMPLETE | SDHCI_INT_ERROR | SDHCI_INT_CMD_ERROR_MASK)) != 0)
            {
                break;
            }
            KeStallExecutionProcessor(100);
        }
        if (Timeout == 10000) Status = STATUS_IO_TIMEOUT;
    }
'@
$oldTimeout=@'
    if (Timeout == 10000)
    {
        Adapter->LastCommandResetStatus = SdioResetHost(Adapter, SDHCI_RESET_CMD);
        return STATUS_IO_TIMEOUT;
    }
'@
$newTimeout=@'
    if (!NT_SUCCESS(Status))
    {
        Adapter->LastCommandResetStatus = SdioResetHost(Adapter, SDHCI_RESET_CMD);
        return Status;
    }
'@
$oldLoop=$oldLoop.Replace("`r`n","`n")
$newLoop=$newLoop.Replace("`r`n","`n")
$oldTimeout=$oldTimeout.Replace("`r`n","`n")
$newTimeout=$newTimeout.Replace("`r`n","`n")
if(!$expected.Contains($oldLoop) -or !$expected.Contains($oldTimeout)){throw 'Cannot locate baseline command wait.'}
$expected=$expected.Replace($oldLoop,$newLoop).Replace($oldTimeout,$newTimeout)
$actual=(Get-ScopeSource 'src/sdio/sdio.c').Replace('#include "cmd52_wait.h"','')
if((ConvertTo-ScopeToken $actual) -cne (ConvertTo-ScopeToken $expected)){throw 'Unexpected SDIO transport change.'}
Write-Output 'PASS: exp0.6.18 queue/scheduler/firmware/radio/tools retained; only verified-runtime CMD52 waiting and counters changed.'
