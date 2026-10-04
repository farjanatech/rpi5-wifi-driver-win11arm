Set-StrictMode -Version Latest
$ErrorActionPreference='Stop'
# v0.7.1.10 permits only negotiated pressure-only two-frame host TX glom
# on top of the successful v0.7.1.9 backlog branch. Unrelated source is exact.
& python (Join-Path $PSScriptRoot 'tx_credit_scope_tests.py')
if($LASTEXITCODE -ne 0){throw 'TX glom experiment isolation guard failed.'}
