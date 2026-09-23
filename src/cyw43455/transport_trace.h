/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Included after CYW_NETWORK by network.c and production-helper mock tests.
 * Call once per worker iteration before periodic diagnostics. Only a
 * monotonic-clock read and interval comparison occur between one-second
 * captures. All bus state comes from existing observations: no added I/O.
 */
static VOID CywTransportSample(PRPI5CYW_ADAPTER A)
{
    CYW_NETWORK *N=A->Network;CYW_TRANSPORT_STATE *T=&A->Transport;
    CYW_TRANSPORT_TRACE *trace=&T->Trace;CYW_TRANSPORT_TRACE_ENTRY *entry;
    ULONG64 now=KeQueryInterruptTime();
    if(!N)return;
    if(trace->Version && now>=T->TraceLast100ns &&
       now-T->TraceLast100ns<CYW_TRANSPORT_TRACE_INTERVAL)return;
    if(!trace->Version) {
        trace->Version=CYW_TRANSPORT_TRACE_VERSION;
        trace->EntryBytes=(unsigned int)sizeof(*entry);trace->Capacity=CYW_TRANSPORT_TRACE_CAPACITY;
        trace->Origin100ns=now;
    }
    T->TraceLast100ns=now;
    entry=&trace->Entry[trace->Next];
    entry->Time100ns=now;entry->Serial=++trace->Serial;
    entry->Flags=(N->RxPending?CYW_TRACE_RX_PENDING:0u) |
        (N->Ready?CYW_TRACE_READY:0u) | (N->Authorized?CYW_TRACE_AUTHORIZED:0u) |
        (N->Paused?CYW_TRACE_PAUSED:0u) | (T->Halted?CYW_TRACE_HALTED:0u);
    entry->NetworkPhase=A->NetworkPhase;
    entry->LastPending=T->LastPending;entry->LastInterrupt=T->LastInterrupt;
    entry->LastMailbox=T->LastMailbox;entry->RxFrames=T->Frames;
    entry->EmptyReads=T->EmptyReads;
    entry->TxPacketsLow=(unsigned int)A->TxPackets;entry->RxPacketsLow=(unsigned int)A->RxPackets;
    entry->PendingReads=T->PendingReads;entry->PendingEmpty=T->PendingEmpty;
    entry->StatusReads=T->StatusReads;entry->StatusNoEvents=T->StatusNoEvents;
    entry->FrameNotifications=T->FrameNotifications;entry->MailReads=T->MailReads;
    entry->FallbackReads=T->FallbackReads;entry->FallbackFrames=T->FallbackFrames;
    entry->FallbackMailbox=T->FallbackMailbox;entry->ServiceErrors=T->ServiceErrors;
    entry->SequenceMismatches=T->SequenceMismatches;
    /* QueueDepth is an advisory concurrent NDIS-admission snapshot. It is not
     * used to change admission, infer ownership, or suppress packet work. */
    entry->QueueDepth=A->TxQueueFrames;entry->QueueFull=A->TxQueueFull;
    entry->CreditWaits=A->TxCreditWaits;
    entry->TxSequence=N->TxSeq;entry->TxMaximum=N->TxMax;entry->TxFlow=N->TxFlow;
    entry->GlobalFlow=T->GlobalFlow;
    entry->EchoLate=A->PacketProbe.EchoLate;entry->EchoMaxMs=A->PacketProbe.EchoMaxMs;
    trace->Next=(trace->Next+1u)%CYW_TRANSPORT_TRACE_CAPACITY;
    if(trace->Count<CYW_TRANSPORT_TRACE_CAPACITY)trace->Count++;
}
