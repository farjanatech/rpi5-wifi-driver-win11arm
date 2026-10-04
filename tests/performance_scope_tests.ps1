Set-StrictMode -Version Latest
$ErrorActionPreference='Stop'
# v0.7.1.14 is measurement-only. Every production driver source file must
# remain byte-for-byte the green v0.7.1.11 baseline.
& python (Join-Path $PSScriptRoot 'tx_credit_scope_tests.py')
if($LASTEXITCODE -ne 0){throw 'v0.7.1.14 measurement-only isolation guard failed.'}
