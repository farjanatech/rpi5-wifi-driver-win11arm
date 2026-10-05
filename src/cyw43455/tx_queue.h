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
    count=Q->Outstanding+Q->BacklogCount+Q->Completing;
    KeReleaseSpinLock(&Q->Lock,irql);return count;
}
static NDIS_STATUS CywTxPrepare(PRPI5CYW_ADAPTER A,PNET_BUFFER_LIST Nbl,CYW_PENDING_SEND *Item)
{
    PNET_BUFFER nb;ULONG frames=0,bytes=0,len;
    for(nb=NET_BUFFER_LIST_FIRST_NB(Nbl);nb;nb=NET_BUFFER_NEXT_NB(nb)) {
        len=NET_BUFFER_DATA_LENGTH(nb);
        if(len<14 || len>1514)return NDIS_STATUS_INVALID_LENGTH;
        if(++frames>CYW_TX_LIMIT) {A->TxOversizedNbl++;return NDIS_STATUS_RESOURCES;}
        bytes+=len;
    }
    if(!frames)return NDIS_STATUS_INVALID_LENGTH;
    RtlZeroMemory(Item,sizeof(*Item));
    Item->Nbl=Nbl;Item->Next=NET_BUFFER_LIST_FIRST_NB(Nbl);
    Item->CancelId=NDIS_GET_NET_BUFFER_LIST_CANCEL_ID(Nbl);
    Item->Frames=Item->HeldFrames=frames;Item->Bytes=bytes;
    Item->Submitted=KeQueryInterruptTime();
    return NDIS_STATUS_SUCCESS;
}
static BOOLEAN CywTxActiveFits(CYW_TX_STATE *Q,ULONG Frames)
{
    return (BOOLEAN)(Q->Outstanding<CYW_TX_LIMIT && Q->Count<CYW_TX_LIMIT &&
        Q->Frames<=CYW_TX_LIMIT-Frames);
}
static BOOLEAN CywTxBacklogFits(CYW_TX_STATE *Q,ULONG Frames)
{
    return (BOOLEAN)(Q->BacklogCount<CYW_TX_BACKLOG_LIMIT &&
        Q->BacklogFrames<=CYW_TX_BACKLOG_LIMIT-Frames);
}
static VOID CywTxActivateLocked(PRPI5CYW_ADAPTER A,CYW_TX_STATE *Q,
    const CYW_PENDING_SEND *Item,BOOLEAN CountAccepted)
{
    Q->Entries[Q->Count++]=*Item;
    Q->Frames+=Item->HeldFrames;Q->Bytes+=Item->Bytes;Q->Outstanding++;
    A->TxQueueFrames=Q->Frames;
    if(Q->Frames>A->TxQueueHighWater)A->TxQueueHighWater=Q->Frames;
    if(CountAccepted)A->TxNblAccepted++;
}
static VOID CywTxBacklogLocked(PRPI5CYW_ADAPTER A,CYW_TX_STATE *Q,
    const CYW_PENDING_SEND *Item)
{
    Q->Backlog[Q->BacklogCount++]=*Item;
    Q->BacklogFrames+=Item->HeldFrames;Q->BacklogBytes+=Item->Bytes;
    A->TxBacklogCurrent=Q->BacklogFrames;A->TxBacklogNblCurrent=Q->BacklogCount;
    if(Q->BacklogFrames>A->TxBacklogHighWater)A->TxBacklogHighWater=Q->BacklogFrames;
    A->TxBacklogAccepted++;A->TxNblAccepted++;
}
/* Legacy active-only admission is retained only by host ownership/scheduler
 * tests for exact v0.7.1.8 rollback reasoning. Production uses the backlog
 * admission below, so do not leave an unreferenced static function in /W4 builds. */
