/* SPDX-License-Identifier: GPL-3.0-or-later
 * Included after tx_queue.h by network.c. The one PASSIVE bus worker uses
 * this read-only scheduling hint; it can shorten a wait only when ordinary
 * work made no progress.
 * It neither grants firmware credits nor overrides any flow-control gate.
 */
static BOOLEAN CywTxRetryEligible(PRPI5CYW_ADAPTER A)
{
    CYW_NETWORK *N;
    KIRQL irql;
    BOOLEAN eligible;
    if(!A || !A->Network)return FALSE;
    N=A->Network;
    if(A->IoStopped || N->Stop || N->Paused || N->SelectingBand ||
       !N->Ready || !N->Associated || !N->Authorized || !N->Published ||
       A->Transport.Halted || A->Transport.GlobalFlow ||
       !CywTransportPriorityAllowed(&A->Transport,N->TxFlow) ||
       N->TxSeq!=N->TxMax)return FALSE;
    /* Exact equality means exhausted credit, including sequence wrap. Do not
     * treat every !CywTxCredit result (e.g. an invalid >64 window) as eligible.
     * Submission owns these queue fields concurrently, so inspect under Lock.
     * Nothing else is called while holding the lock; in particular, no SDIO. */
    KeAcquireSpinLock(&N->Sends.Lock,&irql);
    eligible=(BOOLEAN)(N->Sends.Count!=0 && N->Sends.Gate==NDIS_STATUS_SUCCESS);
    KeReleaseSpinLock(&N->Sends.Lock,irql);
    return eligible;
}
