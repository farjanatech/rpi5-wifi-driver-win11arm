/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Only the POST-receive pump may extend its normal four-frame budget. No
 * per-received-frame scheduling, queue enlargement, packet coalescing, early
 * completion, fabricated credit, or FIFO replay. Actual ownership, fresh F1
 * checks, cancellation and error handling remain in the original pump.
 *
 * A two-ms between-frame deadline bounds the extension; it cannot preempt a
 * transfer/completion already executing. Every SDIO transfer retains its own
 * deadline. Under low pressure or exhausted credits this is the original pump.
 */
static NTSTATUS CywTxPostReceivePump(PRPI5CYW_ADAPTER A,CYW_TX_STATE *Q,PULONG Sent)
{
    NTSTATUS status;
    ULONG extra,step;
    ULONG64 start,now;
    status=CywMeasuredTxPump(A,Q,4,Sent);
    if(!NT_SUCCESS(status) || *Sent!=4 || !CywTxPressureEligible(A,32))return status;
    A->TxPressurePasses++;
    start=KeQueryInterruptTime();
    for(extra=0;extra<4;++extra) {
        now=KeQueryInterruptTime();
        if(now<start || now-start>=20000ULL) {
            A->TxPressureDeadlineYields++;break;
        }
        /* Hysteresis ends this pass below 16 retained frames; cancellation,
         * link/power state, flow and available credits may stop it sooner. */
        if(!CywTxPressureEligible(A,16))break;
        status=CywMeasuredTxPump(A,Q,1,&step);
        *Sent+=step;A->TxPressureFrames+=step;
        if(!NT_SUCCESS(status) || !step)break;
    }
    return status;
}