#ifdef RPI5CYW_HOST_TEST
static NDIS_STATUS CywTxSubmit(PRPI5CYW_ADAPTER A,CYW_TX_STATE *Q,PNET_BUFFER_LIST Nbl)
{
    KIRQL irql;CYW_PENDING_SEND item;NDIS_STATUS status;
    KeAcquireSpinLock(&Q->Lock,&irql);status=Q->Gate;KeReleaseSpinLock(&Q->Lock,irql);
    if(status!=NDIS_STATUS_SUCCESS)return status;
    status=CywTxPrepare(A,Nbl,&item);if(status!=NDIS_STATUS_SUCCESS)return status;
    KeAcquireSpinLock(&Q->Lock,&irql);status=Q->Gate;
    if(status==NDIS_STATUS_SUCCESS) {
        if(!CywTxActiveFits(Q,item.HeldFrames)) {
            A->TxQueueFull++;status=NDIS_STATUS_RESOURCES;
        } else {
            CywTxActivateLocked(A,Q,&item,TRUE);status=NDIS_STATUS_PENDING;
        }
    }
    KeReleaseSpinLock(&Q->Lock,irql);return status;
}
#endif
/* Production admission. The active queue remains exactly 64 frames. Once any
 * NBL is backlogged, later NBLs also backlog until older work is promoted, so
 * concurrent/reentrant sends cannot overtake deferred ownership. */
