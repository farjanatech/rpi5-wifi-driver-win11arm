Set-StrictMode -Version Latest
$ErrorActionPreference='Stop'
& python (Join-Path $PSScriptRoot 'stability_recovery_scope_tests.py')
if($LASTEXITCODE -ne 0){throw 'v0.7.1.20 stability recovery isolation guard failed.'}
