/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Two-frame TX glom ownership tests. The original queue suite runs first with
 * TxGlomEnabled=0, proving the compile-time candidate does not alter baseline
 * behavior until firmware negotiation explicitly enables it.
 */
#define RPI5CYW_TX_GLOM2 1
#define main baseline_queue_main
#include "tx_queue_tests.c"
#undef main

static NET_BUFFER_LIST GlomNbl[CYW_TX_LIMIT+4];
static NET_BUFFER GlomNb[CYW_TX_LIMIT+4];

static void FillUnique(ULONG count)
{
    ULONG i;Init();
    for(i=0;i<count;++i) {
        Packet(&GlomNbl[i],&GlomNb[i],100,&GlomNbl[i]);
        CHECK(CywTxSubmit(&TestAdapter,&TestQueue,&GlomNbl[i])==NDIS_STATUS_PENDING);
    }
}
static void Finish(ULONG count)
{
    ULONG i;
    CywTxFlush(&TestAdapter,&TestQueue,NDIS_STATUS_PAUSED);
    CHECK(!CywTxOutstanding(&TestQueue) && !TestQueue.Frames && !TestQueue.BacklogFrames);
    for(i=0;i<count;++i)CHECK(GlomNbl[i].Completions<=1);
}
int main(void)
{
    ULONG sent,i;
    if(baseline_queue_main())return 1;

    /* Under active pressure, four-frame pump budget becomes exactly two
     * two-frame F2 chains. No active/backlog limit changes. */
    FillUnique(CYW_TX_LIMIT);TestAdapter.TxGlomEnabled=1;Credits=64;
    CHECK(CywTxPump(&TestAdapter,&TestQueue,4,&sent)==STATUS_SUCCESS && sent==4);
    CHECK(PairTransferCalls==2 && TransferCalls==0 && Credits==60);
    CHECK(CompletionNbls==4 && TestQueue.Frames==60 && TestQueue.Count==60);
    for(i=0;i<4;++i)CHECK(GlomNbl[i].Completions==1 && GlomNbl[i].Status==NDIS_STATUS_SUCCESS);
    Finish(CYW_TX_LIMIT);

    /* One cached credit never enters the pair path; retain useful single-frame
     * progress rather than waiting for a second credit. */
    FillUnique(CYW_TX_LIMIT);TestAdapter.TxGlomEnabled=1;Credits=1;
    CHECK(CywTxPump(&TestAdapter,&TestQueue,2,&sent)==STATUS_SUCCESS && sent==1);
    CHECK(!PairTransferCalls && TransferCalls==1 && TestAdapter.TxCreditWaits==1);
    Finish(CYW_TX_LIMIT);

    /* Fresh pair gate can collapse after cached eligibility. DEVICE_BUSY means
     * no F2 occurred, so the pump falls back to singles without loss/replay. */
    FillUnique(CYW_TX_LIMIT);TestAdapter.TxGlomEnabled=1;Credits=4;PairBusy=1;
    CHECK(CywTxPump(&TestAdapter,&TestQueue,2,&sent)==STATUS_SUCCESS && sent==2);
    CHECK(PairTransferCalls==1 && TransferCalls==2 && Credits==2);
    CHECK(GlomNbl[0].Completions==1 && GlomNbl[1].Completions==1);
    Finish(CYW_TX_LIMIT);

    /* A failed aggregate is indeterminate as a whole: both constituent NBLs
     * fail exactly once and are never replayed individually. */
    FillUnique(CYW_TX_LIMIT);TestAdapter.TxGlomEnabled=1;Credits=4;PairFail=1;
    CHECK(CywTxPump(&TestAdapter,&TestQueue,4,&sent)==STATUS_IO_DEVICE_ERROR && sent==0);
    CHECK(PairTransferCalls==1 && TransferCalls==0);
    CHECK(GlomNbl[0].Completions==1 && GlomNbl[0].Status==NDIS_STATUS_FAILURE);
    CHECK(GlomNbl[1].Completions==1 && GlomNbl[1].Status==NDIS_STATUS_FAILURE);
    CHECK(TestQueue.Count==CYW_TX_LIMIT-2 && TestQueue.Frames==CYW_TX_LIMIT-2);
    Finish(CYW_TX_LIMIT);

    /* Cancellation arriving while the one F2 chain is in flight cannot retract
     * bytes already submitted, but ownership/status remains exactly-once. */
    FillUnique(CYW_TX_LIMIT);TestAdapter.TxGlomEnabled=1;Credits=4;PairHook=1;
    CHECK(CywTxPump(&TestAdapter,&TestQueue,2,&sent)==STATUS_SUCCESS && sent==2);
    CHECK(GlomNbl[0].Status==NDIS_STATUS_SUCCESS);
    CHECK(GlomNbl[1].Status==NDIS_STATUS_SEND_ABORTED && TestAdapter.TxCancelled==1);
    CHECK(TestAdapter.TxPackets==2 && PairTransferCalls==1);
    Finish(CYW_TX_LIMIT);

    /* Production backlog promotion remains FIFO: completing a pair promotes
     * exactly the two oldest deferred one-frame NBLs back into the 64-frame
     * active window. */
    Init();Credits=0;
    for(i=0;i<CYW_TX_LIMIT+2;++i) {
        Packet(&GlomNbl[i],&GlomNb[i],100,&GlomNbl[i]);
        CHECK(CywTxSubmitWithBacklog(&TestAdapter,&TestQueue,&GlomNbl[i])==NDIS_STATUS_PENDING);
    }
    CHECK(TestQueue.Count==CYW_TX_LIMIT && TestQueue.BacklogCount==2);
    TestAdapter.TxGlomEnabled=1;Credits=4;
    CHECK(CywTxPump(&TestAdapter,&TestQueue,2,&sent)==STATUS_SUCCESS && sent==2);
    CHECK(PairTransferCalls==1 && TestQueue.Count==CYW_TX_LIMIT && !TestQueue.BacklogCount);
    CHECK(TestQueue.Entries[CYW_TX_LIMIT-2].Nbl==&GlomNbl[CYW_TX_LIMIT]);
    CHECK(TestQueue.Entries[CYW_TX_LIMIT-1].Nbl==&GlomNbl[CYW_TX_LIMIT+1]);
    CHECK(TestAdapter.TxBacklogPromoted==2);
    Finish(CYW_TX_LIMIT+2);

    /* Multi-NB/partially retained entries stay on the original single path. */
    Init();Credits=8;
    for(i=0;i<33;++i){Packet(&GlomNbl[i],&GlomNb[i],100,&GlomNbl[i]);}
    GlomNb[0].Next=&GlomNb[1];
    CHECK(CywTxSubmit(&TestAdapter,&TestQueue,&GlomNbl[0])==NDIS_STATUS_PENDING);
    for(i=2;i<33;++i)CHECK(CywTxSubmit(&TestAdapter,&TestQueue,&GlomNbl[i])==NDIS_STATUS_PENDING);
    TestAdapter.TxGlomEnabled=1;
    CHECK(CywTxPump(&TestAdapter,&TestQueue,2,&sent)==STATUS_SUCCESS && sent==2);
    CHECK(!PairTransferCalls && TransferCalls==2);
    Finish(33);

    CHECK(!Locks);
    if(Failures)return 1;
    puts("PASS: guarded two-frame TX glom preserves credits, FIFO backlog promotion, cancellation, fault and multi-NB ownership");
    return 0;
}