static NDIS_STATUS CywTxSubmitWithBacklog(
    PRPI5CYW_ADAPTER A,CYW_TX_STATE *Q,PNET_BUFFER_LIST Nbl)
{
    KIRQL irql;CYW_PENDING_SEND item;NDIS_STATUS status;
    KeAcquireSpinLock(&Q->Lock,&irql);status=Q->Gate;KeReleaseSpinLock(&Q->Lock,irql);
    if(status!=NDIS_STATUS_SUCCESS)return status;
    status=CywTxPrepare(A,Nbl,&item);if(status!=NDIS_STATUS_SUCCESS)return status;
    KeAcquireSpinLock(&Q->Lock,&irql);status=Q->Gate;
    if(status==NDIS_STATUS_SUCCESS) {
        if(!Q->BacklogCount && CywTxActiveFits(Q,item.HeldFrames)) {
            CywTxActivateLocked(A,Q,&item,TRUE);status=NDIS_STATUS_PENDING;
        } else if(CywTxBacklogFits(Q,item.HeldFrames)) {
            CywTxBacklogLocked(A,Q,&item);status=NDIS_STATUS_PENDING;
        } else {
            A->TxQueueFull++;A->TxBacklogFull++;status=NDIS_STATUS_RESOURCES;
        }
    }
    KeReleaseSpinLock(&Q->Lock,irql);return status;
}
static VOID CywTxCancel(CYW_TX_STATE *Q,PVOID CancelId)
{
    ULONG i;KIRQL irql;KeAcquireSpinLock(&Q->Lock,&irql);
    for(i=0;i<Q->Count;++i)if(Q->Entries[i].CancelId==CancelId)Q->Entries[i].Cancelled=TRUE;
    for(i=0;i<Q->BacklogCount;++i)
        if(Q->Backlog[i].CancelId==CancelId)Q->Backlog[i].Cancelled=TRUE;
    KeReleaseSpinLock(&Q->Lock,irql);
}
/* Caller holds Lock; bounded metadata records are moved, never packet data. */
static PNET_BUFFER_LIST CywTxRemove(PRPI5CYW_ADAPTER A,CYW_TX_STATE *Q,ULONG Index,NDIS_STATUS Status)
{
    PNET_BUFFER_LIST nbl=Q->Entries[Index].Nbl;ULONG i;
    if(Status!=NDIS_STATUS_SUCCESS)
        Rpi5CywTrafficDrop(A,TRUE,Q->Entries[Index].Frames,Status==NDIS_STATUS_FAILURE);
    Q->Frames-=Q->Entries[Index].HeldFrames;Q->Bytes-=Q->Entries[Index].Bytes;
    A->TxQueueFrames=Q->Frames;
    for(i=Index+1;i<Q->Count;++i)Q->Entries[i-1]=Q->Entries[i];
    Q->Count--;RtlZeroMemory(&Q->Entries[Q->Count],sizeof(Q->Entries[0]));
    return nbl;
}
static BOOLEAN CywTxPromoteOneLocked(PRPI5CYW_ADAPTER A,CYW_TX_STATE *Q);
static PNET_BUFFER_LIST CywTxBacklogRemove(
    PRPI5CYW_ADAPTER A,CYW_TX_STATE *Q,ULONG Index,NDIS_STATUS Status)
{
    PNET_BUFFER_LIST nbl=Q->Backlog[Index].Nbl;ULONG i;
    if(Status!=NDIS_STATUS_SUCCESS)
        Rpi5CywTrafficDrop(A,TRUE,Q->Backlog[Index].Frames,Status==NDIS_STATUS_FAILURE);
    Q->BacklogFrames-=Q->Backlog[Index].HeldFrames;
    Q->BacklogBytes-=Q->Backlog[Index].Bytes;
    for(i=Index+1;i<Q->BacklogCount;++i)Q->Backlog[i-1]=Q->Backlog[i];
    Q->BacklogCount--;RtlZeroMemory(&Q->Backlog[Q->BacklogCount],sizeof(Q->Backlog[0]));
    A->TxBacklogCurrent=Q->BacklogFrames;A->TxBacklogNblCurrent=Q->BacklogCount;
    /* BacklogCount is part of outstanding ownership. Move that reference to
     * Completing before dropping Lock so Pause/D3 cannot observe a false zero
     * between metadata removal and the NDIS completion callback. */
    Q->Completing++;
    (VOID)CywTxPromoteOneLocked(A,Q);
    return nbl;
}
static BOOLEAN CywTxPromoteOneLocked(PRPI5CYW_ADAPTER A,CYW_TX_STATE *Q)
{
    CYW_PENDING_SEND item;ULONG i;ULONG64 now,delay;
    if(Q->Gate!=NDIS_STATUS_SUCCESS || !Q->BacklogCount ||
       !CywTxActiveFits(Q,Q->Backlog[0].HeldFrames))return FALSE;
    item=Q->Backlog[0];
    Q->BacklogFrames-=item.HeldFrames;Q->BacklogBytes-=item.Bytes;
    for(i=1;i<Q->BacklogCount;++i)Q->Backlog[i-1]=Q->Backlog[i];
    Q->BacklogCount--;RtlZeroMemory(&Q->Backlog[Q->BacklogCount],sizeof(Q->Backlog[0]));
    A->TxBacklogCurrent=Q->BacklogFrames;A->TxBacklogNblCurrent=Q->BacklogCount;
    now=KeQueryInterruptTime();
    delay=now>=item.Submitted?(now-item.Submitted)/10000ULL:0;
    if(delay>A->TxBacklogMaxDelayMs)
        A->TxBacklogMaxDelayMs=delay>0xffffffffULL?0xffffffffUL:(ULONG)delay;
    CywTxActivateLocked(A,Q,&item,FALSE);A->TxBacklogPromoted++;
    return TRUE;
}
static VOID CywTxComplete(PRPI5CYW_ADAPTER A,CYW_TX_STATE *Q,
    PNET_BUFFER_LIST Nbl,NDIS_STATUS Status)
{
    KIRQL irql;
    /* PASSIVE_LEVEL worker only. Do not touch Nbl after completion/reentrancy. */
    NET_BUFFER_LIST_NEXT_NBL(Nbl)=NULL;NET_BUFFER_LIST_STATUS(Nbl)=Status;
    if(Status!=NDIS_STATUS_SUCCESS)A->TxErrors++;
    if(Status==NDIS_STATUS_SEND_ABORTED)A->TxCancelled++;
    /* Active Outstanding remains charged after removal until this same lock
     * transfer, so callback lifetime is never invisible to Pause/D3. */
    KeAcquireSpinLock(&Q->Lock,&irql);Q->Outstanding--;
    (VOID)CywTxPromoteOneLocked(A,Q);Q->Completing++;
    KeReleaseSpinLock(&Q->Lock,irql);
    NdisMSendNetBufferListsComplete(A->MiniportHandle,Nbl,0);
    KeAcquireSpinLock(&Q->Lock,&irql);Q->Completing--;A->TxNblCompleted++;
    KeReleaseSpinLock(&Q->Lock,irql);
}
static VOID CywTxCompleteBacklog(PRPI5CYW_ADAPTER A,CYW_TX_STATE *Q,
    PNET_BUFFER_LIST Nbl,NDIS_STATUS Status)
{
    KIRQL irql;
    /* CywTxBacklogRemove already transferred BacklogCount ownership into
     * Completing while holding Lock; do not create a gap or double-count it. */
    NET_BUFFER_LIST_NEXT_NBL(Nbl)=NULL;NET_BUFFER_LIST_STATUS(Nbl)=Status;
    if(Status!=NDIS_STATUS_SUCCESS)A->TxErrors++;
    if(Status==NDIS_STATUS_SEND_ABORTED)A->TxCancelled++;
    NdisMSendNetBufferListsComplete(A->MiniportHandle,Nbl,0);
    KeAcquireSpinLock(&Q->Lock,&irql);Q->Completing--;A->TxNblCompleted++;
    KeReleaseSpinLock(&Q->Lock,irql);
}
static NDIS_STATUS CywTxAbortStatus(CYW_TX_STATE *Q,CYW_PENDING_SEND *Item,ULONG64 Now)
{
    if(Q->Gate!=NDIS_STATUS_SUCCESS)return Q->Gate;
    if(Item->Cancelled)return NDIS_STATUS_SEND_ABORTED;
    if(Now-Item->Submitted>=CYW_TX_MAX_AGE)return NDIS_STATUS_FAILURE;
    return NDIS_STATUS_SUCCESS;
}
#if RPI5CYW_TX_SERVICE_BURST4
/* Count actual remaining frames, not retained admission frames. The result is
 * bounded by the current pump budget and the hard four-frame service cap. */
