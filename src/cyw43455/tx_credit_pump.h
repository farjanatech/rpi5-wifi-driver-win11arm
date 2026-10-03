/* SPDX-License-Identifier: GPL-3.0-or-later */
/* 0.7.1.6: dispatch-order experiment ONLY. Invoke the exact stable pump once.
 * Both modes have the same four-frame base budget, original high-pressure
 * extension, deadline, ownership and fresh F1 gates. There is NO lower-pressure
 * extension, second pass, replay, or invented credit. Only network.c's existing
 * compile-time switch decides whether this pump runs before diagnostic export.
 */
static NTSTATUS CywTxCreditPostReceivePump(PRPI5CYW_ADAPTER A,CYW_TX_STATE *Q,PULONG Sent)
{
    ULONG passes=A->TxPressurePasses,deadlines=A->TxPressureDeadlineYields;
    CYW_TX_CREDIT_DIAG *D=&A->TxCreditDiag;
    NTSTATUS status=CywTxPostReceivePump(A,Q,Sent);
    if(A->TxPressurePasses!=passes) {
        CywTxDiagInc(&D->ExtraPasses);
        if(*Sent>4)D->ExtraFrames=CywTxDiagAdd(D->ExtraFrames,*Sent-4);
        D->ExtraDeadlineYields=CywTxDiagAdd(D->ExtraDeadlineYields,
            (unsigned)(A->TxPressureDeadlineYields-deadlines));
    }
    return status;
}
