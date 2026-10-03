/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Actual SDPCM sender, shared with the mocked transport integration tests. */
static NTSTATUS CywSendFrame(PRPI5CYW_ADAPTER A, UCHAR Channel, PUCHAR Data, ULONG Length)
{
    CYW_NETWORK *N=A->Network;
    ULONG total=Length+12, padded=(total+3)&~3UL;
    NTSTATUS Status;
/* TX-CREDIT-DIAG-BEGIN */
    CYW_TX_CREDIT_DIAG *D=&A->TxCreditDiag;CYW_TXD_U64 detailStart=0;
    unsigned reads=0,acks=0,mails=0,commands=0;
/* TX-CREDIT-DIAG-END */
    if(Length>CYW_CONTROL_CAPACITY-12)return STATUS_INVALID_BUFFER_SIZE;
    if(A->FifoBlockReady && padded>512)padded=(padded+511)&~511UL;
    if(Channel==2) {
        /* Fresh global state before EVERY data frame, including frames in the
         * same four-frame TX pump. RX batching never makes this gate stale. */
        A->Transport.TxStatusChecks++;
/* TX-CREDIT-DIAG-BEGIN */
        CywTxDiagInc(&D->F1Calls);
        reads=A->Transport.StatusReads;acks=A->Transport.StatusAcks;mails=A->Transport.MailReads;
        detailStart=CYW_TXD_CLOCK(A);
/* TX-CREDIT-DIAG-END */
        Status=CywTransportService(A,FALSE);
/* TX-CREDIT-DIAG-BEGIN */
        if(RPI5CYW_DETAILED_TIMING && A->Timing.Enabled)
            CywTxDiagDuration(D,&D->F1Ticks,&D->F1MaxTicks,detailStart,CYW_TXD_CLOCK(A));
        D->F1StatusReads=CywTxDiagAdd(D->F1StatusReads,(unsigned)(A->Transport.StatusReads-reads));
        D->F1StatusAcks=CywTxDiagAdd(D->F1StatusAcks,(unsigned)(A->Transport.StatusAcks-acks));
        D->F1MailboxReads=CywTxDiagAdd(D->F1MailboxReads,(unsigned)(A->Transport.MailReads-mails));
        if(!NT_SUCCESS(Status))CywTxDiagInc(&D->F1Errors);
        else if(A->Transport.GlobalFlow || !CywTransportPriorityAllowed(&A->Transport,N->TxFlow))
            CywTxDiagInc(&D->F1FlowBusy);
/* TX-CREDIT-DIAG-END */
        if(!NT_SUCCESS(Status))return Status;
        if(A->Transport.GlobalFlow || !CywTransportPriorityAllowed(&A->Transport,N->TxFlow))
            return STATUS_DEVICE_BUSY;
    }
    if(A->Transport.Halted)return STATUS_DEVICE_NOT_READY;
    if(!CywTxCredit(N->TxSeq,N->TxMax,0))return STATUS_DEVICE_BUSY;
    RtlZeroMemory(N->Tx,padded);
    CywPut16(N->Tx,(USHORT)total);CywPut16(N->Tx+2,(USHORT)~total);
    N->Tx[4]=N->TxSeq;N->Tx[5]=Channel;N->Tx[7]=12;
    RtlCopyMemory(N->Tx+12,Data,Length);
/* TX-CREDIT-DIAG-BEGIN */
    if(Channel==2) {
        CywTxDiagInc(&D->F2Calls);commands=(unsigned)A->Cmd53WriteCount;detailStart=CYW_TXD_CLOCK(A);
    }
/* TX-CREDIT-DIAG-END */
    Status=SdioFifoTransfer(A,N->Tx,padded,TRUE);
/* TX-CREDIT-DIAG-BEGIN */
    if(Channel==2) {
        D->F2Cmd53Writes=CywTxDiagAdd(D->F2Cmd53Writes,(unsigned)((unsigned)A->Cmd53WriteCount-commands));
        if(RPI5CYW_DETAILED_TIMING && A->Timing.Enabled)
            CywTxDiagDuration(D,&D->F2Ticks,&D->F2MaxTicks,detailStart,CYW_TXD_CLOCK(A));
        if(!NT_SUCCESS(Status))CywTxDiagInc(&D->F2Errors);
        else {D->F2PayloadBytes=CywTxDiagAdd(D->F2PayloadBytes,Length);
              D->F2PaddedBytes=CywTxDiagAdd(D->F2PaddedBytes,padded);}
    }
/* TX-CREDIT-DIAG-END */
    RtlSecureZeroMemory(N->Tx,padded);
    if(NT_SUCCESS(Status))N->TxSeq++;
    return Status;
}
