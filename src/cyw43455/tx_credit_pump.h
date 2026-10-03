/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Retain the EXACT baseline high-pressure pump. Only an otherwise unused
 * extension at lower occupancy may spend up to four more confirmed credits.
 * No second extension after a baseline deadline, BUSY, fault, or short pump.
 * The unchanged gate and sender revalidate lifecycle/flow/credits per frame.
 */
static NTSTATUS CywTxCreditPostReceivePump(PRPI5CYW_ADAPTER A,CYW_TX_STATE *Q,PULONG Sent)
{
    ULONG passes=A->TxPressurePasses,deadlines=A->TxPressureDeadlineYields,step,extra;
    ULONG64 start,now;CYW_TX_CREDIT_DIAG *D=&A->TxCreditDiag;
    NTSTATUS status=CywTxPostReceivePump(A,Q,Sent);
    if(A->TxPressurePasses!=passes) {
        CywTxDiagInc(&D->ExtraPasses);
        if(*Sent>4)D->ExtraFrames=CywTxDiagAdd(D->ExtraFrames,*Sent-4);
        D->ExtraDeadlineYields=CywTxDiagAdd(D->ExtraDeadlineYields,
            (unsigned)(A->TxPressureDeadlineYields-deadlines));
        return status; /* Never replenish the original extension's deadline. */
    }
    if(!RPI5CYW_TX_CREDIT_SCHEDULING || !NT_SUCCESS(status) || *Sent!=4 ||
       !CywTxPressureEligible(A,1))return status;
    CywTxDiagInc(&D->ExtraPasses);start=KeQueryInterruptTime();
    for(extra=0;extra<4;++extra) {
        now=KeQueryInterruptTime();
        if(now<start || now-start>=20000ULL) {
            CywTxDiagInc(&D->ExtraDeadlineYields);break;
        }
        if(!CywTxPressureEligible(A,1))break;
        status=CywMeasuredTxPump(A,Q,1,&step);
        *Sent+=step;D->ExtraFrames=CywTxDiagAdd(D->ExtraFrames,step);
        if(!NT_SUCCESS(status) || !step)break;
    }
    return status;
}
