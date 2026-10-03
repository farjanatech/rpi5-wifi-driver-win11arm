Set-StrictMode -Version Latest
$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot '..\utility\Get-RPi5-WiFi-TxCredit.ps1') -LibraryOnly
function Assert-Tx([bool]$Condition,[string]$Message){if(-not $Condition){throw $Message}}
function Set-Word([byte[]]$Data,[int]$Index,[uint64]$Value){[BitConverter]::GetBytes($Value).CopyTo($Data,8*$Index)}
function New-Snapshot {
    $b=New-Object byte[] 464;$t=New-Object byte[] 568
    Set-Word $b 0 1;Set-Word $b 1 464;Set-Word $b 2 100;Set-Word $b 3 1100
    Set-Word $b 4 1000;Set-Word $b 5 1
    Set-Word $t 0 2;Set-Word $t 1 13;Set-Word $t 2 1000;Set-Word $t 3 100;Set-Word $t 4 1000
    [pscustomobject]@{TxCreditV1=$b;TimingV2=$t;WorkerStartCount=1}
}
function Assert-Throws([scriptblock]$Action){$threw=$false;try{& $Action | Out-Null}catch{$threw=$true};Assert-Tx $threw 'Expected rejection.'}
Assert-Tx ($script:CywTxCreditFields.Count -eq 58) 'ABI field count.'
$a=New-Snapshot;$b=New-Snapshot;Set-Word $b.TxCreditV1 3 2100
Set-Word $b.TxCreditV1 8 2;Set-Word $b.TxCreditV1 9 8;Set-Word $b.TxCreditV1 18 10
Set-Word $b.TxCreditV1 15 4
$v=Get-CywTxCreditReport -After $b -Before $a
Assert-Tx ($v.IntervalSeconds -eq 1 -and $v.FramesPerPump -eq 4 -and $v.PumpMeanUs -eq 5000) 'Interval math.'
Assert-Tx ($null -eq $v.F1MeanUs -and $null -eq $v.F2MeanUs) 'Disabled timing must be unavailable.'
Assert-Tx ($v.CumulativeMaxima.PumpMaxFrames -eq 4 -and $null -eq $v.Delta.PSObject.Properties['PumpMaxFrames']) 'Maxima are cumulative, not differences.'
Assert-Throws {Get-CywTxCreditReport -After $a -Before $a}
foreach($index in @(0,1,2,4,6,7)) {
    $x=New-Snapshot;Set-Word $x.TxCreditV1 $index 999
    Assert-Throws {ConvertFrom-CywTxCreditSnapshot $x}
}
$x=New-Snapshot;$x.TimingV2=New-Object byte[] 0;Assert-Throws {ConvertFrom-CywTxCreditSnapshot $x}
$x=New-Snapshot;$x.WorkerStartCount=2;Assert-Throws {ConvertFrom-CywTxCreditSnapshot $x}
$x=New-Snapshot;Set-Word $x.TimingV2 4 1200;Assert-Throws {ConvertFrom-CywTxCreditSnapshot $x}
$x=New-Snapshot;Set-Word $x.TxCreditV1 3 2100;Set-Word $x.TxCreditV1 6 1
Assert-Throws {Get-CywTxCreditReport -After $x -Before $a}
$x=New-Snapshot;Set-Word $x.TxCreditV1 3 2100;Set-Word $a.TxCreditV1 8 2
Assert-Throws {Get-CywTxCreditReport -After $x -Before $a}
$a=New-Snapshot;Set-Word $x.TxCreditV1 8 ([uint64]::MaxValue)
Assert-Throws {Get-CywTxCreditReport -After $x -Before $a}
$a=New-Snapshot;$b=New-Snapshot;Set-Word $b.TxCreditV1 3 2100
Set-Word $a.TxCreditV1 7 1;Set-Word $b.TxCreditV1 7 1
Set-Word $b.TxCreditV1 30 2;Set-Word $b.TxCreditV1 36 6
Set-Word $b.TxCreditV1 38 4;Set-Word $b.TxCreditV1 43 8
$v=Get-CywTxCreditReport -After $b -Before $a
Assert-Tx ($v.F1MeanUs -eq 3000 -and $v.F2MeanUs -eq 2000 -and $null -eq $v.FramesPerPump) 'Detailed time and zero denominator.'
# JSON roundtrip must preserve UInt64 diagnostic bytes (never JSON numeric counters).
$round=$b | ConvertTo-Json -Depth 4 | ConvertFrom-Json
Assert-Tx ((ConvertFrom-CywTxCreditSnapshot $round).SnapshotQpc -eq 2100) 'Snapshot JSON roundtrip.'
Write-Host 'PASS: TX snapshot ABI, deltas, unavailable timing, stale/restart rejection, saturation, maxima and JSON roundtrip.'
