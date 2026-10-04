Set-StrictMode -Version Latest
$ErrorActionPreference='Stop'
# v0.7.1.9 permits only the bounded pending-NBL backlog and its diagnostics on
# top of the promoted v0.7.1.8 baseline. All unrelated production code is exact.
& python (Join-Path $PSScriptRoot 'tx_credit_scope_tests.py')
if($LASTEXITCODE -ne 0){throw 'TX credit experiment isolation guard failed.'}
