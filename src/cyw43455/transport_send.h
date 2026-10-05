/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Actual SDPCM sender, shared with the mocked transport integration tests. */
static NTSTATUS CywSendFrameCore(PRPI5CYW_ADAPTER A, UCHAR Channel,
    PUCHAR Data, ULONG Length, BOOLEAN ServiceData, BOOLEAN WantSecond,
    PBOOLEAN PermitSecond)
{
    CYW_NETWORK *N=A->Network;
    ULONG header=A->TxGlomEnabled?20u:12u;
    ULONG total=Length+header,padded=(total+3)&~3UL,tailPad,word;
    NTSTATUS Status;
    BOOLEAN grant=FALSE;
/* TX-CREDIT-DIAG-BEGIN */
    CYW_TX_CREDIT_DIAG *D=&A->TxCreditDiag;CYW_TXD_U64 detailStart=0;
    unsigned reads=0,acks=0,mails=0,commands=0;
/* TX-CREDIT-DIAG-END */
    if(PermitSecond)*PermitSecond=FALSE;
    if(Length>CYW_CONTROL_CAPACITY-header)return STATUS_INVALID_BUFFER_SIZE;
    if(A->FifoBlockReady && padded>512)padded=(padded+511)&~511UL;
    if(padded>CYW_CONTROL_CAPACITY || padded<total)return STATUS_INVALID_BUFFER_SIZE;
    if(Channel==2) {
        if(ServiceData) {
            /* Fresh F1 service remains mandatory for the first frame in every
             * burst. Only this observed state may authorize one following
             * ordinary F2 frame; the grant never survives beyond the caller's
             * current pump invocation. */
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
            if(WantSecond &&
               CywTxCredit(N->TxSeq,N->TxMax,0) &&
               CywTxCredit((UCHAR)(N->TxSeq+1),N->TxMax,0))
                grant=TRUE;
        } else {
#if RPI5CYW_TX_SERVICE_BURST2
            A->TxServiceBurstSecondAttempts++;
#endif
            /* Second-frame service reuse is intentionally conservative: cached
             * global/priority flow state and a real remaining credit must still
             * be valid immediately before F2. No status is invented. */
            if(A->Transport.GlobalFlow ||
               !CywTransportPriorityAllowed(&A->Transport,N->TxFlow) ||
               !CywTxCredit(N->TxSeq,N->TxMax,0)) {
#if RPI5CYW_TX_SERVICE_BURST2
                A->TxServiceBurstSecondBusy++;
#endif
                return STATUS_DEVICE_BUSY;
            }
        }
    }
    if(A->Transport.Halted)return STATUS_DEVICE_NOT_READY;
    if(!CywTxCredit(N->TxSeq,N->TxMax,0)) {
#if RPI5CYW_TX_SERVICE_BURST2
        if(Channel==2 && !ServiceData)A->TxServiceBurstSecondBusy++;
#endif
        return STATUS_DEVICE_BUSY;
    }

    RtlZeroMemory(N->Tx,padded);
    if(A->TxGlomEnabled) {
        /* Once bus:rxglom succeeds, brcmfmac switches the host TX header
         * length globally, not only for multi-frame chains. Every subsequent
         * host->firmware control/data frame carries the 8-byte HW extension.
         * The first/only HW length covers the complete padded transfer. */
        tailPad=padded-total;
        if(tailPad>0xffffUL)return STATUS_INVALID_BUFFER_SIZE;
        CywPut16(N->Tx,(USHORT)padded);CywPut16(N->Tx+2,(USHORT)~padded);
        word=(total-4)|(1u<<24);CywPut32(N->Tx+4,word);
        CywPut32(N->Tx+8,tailPad<<16);
        N->Tx[12]=N->TxSeq;N->Tx[13]=Channel;N->Tx[15]=20;
        RtlCopyMemory(N->Tx+20,Data,Length);
    } else {
        CywPut16(N->Tx,(USHORT)total);CywPut16(N->Tx+2,(USHORT)~total);
        N->Tx[4]=N->TxSeq;N->Tx[5]=Channel;N->Tx[7]=12;
        RtlCopyMemory(N->Tx+12,Data,Length);
    }
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
    if(NT_SUCCESS(Status) && A->TxGlomEnabled) {
        if(Channel==2)A->TxGlomExtendedDataSingles++;
        else A->TxGlomExtendedControlSingles++;
    }
#if RPI5CYW_TX_SERVICE_BURST2
    if(Channel==2 && !ServiceData) {
        if(NT_SUCCESS(Status)) {
            A->TxServiceBurstSecondSuccess++;
            A->TxServiceBurstSavedStatusChecks++;
        } else A->TxServiceBurstSecondErrors++;
    }
#endif
    RtlSecureZeroMemory(N->Tx,padded);
    if(NT_SUCCESS(Status)) {
        N->TxSeq++;
#if RPI5CYW_TX_SERVICE_BURST2
        if(Channel==2 && ServiceData && grant && PermitSecond) {
            A->TxServiceBurstGrants++;
            *PermitSecond=TRUE;
        }
#endif
    }
    return Status;
}

