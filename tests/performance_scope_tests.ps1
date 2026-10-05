Set-StrictMode -Version Latest
$ErrorActionPreference='Stop'
# v0.7.1.15 may change only the measured TX service/status amortization path.
# Unrelated v0.7.1.11 runtime and the v0.7.1.14 utility stay protected.
& python (Join-Path $PSScriptRoot 'tx_credit_scope_tests.py')
if($LASTEXITCODE -ne 0){throw 'v0.7.1.15 service-burst isolation guard failed.'}
