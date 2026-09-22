Set-StrictMode -Version Latest
$ErrorActionPreference='Stop'
. (Join-Path (Split-Path -Parent $PSScriptRoot) 'utility\Get-RPi5-WiFi-Timing.ps1') -TimingLibraryOnly
function Get-TimingFixture {
    param([uint64]$Count=2,[uint64]$Ticks=200000,[uint64]$Stamp=500000,[uint64]$Session=10,
        [ValidateSet(1,2)][int]$Version=1,[uint64]$Mask=8191)
    $headerBytes=if($Version -eq 2){48}else{40}
    $bytes=New-Object byte[] ($headerBytes+520)
    $values=@([uint64]$Version,[uint64]13,[uint64]10000000,$Session,$Stamp)
    for($i=0;$i -lt 5;$i++){[BitConverter]::GetBytes([uint64]$values[$i]).CopyTo($bytes,$i*8)}
    if($Version -eq 2){[BitConverter]::GetBytes($Mask).CopyTo($bytes,40)}
    for($i=0;$i -lt 13;$i++){
        if($Version -eq 1 -or ($Mask -band (1L -shl $i)) -ne 0){
            $values=@($Count,$Ticks,[uint64]100000,$Count,[uint64]0)
            for($j=0;$j -lt 5;$j++){[BitConverter]::GetBytes([uint64]$values[$j]).CopyTo($bytes,$headerBytes+40*$i+8*$j)}
        }
    }
    $diagnostic=[pscustomobject]@{DiagVersion=21+$Version}
    $diagnostic | Add-Member -NotePropertyName ("TimingV$Version") -NotePropertyValue $bytes
    return $diagnostic
}
function Assert-TimingRejected {
    param($Value,[string]$Label)
    $rejected=$false
    try{$null=ConvertFrom-Rpi5Timing $Value}catch{$rejected=$true}
    if(-not $rejected){throw "Invalid snapshot accepted: $Label"}
}
foreach($version in @(1,2)){
    $before=Get-TimingFixture -Version $version
    $after=Get-TimingFixture -Version $version -Count 5 -Ticks 500000 -Stamp 900000
    $r=Get-Rpi5TimingReport $before $after
    if(-not $r.ComparableSnapshots -or $r.Rows.Count -ne 13 -or $r.Rows[0].DeltaCount -ne 3 -or
        $r.Rows[0].DeltaTotalMs -ne 30 -or $r.Rows[0].DeltaMeanMs -ne 10 -or
        $r.Rows[0].CumulativeMaxMs -ne 10 -or $r.Rows[12].Name -ne 'CreditRecheck' -or
        $r.Version -ne $version -or $r.ActiveMask -ne 8191 -or -not $r.Rows[7].Enabled){throw 'Units/layout/deltas incorrect.'}
    $r=Get-Rpi5TimingReport $before (Get-TimingFixture -Version $version -Session 20)
    if($r.ComparableSnapshots -or $null -ne $r.Rows[0].DeltaCount){throw 'Session reset compared.'}
    $r=Get-Rpi5TimingReport $before $before
    if($r.ComparableSnapshots){throw 'Identical snapshot compared.'}
    $r=Get-Rpi5TimingReport $after (Get-TimingFixture -Version $version -Count 2 -Stamp 1000000)
    if($null -ne $r.Rows[0].DeltaCount){throw 'Decreased counter compared.'}
    $r=Get-Rpi5TimingReport $null $after
    if($r.ComparableSnapshots -or $r.Rows[0].Count -ne 5){throw 'Missing baseline not handled.'}
    foreach($offset in @(0,8,16)){
        $bad=Get-TimingFixture -Version $version
        [BitConverter]::GetBytes([uint64]0).CopyTo($bad.("TimingV$version"),$offset)
        Assert-TimingRejected $bad 'Version/count/frequency'
    }
    Assert-TimingRejected (Get-TimingFixture -Version $version -Stamp 5) 'Timestamp before session'
    Assert-TimingRejected (Get-TimingFixture -Version $version -Count 0) 'Nonzero duration with zero count'
}
foreach($bad in @($null,[pscustomobject]@{},[pscustomobject]@{DiagVersion=22;TimingV1=@(0,1)},
    [pscustomobject]@{DiagVersion=23;TimingV2=@(0,1)})){
    Assert-TimingRejected $bad 'Missing/truncated'
}
$before=Get-TimingFixture -Version 2 -Mask 4223
$after=Get-TimingFixture -Version 2 -Mask 4223 -Count 5 -Ticks 500000 -Stamp 900000
$r=Get-Rpi5TimingReport $before $after
if(-not $r.ComparableSnapshots -or $r.ActiveMask -ne 4223){throw 'Aggregate snapshots not compared.'}
for($i=0;$i -lt 13;$i++){
    $enabled=($i -lt 7 -or $i -eq 12)
    if($r.Rows[$i].Enabled -ne $enabled){throw 'Wrong active bucket mapping.'}
    if($enabled){if($r.Rows[$i].DeltaCount -ne 3){throw 'Aggregate delta missing.'}}
    else{
        foreach($name in @('Count','TotalMs','CumulativeMaxMs','AtLeast10ms','AtLeast100ms',
            'DeltaCount','DeltaTotalMs','DeltaMeanMs','DeltaAtLeast10ms','DeltaAtLeast100ms')){
            if($null -ne $r.Rows[$i].$name){throw 'Disabled bucket falsely reports measured data.'}
        }
    }
}
$changed=Get-TimingFixture -Version 2 -Count 5 -Ticks 500000 -Stamp 900000
$r=Get-Rpi5TimingReport $before $changed
if($r.ComparableSnapshots -or $null -ne $r.Rows[0].DeltaCount){throw 'Changed mode compared.'}
$r=Get-Rpi5TimingReport (Get-TimingFixture) $changed
if($r.ComparableSnapshots){throw 'Changed schema compared.'}
$changed=Get-TimingFixture -Version 2 -Mask 4223 -Count 5 -Ticks 500000 -Stamp 900000
[BitConverter]::GetBytes([uint64]20000000).CopyTo($changed.TimingV2,16)
$r=Get-Rpi5TimingReport $before $changed
if($r.ComparableSnapshots){throw 'Changed frequency compared.'}
$stale=Get-TimingFixture
$stale.DiagVersion=23
Assert-TimingRejected $stale 'Stale v1 on .23'
$after | Add-Member -NotePropertyName TimingV1 -NotePropertyValue (Get-TimingFixture).TimingV1
if((ConvertFrom-Rpi5Timing $after).Version -ne 2){throw 'Stale v1 preferred over v2.'}
Assert-TimingRejected (Get-TimingFixture -Version 2 -Mask 8192) 'Unknown mask bits'
$bad=Get-TimingFixture -Version 2 -Mask 4223
[BitConverter]::GetBytes([uint64]1).CopyTo($bad.TimingV2,48+40*7)
Assert-TimingRejected $bad 'Measurements in disabled bucket'
$bad=Get-TimingFixture -Version 2
[BitConverter]::GetBytes([uint64]1).CopyTo($bad.TimingV2,0)
Assert-TimingRejected $bad 'Mismatched value/schema'
$r=Get-Rpi5TimingReport $null (Get-TimingFixture -Version 2 -Mask 0)
if(@($r.Rows | Where-Object Enabled).Count -ne 0 -or $null -ne $r.Rows[0].Count){throw 'All-disabled snapshot reported measurements.'}
$saturated=Get-TimingFixture -Version 2 -Mask 4223 -Count ([uint64]::MaxValue) -Ticks ([uint64]::MaxValue) -Stamp 900000
$r=Get-Rpi5TimingReport $before $saturated
if($null -ne $r.Rows[0].DeltaCount){throw 'Saturated counter compared.'}
Write-Output 'PASS: v1/v2 ABI, units, active masks, null disabled metrics, stale values, schema/mode/frequency/session changes, saturation.'
