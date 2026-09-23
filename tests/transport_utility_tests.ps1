Set-StrictMode -Version Latest
$ErrorActionPreference='Stop'
. (Join-Path (Split-Path -Parent $PSScriptRoot) 'utility\Get-RPi5-WiFi-Transport.ps1') -TransportLibraryOnly
function Get-TransportFixture {
    param([uint32]$Count=3,[uint32]$Next=3,[uint32]$Serial=3)
    $bytes=New-Object byte[] 16416
    $header=@([uint32]1,[uint32]128,[uint32]128,$Count,$Next,$Serial)
    for($i=0;$i -lt 6;$i++){[BitConverter]::GetBytes([uint32]$header[$i]).CopyTo($bytes,4*$i)}
    [BitConverter]::GetBytes([uint64]100).CopyTo($bytes,24)
    $firstSlot=if($Count -eq 128){$Next}else{0}
    for($i=0;$i -lt $Count;$i++) {
        $slot=($firstSlot+$i)%128;$offset=32+128*$slot
        $entrySerial=[uint32](([uint64]$Serial+[uint64]4294967296-[uint64]$Count+1+[uint64]$i)%[uint64]4294967296)
        [BitConverter]::GetBytes([uint64](100+10000000*$i)).CopyTo($bytes,$offset)
        [BitConverter]::GetBytes($entrySerial).CopyTo($bytes,$offset+8)
        [BitConverter]::GetBytes([uint32]6).CopyTo($bytes,$offset+12) # Ready + authorized
        [BitConverter]::GetBytes([uint32]600).CopyTo($bytes,$offset+16)
        [BitConverter]::GetBytes([uint32](10*$i)).CopyTo($bytes,$offset+32) # RxFrames
        [BitConverter]::GetBytes([uint32]32).CopyTo($bytes,$offset+108) # TxMaximum
        [BitConverter]::GetBytes([uint32]1234).CopyTo($bytes,$offset+124) # EchoMaxMs
    }
    return [pscustomobject]@{DiagVersion=24;TransportTraceV1=$bytes}
}
function Assert-TransportRejected {
    param($Value,[string]$Label)
    $rejected=$false
    try{$null=ConvertFrom-Rpi5Transport $Value}catch{$rejected=$true}
    if(-not $rejected){throw "Invalid transport snapshot accepted: $Label"}
}
$valid=Get-TransportFixture
$report=Get-Rpi5TransportReport $valid
if($report.Version -ne 1 -or $report.EntryBytes -ne 128 -or $report.Rows.Count -ne 3 -or
   $report.Rows[0].Serial -ne 1 -or $report.Rows[2].Serial -ne 3 -or $report.Origin100ns -ne 100 -or
   $report.Rows[2].SecondsSinceTraceOrigin -ne 2 -or $report.Rows[1].SecondsSincePrevious -ne 1 -or
   $report.Rows[1].CounterDeltas.RxFrames -ne 10 -or $null -ne $report.Rows[0].CounterDeltas -or
   $report.Rows[0].TxMaximum -ne 32 -or $report.Rows[0].EchoMaxMs -ne 1234 -or
   $report.TimestampKind -ne 'Monotonic100nsSinceBoot' -or $report.Notice -notmatch 'NOT UTC') {
    throw 'Trace ABI, units, fields, deltas, or monotonic-time notice incorrect.'
}
$wrapped=Get-TransportFixture -Count 128 -Next 2 -Serial 130
$report=ConvertFrom-Rpi5Transport $wrapped
if($report.Rows[0].Slot -ne 2 -or $report.Rows[0].Serial -ne 3 -or
   $report.Rows[-1].Slot -ne 1 -or $report.Rows[-1].Serial -ne 130) {throw 'Wrapped ring not chronological.'}
$report=ConvertFrom-Rpi5Transport (Get-TransportFixture -Count 128 -Next 1 -Serial 1)
if($report.Rows[-2].Serial -ne 0 -or $report.Rows[-1].Serial -ne 1) {throw 'Serial rollover rejected or reordered.'}
$wrappedCounter=Get-TransportFixture
[BitConverter]::GetBytes([uint32]::MaxValue).CopyTo($wrappedCounter.TransportTraceV1,32+32)
[BitConverter]::GetBytes([uint32]1).CopyTo($wrappedCounter.TransportTraceV1,32+128+32)
$report=ConvertFrom-Rpi5Transport $wrappedCounter
if($report.Rows[1].CounterDeltas.RxFrames -ne 2 -or
   (Get-Rpi5TransportCounterDelta ([uint32]::MaxValue) 0) -ne 1) {throw 'Counter rollover delta incorrect.'}
foreach($value in @($null,[pscustomobject]@{},[pscustomobject]@{DiagVersion=24},
    [pscustomobject]@{DiagVersion=24;TransportTraceV1=@(0,1)},
    [pscustomobject]@{DiagVersion=24;TransportTraceV2=$valid.TransportTraceV1})) {
    Assert-TransportRejected $value 'Missing/truncated/unknown property'
}
$stale=Get-TransportFixture;$stale.DiagVersion=23
Assert-TransportRejected $stale 'Stale v1 belonging to an earlier driver'
foreach($pair in @(@(0,0),@(0,2),@(4,120),@(8,64),@(12,0),@(12,129),@(16,128),@(16,1),@(20,7))) {
    $bad=Get-TransportFixture
    [BitConverter]::GetBytes([uint32]$pair[1]).CopyTo($bad.TransportTraceV1,[int]$pair[0])
    Assert-TransportRejected $bad 'Invalid version/size/capacity/count/next/serial'
}
$bad=Get-TransportFixture
[BitConverter]::GetBytes([uint64]99).CopyTo($bad.TransportTraceV1,32)
Assert-TransportRejected $bad 'Before trace origin'
$bad=Get-TransportFixture
[BitConverter]::GetBytes([uint64]100).CopyTo($bad.TransportTraceV1,32+128)
Assert-TransportRejected $bad 'Duplicate timestamp'
$bad=Get-TransportFixture
[BitConverter]::GetBytes([uint32]9).CopyTo($bad.TransportTraceV1,32+128+8)
Assert-TransportRejected $bad 'Nonconsecutive serial'
$bad=Get-TransportFixture -Count 128 -Next 2 -Serial 130
[BitConverter]::GetBytes([uint32]129).CopyTo($bad.TransportTraceV1,20)
Assert-TransportRejected $bad 'Newest serial disagrees with header'
foreach($offset in @(12,20,104,108,112,116)) {
    $bad=Get-TransportFixture
    [BitConverter]::GetBytes([uint32]256).CopyTo($bad.TransportTraceV1,32+$offset)
    Assert-TransportRejected $bad 'Invalid flags/CCCR/sequence/credit/flow/state range'
}
Write-Output 'PASS: transport trace ABI, passive decoding, timestamp units, wrapped chronology, modulo-32 counters, missing/stale/unknown/invalid snapshots.'
