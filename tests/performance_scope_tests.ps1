Set-StrictMode -Version Latest
$ErrorActionPreference='Stop'
# v0.7.1.19 changes only adaptive TX classification on the green v0.7.1.18
# baseline. RX/fixed-port SDIO, firmware, scheduling and utilities stay frozen.
& python (Join-Path $PSScriptRoot 'tx_adaptive_hybrid_scope_tests.py')
if($LASTEXITCODE -ne 0){throw 'v0.7.1.19 adaptive TX hybrid isolation guard failed.'}
