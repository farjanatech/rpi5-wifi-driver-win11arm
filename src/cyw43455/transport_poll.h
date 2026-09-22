/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Actual FIFO poller, included by network.c and mocked transport tests. */
static NTSTATUS CywPoll(PRPI5CYW_ADAPTER A,PULONG Channel,PULONG Offset,PULONG Length)
{
    CYW_NETWORK *N=A->Network;uint32_t len,off;NTSTATUS Status,cleanup;
    if(N->Stop)return STATUS_CANCELLED;
    /* Only the worker's bounded RX batch reuses an already serviced status.
     * Control/reply polling is always fresh, and a zero FIFO header ends the
     * batch hint. A new data send independently checks F1 before transmitting. */
    if(!N->RxBatch || !N->RxBatchServiced || !N->RxPending) {
        Status=CywTransportService(A,TRUE);
        if(!NT_SUCCESS(Status))return Status;
        N->RxBatchServiced=TRUE;
    }
    if(!N->RxPending)return STATUS_NO_MORE_ENTRIES;
    TRY(SdioFifoTransfer(A,N->Rx,64,FALSE));
    if(CywLe32(N->Rx)==0) {
        A->Transport.EmptyReads++;N->RxPending=FALSE;N->RxBatchServiced=FALSE;
        return STATUS_NO_MORE_ENTRIES;
    }
    if(!CywSdpcmHeader(N->Rx,64,&len,&off)) {Status=STATUS_DEVICE_DATA_ERROR;goto Exit;}
    if(len>64)TRY(SdioFifoTransfer(A,N->Rx+64,(len-64+3)&~3UL,FALSE));
    N->RxPending=TRUE;
    CywTransportSequence(&A->Transport,N->Rx[4]);
    if((UCHAR)(N->Rx[9]-N->TxSeq)<=0x40)N->TxMax=N->Rx[9];
    N->TxFlow=N->Rx[8];
    CywTransportSetPriority(&A->Transport,N->TxFlow,KeQueryInterruptTime());
    *Channel=N->Rx[5]&15;*Offset=off;*Length=len;
    /* Aggregation remains disabled. No speculative read-ahead or silent
     * resynchronization: unsupported/bad frames stop the existing worker. */
    if(*Channel==3 || (N->Rx[5]&0x80)) {Status=STATUS_NOT_SUPPORTED;goto Exit;}
    if(*Channel==1)CywEvent(A,N->Rx+off,len-off);
    if(*Channel==2)CywReceive(A,N->Rx+off,len-off);
    return STATUS_SUCCESS;
Exit:
    N->RxPending=FALSE;N->RxBatchServiced=FALSE;
    cleanup=SdioCmd52Write(A,0,6,2,0);
    if(!NT_SUCCESS(cleanup))A->Transport.RxAbortFailures++;
    /* FRAMECTRL bit 0 terminates READ; bit 1 would terminate a WRITE. We
     * still return the original failure and stop, not retry an unflushed FIFO. */
    cleanup=SdioCmd52Write(A,1,0x1000d,1,0);
    if(!NT_SUCCESS(cleanup))A->Transport.RxAbortFailures++;
    return Status;
}
