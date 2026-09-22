/* SPDX-License-Identifier: GPL-3.0-or-later
 * SDPCM next-length read-ahead adapted from Ahmed ARIF's CywBusThread,
 * ReactOS 929bdd689d1e18d0ef71214741d4e16eec74409c (2026, GPL-2.0-or-later),
 * and Linux v6.12 brcmfmac/sdio.c (Broadcom ISC). See THIRD_PARTY_NOTICES.md.
 * Included by network.c and the host regression harness. One worker owns F2.
 */
static NTSTATUS CywPollFrame(PRPI5CYW_ADAPTER A, PULONG Channel, PULONG Offset,
                             PULONG Length, BOOLEAN ReadAhead)
{
    CYW_NETWORK *N=A->Network;
    UCHAR pending; ULONG ist,mail,first,next,commands,baseline; uint32_t len,off;
    NTSTATUS Status; BOOLEAN ahead=FALSE;
    *Channel=*Offset=*Length=0;
    if(!ReadAhead)N->RxNextLength=0;
    if(N->Stop) {N->RxNextLength=0;return STATUS_CANCELLED;}
    first=N->RxNextLength;
    N->RxNextLength=0; /* consume exactly once, including every failure exit */
    if(ReadAhead && N->RxPending && first>=64 && first<=2048 && !(first&15))ahead=TRUE;
    else first=64;
    /* Preserve the proven interrupt/mailbox checks for EVERY packet. Hints
     * save only F2 header commands; no interrupt, credit or TX scheduling change. */
    TRY(SdioCmd52Read(A,0,5,&pending));
    if(!N->RxPending && !(pending&6))return STATUS_NO_MORE_ENTRIES;
    TRY(CywBpRead(A,A->SdioCoreBase+0x20,&ist));
    ist&=0x200000f0;
    if(ist)TRY(CywBpWrite(A,A->SdioCoreBase+0x20,ist));
    if(ist&0x80) {
        TRY(CywBpRead(A,A->SdioCoreBase+0x4c,&mail));
        TRY(CywBpWrite(A,A->SdioCoreBase+0x40,2));
        UNREFERENCED_PARAMETER(mail);
    }
    if(ahead)A->RxReadAheadAttempts++;
    else A->RxHeaderReads++;
    TRY(SdioFifoTransfer(A,N->Rx,first,FALSE));
    if(CywLe32(N->Rx)==0) {N->RxPending=FALSE;return STATUS_NO_MORE_ENTRIES;}
    if(!CywSdpcmHeader(N->Rx,first,&len,&off)) {Status=STATUS_DEVICE_DATA_ERROR;goto Exit;}
    /* A hint is a 16-byte-rounded promise, not a trusted packet length. Never
     * deliver a short, over-read or differently-sized frame to Windows. */
    if(ahead && ((len+15)&~15UL)!=first) {
        A->RxReadAheadMismatch++;Status=STATUS_DEVICE_DATA_ERROR;goto Exit;
    }
    if(len>first)TRY(SdioFifoTransfer(A,N->Rx+first,(len-first+3)&~3UL,FALSE));
    N->RxPending=TRUE;
    if((UCHAR)(N->Rx[9]-N->TxSeq)<=0x40)N->TxMax=N->Rx[9];
    N->TxFlow=N->Rx[8];
    /* rxglom remains disabled. No nested packets or aggregation introduced. */
    if((N->Rx[5]&15)==3 || (N->Rx[5]&0x80)) {Status=STATUS_NOT_SUPPORTED;goto Exit;}
    if(ahead) {
        A->RxReadAheadFrames++;
        /* Actual byte-mode CMD53 count difference, not a speed estimate. */
        baseline=1+(len>64?(((len-64+3)&~3UL)+511)/512:0);
        commands=(first+511)/512;
        if(baseline>commands)A->RxReadAheadSavedCommands+=baseline-commands;
    }
    next=(ULONG)N->Rx[6]<<4;
    if(ReadAhead && next>=64 && next<=2048)N->RxNextLength=next;
    else if(ReadAhead && next)A->RxReadAheadHintIgnored++;
    *Channel=N->Rx[5]&15;*Offset=off;*Length=len;
    if(*Channel==1)CywEvent(A,N->Rx+off,len-off);
    if(*Channel==2)CywReceive(A,N->Rx+off,len-off);
    return STATUS_SUCCESS;
Exit:
    N->RxPending=FALSE;N->RxNextLength=0;
    /* Preserve fail-closed transport handling. A consumed bad frame cannot
     * safely be retried with a smaller read from the middle of the FIFO. */
    (void)SdioCmd52Write(A,0,6,2,0);
    (void)SdioCmd52Write(A,1,0x1000d,2,0);
    return Status;
}
/* Explicit firmware commands always retain the original header-first path,
 * including data/event packets encountered while waiting for their reply. */
static NTSTATUS CywPoll(PRPI5CYW_ADAPTER A,PULONG Channel,PULONG Offset,PULONG Length)
{return CywPollFrame(A,Channel,Offset,Length,FALSE);}
