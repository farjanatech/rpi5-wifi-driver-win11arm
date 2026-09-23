/* SPDX-License-Identifier: GPL-3.0-or-later
 * Included by tx_queue.h. Worker-local, completion-only batching: payloads,
 * FIFO order, credits, and TX/RX processing budgets are not changed.
 * No allocations/timers. All NBLs here are fully transferred SUCCESS results.
 */
#define CYW_TX_COMPLETION_BATCH_LIMIT 4u
#define CYW_TX_COMPLETION_BATCH_AGE 20000ULL /* 2 ms; checked between operations */
typedef struct {
    PNET_BUFFER_LIST Head,Tail;
    ULONG Count,Frames,Bytes;
    ULONG64 Started;
} CYW_TX_COMPLETION_BATCH;

/* Caller holds Q.Lock while detaching this NBL; all original NB chains remain
 * intact. The charge is still included in Q.Frames/Bytes/Outstanding. */
static VOID CywTxBatchAppend(CYW_TX_COMPLETION_BATCH *Batch,PNET_BUFFER_LIST Nbl,
    ULONG Frames,ULONG Bytes,ULONG64 Now)
{
    NET_BUFFER_LIST_NEXT_NBL(Nbl)=NULL;NET_BUFFER_LIST_STATUS(Nbl)=NDIS_STATUS_SUCCESS;
    if(Batch->Tail)NET_BUFFER_LIST_NEXT_NBL(Batch->Tail)=Nbl;
    else {Batch->Head=Nbl;Batch->Started=Now;}
    Batch->Tail=Nbl;Batch->Count++;Batch->Frames+=Frames;Batch->Bytes+=Bytes;
}
static VOID CywTxBatchFlush(PRPI5CYW_ADAPTER A,CYW_TX_STATE *Q,CYW_TX_COMPLETION_BATCH *Batch)
{
    KIRQL irql;PNET_BUFFER_LIST chain;ULONG count;
    if(!Batch->Count)return;
    chain=Batch->Head;count=Batch->Count;
    /* Release admission immediately before returning ownership: completion may
     * synchronously reenter send. Completing keeps pause's lifetime reference
     * until the callback returns, even if all pending queue entries are empty. */
    KeAcquireSpinLock(&Q->Lock,&irql);
    Q->Frames-=Batch->Frames;Q->Bytes-=Batch->Bytes;
    Q->Outstanding-=count;Q->Completing+=count;A->TxQueueFrames=Q->Frames;
    A->TxCompletionBatchCalls++;A->TxCompletionBatchNbls+=count;
    if(count>A->TxCompletionBatchMax)A->TxCompletionBatchMax=count;
    KeReleaseSpinLock(&Q->Lock,irql);
    RtlZeroMemory(Batch,sizeof(*Batch));
    NdisMSendNetBufferListsComplete(A->MiniportHandle,chain,0);
    /* All chain pointers/data are inaccessible after the NDIS call. */
    KeAcquireSpinLock(&Q->Lock,&irql);
    Q->Completing-=count;A->TxNblCompleted+=count;
    KeReleaseSpinLock(&Q->Lock,irql);
}
static VOID CywTxBatchFlushIfDue(PRPI5CYW_ADAPTER A,CYW_TX_STATE *Q,CYW_TX_COMPLETION_BATCH *Batch)
{
    ULONG64 now;
    if(!Batch->Count)return;
    now=KeQueryInterruptTime();
    if(Batch->Count>=CYW_TX_COMPLETION_BATCH_LIMIT || now<Batch->Started ||
        now-Batch->Started>=CYW_TX_COMPLETION_BATCH_AGE)
        CywTxBatchFlush(A,Q,Batch);
}
