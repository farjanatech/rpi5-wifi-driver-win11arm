/* Actual pending queue + send-chain dispatch, simulated NDIS/transport only. */
#include <stdio.h>
#include <stdlib.h>
#define RPI5CYW_HOST_TEST 1
#include "../src/driver/driver.h"
typedef unsigned long NDIS_STATUS;
typedef int KSPIN_LOCK,KIRQL;
typedef struct TEST_NB { struct TEST_NB *Next;ULONG Length;UCHAR Data[1514];int MapFail,Copy; } NET_BUFFER,*PNET_BUFFER;
typedef struct TEST_NBL { struct TEST_NBL *Next;PNET_BUFFER First;PVOID CancelId;NDIS_STATUS Status;ULONG Completions,Flags; } NET_BUFFER_LIST,*PNET_BUFFER_LIST;
#define NET_BUFFER_LIST_NEXT_NBL(n) ((n)->Next)
#define NET_BUFFER_LIST_FIRST_NB(n) ((n)->First)
#define NET_BUFFER_NEXT_NB(n) ((n)->Next)
#define NET_BUFFER_DATA_LENGTH(n) ((n)->Length)
#define NET_BUFFER_LIST_STATUS(n) ((n)->Status)
#define NDIS_GET_NET_BUFFER_LIST_CANCEL_ID(n) ((n)->CancelId)
#define NDIS_STATUS_SUCCESS 0UL
#define NDIS_STATUS_PENDING 0x103UL
#define NDIS_STATUS_RESOURCES 0xc000009aUL
#define NDIS_STATUS_INVALID_LENGTH 0xc0010014UL
#define NDIS_STATUS_PAUSED 0xc023002aUL
#define NDIS_STATUS_SEND_ABORTED 0xc001000cUL
#define NDIS_STATUS_FAILURE 0xc0000001UL
#define NDIS_STATUS_MEDIA_DISCONNECTED 0xc000020cUL
#define NDIS_STATUS_LOW_POWER_STATE 0xc023002fUL
#define STATUS_DEVICE_BUSY ((NTSTATUS)0x80000011L)
#define STATUS_INSUFFICIENT_RESOURCES ((NTSTATUS)0xc000009aL)
#define NDIS_TEST_SEND_AT_DISPATCH_LEVEL(f) ((f)&1)
#define NDIS_SEND_COMPLETE_FLAGS_DISPATCH_LEVEL 1
#define RtlCopyMemory memcpy
#include "../src/cyw43455/tx_types.h"
static ULONG Failures,Locks,TransferCalls,Credits,Busy,FailTransfer,Hook,Reenter,Immediate,Poison,CheckCompleting;
static ULONG HookAt,BusyAt,FailAt,CompletionCalls,CompletionNbls,LargestCompletion,ProbeCalls;
static ULONG CheckCallbackFrames,ExpectedCallbackFrames,ExpectedProbeFrames,ExpectedProbeBytes;
static ULONG CheckImmediateBoundary;
static ULONG CallbackProbeCalls;
static NDIS_STATUS CallbackProbeStatus[4];
static ULONG64 Clock,TransferTicks;
static PNET_BUFFER_LIST ProbeNbl;
static PNET_BUFFER_LIST CallbackProbeNbl,MustCompleteBeforeProbe;
static PVOID CancelCompletedId;
static CYW_TX_STATE TestQueue;
static RPI5CYW_ADAPTER TestAdapter;
VOID Rpi5CywTrafficDrop(PRPI5CYW_ADAPTER A,BOOLEAN Tx,ULONG Frames,BOOLEAN Error)
{if(Error)A->Traffic.Errors[Tx]+=Frames;else A->Traffic.Discards[Tx]+=Frames;}
static NET_BUFFER_LIST Reentrant;
static NET_BUFFER ReentrantNb;
static NET_BUFFER ReentrantChain[CYW_TX_LIMIT];
#define CHECK(x) do {if(!(x)){printf("FAIL %d: %s\n",__LINE__,#x);Failures++;}}while(0)
static void KeAcquireSpinLock(KSPIN_LOCK *lock,KIRQL *irql) {CHECK(!*lock && !Locks);*lock=1;Locks++;*irql=0;}
static void KeReleaseSpinLock(KSPIN_LOCK *lock,KIRQL irql) {(void)irql;CHECK(*lock && Locks==1);*lock=0;Locks--;}
ULONG64 KeQueryInterruptTime(void) {return Clock;}
static PUCHAR NdisGetDataBuffer(PNET_BUFFER nb,ULONG length,PUCHAR storage,ULONG align,ULONG offset)
{(void)align;(void)offset;CHECK(!Locks && length==nb->Length);if(nb->MapFail)return NULL;if(nb->Copy){memcpy(storage,nb->Data,length);return storage;}return nb->Data;}
static BOOLEAN CywTxCanTransfer(PRPI5CYW_ADAPTER adapter) {(void)adapter;CHECK(!Locks);return Credits!=0;}
static NTSTATUS CywTxTransfer(PRPI5CYW_ADAPTER adapter,PUCHAR data,ULONG length);
static void NdisMSendNetBufferListsComplete(NDIS_HANDLE handle,PNET_BUFFER_LIST nbl,ULONG flags);
#include "../src/cyw43455/tx_queue.h"
/* Production pressure wrapper around the ACTUAL queue/pump. The network-state
 * gate is exercised separately by tx_retry_gate_tests.c; this fixture models
 * its queue/credit inputs and cancellation/flow changes during a transfer. */
