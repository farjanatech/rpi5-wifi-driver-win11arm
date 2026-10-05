Set-StrictMode -Version Latest
$ErrorActionPreference='Stop'
# v0.7.1.17 rejects the hardware-failing v0.7.1.16 register-buffer PIO
# experiment and restores v0.7.1.15 production source exactly. Utilities stay frozen.
& python (Join-Path $PSScriptRoot 'tx_credit_scope_tests.py')
if($LASTEXITCODE -ne 0){throw 'v0.7.1.17 fixed-port PIO rollback isolation guard failed.'}
