Set-StrictMode -Version Latest
$ErrorActionPreference='Stop'
# v0.7.1.16 may change only aligned F2 block-copy mechanics on top of the
# measured v0.7.1.15 Service-Burst2 candidate. Utilities stay frozen.
& python (Join-Path $PSScriptRoot 'tx_credit_scope_tests.py')
if($LASTEXITCODE -ne 0){throw 'v0.7.1.16 F2 buffer-PIO isolation guard failed.'}