static ULONG CywTxBurstPendingFrames(CYW_TX_STATE *Q,ULONG Remaining)
{
    KIRQL irql;ULONG i,pending=0,take,cap=Remaining;
    if(cap>RPI5CYW_TX_SERVICE_BURST_MAX)cap=RPI5CYW_TX_SERVICE_BURST_MAX;
    if(!cap)return 0;
    KeAcquireSpinLock(&Q->Lock,&irql);
    if(Q->Gate==NDIS_STATUS_SUCCESS) {
        for(i=0;i<Q->Count && pending<cap;++i) {
            take=Q->Entries[i].Frames;
            if(take>cap-pending)take=cap-pending;
            pending+=take;
        }
    }
    KeReleaseSpinLock(&Q->Lock,irql);
    return pending;
}
#endif

#if RPI5CYW_TX_GLOM2
/* Glom only under real queue pressure, only for two independent one-frame
 * NBLs, and only while the cached state already shows two usable credits.
 * CywTxTransferPair performs the mandatory fresh F1 service again before F2. */
static BOOLEAN CywTxPairCandidate(PRPI5CYW_ADAPTER A,CYW_TX_STATE *Q,
    ULONG Remaining,PNET_BUFFER *First,PNET_BUFFER *Second)
{
    KIRQL irql;BOOLEAN eligible=FALSE;ULONG64 now;
    if(Remaining<2 || !A->TxGlomEnabled || !CywTxCanTransferPair(A))return FALSE;
    now=KeQueryInterruptTime();
    KeAcquireSpinLock(&Q->Lock,&irql);
    if(Q->Gate==NDIS_STATUS_SUCCESS && Q->Count>=2 &&
       (Q->BacklogCount!=0 || Q->Frames>=RPI5CYW_TX_GLOM_PRESSURE_THRESHOLD) &&
       Q->Entries[0].Frames==1 && Q->Entries[0].HeldFrames==1 &&
       Q->Entries[1].Frames==1 && Q->Entries[1].HeldFrames==1 &&
       Q->Entries[0].Next && Q->Entries[1].Next &&
       !NET_BUFFER_NEXT_NB(Q->Entries[0].Next) &&
       !NET_BUFFER_NEXT_NB(Q->Entries[1].Next) &&
       CywTxAbortStatus(Q,&Q->Entries[0],now)==NDIS_STATUS_SUCCESS &&
       CywTxAbortStatus(Q,&Q->Entries[1],now)==NDIS_STATUS_SUCCESS) {
        *First=Q->Entries[0].Next;*Second=Q->Entries[1].Next;eligible=TRUE;
    }
    KeReleaseSpinLock(&Q->Lock,irql);
    return eligible;
}
#endif
static VOID CywTxFlush(PRPI5CYW_ADAPTER A,CYW_TX_STATE *Q,NDIS_STATUS Status)
{
    KIRQL irql;PNET_BUFFER_LIST nbl;BOOLEAN active;
    CywTxSetGate(Q,Status);
    for(;;) {
        KeAcquireSpinLock(&Q->Lock,&irql);nbl=NULL;active=FALSE;
        if(Q->Count) {nbl=CywTxRemove(A,Q,0,Status);active=TRUE;}
        else if(Q->BacklogCount)nbl=CywTxBacklogRemove(A,Q,0,Status);
        KeReleaseSpinLock(&Q->Lock,irql);
        if(!nbl)break;
        if(active)CywTxComplete(A,Q,nbl,Status);
        else CywTxCompleteBacklog(A,Q,nbl,Status);
    }
}
static NTSTATUS CywTxPump(PRPI5CYW_ADAPTER A,CYW_TX_STATE *Q,ULONG Budget,PULONG Sent)
{
    ULONG i,len;ULONG64 now;KIRQL irql;PUCHAR data;PNET_BUFFER nb;
    PNET_BUFFER_LIST nbl;NDIS_STATUS completion=NDIS_STATUS_SUCCESS;NTSTATUS status;
    BOOLEAN active;
#if RPI5CYW_TX_SERVICE_BURST4
    ULONG serviceRemaining=0,wantFrames,reusePosition=2;
    BOOLEAN useServiceReuse;
#endif
#if RPI5CYW_TX_GLOM2
    ULONG len2;PUCHAR data2;PNET_BUFFER nb2;PNET_BUFFER_LIST nbl2;
    NDIS_STATUS completion2;
#endif
    *Sent=0;
    /* Cancelled/expired entries behind a flow-controlled head must not wait
     * for credits. Backlog ownership gets the same bounded cleanup. */
    for(i=0;i<CYW_TX_LIMIT+CYW_TX_BACKLOG_LIMIT;++i) {
        ULONG index;now=KeQueryInterruptTime();nbl=NULL;active=FALSE;
        KeAcquireSpinLock(&Q->Lock,&irql);
        for(index=0;index<Q->Count;++index) {
            completion=CywTxAbortStatus(Q,&Q->Entries[index],now);
            if(completion!=NDIS_STATUS_SUCCESS) {
                if(Q->Gate==NDIS_STATUS_SUCCESS && !Q->Entries[index].Cancelled)A->TxExpired++;
                nbl=CywTxRemove(A,Q,index,completion);active=TRUE;break;
            }
        }
        if(!nbl)for(index=0;index<Q->BacklogCount;++index) {
            completion=CywTxAbortStatus(Q,&Q->Backlog[index],now);
            if(completion!=NDIS_STATUS_SUCCESS) {
                if(Q->Gate==NDIS_STATUS_SUCCESS) {
                    if(Q->Backlog[index].Cancelled)A->TxBacklogCancelled++;
                    else {A->TxExpired++;A->TxBacklogExpired++;}
                }
                nbl=CywTxBacklogRemove(A,Q,index,completion);break;
            }
        }
        KeReleaseSpinLock(&Q->Lock,irql);
        if(!nbl)break;
        if(active)CywTxComplete(A,Q,nbl,completion);
        else CywTxCompleteBacklog(A,Q,nbl,completion);
    }
    while(*Sent<Budget) {
        KeAcquireSpinLock(&Q->Lock,&irql);
        if(!Q->Count)(VOID)CywTxPromoteOneLocked(A,Q);
        if(!Q->Count) {KeReleaseSpinLock(&Q->Lock,irql);break;}
        completion=CywTxAbortStatus(Q,&Q->Entries[0],KeQueryInterruptTime());
        if(completion!=NDIS_STATUS_SUCCESS) {
            nbl=CywTxRemove(A,Q,0,completion);KeReleaseSpinLock(&Q->Lock,irql);
            CywTxComplete(A,Q,nbl,completion);break;
        }
        nb=Q->Entries[0].Next;
        now=(KeQueryInterruptTime()-Q->Entries[0].Submitted)/10000ULL;
        if(now>A->TxQueueMaxDelayMs)A->TxQueueMaxDelayMs=now>0xffffffffULL?0xffffffffUL:(ULONG)now;
        KeReleaseSpinLock(&Q->Lock,irql);
#if RPI5CYW_TX_GLOM2
        nb2=NULL;
        if(
#if RPI5CYW_TX_SERVICE_BURST4
           !serviceRemaining &&
#endif
           CywTxPairCandidate(A,Q,Budget-*Sent,&nb,&nb2)) {
            len=NET_BUFFER_DATA_LENGTH(nb);len2=NET_BUFFER_DATA_LENGTH(nb2);
            RtlZeroMemory(Q->Frame,4);Q->Frame[0]=0x20;
            RtlZeroMemory(Q->Frame2,4);Q->Frame2[0]=0x20;
            data=NdisGetDataBuffer(nb,len,Q->Frame+4,1,0);
            data2=NdisGetDataBuffer(nb2,len2,Q->Frame2+4,1,0);
            if(data && data!=Q->Frame+4)RtlCopyMemory(Q->Frame+4,data,len);
            if(data2 && data2!=Q->Frame2+4)RtlCopyMemory(Q->Frame2+4,data2,len2);
            if(data && data2) {
                status=CywTxTransferPair(A,Q->Frame,len+4,Q->Frame2,len2+4);
                /* BUSY means fresh F1 no longer has two credits/flow room.
                 * No F2 occurred, so safely fall through to the proven
                 * one-frame sender rather than wasting an available credit. */
                if(status!=STATUS_DEVICE_BUSY) {
#if RPI5CYW_TX_SERVICE_BURST4
                    serviceRemaining=0;
#endif
                    KeAcquireSpinLock(&Q->Lock,&irql);nbl=nbl2=NULL;
                    completion=CywTxAbortStatus(Q,&Q->Entries[0],KeQueryInterruptTime());
                    completion2=CywTxAbortStatus(Q,&Q->Entries[1],KeQueryInterruptTime());
                    if(NT_SUCCESS(status)) {
                        A->TxPackets+=2;*Sent+=2;
                        Q->Entries[0].Next=NET_BUFFER_NEXT_NB(nb);
                        Q->Entries[1].Next=NET_BUFFER_NEXT_NB(nb2);
                        Q->Entries[0].Frames--;Q->Entries[1].Frames--;
                    } else {
                        if(completion==NDIS_STATUS_SUCCESS)completion=NDIS_STATUS_FAILURE;
                        if(completion2==NDIS_STATUS_SUCCESS)completion2=NDIS_STATUS_FAILURE;
                    }
                    nbl=CywTxRemove(A,Q,0,completion);
                    nbl2=CywTxRemove(A,Q,0,completion2);
                    KeReleaseSpinLock(&Q->Lock,irql);
                    if(nbl)CywTxComplete(A,Q,nbl,completion);
                    if(nbl2)CywTxComplete(A,Q,nbl2,completion2);
                    if(!NT_SUCCESS(status))return status;
                    continue;
                }
            }
        }
#endif
        if(!CywTxCanTransfer(A)) {
#if RPI5CYW_TX_SERVICE_BURST4
            serviceRemaining=0;
#endif
            A->TxCreditWaits++;break;
        }
        len=NET_BUFFER_DATA_LENGTH(nb);
        RtlZeroMemory(Q->Frame,4);Q->Frame[0]=0x20;
#if RPI5CYW_TX_SERVICE_BURST4
        useServiceReuse=(BOOLEAN)(serviceRemaining!=0);
#endif
        data=NdisGetDataBuffer(nb,len,Q->Frame+4,1,0);
        if(data && data!=Q->Frame+4)RtlCopyMemory(Q->Frame+4,data,len);
#if RPI5CYW_TX_SERVICE_BURST4
        if(data) {
            if(useServiceReuse) {
                status=CywTxTransferBurstReuse(A,Q->Frame,len+4,reusePosition);
                if(NT_SUCCESS(status)) {
                    if(serviceRemaining)serviceRemaining--;
                    reusePosition++;
                }
            } else {
                wantFrames=CywTxBurstPendingFrames(Q,Budget-*Sent);
                status=CywTxTransferBurstStart(A,Q->Frame,len+4,wantFrames,&serviceRemaining);
                if(serviceRemaining)reusePosition=2;
            }
        } else status=STATUS_INSUFFICIENT_RESOURCES;
#else
        status=data?CywTxTransfer(A,Q->Frame,len+4):STATUS_INSUFFICIENT_RESOURCES;
#endif
        if(status==STATUS_DEVICE_BUSY) {
#if RPI5CYW_TX_SERVICE_BURST4
            serviceRemaining=0;
#endif
            A->TxCreditWaits++;break;
        }
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
        if(completion!=NDIS_STATUS_SUCCESS || !Q->Entries[0].Frames)nbl=CywTxRemove(A,Q,0,completion);
#if RPI5CYW_TX_SERVICE_BURST4
        if(completion!=NDIS_STATUS_SUCCESS)serviceRemaining=0;
#endif
        KeReleaseSpinLock(&Q->Lock,irql);
        if(nbl)CywTxComplete(A,Q,nbl,completion);
        if(!NT_SUCCESS(status) && data) {
#if RPI5CYW_TX_SERVICE_BURST4
            serviceRemaining=0;
#endif
            return status; /* Bus fault: fail rest in worker exit. */
        }
        if(!data) {
#if RPI5CYW_TX_SERVICE_BURST4
            serviceRemaining=0;
#endif
            break; /* Mapping failure is per-NBL, not a radio failure. */
        }
    }
    return STATUS_SUCCESS;
}