static NTSTATUS CywSendFrame(PRPI5CYW_ADAPTER A, UCHAR Channel, PUCHAR Data, ULONG Length)
{
    return CywSendFrameCore(A,Channel,Data,Length,TRUE,FALSE,NULL);
}

#if RPI5CYW_TX_SERVICE_BURST2
static NTSTATUS CywSendDataBurstStart(PRPI5CYW_ADAPTER A,PUCHAR Data,ULONG Length,
    BOOLEAN WantSecond,PBOOLEAN PermitSecond)
{
    return CywSendFrameCore(A,2,Data,Length,TRUE,WantSecond,PermitSecond);
}
static NTSTATUS CywSendDataBurstSecond(PRPI5CYW_ADAPTER A,PUCHAR Data,ULONG Length)
{
    return CywSendFrameCore(A,2,Data,Length,FALSE,FALSE,NULL);
}
#endif

#if RPI5CYW_TX_GLOM2
/* Linux brcmfmac host TX glom format, deliberately capped at two frames:
 * 4-byte chain HW header, 8-byte HW extension, 8-byte SDPCM SW header.
 * The first HW length covers the entire padded chain; each subframe extension
 * carries its own logical length and tail padding. One fresh F1 service gates
 * both frames, and two real firmware credits are required before F2 is touched.
 */
