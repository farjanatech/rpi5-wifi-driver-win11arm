Set-StrictMode -Version Latest
$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot '..\utility\Get-RPi5-WiFi-TxCredit.ps1') -LibraryOnly
function Assert-Tx([bool]$Condition,[string]$Message){if(-not $Condition){throw $Message}}
function ConvertTo-FixtureWord([byte[]]$Data,[int]$Index,[uint64]$Value){[BitConverter]::GetBytes($Value).CopyTo($Data,8*$Index)}
function Get-TxSnapshotFixture {
    $b=New-Object byte[] 464;$t=New-Object byte[] 568
    ConvertTo-FixtureWord $b 0 1;ConvertTo-FixtureWord $b 1 464;ConvertTo-FixtureWord $b 2 100;ConvertTo-FixtureWord $b 3 1100
    ConvertTo-FixtureWord $b 4 1000;ConvertTo-FixtureWord $b 5 1
    ConvertTo-FixtureWord $t 0 2;ConvertTo-FixtureWord $t 1 13;ConvertTo-FixtureWord $t 2 1000;ConvertTo-FixtureWord $t 3 100;ConvertTo-FixtureWord $t 4 1000
    [pscustomobject]@{TxCreditV1=$b;TimingV2=$t;WorkerStartCount=1}
}
function Assert-Rejection([scriptblock]$Action){$threw=$false;try{& $Action | Out-Null}catch{$threw=$true};Assert-Tx $threw 'Expected rejection.'}
Assert-Tx ($script:CywTxCreditFields.Count -eq 58) 'ABI field count.'
$a=Get-TxSnapshotFixture;$b=Get-TxSnapshotFixture;ConvertTo-FixtureWord $b.TxCreditV1 3 2100
ConvertTo-FixtureWord $b.TxCreditV1 8 2;ConvertTo-FixtureWord $b.TxCreditV1 9 8;ConvertTo-FixtureWord $b.TxCreditV1 18 10
ConvertTo-FixtureWord $b.TxCreditV1 15 4
$v=Get-CywTxCreditReport -After $b -Before $a
Assert-Tx ($v.IntervalSeconds -eq 1 -and $v.FramesPerPump -eq 4 -and $v.PumpMeanUs -eq 5000) 'Interval math.'
Assert-Tx ($null -eq $v.F1MeanUs -and $null -eq $v.F2MeanUs) 'Disabled timing must be unavailable.'
Assert-Tx ($v.CumulativeMaxima.PumpMaxFrames -eq 4 -and $null -eq $v.Delta.PSObject.Properties['PumpMaxFrames']) 'Maxima are cumulative, not differences.'
Assert-Rejection {Get-CywTxCreditReport -After $a -Before $a}
foreach($index in @(0,1,2,4,6,7)) {
    $x=Get-TxSnapshotFixture;ConvertTo-FixtureWord $x.TxCreditV1 $index 999
    Assert-Rejection {ConvertFrom-CywTxCreditSnapshot $x}
}
$x=Get-TxSnapshotFixture;$x.TimingV2=New-Object byte[] 0;Assert-Rejection {ConvertFrom-CywTxCreditSnapshot $x}
$x=Get-TxSnapshotFixture;$x.WorkerStartCount=2;Assert-Rejection {ConvertFrom-CywTxCreditSnapshot $x}
$x=Get-TxSnapshotFixture;ConvertTo-FixtureWord $x.TimingV2 4 1200;Assert-Rejection {ConvertFrom-CywTxCreditSnapshot $x}
$x=Get-TxSnapshotFixture;ConvertTo-FixtureWord $x.TxCreditV1 3 2100;ConvertTo-FixtureWord $x.TxCreditV1 6 1
Assert-Rejection {Get-CywTxCreditReport -After $x -Before $a}
$x=Get-TxSnapshotFixture;ConvertTo-FixtureWord $x.TxCreditV1 3 2100;ConvertTo-FixtureWord $a.TxCreditV1 8 2
Assert-Rejection {Get-CywTxCreditReport -After $x -Before $a}
$a=Get-TxSnapshotFixture;ConvertTo-FixtureWord $x.TxCreditV1 8 ([uint64]::MaxValue)
Assert-Rejection {Get-CywTxCreditReport -After $x -Before $a}
$a=Get-TxSnapshotFixture;$b=Get-TxSnapshotFixture;ConvertTo-FixtureWord $b.TxCreditV1 3 2100
ConvertTo-FixtureWord $a.TxCreditV1 7 1;ConvertTo-FixtureWord $b.TxCreditV1 7 1
ConvertTo-FixtureWord $b.TxCreditV1 30 2;ConvertTo-FixtureWord $b.TxCreditV1 36 6
ConvertTo-FixtureWord $b.TxCreditV1 38 4;ConvertTo-FixtureWord $b.TxCreditV1 43 8
$v=Get-CywTxCreditReport -After $b -Before $a
Assert-Tx ($v.F1MeanUs -eq 3000 -and $v.F2MeanUs -eq 2000 -and $null -eq $v.FramesPerPump) 'Detailed time and zero denominator.'
# JSON roundtrip uses explicit base64, never ETS-decorated PS 5.1 byte arrays.
$round=ConvertTo-CywTxCreditArchive $b | ConvertTo-Json -Depth 4 | ConvertFrom-Json
Assert-Tx ((ConvertFrom-CywTxCreditSnapshot $round).SnapshotQpc -eq 2100) 'Snapshot JSON roundtrip.'
$again=ConvertTo-CywTxCreditArchive $round
Assert-Tx ($again.TxCreditV1 -ceq $round.TxCreditV1 -and $again.TimingV2 -ceq $round.TimingV2) 'Archived snapshot can be saved again.'
$x=ConvertTo-CywTxCreditArchive $b;$x.TxCreditV1='not base64!'
Assert-Rejection {ConvertFrom-CywTxCreditSnapshot $x}
Write-Output 'PASS: TX snapshot ABI, deltas, unavailable timing, stale/restart rejection, saturation, maxima and JSON roundtrip.'