static ULONG PressureBlocked;
static BOOLEAN CywTxPressureEligible(PRPI5CYW_ADAPTER A,ULONG Threshold)
{
    KIRQL irql;BOOLEAN eligible;(void)A;
    KeAcquireSpinLock(&TestQueue.Lock,&irql);
    eligible=(BOOLEAN)(!PressureBlocked && Credits && TestQueue.Count &&
        TestQueue.Frames>=Threshold && TestQueue.Gate==NDIS_STATUS_SUCCESS);
    KeReleaseSpinLock(&TestQueue.Lock,irql);return eligible;
}
static NTSTATUS CywMeasuredTxPump(PRPI5CYW_ADAPTER A,CYW_TX_STATE *Q,ULONG Budget,PULONG Sent)
{return CywTxPump(A,Q,Budget,Sent);}
#include "../src/cyw43455/tx_pressure_pump.h"
static NTSTATUS CywTxTransfer(PRPI5CYW_ADAPTER adapter,PUCHAR data,ULONG length)
{
    (void)adapter;CHECK(!Locks);
    if(CheckImmediateBoundary)CHECK(CompletionNbls==TransferCalls);
    TransferCalls++;
    CHECK(length>=18 && length<=1518 && data[0]==0x20 && !data[1] && !data[2] && !data[3]);
    Clock+=TransferTicks;
    if(!HookAt || HookAt==TransferCalls) {
        if(Hook==1)CywTxCancel(&TestQueue,TestQueue.Entries[0].CancelId);
        if(Hook==2)CywTxSetGate(&TestQueue,NDIS_STATUS_PAUSED);
        if(Hook==3)CywTxSetGate(&TestQueue,NDIS_STATUS_LOW_POWER_STATE);
        if(Hook==4) {
            ProbeCalls++;
            CHECK(TestQueue.Frames==ExpectedProbeFrames && TestQueue.Bytes==ExpectedProbeBytes);
            CHECK(TestQueue.Outstanding==CYW_TX_LIMIT && TestQueue.Count==CYW_TX_LIMIT);
            CHECK(CywTxSubmit(&TestAdapter,&TestQueue,ProbeNbl)==NDIS_STATUS_RESOURCES);
        }
        if(Hook==5)CywTxCancel(&TestQueue,CancelCompletedId);
        if(Hook==6)Clock=0;
        if(Hook==7)PressureBlocked=1;
    }
    if(Busy || (BusyAt && BusyAt==TransferCalls))return STATUS_DEVICE_BUSY;
    if(FailTransfer || (FailAt && FailAt==TransferCalls))return STATUS_IO_DEVICE_ERROR;
    CHECK(Credits>0);Credits--;return STATUS_SUCCESS;
}
static void NdisMSendNetBufferListsComplete(NDIS_HANDLE handle,PNET_BUFFER_LIST nbl,ULONG flags)
{
    PNET_BUFFER_LIST next;ULONG count=0;
    (void)handle;CHECK(!Locks && nbl!=NULL);CompletionCalls++;
    while(nbl) {
        CHECK(nbl!=(PNET_BUFFER_LIST)(size_t)1 && count<1);
        if(nbl==(PNET_BUFFER_LIST)(size_t)1 || count>=1)break;
        next=nbl->Next;CHECK(nbl->Completions==0);nbl->Completions++;nbl->Flags=flags;count++;
        if(Poison) {
            /* Returned NBLs may be freed/reused synchronously. The production
             * queue must not access this NBL or its NB chain after handoff. */
            nbl->Next=(PNET_BUFFER_LIST)(size_t)1;
            nbl->First=(PNET_BUFFER)(size_t)1;
        }
        nbl=next;
    }
    CompletionNbls+=count;if(count>LargestCompletion)LargestCompletion=count;
    if(CheckCompleting)CHECK(TestQueue.Completing==count && CywTxOutstanding(&TestQueue)==TestQueue.Outstanding+count);
    if(CheckCallbackFrames) {
        CheckCallbackFrames=0;CHECK(TestQueue.Frames==ExpectedCallbackFrames);
        CHECK(TestAdapter.TxQueueFrames==ExpectedCallbackFrames);
    }
    if(CallbackProbeNbl && CallbackProbeCalls<4) {
        /* Earlier successful sends cannot free the retained failed multi-NB
         * chain's charge. Its own callback may admit a full replacement. */
        CHECK(MustCompleteBeforeProbe->Completions==(CallbackProbeCalls==3?1u:0u));
        CallbackProbeStatus[CallbackProbeCalls++]=CywTxSubmit(&TestAdapter,&TestQueue,CallbackProbeNbl);
    }
    if(Reenter){Reenter=0;CHECK(CywTxSubmit(&TestAdapter,&TestQueue,&Reentrant)==NDIS_STATUS_PENDING);}
}
static NDIS_STATUS CywNetworkSend(PRPI5CYW_ADAPTER adapter,PNET_BUFFER_LIST nbl)
{
    NDIS_STATUS status=CywTxSubmit(adapter,&TestQueue,nbl);ULONG sent;
    if(Immediate && status==NDIS_STATUS_PENDING)CHECK(CywTxPump(adapter,&TestQueue,4,&sent)==0);
    return status;
}
#include "../src/cyw43455/tx_dispatch.h"
static void Init(void)
{
    memset(&TestAdapter,0,sizeof(TestAdapter));memset(&TestQueue,0,sizeof(TestQueue));Clock=0;Credits=100;
    TransferCalls=Busy=FailTransfer=Hook=Reenter=Immediate=Poison=CheckCompleting=0;CHECK(!Locks);
    HookAt=BusyAt=FailAt=CompletionCalls=CompletionNbls=LargestCompletion=ProbeCalls=0;
    CheckCallbackFrames=ExpectedCallbackFrames=ExpectedProbeFrames=ExpectedProbeBytes=0;
    CheckImmediateBoundary=0;
    TransferTicks=0;ProbeNbl=NULL;CancelCompletedId=NULL;
    CallbackProbeCalls=0;CallbackProbeStatus[0]=CallbackProbeStatus[1]=NDIS_STATUS_FAILURE;
    CallbackProbeStatus[2]=CallbackProbeStatus[3]=NDIS_STATUS_FAILURE;
    CallbackProbeNbl=MustCompleteBeforeProbe=NULL;
    PressureBlocked=0;
}
static void Packet(PNET_BUFFER_LIST nbl,PNET_BUFFER nb,ULONG length,PVOID id)
{
    memset(nbl,0,sizeof(*nbl));memset(nb,0,sizeof(*nb));nb->Length=length;
    nbl->First=nb;nbl->CancelId=id;
}
static void PressureFill(PNET_BUFFER_LIST nbl,PNET_BUFFER nb,ULONG count,PVOID id)
{
    ULONG i;
    Init();for(i=0;i<count;i++) {
        Packet(&nbl[i],&nb[i],100,id);
        CHECK(CywTxSubmit(&TestAdapter,&TestQueue,&nbl[i])==NDIS_STATUS_PENDING);
    }
}
static void PressureTests(PNET_BUFFER_LIST nbl,PNET_BUFFER nb,PVOID id)
{
    ULONG sent,i;
    /* Low-pressure scheduling remains exactly four. Start threshold is tested
     * AFTER the normal pump; no ownership or admission limits are enlarged. */
    PressureFill(nbl,nb,35,id);
    CHECK(CywTxPostReceivePump(&TestAdapter,&TestQueue,&sent)==0 && sent==4);
    CHECK(!TestAdapter.TxPressurePasses && TestQueue.Frames==31);
    PressureFill(nbl,nb,36,id);
    CHECK(CywTxPostReceivePump(&TestAdapter,&TestQueue,&sent)==0 && sent==8);
    CHECK(TestAdapter.TxPressurePasses==1 && TestAdapter.TxPressureFrames==4);
    CHECK(TestQueue.Frames==28 && !TestAdapter.TxQueueFull);

    PressureFill(nbl,nb,64,id);Credits=4;
    CHECK(CywTxPostReceivePump(&TestAdapter,&TestQueue,&sent)==0 && sent==4);
    CHECK(!TestAdapter.TxPressurePasses && !TestAdapter.TxPressureFrames);
    PressureFill(nbl,nb,64,id);Credits=5;
    CHECK(CywTxPostReceivePump(&TestAdapter,&TestQueue,&sent)==0 && sent==5);
    CHECK(TestAdapter.TxPressureFrames==1 && TransferCalls==5 && TestQueue.Frames==59);
    PressureFill(nbl,nb,64,id);PressureBlocked=1;
    CHECK(CywTxPostReceivePump(&TestAdapter,&TestQueue,&sent)==0 && sent==4);
    CHECK(!TestAdapter.TxPressurePasses);

    /* Extension deadline: actual completed frames remain owned/completed once.
     * A backward monotonic clock also ends the extension safely. */
    PressureFill(nbl,nb,64,id);TransferTicks=10000;
    CHECK(CywTxPostReceivePump(&TestAdapter,&TestQueue,&sent)==0 && sent==6);
    CHECK(TestAdapter.TxPressureFrames==2 && TestAdapter.TxPressureDeadlineYields==1);
    PressureFill(nbl,nb,64,id);TransferTicks=1000;Hook=6;HookAt=5;
    CHECK(CywTxPostReceivePump(&TestAdapter,&TestQueue,&sent)==0 && sent==5);
    CHECK(TestAdapter.TxPressureDeadlineYields==1 && TestAdapter.TxPressureFrames==1);

    PressureFill(nbl,nb,64,id);BusyAt=6;
    CHECK(CywTxPostReceivePump(&TestAdapter,&TestQueue,&sent)==0 && sent==5);
    CHECK(TransferCalls==6 && TestQueue.Frames==59 && !nbl[5].Completions);
    CHECK(TestAdapter.TxPressureFrames==1);
    PressureFill(nbl,nb,64,id);FailAt=6;
    CHECK(CywTxPostReceivePump(&TestAdapter,&TestQueue,&sent)==STATUS_IO_DEVICE_ERROR && sent==5);
    CHECK(TransferCalls==6 && TestAdapter.TxPressureFrames==1 && TestQueue.Frames==58);
    CHECK(nbl[5].Completions==1 && nbl[5].Status==NDIS_STATUS_FAILURE);
    CywTxFlush(&TestAdapter,&TestQueue,NDIS_STATUS_FAILURE);
    for(i=0;i<64;i++)CHECK(nbl[i].Completions==1);

    PressureFill(nbl,nb,64,id);Hook=2;HookAt=5;
    CHECK(CywTxPostReceivePump(&TestAdapter,&TestQueue,&sent)==0 && sent==5);
    CHECK(nbl[4].Status==NDIS_STATUS_PAUSED && TestQueue.Frames==59);
    CywTxFlush(&TestAdapter,&TestQueue,NDIS_STATUS_PAUSED);
    CHECK(!CywTxOutstanding(&TestQueue));
    PressureFill(nbl,nb,64,id);Hook=7;HookAt=5;
    CHECK(CywTxPostReceivePump(&TestAdapter,&TestQueue,&sent)==0 && sent==5);
    CHECK(TestAdapter.TxPressureFrames==1);
    PressureFill(nbl,nb,64,id);Hook=1;HookAt=5;
    CHECK(CywTxPostReceivePump(&TestAdapter,&TestQueue,&sent)==0 && sent==5);
    CHECK(TransferCalls==5 && TestAdapter.TxCancelled==60 && !CywTxOutstanding(&TestQueue));
    for(i=0;i<64;i++)CHECK(nbl[i].Completions==1);

    /* Full-queue completion reentry and poisoned returned NBLs are safe even
     * when additional sends are selected. No completed packet is replayed. */
    PressureFill(nbl,nb,64,id);Poison=CheckCompleting=1;
    Packet(&Reentrant,&ReentrantNb,100,id);Reenter=1;
    CHECK(CywTxPostReceivePump(&TestAdapter,&TestQueue,&sent)==0 && sent==8);
    CHECK(TestQueue.Frames==57 && !TestAdapter.TxQueueFull && !Reentrant.Completions);
    CHECK(CompletionNbls==8 && LargestCompletion==1);
    CywTxFlush(&TestAdapter,&TestQueue,NDIS_STATUS_PAUSED);
    CHECK(!CywTxOutstanding(&TestQueue) && Reentrant.Completions==1);
    CHECK(!Locks);
}
static void ImmediateOwnershipTests(PNET_BUFFER_LIST nbl,PNET_BUFFER nb,PVOID id)
{
    ULONG sent,i;
    /* The first immediate callback releases one slot and reentry refills it.
     * During the next transfer all 64 frames/bytes/NBLs are still charged:
     * another admission must fail while Completing protects callback lifetime. */
    Init();for(i=0;i<=CYW_TX_LIMIT;i++)Packet(&nbl[i],&nb[i],100,id);
    for(i=0;i<CYW_TX_LIMIT;i++)CHECK(CywTxSubmit(&TestAdapter,&TestQueue,&nbl[i])==NDIS_STATUS_PENDING);
    ProbeNbl=&nbl[CYW_TX_LIMIT];ExpectedProbeFrames=CYW_TX_LIMIT;ExpectedProbeBytes=CYW_TX_LIMIT*100;
    Hook=4;HookAt=2;CheckCallbackFrames=1;ExpectedCallbackFrames=CYW_TX_LIMIT-1;
    Packet(&Reentrant,&ReentrantNb,100,id);Reenter=CheckCompleting=Poison=1;
    CHECK(CywTxPump(&TestAdapter,&TestQueue,4,&sent)==0 && sent==4);
    CHECK(ProbeCalls==1 && TestAdapter.TxQueueFull==1 && !ProbeNbl->Completions);
    CHECK(CompletionCalls==4 && CompletionNbls==4 && LargestCompletion==1);
    CHECK(TestQueue.Count==61 && TestQueue.Outstanding==61 && TestQueue.Frames==61 && TestQueue.Bytes==6100);
    CHECK(!TestQueue.Completing && !Reentrant.Completions);
    for(i=0;i<4;i++)CHECK(nbl[i].Completions==1 && nbl[i].Status==NDIS_STATUS_SUCCESS && !nbl[i].Flags && !nb[i].Next);
    CywTxFlush(&TestAdapter,&TestQueue,NDIS_STATUS_PAUSED);
    CHECK(!CywTxOutstanding(&TestQueue) && !TestQueue.Frames && !TestQueue.Bytes && CompletionNbls==65);
    for(i=0;i<CYW_TX_LIMIT;i++)CHECK(nbl[i].Completions==1);
    CHECK(Reentrant.Completions==1);

    /* Each completed NBL is returned immediately, even with a larger budget. */
    Init();Poison=CheckCompleting=CheckImmediateBoundary=1;
    for(i=0;i<8;i++){Packet(&nbl[i],&nb[i],100,id);CHECK(CywTxSubmit(&TestAdapter,&TestQueue,&nbl[i])==NDIS_STATUS_PENDING);}
    CHECK(CywTxPump(&TestAdapter,&TestQueue,8,&sent)==0 && sent==8);
    CHECK(CompletionCalls==8 && CompletionNbls==8 && LargestCompletion==1 && !CywTxOutstanding(&TestQueue));
    for(i=0;i<8;i++)CHECK(nbl[i].Completions==1);

    /* Partially transmitted multi-NB chains are never completed or uncharged. */
    Init();for(i=0;i<5;i++)Packet(&nbl[i],&nb[i],100,id);
    nb[1].Next=&nb[2];nb[2].Next=&nb[3];
    CHECK(CywTxSubmit(&TestAdapter,&TestQueue,&nbl[0])==NDIS_STATUS_PENDING);
    CHECK(CywTxSubmit(&TestAdapter,&TestQueue,&nbl[1])==NDIS_STATUS_PENDING);
    CHECK(CywTxSubmit(&TestAdapter,&TestQueue,&nbl[4])==NDIS_STATUS_PENDING);
    CHECK(CywTxPump(&TestAdapter,&TestQueue,2,&sent)==0 && sent==2);
    CHECK(nbl[0].Completions==1 && !nbl[1].Completions && !nbl[4].Completions);
    CHECK(TestQueue.Count==2 && TestQueue.Frames==4 && TestQueue.Bytes==400 && TestQueue.Outstanding==2);
    CHECK(TestQueue.Entries[0].Frames==2 && TestQueue.Entries[0].HeldFrames==3);
    CHECK(nb[1].Next==&nb[2] && nb[2].Next==&nb[3] && !nb[3].Next);
    CHECK(CywTxPump(&TestAdapter,&TestQueue,4,&sent)==0 && sent==3 && !CywTxOutstanding(&TestQueue));
    CHECK(CompletionCalls==3 && CompletionNbls==3 && LargestCompletion==1);
    CHECK(nbl[1].Completions==1 && nbl[4].Completions==1 && TestAdapter.TxPackets==5);

    /* Credit exhaustion, BUSY, bus failure and mapping failure must not undo
     * already completed successes or strand pending ownership. */
    Init();Credits=2;CheckCompleting=1;
    for(i=0;i<3;i++){Packet(&nbl[i],&nb[i],100,id);CHECK(CywTxSubmit(&TestAdapter,&TestQueue,&nbl[i])==NDIS_STATUS_PENDING);}
    CHECK(CywTxPump(&TestAdapter,&TestQueue,4,&sent)==0 && sent==2);
    CHECK(CompletionCalls==2 && CompletionNbls==2 && TestQueue.Frames==1 && TestQueue.Bytes==100 && TestQueue.Count==1);
    CHECK(!nbl[2].Completions && !TestQueue.Completing && TestAdapter.TxCreditWaits==1);
    CywTxFlush(&TestAdapter,&TestQueue,NDIS_STATUS_PAUSED);CHECK(!CywTxOutstanding(&TestQueue));

    Init();BusyAt=2;CheckCompleting=Poison=1;
    for(i=0;i<3;i++){Packet(&nbl[i],&nb[i],100,id);CHECK(CywTxSubmit(&TestAdapter,&TestQueue,&nbl[i])==NDIS_STATUS_PENDING);}
    CHECK(CywTxPump(&TestAdapter,&TestQueue,4,&sent)==0 && sent==1 && TransferCalls==2);
    CHECK(nbl[0].Completions==1 && !nbl[1].Completions && TestQueue.Frames==2 && TestQueue.Outstanding==2);
    CHECK(CompletionCalls==1 && TestAdapter.TxCreditWaits==1);
    CHECK(CywTxPump(&TestAdapter,&TestQueue,4,&sent)==0 && sent==2 && CompletionCalls==3 && CompletionNbls==3);
    CHECK(!CywTxOutstanding(&TestQueue) && nbl[1].Completions==1 && nbl[2].Completions==1);

    Init();FailAt=3;CheckCompleting=Poison=1;
    for(i=0;i<4;i++){Packet(&nbl[i],&nb[i],100,id);CHECK(CywTxSubmit(&TestAdapter,&TestQueue,&nbl[i])==NDIS_STATUS_PENDING);}
    CHECK(CywTxPump(&TestAdapter,&TestQueue,4,&sent)==STATUS_IO_DEVICE_ERROR && sent==2);
    CHECK(nbl[0].Status==NDIS_STATUS_SUCCESS && nbl[1].Status==NDIS_STATUS_SUCCESS && nbl[2].Status==NDIS_STATUS_FAILURE);
    CHECK(CompletionCalls==3 && CompletionNbls==3 && TestQueue.Count==1 && TestQueue.Frames==1 && TestQueue.Bytes==100);
    CHECK(!nbl[3].Completions && TestAdapter.TxErrors==1 && TestAdapter.TxPackets==2 && !TestQueue.Completing);
    CywTxFlush(&TestAdapter,&TestQueue,NDIS_STATUS_MEDIA_DISCONNECTED);
    for(i=0;i<4;i++)CHECK(nbl[i].Completions==1);
    CHECK(!CywTxOutstanding(&TestQueue));

    /* Three single-frame sends plus a failing 61-NB chain start at exactly64
     * retained frames. Each successful callback must reject a new 64-NB chain
     * while any of the original 61-NB chain remains owned. The failure's own
     * immediate callback is the first point that can admit the replacement. */
    Init();FailAt=4;CheckCompleting=Poison=1;
    for(i=0;i<CYW_TX_LIMIT;i++)Packet(&nbl[i],&nb[i],100,id);
    for(i=3;i<CYW_TX_LIMIT-1;i++)nb[i].Next=&nb[i+1];
    for(i=0;i<4;i++)CHECK(CywTxSubmit(&TestAdapter,&TestQueue,&nbl[i])==NDIS_STATUS_PENDING);
    CHECK(TestQueue.Frames==64 && TestQueue.Bytes==6400 && TestQueue.Outstanding==4);
    Packet(&Reentrant,&ReentrantNb,100,id);memset(ReentrantChain,0,sizeof(ReentrantChain));
    for(i=0;i<CYW_TX_LIMIT;i++) {
        ReentrantChain[i].Length=100;
        if(i+1<CYW_TX_LIMIT)ReentrantChain[i].Next=&ReentrantChain[i+1];
    }
    Reentrant.First=ReentrantChain;CallbackProbeNbl=&Reentrant;MustCompleteBeforeProbe=&nbl[3];
    CHECK(CywTxPump(&TestAdapter,&TestQueue,4,&sent)==STATUS_IO_DEVICE_ERROR && sent==3);
    CHECK(CallbackProbeCalls==4 && CallbackProbeStatus[0]==NDIS_STATUS_RESOURCES && CallbackProbeStatus[1]==NDIS_STATUS_RESOURCES);
    CHECK(CallbackProbeStatus[2]==NDIS_STATUS_RESOURCES && CallbackProbeStatus[3]==NDIS_STATUS_PENDING);
    CHECK(CompletionCalls==4 && CompletionNbls==4);
    CHECK(TestQueue.Frames==64 && TestQueue.Bytes==6400 && TestQueue.Count==1 && TestQueue.Outstanding==1);
    CHECK(!TestQueue.Completing && !Reentrant.Completions && TestAdapter.TxQueueHighWater==64);
    for(i=0;i<4;i++)CHECK(nbl[i].Completions==1);
    CHECK(nbl[3].Status==NDIS_STATUS_FAILURE && nb[3].Next==&nb[4]);
    CywTxFlush(&TestAdapter,&TestQueue,NDIS_STATUS_MEDIA_DISCONNECTED);
    CHECK(Reentrant.Completions==1 && !CywTxOutstanding(&TestQueue) && !TestQueue.Frames && !TestQueue.Bytes);

    Init();CheckCompleting=Poison=1;
    for(i=0;i<3;i++){Packet(&nbl[i],&nb[i],100,id);CHECK(CywTxSubmit(&TestAdapter,&TestQueue,&nbl[i])==NDIS_STATUS_PENDING);}
    nb[1].MapFail=1;
    CHECK(CywTxPump(&TestAdapter,&TestQueue,4,&sent)==0 && sent==1 && TransferCalls==1);
    CHECK(nbl[0].Completions==1 && nbl[0].Status==NDIS_STATUS_SUCCESS && nbl[1].Completions==1 && nbl[1].Status==NDIS_STATUS_FAILURE);
    CHECK(!nbl[2].Completions && CompletionCalls==2 && TestQueue.Outstanding==1 && TestQueue.Frames==1);
    CywTxFlush(&TestAdapter,&TestQueue,NDIS_STATUS_PAUSED);CHECK(!CywTxOutstanding(&TestQueue));

    /* A cancellation of an already returned NBL cannot retract it or touch
     * its poisoned memory. Active and pending cancellation still aborts. */
    Init();Hook=5;HookAt=2;CancelCompletedId=&nbl[0];CheckCompleting=Poison=1;
    for(i=0;i<3;i++){Packet(&nbl[i],&nb[i],100,&nbl[i]);CHECK(CywTxSubmit(&TestAdapter,&TestQueue,&nbl[i])==NDIS_STATUS_PENDING);}
    CHECK(CywTxPump(&TestAdapter,&TestQueue,4,&sent)==0 && sent==3);
    CHECK(CompletionCalls==3 && CompletionNbls==3 && !TestAdapter.TxCancelled && !CywTxOutstanding(&TestQueue));
    for(i=0;i<3;i++)CHECK(nbl[i].Completions==1 && nbl[i].Status==NDIS_STATUS_SUCCESS);

    Init();Hook=1;HookAt=2;CheckCompleting=Poison=1;
    for(i=0;i<3;i++){Packet(&nbl[i],&nb[i],100,&nbl[i]);CHECK(CywTxSubmit(&TestAdapter,&TestQueue,&nbl[i])==NDIS_STATUS_PENDING);}
    CHECK(CywTxPump(&TestAdapter,&TestQueue,4,&sent)==0 && sent==3);
    CHECK(nbl[0].Status==NDIS_STATUS_SUCCESS && nbl[1].Status==NDIS_STATUS_SEND_ABORTED && nbl[2].Status==NDIS_STATUS_SUCCESS);
    CHECK(CompletionCalls==3 && CompletionNbls==3 && TestAdapter.TxCancelled==1 && !CywTxOutstanding(&TestQueue));
    for(i=0;i<3;i++)CHECK(nbl[i].Completions==1);

    Init();Hook=2;HookAt=2;CheckCompleting=Poison=1;
    for(i=0;i<3;i++){Packet(&nbl[i],&nb[i],100,id);CHECK(CywTxSubmit(&TestAdapter,&TestQueue,&nbl[i])==NDIS_STATUS_PENDING);}
    CHECK(CywTxPump(&TestAdapter,&TestQueue,4,&sent)==0 && sent==2);
    CHECK(nbl[0].Status==NDIS_STATUS_SUCCESS && nbl[1].Status==NDIS_STATUS_PAUSED && nbl[2].Status==NDIS_STATUS_PAUSED);
    CHECK(CompletionNbls==3 && !CywTxOutstanding(&TestQueue) && !TestQueue.Frames && !TestQueue.Bytes);
    for(i=0;i<3;i++)CHECK(nbl[i].Completions==1);

    /* Completion timing does not wait for any elapsed-time batch boundary:
     * each success is returned once before the next transfer, fast or slow. */
    Init();TransferTicks=10000ULL;CheckCompleting=Poison=CheckImmediateBoundary=1;
    for(i=0;i<4;i++){Packet(&nbl[i],&nb[i],100,id);CHECK(CywTxSubmit(&TestAdapter,&TestQueue,&nbl[i])==NDIS_STATUS_PENDING);}
    CHECK(CywTxPump(&TestAdapter,&TestQueue,4,&sent)==0 && sent==4);
    CHECK(CompletionCalls==4 && CompletionNbls==4 && LargestCompletion==1);
    CHECK(!CywTxOutstanding(&TestQueue));

    Init();TransferTicks=30000ULL;CheckCompleting=Poison=CheckImmediateBoundary=1;
    for(i=0;i<4;i++){Packet(&nbl[i],&nb[i],100,id);CHECK(CywTxSubmit(&TestAdapter,&TestQueue,&nbl[i])==NDIS_STATUS_PENDING);}
    CHECK(CywTxPump(&TestAdapter,&TestQueue,4,&sent)==0 && sent==4);
    CHECK(CompletionCalls==4 && CompletionNbls==4 && LargestCompletion==1 && !CywTxOutstanding(&TestQueue));

    Init();Clock=10000ULL;Hook=6;HookAt=2;CheckCompleting=Poison=CheckImmediateBoundary=1;
    for(i=0;i<3;i++){Packet(&nbl[i],&nb[i],100,id);CHECK(CywTxSubmit(&TestAdapter,&TestQueue,&nbl[i])==NDIS_STATUS_PENDING);}
    /* Keep submission time at zero to avoid the independent expiry policy's
     * unsigned age: completion itself does not depend on the clock advancing. */
    for(i=0;i<3;i++)TestQueue.Entries[i].Submitted=0;
    CHECK(CywTxPump(&TestAdapter,&TestQueue,4,&sent)==0 && sent==3);
    CHECK(CompletionCalls==3 && CompletionNbls==3 && LargestCompletion==1 && !CywTxOutstanding(&TestQueue));

    Init();CHECK(CywTxPump(&TestAdapter,&TestQueue,0,&sent)==0 && !sent && !CompletionCalls);
    CHECK(CywTxPump(&TestAdapter,&TestQueue,4,&sent)==0 && !sent && !CompletionCalls && !Locks);
}
int main(void)
{
    NET_BUFFER_LIST nbl[CYW_TX_LIMIT+1];NET_BUFFER nb[CYW_TX_LIMIT+1];ULONG sent,i;PVOID id=&TestAdapter;
    Init();Packet(&nbl[0],&nb[0],100,id);Packet(&nbl[1],&nb[1],80,id);
    nb[0].Next=&nb[1];nb[1].Copy=1;
    CHECK(CywTxSubmit(&TestAdapter,&TestQueue,&nbl[0])==NDIS_STATUS_PENDING && !nbl[0].Completions);
    CHECK(TestQueue.Frames==2 && TestQueue.Bytes==180 && CywTxOutstanding(&TestQueue)==1);
    CHECK(CywTxPump(&TestAdapter,&TestQueue,1,&sent)==0 && sent==1 && !nbl[0].Completions);
    CHECK(TestQueue.Frames==2 && TestQueue.Bytes==180); /* Entire NB chain remains owned. */
    CHECK(CywTxPump(&TestAdapter,&TestQueue,4,&sent)==0 && sent==1 && nbl[0].Completions==1);
    CHECK(!TestQueue.Outstanding && !TestQueue.Count && !TestQueue.Frames && !TestQueue.Bytes && TestAdapter.TxPackets==2);
    CHECK(TestAdapter.TxNblAccepted==1 && TestAdapter.TxNblCompleted==1 && nb[0].Next==&nb[1]);

    Init();for(i=0;i<=CYW_TX_LIMIT;i++)Packet(&nbl[i],&nb[i],100,id);
    for(i=0;i<CYW_TX_LIMIT;i++)CHECK(CywTxSubmit(&TestAdapter,&TestQueue,&nbl[i])==NDIS_STATUS_PENDING);
    CHECK(CywTxSubmit(&TestAdapter,&TestQueue,&nbl[CYW_TX_LIMIT])==NDIS_STATUS_RESOURCES && TestAdapter.TxQueueFull==1);
    CHECK(TestQueue.Outstanding==CYW_TX_LIMIT && TestQueue.Frames==CYW_TX_LIMIT && TestQueue.Bytes==100*CYW_TX_LIMIT);
    CHECK(CYW_TX_LIMIT==64 && TestAdapter.TxBurstAdmissions==0 && TestAdapter.TxQueueFrames==CYW_TX_LIMIT);
    CywTxFlush(&TestAdapter,&TestQueue,NDIS_STATUS_PAUSED);
    CHECK(!TestQueue.Outstanding && TestAdapter.TxNblCompleted==CYW_TX_LIMIT && !TestAdapter.TxQueueFrames);
    CHECK(TestAdapter.Traffic.Discards[1]==CYW_TX_LIMIT && TestAdapter.Traffic.Errors[1]==0);
    for(i=0;i<CYW_TX_LIMIT;i++)CHECK(nbl[i].Completions==1 && nbl[i].Status==NDIS_STATUS_PAUSED);
    CHECK(CywTxSubmit(&TestAdapter,&TestQueue,&nbl[CYW_TX_LIMIT])==NDIS_STATUS_PAUSED);

    Init();Packet(&nbl[0],&nb[0],100,id);Credits=0;
    CHECK(CywTxSubmit(&TestAdapter,&TestQueue,&nbl[0])==NDIS_STATUS_PENDING);
    CHECK(CywTxPump(&TestAdapter,&TestQueue,4,&sent)==0 && !sent && !TransferCalls && !nbl[0].Completions);
    Credits=2;Busy=1;CHECK(CywTxPump(&TestAdapter,&TestQueue,4,&sent)==0 && !sent && TestQueue.Frames==1);
    CHECK(!nbl[0].Completions && TestAdapter.TxCreditWaits==2);
    Busy=0;CHECK(CywTxPump(&TestAdapter,&TestQueue,4,&sent)==0 && nbl[0].Status==NDIS_STATUS_SUCCESS);
    CHECK(TestAdapter.TxPackets==1 && nbl[0].Completions==1);

    /* Cancellation behind blocked head, shared IDs, active send cancellation. */
    Init();Credits=0;
    for(i=0;i<3;i++){Packet(&nbl[i],&nb[i],100,i?&TestQueue:id);CHECK(CywTxSubmit(&TestAdapter,&TestQueue,&nbl[i])==NDIS_STATUS_PENDING);}
    CywTxCancel(&TestQueue,&TestQueue);CHECK(CywTxPump(&TestAdapter,&TestQueue,4,&sent)==0);
    CHECK(!nbl[0].Completions && nbl[1].Status==NDIS_STATUS_SEND_ABORTED && nbl[2].Completions==1);
    CHECK(TestQueue.Count==1 && TestAdapter.TxCancelled==2);Credits=1;Hook=1;
    CHECK(CywTxPump(&TestAdapter,&TestQueue,4,&sent)==0 && nbl[0].Status==NDIS_STATUS_SEND_ABORTED && !TestQueue.Outstanding);

    Init();Packet(&nbl[0],&nb[0],100,id);CHECK(CywTxSubmit(&TestAdapter,&TestQueue,&nbl[0])==NDIS_STATUS_PENDING);
    Hook=2;CHECK(CywTxPump(&TestAdapter,&TestQueue,4,&sent)==0 && nbl[0].Status==NDIS_STATUS_PAUSED && !TestQueue.Outstanding);
    Init();Packet(&nbl[0],&nb[0],100,id);CHECK(CywTxSubmit(&TestAdapter,&TestQueue,&nbl[0])==NDIS_STATUS_PENDING);
    Hook=3;CHECK(CywTxPump(&TestAdapter,&TestQueue,4,&sent)==0 && nbl[0].Status==NDIS_STATUS_LOW_POWER_STATE);

    Init();Packet(&nbl[0],&nb[0],100,id);CHECK(CywTxSubmit(&TestAdapter,&TestQueue,&nbl[0])==NDIS_STATUS_PENDING);
    Credits=0;Clock=CYW_TX_MAX_AGE;CHECK(CywTxPump(&TestAdapter,&TestQueue,4,&sent)==0 && TestAdapter.TxExpired==1 && !TestQueue.Outstanding);
    CHECK(nbl[0].Status==NDIS_STATUS_FAILURE);
    CHECK(TestAdapter.Traffic.Errors[1]==1);

    Init();for(i=0;i<2;i++){Packet(&nbl[i],&nb[i],100,id);CHECK(CywTxSubmit(&TestAdapter,&TestQueue,&nbl[i])==NDIS_STATUS_PENDING);}
    FailTransfer=1;CHECK(CywTxPump(&TestAdapter,&TestQueue,4,&sent)==STATUS_IO_DEVICE_ERROR && nbl[0].Completions==1);
    CywTxFlush(&TestAdapter,&TestQueue,NDIS_STATUS_MEDIA_DISCONNECTED);CHECK(nbl[1].Completions==1 && !TestQueue.Outstanding);
    Init();Packet(&nbl[0],&nb[0],100,id);nb[0].MapFail=1;
    CHECK(CywTxSubmit(&TestAdapter,&TestQueue,&nbl[0])==NDIS_STATUS_PENDING);
    CHECK(CywTxPump(&TestAdapter,&TestQueue,4,&sent)==0 && nbl[0].Status==NDIS_STATUS_FAILURE && !TransferCalls);

    Init();Packet(&nbl[0],&nb[0],13,id);CHECK(CywTxSubmit(&TestAdapter,&TestQueue,&nbl[0])==NDIS_STATUS_INVALID_LENGTH);
    nb[0].Length=1515;CHECK(CywTxSubmit(&TestAdapter,&TestQueue,&nbl[0])==NDIS_STATUS_INVALID_LENGTH);
    nbl[0].First=NULL;CHECK(CywTxSubmit(&TestAdapter,&TestQueue,&nbl[0])==NDIS_STATUS_INVALID_LENGTH);
    for(i=0;i<=CYW_TX_LIMIT;i++){Packet(&nbl[i],&nb[i],100,id);if(i)nb[i-1].Next=&nb[i];}
    CHECK(CywTxSubmit(&TestAdapter,&TestQueue,&nbl[0])==NDIS_STATUS_RESOURCES && !TestQueue.Count);
    CHECK(TestAdapter.TxOversizedNbl==1);

    /* A whole multi-NB chain remains charged until its NBL completes. */
    nb[CYW_TX_LIMIT-1].Next=NULL;Credits=CYW_TX_LIMIT;
    CHECK(CywTxSubmit(&TestAdapter,&TestQueue,&nbl[0])==NDIS_STATUS_PENDING);
    CHECK(CywTxPump(&TestAdapter,&TestQueue,1,&sent)==0 && sent==1 && !nbl[0].Completions);
    CHECK(TestQueue.Frames==CYW_TX_LIMIT && TestQueue.Bytes==CYW_TX_LIMIT*100);
    CHECK(CywTxSubmit(&TestAdapter,&TestQueue,&nbl[CYW_TX_LIMIT])==NDIS_STATUS_RESOURCES);
    CHECK(CywTxPump(&TestAdapter,&TestQueue,CYW_TX_LIMIT,&sent)==0 && sent==CYW_TX_LIMIT-1 && nbl[0].Completions==1);
    CHECK(!TestQueue.Frames && !TestQueue.Completing && !CywTxOutstanding(&TestQueue));

    /* Full queue: completion can reenter with a replacement immediately.
     * Regression: old Outstanding accounting rejected that replacement. */
    Init();for(i=0;i<CYW_TX_LIMIT;i++){Packet(&nbl[i],&nb[i],100,id);CHECK(CywTxSubmit(&TestAdapter,&TestQueue,&nbl[i])==NDIS_STATUS_PENDING);}
    Packet(&Reentrant,&ReentrantNb,100,id);Reenter=CheckCompleting=Poison=1;
    CHECK(CywTxPump(&TestAdapter,&TestQueue,1,&sent)==0 && sent==1);
    CHECK(TestQueue.Outstanding==CYW_TX_LIMIT && TestQueue.Count==CYW_TX_LIMIT && !TestQueue.Completing);
    CHECK(!TestAdapter.TxQueueFull && !Reentrant.Completions && nbl[0].Completions==1);
    CywTxFlush(&TestAdapter,&TestQueue,NDIS_STATUS_LOW_POWER_STATE);
    CHECK(!CywTxOutstanding(&TestQueue) && Reentrant.Completions==1);

    /* Restored 64-frame hard cap, held through no credits then drained in small
     * chunks. No early/fake completion or unlimited retained ownership. */
    Init();Credits=0;
    for(i=0;i<CYW_TX_LIMIT;i++){Packet(&nbl[i],&nb[i],1514,id);CHECK(CywTxSubmit(&TestAdapter,&TestQueue,&nbl[i])==NDIS_STATUS_PENDING);}
    CHECK(CywTxPump(&TestAdapter,&TestQueue,4,&sent)==0 && !sent && !nbl[0].Completions);
    CHECK(!TestAdapter.TxQueueFull && !TestAdapter.TxBurstAdmissions && TestQueue.Bytes==CYW_TX_LIMIT*1514);
    for(i=0;i<CYW_TX_LIMIT/2;i++){Credits=2;CHECK(CywTxPump(&TestAdapter,&TestQueue,2,&sent)==0 && sent==2);}
    for(i=0;i<CYW_TX_LIMIT;i++)CHECK(nbl[i].Completions==1 && nbl[i].Status==NDIS_STATUS_SUCCESS);
    CHECK(!CywTxOutstanding(&TestQueue) && !TestAdapter.TxErrors && !TestQueue.Bytes);

    Init();Packet(&nbl[0],&nb[0],100,id);Packet(&Reentrant,&ReentrantNb,100,id);
    CHECK(CywTxSubmit(&TestAdapter,&TestQueue,&nbl[0])==NDIS_STATUS_PENDING);Reenter=1;
    /* Critical .27 regression: completion reentry refills an otherwise empty
     * queue and the replacement uses this SAME pump's remaining budget. */
    CHECK(CywTxPump(&TestAdapter,&TestQueue,4,&sent)==0 && sent==2 && !TestQueue.Outstanding);
    CHECK(nbl[0].Completions==1 && Reentrant.Completions==1);

    /* Mixed send-chain admission + completion BEFORE submit returns. */
    Init();for(i=0;i<3;i++){Packet(&nbl[i],&nb[i],100,id);if(i)nbl[i-1].Next=&nbl[i];}
    nb[1].Length=1;Immediate=Poison=1;CywDispatchSendChain(&TestAdapter,&nbl[0],1);
    for(i=0;i<3;i++)CHECK(nbl[i].Completions==1);
    CHECK(nbl[0].Flags==0 && nbl[1].Flags==1 && nbl[2].Flags==0);
    CHECK(nbl[1].Status==NDIS_STATUS_INVALID_LENGTH && TestAdapter.TxNblCompleted==2 && !TestQueue.Outstanding);
    CHECK(TestAdapter.Traffic.Errors[1]==1 && TestAdapter.Traffic.Discards[1]==0);
    ImmediateOwnershipTests(nbl,nb,id);
    PressureTests(nbl,nb,id);
    CHECK(!Locks);if(Failures)return 1;
    puts("PASS: actual immediate TX/dispatch: retained 64-frame cap, multi-NB, credit/busy/error exits, cancel/pause, same-pump reentry and poisoned returned ownership");return 0;
}
