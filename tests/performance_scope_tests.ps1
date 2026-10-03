Set-StrictMode -Version Latest
$ErrorActionPreference='Stop'
$root=Split-Path -Parent $PSScriptRoot
$baseline='16533ac0e7e477f5c604882d8cc82081119e3f90'
function Read-RepoFile([string]$Path){(Get-Content -LiteralPath (Join-Path $root $Path) -Raw).Replace("`r`n","`n")}
function Get-RevisionFile([string]$Path){(@(& git -C $root show ($baseline+':'+$Path)) -join "`n")+"`n"}
$protected=@(
'src/sdio/sdio.c','src/sdio/sdio.h','src/sdio/fifo_blocks.h','src/sdio/bus_mode.h',
'src/cyw43455/transport_send.h','src/cyw43455/tx_queue.h','src/cyw43455/tx_types.h',
'src/cyw43455/rx_config.h','src/cyw43455/rx_performance.h','src/cyw43455/network_protocol.h',
'src/cyw43455/transport_service.h','src/cyw43455/transport_poll.h','src/cyw43455/firmware.c',
'src/cyw43455/connection.h','src/cyw43455/radio.h','src/cyw43455/tx_dispatch.h',
'src/cyw43455/tx_pressure_gate.h','src/cyw43455/tx_pressure_pump.h','src/cyw43455/tx_retry.h',
'src/cyw43455/tx_retry_gate.h','src/driver/interrupt_policy.h'
)
foreach($p in $protected){& git -C $root diff --quiet $baseline -- $p;if($LASTEXITCODE -ne 0){throw "0.7.1.2 protected surface changed: $p"}}
$h=Read-RepoFile 'src/driver/driver.h'
if($h -notmatch '#define\s+RPI5CYW_TX_LIMIT\s+64u'){throw '64-frame cap changed.'}
$d=Read-RepoFile 'src/driver/driver.c';$bd=Get-RevisionFile 'src/driver/driver.c'
$a=$d.IndexOf('MINIPORT_ISR Rpi5CywInterrupt;');$b=$d.IndexOf('static const NDIS_OID gRpi5CywSupportedOids[]',$a)
$ba=$bd.IndexOf('MINIPORT_ISR Rpi5CywInterrupt;');$bb=$bd.IndexOf('static const NDIS_OID gRpi5CywSupportedOids[]',$ba)
if($d.Substring($a,$b-$a) -cne $bd.Substring($ba,$bb-$ba)){throw '0.7.1.2 interrupt implementation changed.'}
$n=Read-RepoFile 'src/cyw43455/network.c'
foreach($needle in @('CywRecordDisconnect','FirmwareDeauthCount','PowerD3Count','WorkerFailureCount')){if(($n+$h+$d) -notmatch $needle){throw "Missing stability evidence: $needle"}}
foreach($old in @('tx_glom_protocol.h','tx_glom_queue.h','tx_status_burst.h','tx_status_service.h')){
 if(Test-Path -LiteralPath (Join-Path $root "src/cyw43455/$old")){throw "Rejected performance experiment remains: $old"}
}
$inf=Read-RepoFile 'package/rpi5cyw.inf'
if($inf -notmatch '(?m)^DriverVer\s*=\s*10/03/2026,0\.7\.1\.4\s*$'){throw 'Version is not 0.7.1.4.'}
Write-Output 'PASS: 0.7.1.4 is 0.7.1.2 data path plus persistent disconnect/power/lifecycle diagnostics only.'
