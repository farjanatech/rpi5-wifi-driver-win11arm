Set-StrictMode -Version Latest
$ErrorActionPreference='Stop'
# v0.7.1.11 permits only the global post-negotiation TX framing/lifecycle fix
# on top of the failed-but-preserved v0.7.1.10 experiment. Unrelated source is exact.
& python (Join-Path $PSScriptRoot 'tx_credit_scope_tests.py')
if($LASTEXITCODE -ne 0){throw 'TX glom framing-fix isolation guard failed.'}
