[CmdletBinding()]
param(
    [string]$Configuration='Release',
    [ValidateSet('ARM64')][string]$Platform='ARM64',
    [ValidateSet('0','1')][string]$TxCreditScheduling='1',
    [ValidateSet('0','1')][string]$TxGlom2='1'
)
Set-StrictMode -Version Latest
$ErrorActionPreference='Stop'
$root=Split-Path -Parent $PSScriptRoot
$inf=Get-Content (Join-Path $root 'package/rpi5cyw.inf') -Raw
if ($inf -notmatch 'ACPI\\RPI1060' -or $inf -match 'RPI0011') {
    throw 'Damian packaging requires an RPI1060-only INF.'
}
& (Join-Path $PSScriptRoot 'package-ci.ps1') -Configuration $Configuration -Platform $Platform
$stage=Join-Path $root 'artifacts/rpi5cyw-test-driver'
Copy-Item (Join-Path $root 'docs/DAMIAN-EDITION.md') (Join-Path $stage 'README-TESTING.txt')
Copy-Item (Join-Path $root 'docs/RECOVERY-UPDATE.md') (Join-Path $stage 'RECOVERY-UPDATE.md')
Copy-Item (Join-Path $root 'docs/LINK-STABILITY-UPDATE.md') (Join-Path $stage 'LINK-STABILITY-UPDATE.md')
@"
edition=damian
package_version=0.7.1.24-damian.4
kernel_source_baseline=c16aa318da490350126739add45223a186ab0a47
kernel_source_changed=true
runtime_diagnostics=bounded-async-snapshots
tx_refill=fifo-all-available-capacity
warm_recovery=cccr-card-reset-before-cmd5
recovery_status=terminal-errors-published
link_events=preserve-authorization-during-psk-progress
driver_commit=$env:GITHUB_SHA
workflow_run=$env:GITHUB_SERVER_URL/$env:GITHUB_REPOSITORY/actions/runs/$env:GITHUB_RUN_ID
matching_acpi_id=ACPI\RPI1060
reserved_irq_id=ACPI\RPI0011
sdio_mmio_base=0x1001100000
sdio_mmio_length=0x260
sdio_gsi=306
tx_credit_scheduling=$TxCreditScheduling
tx_glom2=$TxGlom2
firmware_contract_repository=farjanatech/rpi5-uefi
firmware_contract_commit=b3f1dc6a6cd3e3c355e7fe512e4dca80cd04e51a
hardware_validation=pending
"@ | Set-Content (Join-Path $stage 'SOURCE_REVISION.txt') -Encoding ASCII
Get-ChildItem $stage -File | Where-Object Name -ne 'SHA256SUMS.txt' | Sort-Object Name | ForEach-Object {
    '{0}  {1}' -f (Get-FileHash $_.FullName -Algorithm SHA256).Hash,$_.Name
} | Set-Content (Join-Path $stage 'SHA256SUMS.txt') -Encoding ASCII
Write-Host 'Packaged Damian Edition RPI1060 driver; Pi hardware validation pending.'
