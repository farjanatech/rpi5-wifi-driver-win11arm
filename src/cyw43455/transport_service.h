/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Included by network.c and the transport mock test. I/O is supplied by the
 * includer. No retries of packet transfers and no changed packet budgets. */
static NTSTATUS CywTransportService(PRPI5CYW_ADAPTER A,BOOLEAN ProbePending)
{
    CYW_NETWORK *N=A->Network;CYW_TRANSPORT_STATE *T=&A->Transport;
    ULONG ist,again=0,mail;UCHAR pending;NTSTATUS status;BOOLEAN fallback=FALSE;
    ULONG64 now;
    if(N->Stop)return STATUS_CANCELLED;
    if(T->Halted)return STATUS_DEVICE_NOT_READY;
    if(ProbePending && !N->RxPending) {
        T->PendingReads++;
        status=SdioCmd52Read(A,0,5,&pending);
        if(!NT_SUCCESS(status))goto Failed;
        T->LastPending=pending;
        if(!(pending&6)) {
            T->PendingEmpty++;
            if(!CywTransportFallbackDue(T,KeQueryInterruptTime()))return STATUS_SUCCESS;
            /* Bound a summary-register blind spot without inventing FIFO
             * work: use the same F1 status/ack path as normal service, at
             * most once per 100 ms since any successful F1 status read.
             * This is defensive coverage, not a proven hardware stall cause. */
            fallback=TRUE;T->FallbackReads++;
        }
        /* CCCR pending can be mailbox/flow-only. It is not by itself a frame
         * indication: only F1 FRAME/NAKHANDLED below establishes new RX work. */
    }
    T->StatusReads++;
    status=CywBpRead(A,A->SdioCoreBase+0x20,&ist);
    if(!NT_SUCCESS(status))goto Failed;
    now=KeQueryInterruptTime();CywTransportStatusObserved(T,now);
    T->LastInterrupt=ist;
    CywTransportSetGlobal(T,(ist&CYW_INT_FC_STATE)!=0,now);
    ist&=CYW_INT_MASK;
    if(!ist)T->StatusNoEvents++;
    if(ist) {
        T->StatusAcks++;
        status=CywBpWrite(A,A->SdioCoreBase+0x20,ist);
        if(!NT_SUCCESS(status))goto Failed;
    }
    if(ist&CYW_INT_FC_CHANGE) {
        T->FcChanges++;
        /* The acknowledge can cross another transition. Follow brcmfmac:
         * acknowledge CHANGE once more, reread, and conservatively stop data
         * TX if CHANGE remains set. A later bounded service debounces again. */
        T->StatusAcks++;
        status=CywBpWrite(A,A->SdioCoreBase+0x20,CYW_INT_FC_CHANGE);
        if(!NT_SUCCESS(status))goto Failed;
        T->StatusReads++;
        status=CywBpRead(A,A->SdioCoreBase+0x20,&again);
        if(!NT_SUCCESS(status))goto Failed;
        now=KeQueryInterruptTime();CywTransportStatusObserved(T,now);
        T->LastInterrupt=again;
        if(!(again&CYW_INT_MASK))T->StatusNoEvents++;
        if(again&CYW_INT_FC_CHANGE)T->FcRaces++;
        CywTransportSetGlobal(T,(again&(CYW_INT_FC_STATE|CYW_INT_FC_CHANGE))!=0,now);
        /* Do not acknowledge new unrelated events here: service them now and
         * leave their hardware indication for the next bounded status pass. */
        ist|=again&CYW_INT_MASK;
    }
    if(ist&CYW_INT_FRAME) {
        T->FrameNotifications++;N->RxPending=TRUE;
        if(fallback)T->FallbackFrames++;
    }
    if(ist&CYW_INT_MAIL) {
        if(fallback)T->FallbackMailbox++;
        T->MailReads++;
        status=CywBpRead(A,A->SdioCoreBase+0x4c,&mail);
        if(!NT_SUCCESS(status))goto Failed;
        T->LastMailbox=mail;
        status=CywBpWrite(A,A->SdioCoreBase+0x40,2);
        if(!NT_SUCCESS(status))goto Failed;
        if(mail&~CYW_MAIL_KNOWN)T->MailUnknown++;
        if(mail&CYW_MAIL_READY)T->MailboxVersion=(mail>>16)&255u;
        if(mail&CYW_MAIL_FLOW) {
            N->TxFlow=(UCHAR)(mail>>24);
            CywTransportSetPriority(T,N->TxFlow,KeQueryInterruptTime());
        }
        if(mail&CYW_MAIL_NAK_HANDLED)N->RxPending=TRUE;
        if(mail&CYW_MAIL_HALT) {
            T->Halted=1;T->FirmwareHalts++;
            return STATUS_DEVICE_NOT_READY;
        }
    }
    return STATUS_SUCCESS;
Failed:
    T->ServiceErrors++;
    /* No data send may follow a failed status read/acknowledge. The caller
     * propagates this transport failure to the existing worker-stop path. */
    CywTransportSetGlobal(T,1,KeQueryInterruptTime());
    return status;
}
