Set-StrictMode -Version Latest
$ErrorActionPreference='Stop'
# v0.7.1.18 changes only bounded TX Service-Burst4 on the green v0.7.1.17
# fixed-port baseline. RX/F2 transport and utilities remain frozen.
& python (Join-Path $PSScriptRoot 'tx_service_burst4_scope_tests.py')
if($LASTEXITCODE -ne 0){throw 'v0.7.1.18 Service-Burst4 isolation guard failed.'}
