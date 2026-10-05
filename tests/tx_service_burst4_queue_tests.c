/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Focused ownership/scheduling tests for v0.7.1.18 Service-Burst4. */
#define main baseline_queue_main
#include "tx_queue_tests.c"
#undef main

static NET_BUFFER_LIST BurstNbl[CYW_TX_LIMIT+4];
static NET_BUFFER BurstNb[CYW_TX_LIMIT+4];

static void FillBurst(ULONG count)
{
    ULONG i;Init();
    for(i=0;i<count;++i) {
        Packet(&BurstNbl[i],&BurstNb[i],100,&BurstNbl[i]);
        CHECK(CywTxSubmit(&TestAdapter,&TestQueue,&BurstNbl[i])==NDIS_STATUS_PENDING);
    }
}
static void FinishBurst(ULONG count)
{
    ULONG i;
    CywTxFlush(&TestAdapter,&TestQueue,NDIS_STATUS_PAUSED);
    CHECK(!CywTxOutstanding(&TestQueue));
    for(i=0;i<count;++i)CHECK(BurstNbl[i].Completions<=1);
}
int main(void)
{
    ULONG sent;
    if(baseline_queue_main())return 1;

    /* Four frames / four credits: one fresh service plus positions 2,3,4. */
    FillBurst(5);Credits=4;TestAdapter.TxGlomEnabled=0;
    CHECK(CywTxPump(&TestAdapter,&TestQueue,4,&sent)==STATUS_SUCCESS && sent==4);
    CHECK(TransferCalls==4 && BurstStartCalls==1 && BurstReuseCalls==3);
    CHECK(BurstReusePosition[2]==1 && BurstReusePosition[3]==1 && BurstReusePosition[4]==1);
    CHECK(TestAdapter.TxServiceBurstGrants==1 && TestAdapter.TxServiceBurstGrantedFollowers==3);
    CHECK(TestAdapter.TxServiceBurstMaxFollowers==3 && TestAdapter.TxServiceBurstSavedStatusChecks==3);
    CHECK(Credits==0 && CompletionNbls==4 && !BurstNbl[4].Completions);
    FinishBurst(5);

    /* Credit windows 1,2,3 permit exactly 0,1,2 followers. */
    FillBurst(4);Credits=1;
    CHECK(CywTxPump(&TestAdapter,&TestQueue,4,&sent)==STATUS_SUCCESS && sent==1);
    CHECK(BurstStartCalls==1 && !BurstReuseCalls && !TestAdapter.TxServiceBurstGrants);
    FinishBurst(4);
    FillBurst(4);Credits=2;
    CHECK(CywTxPump(&TestAdapter,&TestQueue,4,&sent)==STATUS_SUCCESS && sent==2);
    CHECK(BurstStartCalls==1 && BurstReuseCalls==1 && BurstReusePosition[2]==1);
    CHECK(TestAdapter.TxServiceBurstGrantedFollowers==1);
    FinishBurst(4);
    FillBurst(4);Credits=3;
    CHECK(CywTxPump(&TestAdapter,&TestQueue,4,&sent)==STATUS_SUCCESS && sent==3);
    CHECK(BurstStartCalls==1 && BurstReuseCalls==2 && BurstReusePosition[3]==1);
    CHECK(TestAdapter.TxServiceBurstGrantedFollowers==2);
    FinishBurst(4);

    /* Pump budget bounds the grant; unused permission never escapes. */
    FillBurst(4);Credits=4;
    CHECK(CywTxPump(&TestAdapter,&TestQueue,2,&sent)==STATUS_SUCCESS && sent==2);
    CHECK(BurstStartCalls==1 && BurstReuseCalls==1 && TestAdapter.TxServiceBurstGrantedFollowers==1);
    CHECK(CywTxPump(&TestAdapter,&TestQueue,2,&sent)==STATUS_SUCCESS && sent==2);
    CHECK(BurstStartCalls==2 && BurstReuseCalls==2);
    FinishBurst(4);

    /* BUSY at the third transfer retains first+second and forces later work
     * to begin with a fresh service instead of carrying the old grant. */
    FillBurst(4);Credits=4;BusyAt=3;
    CHECK(CywTxPump(&TestAdapter,&TestQueue,4,&sent)==STATUS_SUCCESS && sent==2);
    CHECK(BurstReusePosition[2]==1 && BurstReusePosition[3]==1);
    CHECK(TestAdapter.TxServiceBurstThirdBusy==1 && TestAdapter.TxCreditWaits==1);
    BusyAt=0;
    CHECK(CywTxPump(&TestAdapter,&TestQueue,4,&sent)==STATUS_SUCCESS && sent==2);
    CHECK(BurstStartCalls==2);
    FinishBurst(4);

    /* Failure on the fourth F2 never replays it. Three prior NBLs complete
     * success, the failing NBL completes once as failure, later work remains. */
    FillBurst(5);Credits=5;FailAt=4;
    CHECK(CywTxPump(&TestAdapter,&TestQueue,4,&sent)==STATUS_IO_DEVICE_ERROR && sent==3);
    CHECK(BurstNbl[0].Status==NDIS_STATUS_SUCCESS && BurstNbl[1].Status==NDIS_STATUS_SUCCESS);
    CHECK(BurstNbl[2].Status==NDIS_STATUS_SUCCESS && BurstNbl[3].Status==NDIS_STATUS_FAILURE);
    CHECK(BurstNbl[3].Completions==1 && !BurstNbl[4].Completions);
    CHECK(TestAdapter.TxServiceBurstFourthErrors==1 && TestAdapter.TxServiceBurstSavedStatusChecks==2);
    FinishBurst(5);

    /* Cancellation during reused position 3 invalidates remaining permission.
     * The next frame may proceed only via a fresh burst start. */
    FillBurst(4);Credits=4;Hook=1;HookAt=3;
    CHECK(CywTxPump(&TestAdapter,&TestQueue,4,&sent)==STATUS_SUCCESS && sent==4);
    CHECK(BurstNbl[2].Status==NDIS_STATUS_SEND_ABORTED && BurstNbl[2].Completions==1);
    CHECK(BurstStartCalls==2 && BurstReusePosition[4]==0);
    CHECK(TestAdapter.TxCancelled==1);
    FinishBurst(4);

#if RPI5CYW_TX_GLOM2
    /* Existing pressure-only two-frame glom keeps precedence over Burst4. */
    FillBurst(CYW_TX_LIMIT);Credits=4;TestAdapter.TxGlomEnabled=1;
    CHECK(CywTxPump(&TestAdapter,&TestQueue,4,&sent)==STATUS_SUCCESS && sent==4);
    CHECK(PairTransferCalls>=1 && !BurstStartCalls);
    CHECK(!TestAdapter.TxServiceBurstGrants);
    FinishBurst(CYW_TX_LIMIT);
#endif

    CHECK(!Locks);
    if(Failures)return 1;
    puts("PASS: Service-Burst4 is bounded by pump/real credits, rechecks flow, invalidates on busy/cancel, preserves glom precedence and never replays F2.");
    return 0;
}
