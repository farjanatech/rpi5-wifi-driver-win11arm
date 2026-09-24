/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Read-only hint for the single bus worker. Credits and flow control are still
 * rechecked by the existing sender before EVERY additional frame. */
static BOOLEAN CywTxPressureEligible(PRPI5CYW_ADAPTER A,ULONG Threshold)
{
    CYW_NETWORK *N;
    KIRQL irql;
    BOOLEAN eligible;
    if(!A || !A->Network)return FALSE;
    N=A->Network;
    if(A->IoStopped || A->FifoTransportFailed || N->Stop || N->Paused ||
       N->SelectingBand || !N->Ready || !N->Associated || !N->Authorized ||
       !N->Published || A->Transport.Halted || A->Transport.GlobalFlow ||
       !CywTransportPriorityAllowed(&A->Transport,N->TxFlow) ||
       !CywTxCredit(N->TxSeq,N->TxMax,0))return FALSE;
    KeAcquireSpinLock(&N->Sends.Lock,&irql);
    eligible=(BOOLEAN)(N->Sends.Gate==NDIS_STATUS_SUCCESS &&
        N->Sends.Count!=0 && N->Sends.Frames>=Threshold);
    KeReleaseSpinLock(&N->Sends.Lock,irql);
    return eligible;
}
