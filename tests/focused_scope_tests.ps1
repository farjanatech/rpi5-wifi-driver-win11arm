Set-StrictMode -Version Latest
$ErrorActionPreference='Stop'
$baseline='6f255d3f20b65ad5b52132a216043a2f99f848a5'
$root=Split-Path -Parent $PSScriptRoot
function Get-ScopeSource {
    param([string]$Path,[switch]$BaselineSource)
    if ($BaselineSource) {
        $lines = & git -C $root show "${baseline}:$Path"
        if ($LASTEXITCODE -ne 0) { throw "Cannot read baseline $Path" }
        return ($lines -join "`n").TrimEnd()
    }
    return (Get-Content -LiteralPath (Join-Path $root $Path) -Raw).Replace("`r`n","`n").TrimEnd()
}
function ConvertTo-ScopeToken {
    param([string]$Text)
    # These specific C headers have no comment markers inside string literals.
    return [regex]::Replace([regex]::Replace($Text,'/\*.*?\*/|//[^\r\n]*','',[Text.RegularExpressions.RegexOptions]::Singleline),'\s+','')
}
# Source identity, not merely a simulation of the intended scheduling.
$changed=@(& git -C $root diff --name-only $baseline HEAD -- src)
if ($LASTEXITCODE -ne 0) { throw 'Cannot compare source scope.' }
$allowed=@('src/cyw43455/tx_queue.h','src/cyw43455/tx_types.h','src/driver/driver.c','src/driver/driver.h')
foreach($file in $changed) { if($file -notin $allowed) { throw "Unexpected driver-source change versus exp0.6.14: $file" } }
if ((Get-ScopeSource 'src/cyw43455/network.c') -cne (Get-ScopeSource 'src/cyw43455/network.c' -BaselineSource)) { throw 'Worker/receive/send scheduling differs from exp0.6.14.' }

$expected=Get-ScopeSource 'src/cyw43455/tx_queue.h' -BaselineSource
$expected=$expected.Replace('count=Q->Outstanding;','count=Q->Outstanding+Q->Completing;')
$expected=$expected.Replace('    NdisMSendNetBufferListsComplete(A->MiniportHandle,Nbl,0);',@'
    KeAcquireSpinLock(&Q->Lock, &irql); Q->Outstanding--; Q->Completing++;
    KeReleaseSpinLock(&Q->Lock, irql);
    NdisMSendNetBufferListsComplete(A->MiniportHandle,Nbl,0);
'@)
$expected=$expected.Replace('Q->Outstanding--;A->TxNblCompleted++;','Q->Completing--;A->TxNblCompleted++;')
$actual=Get-ScopeSource 'src/cyw43455/tx_queue.h'
# Retained diagnostics are write-only observations, not admission decisions.
$actual=$actual.Replace('A->TxQueueFrames=Q->Frames;','')
$actual=$actual.Replace('if(++frames>CYW_TX_LIMIT) {A->TxOversizedNbl++;return NDIS_STATUS_RESOURCES;}','if(++frames>CYW_TX_LIMIT)return NDIS_STATUS_RESOURCES;')
if ((ConvertTo-ScopeToken $actual) -cne (ConvertTo-ScopeToken $expected)) { throw 'Unexpected queue behavior beyond completion accounting.' }
$expected=(Get-ScopeSource 'src/cyw43455/tx_types.h' -BaselineSource).Replace('ULONG Count, Frames, Bytes, Outstanding;','ULONG Count, Frames, Bytes, Outstanding, Completing;')
$actual=(Get-ScopeSource 'src/cyw43455/tx_types.h').Replace('#define CYW_TX_LIMIT RPI5CYW_TX_LIMIT','#define CYW_TX_LIMIT 64u')
if ((ConvertTo-ScopeToken $actual) -cne (ConvertTo-ScopeToken $expected)) { throw 'Unexpected queue layout/limits.' }
$expected=(Get-ScopeSource 'src/driver/driver.h' -BaselineSource).Replace('#define RPI5CYW_FRAME_SIZE 1514',"#define RPI5CYW_FRAME_SIZE 1514`n#define RPI5CYW_TX_LIMIT 64u")
$expected=$expected.Replace('ULONG TxQueueHighWater, TxQueueFull, RxBatchYields;',"ULONG TxQueueHighWater, TxQueueFull, RxBatchYields;`nULONG TxQueueFrames, TxBurstAdmissions, TxOversizedNbl, TxInterleavedPackets;")
if ((ConvertTo-ScopeToken (Get-ScopeSource 'src/driver/driver.h')) -cne (ConvertTo-ScopeToken $expected)) { throw 'Unexpected adapter layout or queue cap.' }
$expected=(Get-ScopeSource 'src/driver/driver.c' -BaselineSource).Replace('SET_DWORD(L"DiagVersion", 14);','SET_DWORD(L"DiagVersion", 16);').Replace('Data.Ulong = 0x0006000e;','Data.Ulong = 0x00060010;')
$actual=Get-ScopeSource 'src/driver/driver.c'
foreach($line in @('SET_DWORD(L"TxQueueLimit", RPI5CYW_TX_LIMIT);','SET_DWORD(L"TxQueueFrames", Adapter->TxQueueFrames);','SET_DWORD(L"TxBurstAdmissions", Adapter->TxBurstAdmissions);','SET_DWORD(L"TxOversizedNbl", Adapter->TxOversizedNbl);','SET_DWORD(L"TxInterleavedPackets", Adapter->TxInterleavedPackets);')) { $actual=$actual.Replace($line,'') }
if ((ConvertTo-ScopeToken $actual) -cne (ConvertTo-ScopeToken $expected)) { throw 'Unexpected driver-core behavior outside diagnostics/version.' }
Write-Output 'PASS: exp0.6.14 worker/transport preserved; 64-frame cap; only completion accounting and diagnostic/version changes in driver source.'
