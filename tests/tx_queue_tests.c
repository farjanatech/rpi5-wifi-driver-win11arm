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
static ULONG CallbackProbeCalls;
static NDIS_STATUS CallbackProbeStatus[2];
static ULONG64 Clock,TransferTicks;
static PNET_BUFFER_LIST ProbeNbl;
static PNET_BUFFER_LIST CallbackProbeNbl,MustCompleteBeforeProbe;
static PVOID CancelStagedId;
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
static NTSTATUS CywTxTransfer(PRPI5CYW_ADAPTER adapter,PUCHAR data,ULONG length)
{
    (void)adapter;CHECK(!Locks);TransferCalls++;
    CHECK(length>=18 && length<=1518 && data[0]==0x20 && !data[1] && !data[2] && !data[3]);
    Clock+=TransferTicks;
    if(!HookAt || HookAt==TransferCalls) {
        if(Hook==1)CywTxCancel(&TestQueue,TestQueue.Entries[0].CancelId);
        if(Hook==2)CywTxSetGate(&TestQueue,NDIS_STATUS_PAUSED);
        if(Hook==3)CywTxSetGate(&TestQueue,NDIS_STATUS_LOW_POWER_STATE);
        if(Hook==4) {
            ProbeCalls++;
            CHECK(TestQueue.Frames==ExpectedProbeFrames && TestQueue.Bytes==ExpectedProbeBytes);
            CHECK(TestQueue.Outstanding==CYW_TX_LIMIT && TestQueue.Count==CYW_TX_LIMIT-1);
            CHECK(CywTxSubmit(&TestAdapter,&TestQueue,ProbeNbl)==NDIS_STATUS_RESOURCES);
        }
        if(Hook==5)CywTxCancel(&TestQueue,CancelStagedId);
        if(Hook==6)Clock=0;
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
        CHECK(nbl!=(PNET_BUFFER_LIST)(size_t)1 && count<CYW_TX_COMPLETION_BATCH_LIMIT);
        if(nbl==(PNET_BUFFER_LIST)(size_t)1 || count>=CYW_TX_COMPLETION_BATCH_LIMIT)break;
        next=nbl->Next;CHECK(nbl->Completions==0);nbl->Completions++;nbl->Flags=flags;count++;
        if(Poison) {
            /* Returned NBLs may be freed/reused synchronously. The production
             * batch must not traverse or append to this chain after handoff. */
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
    if(CallbackProbeNbl && CallbackProbeCalls<2) {
        /* A detached failed multi-NB chain must already have been returned
         * before a SUCCESS callback can release the other retained charges. */
        CHECK(MustCompleteBeforeProbe->Completions==1);
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
    TransferTicks=0;ProbeNbl=NULL;CancelStagedId=NULL;
    CallbackProbeCalls=0;CallbackProbeStatus[0]=CallbackProbeStatus[1]=NDIS_STATUS_FAILURE;
    CallbackProbeNbl=MustCompleteBeforeProbe=NULL;
}
static void Packet(PNET_BUFFER_LIST nbl,PNET_BUFFER nb,ULONG length,PVOID id)
{
    memset(nbl,0,sizeof(*nbl));memset(nb,0,sizeof(*nb));nb->Length=length;
    nbl->First=nb;nbl->CancelId=id;
}
static void BatchTests(PNET_BUFFER_LIST nbl,PNET_BUFFER nb,PVOID id)
{
    ULONG sent,i;
    /* Retained SUCCESS NBLs still consume the exact 64-frame/byte/NBL cap.
     * Only the callback handoff releases four charges, and reentry may use
     * that released capacity while Completing preserves the lifetime count. */
    Init();for(i=0;i<=CYW_TX_LIMIT;i++)Packet(&nbl[i],&nb[i],100,id);
    for(i=0;i<CYW_TX_LIMIT;i++)CHECK(CywTxSubmit(&TestAdapter,&TestQueue,&nbl[i])==NDIS_STATUS_PENDING);
    ProbeNbl=&nbl[CYW_TX_LIMIT];ExpectedProbeFrames=CYW_TX_LIMIT;ExpectedProbeBytes=CYW_TX_LIMIT*100;
    Hook=4;HookAt=2;CheckCallbackFrames=1;ExpectedCallbackFrames=CYW_TX_LIMIT-4;
    Packet(&Reentrant,&ReentrantNb,100,id);Reenter=CheckCompleting=Poison=1;
    CHECK(CywTxPump(&TestAdapter,&TestQueue,4,&sent)==0 && sent==4);
    CHECK(ProbeCalls==1 && TestAdapter.TxQueueFull==1 && !ProbeNbl->Completions);
    CHECK(CompletionCalls==1 && CompletionNbls==4 && LargestCompletion==4);
    CHECK(TestAdapter.TxCompletionBatchCalls==1 && TestAdapter.TxCompletionBatchNbls==4 && TestAdapter.TxCompletionBatchMax==4);
    CHECK(TestQueue.Count==61 && TestQueue.Outstanding==61 && TestQueue.Frames==61 && TestQueue.Bytes==6100);
    CHECK(!TestQueue.Completing && !Reentrant.Completions);
    for(i=0;i<4;i++)CHECK(nbl[i].Completions==1 && nbl[i].Status==NDIS_STATUS_SUCCESS && !nbl[i].Flags && !nb[i].Next);
    CywTxFlush(&TestAdapter,&TestQueue,NDIS_STATUS_PAUSED);
    CHECK(!CywTxOutstanding(&TestQueue) && !TestQueue.Frames && !TestQueue.Bytes && CompletionNbls==65);
    for(i=0;i<CYW_TX_LIMIT;i++)CHECK(nbl[i].Completions==1);
    CHECK(Reentrant.Completions==1);

    /* Even a larger caller budget cannot enlarge the fixed four-NBL batch. */
    Init();Poison=CheckCompleting=1;
    for(i=0;i<8;i++){Packet(&nbl[i],&nb[i],100,id);CHECK(CywTxSubmit(&TestAdapter,&TestQueue,&nbl[i])==NDIS_STATUS_PENDING);}
    CHECK(CywTxPump(&TestAdapter,&TestQueue,8,&sent)==0 && sent==8);
    CHECK(CompletionCalls==2 && CompletionNbls==8 && LargestCompletion==4 && !CywTxOutstanding(&TestQueue));
    CHECK(TestAdapter.TxCompletionBatchCalls==2 && TestAdapter.TxCompletionBatchNbls==8);
    for(i=0;i<8;i++)CHECK(nbl[i].Completions==1);

    /* Partially transmitted multi-NB chains are never staged or uncharged. */
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
    CHECK(CompletionCalls==2 && CompletionNbls==3 && LargestCompletion==2);
    CHECK(nbl[1].Completions==1 && nbl[4].Completions==1 && TestAdapter.TxPackets==5);

    /* Exit paths flush completed successes: credit exhaustion, BUSY, failed
     * bus transfer and mapping failure leave pending ownership recoverable. */
    Init();Credits=2;CheckCompleting=1;
    for(i=0;i<3;i++){Packet(&nbl[i],&nb[i],100,id);CHECK(CywTxSubmit(&TestAdapter,&TestQueue,&nbl[i])==NDIS_STATUS_PENDING);}
    CHECK(CywTxPump(&TestAdapter,&TestQueue,4,&sent)==0 && sent==2);
    CHECK(CompletionCalls==1 && CompletionNbls==2 && TestQueue.Frames==1 && TestQueue.Bytes==100 && TestQueue.Count==1);
    CHECK(!nbl[2].Completions && !TestQueue.Completing && TestAdapter.TxCreditWaits==1);
    CywTxFlush(&TestAdapter,&TestQueue,NDIS_STATUS_PAUSED);CHECK(!CywTxOutstanding(&TestQueue));

    Init();BusyAt=2;CheckCompleting=Poison=1;
    for(i=0;i<3;i++){Packet(&nbl[i],&nb[i],100,id);CHECK(CywTxSubmit(&TestAdapter,&TestQueue,&nbl[i])==NDIS_STATUS_PENDING);}
    CHECK(CywTxPump(&TestAdapter,&TestQueue,4,&sent)==0 && sent==1 && TransferCalls==2);
    CHECK(nbl[0].Completions==1 && !nbl[1].Completions && TestQueue.Frames==2 && TestQueue.Outstanding==2);
    CHECK(CompletionCalls==1 && TestAdapter.TxCreditWaits==1);
    CHECK(CywTxPump(&TestAdapter,&TestQueue,4,&sent)==0 && sent==2 && CompletionCalls==2 && CompletionNbls==3);
    CHECK(!CywTxOutstanding(&TestQueue) && nbl[1].Completions==1 && nbl[2].Completions==1);

    Init();FailAt=3;CheckCompleting=Poison=1;
    for(i=0;i<4;i++){Packet(&nbl[i],&nb[i],100,id);CHECK(CywTxSubmit(&TestAdapter,&TestQueue,&nbl[i])==NDIS_STATUS_PENDING);}
    CHECK(CywTxPump(&TestAdapter,&TestQueue,4,&sent)==STATUS_IO_DEVICE_ERROR && sent==2);
    CHECK(nbl[0].Status==NDIS_STATUS_SUCCESS && nbl[1].Status==NDIS_STATUS_SUCCESS && nbl[2].Status==NDIS_STATUS_FAILURE);
    CHECK(CompletionCalls==2 && CompletionNbls==3 && TestQueue.Count==1 && TestQueue.Frames==1 && TestQueue.Bytes==100);
    CHECK(!nbl[3].Completions && TestAdapter.TxErrors==1 && TestAdapter.TxPackets==2 && !TestQueue.Completing);
    CywTxFlush(&TestAdapter,&TestQueue,NDIS_STATUS_MEDIA_DISCONNECTED);
    for(i=0;i<4;i++)CHECK(nbl[i].Completions==1);
    CHECK(!CywTxOutstanding(&TestQueue));

    /* Regression: three staged successes plus a failed 61-NB chain retain
     * exactly 64 frames. Returning the successes BEFORE the detached failure
     * would expose 64 free admissions while still owning the failed 61 NBs.
     * The failure callback first rejects a new 64-NB chain (3 still held);
     * only the later success callback can admit that full replacement. */
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
    CHECK(CallbackProbeCalls==2 && CallbackProbeStatus[0]==NDIS_STATUS_RESOURCES && CallbackProbeStatus[1]==NDIS_STATUS_PENDING);
    CHECK(CompletionCalls==2 && CompletionNbls==4 && TestAdapter.TxCompletionBatchNbls==3);
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

    /* A cancellation of an already fully-transferred staged NBL cannot
     * retract it. Cancellation of an active or pending NBL still aborts it. */
    Init();Hook=5;HookAt=2;CancelStagedId=&nbl[0];CheckCompleting=Poison=1;
    for(i=0;i<3;i++){Packet(&nbl[i],&nb[i],100,&nbl[i]);CHECK(CywTxSubmit(&TestAdapter,&TestQueue,&nbl[i])==NDIS_STATUS_PENDING);}
    CHECK(CywTxPump(&TestAdapter,&TestQueue,4,&sent)==0 && sent==3);
    CHECK(CompletionCalls==1 && CompletionNbls==3 && !TestAdapter.TxCancelled && !CywTxOutstanding(&TestQueue));
    for(i=0;i<3;i++)CHECK(nbl[i].Completions==1 && nbl[i].Status==NDIS_STATUS_SUCCESS);

    Init();Hook=1;HookAt=2;CheckCompleting=Poison=1;
    for(i=0;i<3;i++){Packet(&nbl[i],&nb[i],100,&nbl[i]);CHECK(CywTxSubmit(&TestAdapter,&TestQueue,&nbl[i])==NDIS_STATUS_PENDING);}
    CHECK(CywTxPump(&TestAdapter,&TestQueue,4,&sent)==0 && sent==3);
    CHECK(nbl[0].Status==NDIS_STATUS_SUCCESS && nbl[1].Status==NDIS_STATUS_SEND_ABORTED && nbl[2].Status==NDIS_STATUS_SUCCESS);
    CHECK(CompletionCalls==2 && CompletionNbls==3 && TestAdapter.TxCancelled==1 && !CywTxOutstanding(&TestQueue));
    for(i=0;i<3;i++)CHECK(nbl[i].Completions==1);

    Init();Hook=2;HookAt=2;CheckCompleting=Poison=1;
    for(i=0;i<3;i++){Packet(&nbl[i],&nb[i],100,id);CHECK(CywTxSubmit(&TestAdapter,&TestQueue,&nbl[i])==NDIS_STATUS_PENDING);}
    CHECK(CywTxPump(&TestAdapter,&TestQueue,4,&sent)==0 && sent==2);
    CHECK(nbl[0].Status==NDIS_STATUS_SUCCESS && nbl[1].Status==NDIS_STATUS_PAUSED && nbl[2].Status==NDIS_STATUS_PAUSED);
    CHECK(CompletionNbls==3 && !CywTxOutstanding(&TestQueue) && !TestQueue.Frames && !TestQueue.Bytes);
    for(i=0;i<3;i++)CHECK(nbl[i].Completions==1);

    /* 2ms is checked between operations, not a hard I/O execution deadline.
     * A slow in-flight transfer flushes at the next safe boundary. */
    Init();TransferTicks=10000ULL;CheckCompleting=Poison=1;
    for(i=0;i<4;i++){Packet(&nbl[i],&nb[i],100,id);CHECK(CywTxSubmit(&TestAdapter,&TestQueue,&nbl[i])==NDIS_STATUS_PENDING);}
    CHECK(CywTxPump(&TestAdapter,&TestQueue,4,&sent)==0 && sent==4);
    CHECK(CompletionCalls==2 && CompletionNbls==4 && LargestCompletion==3 && TestAdapter.TxCompletionBatchMax==3);
    CHECK(!CywTxOutstanding(&TestQueue));

    Init();TransferTicks=30000ULL;CheckCompleting=Poison=1;
    for(i=0;i<4;i++){Packet(&nbl[i],&nb[i],100,id);CHECK(CywTxSubmit(&TestAdapter,&TestQueue,&nbl[i])==NDIS_STATUS_PENDING);}
    CHECK(CywTxPump(&TestAdapter,&TestQueue,4,&sent)==0 && sent==4);
    CHECK(CompletionCalls==2 && CompletionNbls==4 && LargestCompletion==2 && !CywTxOutstanding(&TestQueue));

    Init();Clock=10000ULL;Hook=6;HookAt=2;CheckCompleting=Poison=1;
    for(i=0;i<3;i++){Packet(&nbl[i],&nb[i],100,id);CHECK(CywTxSubmit(&TestAdapter,&TestQueue,&nbl[i])==NDIS_STATUS_PENDING);}
    /* Keep submission time at zero so this tests the batch's rollback guard,
     * rather than the independent queue expiry policy's unsigned age. */
    for(i=0;i<3;i++)TestQueue.Entries[i].Submitted=0;
    CHECK(CywTxPump(&TestAdapter,&TestQueue,4,&sent)==0 && sent==3);
    CHECK(CompletionCalls==2 && CompletionNbls==3 && LargestCompletion==2 && !CywTxOutstanding(&TestQueue));

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
    /* The first completion is handed back at this pump's exit; a send
     * reentered there remains queued for the existing next worker turn. */
    CHECK(CywTxPump(&TestAdapter,&TestQueue,4,&sent)==0 && sent==1 && TestQueue.Outstanding==1);
    CHECK(nbl[0].Completions==1 && !Reentrant.Completions);
    CHECK(CywTxPump(&TestAdapter,&TestQueue,4,&sent)==0 && sent==1 && !TestQueue.Outstanding);
    CHECK(nbl[0].Completions==1 && Reentrant.Completions==1);

    /* Mixed send-chain admission + completion BEFORE submit returns. */
    Init();for(i=0;i<3;i++){Packet(&nbl[i],&nb[i],100,id);if(i)nbl[i-1].Next=&nbl[i];}
    nb[1].Length=1;Immediate=Poison=1;CywDispatchSendChain(&TestAdapter,&nbl[0],1);
    for(i=0;i<3;i++)CHECK(nbl[i].Completions==1);
    CHECK(nbl[0].Flags==0 && nbl[1].Flags==1 && nbl[2].Flags==0);
    CHECK(nbl[1].Status==NDIS_STATUS_INVALID_LENGTH && TestAdapter.TxNblCompleted==2 && !TestQueue.Outstanding);
    CHECK(TestAdapter.Traffic.Errors[1]==1 && TestAdapter.Traffic.Discards[1]==0);
    BatchTests(nbl,nb,id);
    CHECK(!Locks);if(Failures)return 1;
    puts("PASS: actual TX/dispatch + bounded completion batching: retained 64-frame cap, multi-NB, credit/busy/error exits, cancel/pause, age bound, reentrancy and poisoned returned chains");return 0;
}
