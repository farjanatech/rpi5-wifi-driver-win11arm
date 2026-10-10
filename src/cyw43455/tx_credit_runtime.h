/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Only network.c's PASSIVE bus worker calls these helpers. No per-frame
 * allocations, registry writes, firmware commands, or credential/packet data.
 */
static VOID CywTxDiagStart(PRPI5CYW_ADAPTER A)
{
    CYW_TX_CREDIT_DIAG *D=&A->TxCreditDiag;
    RtlZeroMemory(D,sizeof(*D));
    D->Version=CYW_TX_DIAG_VERSION;D->Bytes=sizeof(*D);
    D->SessionQpc=A->Timing.Snapshot.SessionQpc;D->Frequency=A->Timing.Snapshot.Frequency;
    D->WorkerStart=A->WorkerStartCount;D->SchedulingEnabled=RPI5CYW_TX_CREDIT_SCHEDULING;
    D->DetailedTimingEnabled=RPI5CYW_DETAILED_TIMING && A->Timing.Enabled;
}
static VOID CywTxDiagCapture(PRPI5CYW_ADAPTER A,CYW_DIAG_BUFFER *Buffer)
{
    UNICODE_STRING valueName;
    CYW_TX_CREDIT_DIAG snapshot;
    C_ASSERT(sizeof(CYW_TX_CREDIT_DIAG)==8*CYW_TX_DIAG_WORDS);
    if(!A->Timing.Enabled || KeGetCurrentIrql()!=PASSIVE_LEVEL)return;
    snapshot=A->TxCreditDiag;
    snapshot.SnapshotQpc=(CYW_TXD_U64)KeQueryPerformanceCounter(NULL).QuadPart;
    RtlInitUnicodeString(&valueName,L"TxCreditV1");
    CywDiagAppend(Buffer,&valueName,REG_BINARY,&snapshot,sizeof(snapshot));
}
static ULONG CywTxDiagQueue(PRPI5CYW_ADAPTER A,CYW_TX_STATE *Q)
{
    KIRQL irql;ULONG count,frames;CYW_TX_CREDIT_DIAG *D=&A->TxCreditDiag;
    KeAcquireSpinLock(&Q->Lock,&irql);count=Q->Count;frames=Q->Frames;
    KeReleaseSpinLock(&Q->Lock,irql);
    if(count>D->QueueEntriesSampleMax)D->QueueEntriesSampleMax=count;
    if(frames>D->QueueRetainedSampleMax)D->QueueRetainedSampleMax=frames;
    return count;
}
static VOID CywTxDiagPumpBegin(PRPI5CYW_ADAPTER A,CYW_TX_STATE *Q,ULONG Budget)
{
    CYW_TX_CREDIT_DIAG *D=&A->TxCreditDiag;
    CywTxDiagInc(&D->PumpCalls);D->PumpRequestedFrames=CywTxDiagAdd(D->PumpRequestedFrames,Budget);
    if(!CywTxDiagQueue(A,Q))CywTxDiagInc(&D->PumpEmptyStarts);
}
static VOID CywTxDiagPumpEnd(PRPI5CYW_ADAPTER A,CYW_TX_STATE *Q,
    ULONG Budget,ULONG Sent,CYW_TXD_U64 Start)
{
    CYW_TX_CREDIT_DIAG *D=&A->TxCreditDiag;
    if(A->Timing.Enabled)CywTxDiagDuration(D,&D->PumpTicks,&D->PumpMaxTicks,
        Start,(CYW_TXD_U64)KeQueryPerformanceCounter(NULL).QuadPart);
    D->PumpFrames=CywTxDiagAdd(D->PumpFrames,Sent);
    if(!Sent)CywTxDiagInc(&D->PumpNoProgress);
    if(Budget && Sent==Budget)CywTxDiagInc(&D->PumpBudgetHits);
    if(Sent>D->PumpMaxFrames)D->PumpMaxFrames=Sent;
    if(CywTxDiagQueue(A,Q))CywTxDiagInc(&D->PumpPendingEnds);
}
static VOID CywTxDiagGate(PRPI5CYW_ADAPTER A)
{
    CYW_NETWORK *N=A->Network;CYW_TX_CREDIT_DIAG *D=&A->TxCreditDiag;
    unsigned window=CywTxDiagWindow(N->TxSeq,N->TxMax);
    CywTxDiagCredit(D,window);
    /* Match the existing pre-transfer predicate exactly, including its order.
     * Fresh GLOBAL flow is checked later by the UNCHANGED F1 service gate. */
    if(A->IoStopped || N->Stop || N->Paused || N->SelectingBand || !N->Ready ||
       !N->Authorized || !N->Associated)CywTxDiagInc(&D->GateLifecycleBlocked);
    else if(!window)CywTxDiagInc(&D->GateCreditZero);
    else if(window>64)CywTxDiagInc(&D->GateCreditInvalid);
    else if(!CywTransportPriorityAllowed(&A->Transport,N->TxFlow))
        CywTxDiagInc(&D->GatePriorityBlocked);
}
typedef struct {ULONG Passes,Frames,Deadlines;} CYW_TX_POST_OBSERVATION;
static VOID CywTxDiagPostBegin(PRPI5CYW_ADAPTER A,CYW_TXD_U64 RxEnd,CYW_TX_POST_OBSERVATION *P)
{
    CYW_TX_CREDIT_DIAG *D=&A->TxCreditDiag;
    P->Passes=A->TxPressurePasses;P->Frames=A->TxPressureFrames;P->Deadlines=A->TxPressureDeadlineYields;
    CywTxDiagInc(&D->PostRxCalls);
    if(A->Timing.Enabled)CywTxDiagDuration(D,&D->RxEndToPostPumpTicks,
        &D->RxEndToPostPumpMaxTicks,RxEnd,(CYW_TXD_U64)KeQueryPerformanceCounter(NULL).QuadPart);
}
static VOID CywTxDiagPostEnd(PRPI5CYW_ADAPTER A,ULONG Sent,const CYW_TX_POST_OBSERVATION *P)
{
    CYW_TX_CREDIT_DIAG *D=&A->TxCreditDiag;
    if(!RPI5CYW_TX_CREDIT_SCHEDULING) {
        D->ExtraPasses=CywTxDiagAdd(D->ExtraPasses,(unsigned)(A->TxPressurePasses-P->Passes));
        D->ExtraFrames=CywTxDiagAdd(D->ExtraFrames,(unsigned)(A->TxPressureFrames-P->Frames));
        D->ExtraDeadlineYields=CywTxDiagAdd(D->ExtraDeadlineYields,(unsigned)(A->TxPressureDeadlineYields-P->Deadlines));
    }
    D->PostRxFrames=CywTxDiagAdd(D->PostRxFrames,Sent);
    if(Sent>D->PostRxMaxFrames)D->PostRxMaxFrames=Sent;
}
