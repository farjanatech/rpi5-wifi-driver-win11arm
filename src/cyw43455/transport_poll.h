/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Actual FIFO poller, included by network.c and mocked transport tests. */
static NTSTATUS CywPoll(PRPI5CYW_ADAPTER A,PULONG Channel,PULONG Offset,PULONG Length)
{
    CYW_NETWORK *N=A->Network;uint32_t len,off,hint,read,i;
    CYW_RX_GLOM *g=&N->RxGlom;NTSTATUS Status,cleanup;
    if(N->Stop)return STATUS_CANCELLED;
    if(A->FifoTransportFailed)return STATUS_INVALID_DEVICE_STATE;
    if(g->Count) {
        if(g->Pending) {
            TRY(SdioFifoTransfer(A,N->Rx,g->Bytes,FALSE));
            if(!CywRxGlomValidate(g,N->Rx,g->Bytes)) {Status=STATUS_DEVICE_DATA_ERROR;goto Exit;}
            /* Only the outer header carries credits/flow control. All children
             * have now been validated; their payloads cannot overwrite state. */
            if((UCHAR)(N->Rx[9]-N->TxSeq)<=0x40)N->TxMax=N->Rx[9];
            N->TxFlow=N->Rx[8];
            CywTransportSetPriority(&A->Transport,N->TxFlow,KeQueryInterruptTime());
            A->RxGlomGroups++;
        }
        i=g->Index++;
        *Channel=N->Rx[g->Start[i]+5]&15;*Offset=g->Payload[i];*Length=g->End[i];
        CywTransportSequence(&A->Transport,N->Rx[g->Start[i]+4]);
        if(*Channel==1)CywEvent(A,N->Rx+*Offset,*Length-*Offset);
        if(*Channel==2)CywReceive(A,N->Rx+*Offset,*Length-*Offset);
        A->RxGlomFrames++;
        /* One child per poll preserves the existing RX/TX fairness budget.
         * The single bus owner retains the backing buffer until the last one. */
        if(g->Index==g->Count) {N->RxNextLength=g->NextLength;g->Count=0;}
        return STATUS_SUCCESS;
    }
    /* Only the worker's bounded RX batch reuses an already serviced status.
     * Control/reply polling is always fresh, and a zero FIFO header ends the
     * batch hint. A new data send independently checks F1 before transmitting. */
    if(!N->RxBatch || !N->RxBatchServiced || !N->RxPending) {
        Status=CywTransportService(A,TRUE);
        if(!NT_SUCCESS(Status))return Status;
        N->RxBatchServiced=TRUE;
    }
    if(!N->RxPending)return STATUS_NO_MORE_ENTRIES;
    hint=N->RxBatch?N->RxNextLength:0;N->RxNextLength=0;
    read=hint?hint:64;
    TRY(SdioFifoTransfer(A,N->Rx,read,FALSE));
    if(CywLe32(N->Rx)==0) {
        A->Transport.EmptyReads++;N->RxPending=FALSE;N->RxBatchServiced=FALSE;
        return STATUS_NO_MORE_ENTRIES;
    }
    if(!CywSdpcmHeader(N->Rx,read,&len,&off)) {Status=STATUS_DEVICE_DATA_ERROR;goto Exit;}
    if(hint) {
        if(!CywRxReadAheadValid(N->Rx,hint,len)) {
            A->RxReadAheadRejected++;Status=STATUS_DEVICE_DATA_ERROR;goto Exit;
        }
        A->RxReadAhead++;
    } else if(len>64)TRY(SdioFifoTransfer(A,N->Rx+64,(len-64+3)&~3UL,FALSE));
    N->RxPending=TRUE;
    CywTransportSequence(&A->Transport,N->Rx[4]);
    if((UCHAR)(N->Rx[9]-N->TxSeq)<=0x40)N->TxMax=N->Rx[9];
    N->TxFlow=N->Rx[8];
    CywTransportSetPriority(&A->Transport,N->TxFlow,KeQueryInterruptTime());
    *Channel=N->Rx[5]&15;*Offset=off;*Length=len;
    if(*Channel==3) {
        if(!A->RxGlomEnabled) {Status=STATUS_NOT_SUPPORTED;goto Exit;}
        if(!(N->Rx[5]&0x80) || !CywRxGlomDescriptor(g,N->Rx+off,len-off)) {
            A->RxGlomErrors++;Status=STATUS_DEVICE_DATA_ERROR;goto Exit;
        }
        /* Descriptor lengths, not the one-byte read-ahead hint, determine
         * the next superframe transaction. */
        return STATUS_SUCCESS;
    }
    if(N->Rx[5]&0x80) {Status=STATUS_NOT_SUPPORTED;goto Exit;}
    N->RxNextLength=CywRxNextLength(N->Rx);
    if(*Channel==1)CywEvent(A,N->Rx+off,len-off);
    if(*Channel==2)CywReceive(A,N->Rx+off,len-off);
    return STATUS_SUCCESS;
Exit:
    A->FifoTransportFailed=1;
    if(g->Count)A->RxGlomErrors++;
    RtlZeroMemory(g,sizeof(*g));N->RxNextLength=0;
    N->RxPending=FALSE;N->RxBatchServiced=FALSE;
    cleanup=SdioCmd52Write(A,0,6,2,0);
    if(!NT_SUCCESS(cleanup))A->Transport.RxAbortFailures++;
    /* FRAMECTRL bit 0 terminates READ; bit 1 would terminate a WRITE. We
     * still return the original failure and stop, not retry an unflushed FIFO. */
    cleanup=SdioCmd52Write(A,1,0x1000d,1,0);
    if(!NT_SUCCESS(cleanup))A->Transport.RxAbortFailures++;
    return Status;
}
