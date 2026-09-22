Set-StrictMode -Version Latest
$ErrorActionPreference='Stop'
. (Join-Path (Split-Path -Parent $PSScriptRoot) 'utility\Get-RPi5-WiFi-Timing.ps1') -TimingLibraryOnly
function New-TimingFixture {
    param([uint64]$Count=2,[uint64]$Ticks=200000,[uint64]$Stamp=500000,[uint64]$Session=10)
    $bytes=New-Object byte[] 560
    $values=@([uint64]1,[uint64]13,[uint64]10000000,$Session,$Stamp)
    for($i=0;$i -lt 5;$i++){[BitConverter]::GetBytes([uint64]$values[$i]).CopyTo($bytes,$i*8)}
    for($i=0;$i -lt 13;$i++){
        $values=@($Count,$Ticks,[uint64]100000,$Count,[uint64]0)
        for($j=0;$j -lt 5;$j++){[BitConverter]::GetBytes([uint64]$values[$j]).CopyTo($bytes,40+40*$i+8*$j)}
    }
    [pscustomobject]@{DiagVersion=22;TimingV1=$bytes}
}
$before=New-TimingFixture
$after=New-TimingFixture -Count 5 -Ticks 500000 -Stamp 900000
$r=Get-Rpi5TimingReport $before $after
if(-not $r.ComparableSnapshots -or $r.Rows.Count -ne 13 -or $r.Rows[0].DeltaCount -ne 3 -or
    $r.Rows[0].DeltaTotalMs -ne 30 -or $r.Rows[0].DeltaMeanMs -ne 10 -or
    $r.Rows[0].CumulativeMaxMs -ne 10 -or $r.Rows[12].Name -ne 'CreditRecheck') {throw 'Units/layout/deltas incorrect.'}
$r=Get-Rpi5TimingReport $before (New-TimingFixture -Session 20)
if($r.ComparableSnapshots -or $null -ne $r.Rows[0].DeltaCount){throw 'Session reset compared.'}
$r=Get-Rpi5TimingReport $before $before
if($r.ComparableSnapshots){throw 'Identical snapshot compared.'}
$r=Get-Rpi5TimingReport $after (New-TimingFixture -Count 2 -Stamp 1000000)
if($null -ne $r.Rows[0].DeltaCount){throw 'Decreased counter compared.'}
$r=Get-Rpi5TimingReport $null $after
if($r.ComparableSnapshots -or $r.Rows[0].Count -ne 5){throw 'Missing baseline not handled.'}
foreach($bad in @($null,[pscustomobject]@{},[pscustomobject]@{DiagVersion=22;TimingV1=@(0,1)})){
    $rejected=$false
    try{$null=ConvertFrom-Rpi5Timing $bad}catch{$rejected=$true}
    if(-not $rejected){throw 'Invalid/missing snapshot accepted.'}
}
foreach($offset in @(0,8,16)){
    $bad=New-TimingFixture
    [BitConverter]::GetBytes([uint64]0).CopyTo($bad.TimingV1,$offset)
    $rejected=$false
    try{$null=ConvertFrom-Rpi5Timing $bad}catch{$rejected=$true}
    if(-not $rejected){throw 'Invalid version/count/frequency accepted.'}
}
Write-Output 'PASS: fixed ABI, units, coherent snapshot validation, missing data, resets, cumulative vs delta metrics.'
