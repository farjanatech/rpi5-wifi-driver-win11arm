Set-StrictMode -Version Latest
$ErrorActionPreference='Stop'
# The experiment's sole exception is additive TX observations and a separately
# selectable post-RX scheduler. All original protected code is compared exactly.
& python (Join-Path $PSScriptRoot 'tx_credit_scope_tests.py')
if($LASTEXITCODE -ne 0){throw 'TX credit experiment isolation guard failed.'}
