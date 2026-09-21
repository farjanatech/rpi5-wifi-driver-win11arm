/* SPDX-License-Identifier: GPL-3.0-or-later
 * Pending completion / busy retry design references Ahmed ARIF's GPL-2.0-or-
 * later cyw43455.c at 929bdd689d1e18d0ef71214741d4e16eec74409c.
 * This bounded implementation and its host simulation share this exact file.
 * CywTxCanTransfer / CywTxTransfer are supplied by the serialized bus owner.
 */
static VOID CywTxSetGate(CYW_TX_STATE *Q, NDIS_STATUS Status)
{
    KIRQL irql;KeAcquireSpinLock(&Q->Lock,&irql);Q->Gate=Status;
    KeReleaseSpinLock(&Q->Lock,irql);
}
static ULONG CywTxOutstanding(CYW_TX_STATE *Q)
{
    ULONG count;KIRQL irql;KeAcquireSpinLock(&Q->Lock,&irql);
    count=Q->Outstanding;KeReleaseSpinLock(&Q->Lock,irql);return count;
}
static NDIS_STATUS CywTxSubmit(PRPI5CYW_ADAPTER A,CYW_TX_STATE *Q,PNET_BUFFER_LIST Nbl)
{
    PNET_BUFFER nb;ULONG frames=0,bytes=0,len;KIRQL irql;
    CYW_PENDING_SEND *item;NDIS_STATUS status;
    KeAcquireSpinLock(&Q->Lock,&irql);status=Q->Gate;
    KeReleaseSpinLock(&Q->Lock,irql);
    if(status!=NDIS_STATUS_SUCCESS)return status;
    for(nb=NET_BUFFER_LIST_FIRST_NB(Nbl);nb;nb=NET_BUFFER_NEXT_NB(nb)) {
        len=NET_BUFFER_DATA_LENGTH(nb);
        if(len<14 || len>1514)return NDIS_STATUS_INVALID_LENGTH;
        if(++frames>CYW_TX_LIMIT)return NDIS_STATUS_RESOURCES;
        bytes+=len;
    }
    if(!frames)return NDIS_STATUS_INVALID_LENGTH;
    KeAcquireSpinLock(&Q->Lock,&irql);status=Q->Gate;
    if(status==NDIS_STATUS_SUCCESS) {
        if(Q->Outstanding>=CYW_TX_LIMIT || Q->Frames>CYW_TX_LIMIT-frames) {
            A->TxQueueFull++;status=NDIS_STATUS_RESOURCES;
        } else {
            item=&Q->Entries[Q->Count++];RtlZeroMemory(item,sizeof(*item));
            item->Nbl=Nbl;item->Next=NET_BUFFER_LIST_FIRST_NB(Nbl);
            item->CancelId=NDIS_GET_NET_BUFFER_LIST_CANCEL_ID(Nbl);
            item->Frames=item->HeldFrames=frames;item->Bytes=bytes;item->Submitted=KeQueryInterruptTime();
            Q->Frames+=frames;Q->Bytes+=bytes;Q->Outstanding++;
            if(Q->Frames>A->TxQueueHighWater)A->TxQueueHighWater=Q->Frames;
            A->TxNblAccepted++;status=NDIS_STATUS_PENDING;
        }
    }
    KeReleaseSpinLock(&Q->Lock,irql);return status;
}
static VOID CywTxCancel(CYW_TX_STATE *Q,PVOID CancelId)
{
    ULONG i;KIRQL irql;KeAcquireSpinLock(&Q->Lock,&irql);
    for(i=0;i<Q->Count;++i)if(Q->Entries[i].CancelId==CancelId)Q->Entries[i].Cancelled=TRUE;
    KeReleaseSpinLock(&Q->Lock,irql);
}
/* Caller holds Lock; at most 64 metadata records are moved, never packet data. */
static PNET_BUFFER_LIST CywTxRemove(CYW_TX_STATE *Q,ULONG Index)
{
    PNET_BUFFER_LIST nbl=Q->Entries[Index].Nbl;ULONG i;
    Q->Frames-=Q->Entries[Index].HeldFrames;Q->Bytes-=Q->Entries[Index].Bytes;
    for(i=Index+1;i<Q->Count;++i)Q->Entries[i-1]=Q->Entries[i];
    Q->Count--;RtlZeroMemory(&Q->Entries[Q->Count],sizeof(Q->Entries[0]));
    return nbl;
}
static VOID CywTxComplete(PRPI5CYW_ADAPTER A,CYW_TX_STATE *Q,PNET_BUFFER_LIST Nbl,NDIS_STATUS Status)
{
    KIRQL irql;
    /* PASSIVE_LEVEL worker only. Do not touch Nbl after completion/reentrancy. */
    NET_BUFFER_LIST_NEXT_NBL(Nbl)=NULL;NET_BUFFER_LIST_STATUS(Nbl)=Status;
    if(Status!=NDIS_STATUS_SUCCESS)A->TxErrors++;
    if(Status==NDIS_STATUS_SEND_ABORTED)A->TxCancelled++;
    NdisMSendNetBufferListsComplete(A->MiniportHandle,Nbl,0);
    KeAcquireSpinLock(&Q->Lock,&irql);Q->Outstanding--;A->TxNblCompleted++;
    KeReleaseSpinLock(&Q->Lock,irql);
}
static NDIS_STATUS CywTxAbortStatus(CYW_TX_STATE *Q,CYW_PENDING_SEND *Item,ULONG64 Now)
{
    if(Q->Gate!=NDIS_STATUS_SUCCESS)return Q->Gate;
    if(Item->Cancelled)return NDIS_STATUS_SEND_ABORTED;
    if(Now-Item->Submitted>=CYW_TX_MAX_AGE)return NDIS_STATUS_FAILURE;
    return NDIS_STATUS_SUCCESS;
}
static VOID CywTxFlush(PRPI5CYW_ADAPTER A,CYW_TX_STATE *Q,NDIS_STATUS Status)
{
    KIRQL irql;PNET_BUFFER_LIST nbl;
    CywTxSetGate(Q,Status);
    for(;;) {
        KeAcquireSpinLock(&Q->Lock,&irql);
        nbl=Q->Count?CywTxRemove(Q,0):NULL;
        KeReleaseSpinLock(&Q->Lock,irql);
        if(!nbl)break;
        CywTxComplete(A,Q,nbl,Status);
    }
}
static NTSTATUS CywTxPump(PRPI5CYW_ADAPTER A,CYW_TX_STATE *Q,ULONG Budget,PULONG Sent)
{
    ULONG i,len;ULONG64 now;KIRQL irql;PUCHAR data;PNET_BUFFER nb;
    PNET_BUFFER_LIST nbl;NDIS_STATUS completion=NDIS_STATUS_SUCCESS;NTSTATUS status;
    *Sent=0;
    /* Cancelled/expired entries behind a flow-controlled head must not wait
     * for credits. Completion is outside Lock and permits NDIS reentrancy. */
    for(i=0;i<CYW_TX_LIMIT;++i) {
        ULONG index;now=KeQueryInterruptTime();nbl=NULL;
        KeAcquireSpinLock(&Q->Lock,&irql);
        for(index=0;index<Q->Count;++index) {
            completion=CywTxAbortStatus(Q,&Q->Entries[index],now);
            if(completion!=NDIS_STATUS_SUCCESS) {
                if(Q->Gate==NDIS_STATUS_SUCCESS && !Q->Entries[index].Cancelled)A->TxExpired++;
                nbl=CywTxRemove(Q,index);break;
            }
        }
        KeReleaseSpinLock(&Q->Lock,irql);
        if(!nbl)break;
        CywTxComplete(A,Q,nbl,completion);
    }
    while(*Sent<Budget) {
        KeAcquireSpinLock(&Q->Lock,&irql);
        if(!Q->Count) {KeReleaseSpinLock(&Q->Lock,irql);break;}
        completion=CywTxAbortStatus(Q,&Q->Entries[0],KeQueryInterruptTime());
        if(completion!=NDIS_STATUS_SUCCESS) {
            nbl=CywTxRemove(Q,0);KeReleaseSpinLock(&Q->Lock,irql);
            CywTxComplete(A,Q,nbl,completion);break;
        }
        nb=Q->Entries[0].Next;
        now=(KeQueryInterruptTime()-Q->Entries[0].Submitted)/10000ULL;
        if(now>A->TxQueueMaxDelayMs)A->TxQueueMaxDelayMs=now>0xffffffffULL?0xffffffffUL:(ULONG)now;
        KeReleaseSpinLock(&Q->Lock,irql);
        if(!CywTxCanTransfer(A)) {A->TxCreditWaits++;break;}
        len=NET_BUFFER_DATA_LENGTH(nb);
        RtlZeroMemory(Q->Frame,4);Q->Frame[0]=0x20;
        data=NdisGetDataBuffer(nb,len,Q->Frame+4,1,0);
        if(data && data!=Q->Frame+4)RtlCopyMemory(Q->Frame+4,data,len);
        status=data?CywTxTransfer(A,Q->Frame,len+4):STATUS_INSUFFICIENT_RESOURCES;
        if(status==STATUS_DEVICE_BUSY) {A->TxCreditWaits++;break;}
        /* Head cannot be removed by submission/cancellation while the worker
         * is in SDIO. Observe cancellation/pause again before completing. */
        KeAcquireSpinLock(&Q->Lock,&irql);nbl=NULL;
        completion=CywTxAbortStatus(Q,&Q->Entries[0],KeQueryInterruptTime());
        if(NT_SUCCESS(status)) {
            A->TxPackets++;(*Sent)++;
            Q->Entries[0].Next=NET_BUFFER_NEXT_NB(nb);
            /* NDIS owns the whole NB chain: already-transferred buffers are
             * still retained until NBL completion, so do not release their
             * admission budget early. */
            Q->Entries[0].Frames--;
        } else if(completion==NDIS_STATUS_SUCCESS)completion=NDIS_STATUS_FAILURE;
        if(completion!=NDIS_STATUS_SUCCESS || !Q->Entries[0].Frames)nbl=CywTxRemove(Q,0);
        KeReleaseSpinLock(&Q->Lock,irql);
        if(nbl)CywTxComplete(A,Q,nbl,completion);
        if(!NT_SUCCESS(status) && data)return status; /* Bus fault: fail rest in worker exit. */
        if(!data)break; /* Mapping failure is per-NBL, not a radio failure. */
    }
    return STATUS_SUCCESS;
}