static NTSTATUS CywSendDataPair(PRPI5CYW_ADAPTER A,
    PUCHAR Data1,ULONG Length1,PUCHAR Data2,ULONG Length2)
{
    CYW_NETWORK *N=A->Network;
    ULONG logical1,logical2,pad1,pad2,off2,used,padded;
    ULONG word,commands;
    NTSTATUS Status;
    CYW_TX_CREDIT_DIAG *D=&A->TxCreditDiag;CYW_TXD_U64 detailStart=0;
    unsigned reads=0,acks=0,mails=0;

    A->TxGlomAttempts++;
    if(!A->TxGlomEnabled || !Data1 || !Data2 ||
       Length1>CYW_CONTROL_CAPACITY-20 || Length2>CYW_CONTROL_CAPACITY-20)
        return STATUS_INVALID_BUFFER_SIZE;

    A->Transport.TxStatusChecks++;
    CywTxDiagInc(&D->F1Calls);
    reads=A->Transport.StatusReads;acks=A->Transport.StatusAcks;mails=A->Transport.MailReads;
    detailStart=CYW_TXD_CLOCK(A);
    Status=CywTransportService(A,FALSE);
    if(RPI5CYW_DETAILED_TIMING && A->Timing.Enabled)
        CywTxDiagDuration(D,&D->F1Ticks,&D->F1MaxTicks,detailStart,CYW_TXD_CLOCK(A));
    D->F1StatusReads=CywTxDiagAdd(D->F1StatusReads,(unsigned)(A->Transport.StatusReads-reads));
    D->F1StatusAcks=CywTxDiagAdd(D->F1StatusAcks,(unsigned)(A->Transport.StatusAcks-acks));
    D->F1MailboxReads=CywTxDiagAdd(D->F1MailboxReads,(unsigned)(A->Transport.MailReads-mails));
    if(!NT_SUCCESS(Status)) {
        CywTxDiagInc(&D->F1Errors);A->TxGlomErrors++;return Status;
    }
    if(A->Transport.GlobalFlow || !CywTransportPriorityAllowed(&A->Transport,N->TxFlow) ||
       !CywTxCredit(N->TxSeq,N->TxMax,0) ||
       !CywTxCredit((UCHAR)(N->TxSeq+1),N->TxMax,0)) {
        if(A->Transport.GlobalFlow || !CywTransportPriorityAllowed(&A->Transport,N->TxFlow))
            CywTxDiagInc(&D->F1FlowBusy);
        A->TxGlomBusyFallbacks++;return STATUS_DEVICE_BUSY;
    }
    if(A->Transport.Halted)return STATUS_DEVICE_NOT_READY;

    logical1=Length1+20;logical2=Length2+20;
    pad1=(4-(logical1&3))&3;off2=logical1+pad1;
    used=off2+logical2;padded=(used+3)&~3UL;
    if(A->FifoBlockReady && padded>512)padded=(padded+511)&~511UL;
    if(padded>CYW_CONTROL_CAPACITY || padded<used)return STATUS_INVALID_BUFFER_SIZE;
    pad2=padded-used;
    if(pad1>0xffffUL || pad2>0xffffUL)return STATUS_INVALID_BUFFER_SIZE;

    RtlZeroMemory(N->Tx,padded);
    /* First subframe: HW length is replaced by total chain length. */
    CywPut16(N->Tx,(USHORT)padded);CywPut16(N->Tx+2,(USHORT)~padded);
    word=(logical1-4);CywPut32(N->Tx+4,word);
    CywPut32(N->Tx+8,pad1<<16);
    N->Tx[12]=N->TxSeq;N->Tx[13]=2;N->Tx[15]=20;
    RtlCopyMemory(N->Tx+20,Data1,Length1);

    /* Second/final subframe retains its own logical HW length. */
    CywPut16(N->Tx+off2,(USHORT)logical2);
    CywPut16(N->Tx+off2+2,(USHORT)~logical2);
    word=(logical2-4)|(1u<<24);CywPut32(N->Tx+off2+4,word);
    CywPut32(N->Tx+off2+8,pad2<<16);
    N->Tx[off2+12]=(UCHAR)(N->TxSeq+1);N->Tx[off2+13]=2;N->Tx[off2+15]=20;
    RtlCopyMemory(N->Tx+off2+20,Data2,Length2);

    CywTxDiagInc(&D->F2Calls);commands=(unsigned)A->Cmd53WriteCount;
    detailStart=CYW_TXD_CLOCK(A);
    Status=SdioFifoTransfer(A,N->Tx,padded,TRUE);
    D->F2Cmd53Writes=CywTxDiagAdd(D->F2Cmd53Writes,
        (unsigned)((unsigned)A->Cmd53WriteCount-commands));
    if(RPI5CYW_DETAILED_TIMING && A->Timing.Enabled)
        CywTxDiagDuration(D,&D->F2Ticks,&D->F2MaxTicks,detailStart,CYW_TXD_CLOCK(A));
    if(!NT_SUCCESS(Status)) {
        CywTxDiagInc(&D->F2Errors);A->TxGlomErrors++;
    } else {
        D->F2PayloadBytes=CywTxDiagAdd(D->F2PayloadBytes,Length1+Length2);
        D->F2PaddedBytes=CywTxDiagAdd(D->F2PaddedBytes,padded);
        N->TxSeq=(UCHAR)(N->TxSeq+2);
        A->TxGlomChains++;A->TxGlomFrames+=2;
        A->TxGlomPayloadBytes+=Length1+Length2;
        A->TxGlomPaddedBytes+=padded;
    }
    RtlSecureZeroMemory(N->Tx,padded);
    return Status;
}
#endif
