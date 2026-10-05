/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Focused ownership/scheduling tests for the v0.7.1.15 service-burst2 path. */
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

    /* Two ordinary frames: one fresh service grant, then exactly one reused
     * service state. F2 remains two independent transfers. */
    FillBurst(4);Credits=4;TestAdapter.TxGlomEnabled=0;
    CHECK(CywTxPump(&TestAdapter,&TestQueue,2,&sent)==STATUS_SUCCESS && sent==2);
    CHECK(TransferCalls==2 && BurstStartCalls==1 && BurstSecondCalls==1);
    CHECK(TestAdapter.TxServiceBurstGrants==1);
    CHECK(TestAdapter.TxServiceBurstSecondAttempts==1);
    CHECK(TestAdapter.TxServiceBurstSecondSuccess==1);
    CHECK(TestAdapter.TxServiceBurstSavedStatusChecks==1);
    CHECK(Credits==2 && CompletionNbls==2);
    FinishBurst(4);

    /* A one-frame pump budget cannot create a grant that would escape into a
     * later pump invocation. */
    FillBurst(2);Credits=4;
    CHECK(CywTxPump(&TestAdapter,&TestQueue,1,&sent)==STATUS_SUCCESS && sent==1);
    CHECK(BurstStartCalls==1 && !BurstSecondCalls);
    CHECK(!TestAdapter.TxServiceBurstGrants && !TestAdapter.TxServiceBurstSavedStatusChecks);
    CHECK(CywTxPump(&TestAdapter,&TestQueue,1,&sent)==STATUS_SUCCESS && sent==1);
    CHECK(BurstStartCalls==2 && !BurstSecondCalls);
    FinishBurst(2);

    /* One real credit makes progress on one frame but never grants a second. */
    FillBurst(2);Credits=1;
    CHECK(CywTxPump(&TestAdapter,&TestQueue,2,&sent)==STATUS_SUCCESS && sent==1);
    CHECK(BurstStartCalls==1 && !BurstSecondCalls && !TestAdapter.TxServiceBurstGrants);
    CHECK(TestAdapter.TxCreditWaits==1 && !BurstNbl[1].Completions);
    FinishBurst(2);

    /* If the service-reused second frame reports BUSY, the first success is
     * retained and the second frame remains pending for a fresh later service. */
    FillBurst(3);Credits=3;BusyAt=2;
    CHECK(CywTxPump(&TestAdapter,&TestQueue,3,&sent)==STATUS_SUCCESS && sent==1);
    CHECK(BurstStartCalls==1 && BurstSecondCalls==1);
    CHECK(TestAdapter.TxServiceBurstSecondBusy==1);
    CHECK(BurstNbl[0].Completions==1 && !BurstNbl[1].Completions);
    CHECK(TestAdapter.TxCreditWaits==1);
    CHECK(CywTxPump(&TestAdapter,&TestQueue,3,&sent)==STATUS_SUCCESS && sent==2);
    CHECK(BurstNbl[1].Completions==1 && BurstNbl[2].Completions==1);
    FinishBurst(3);

    /* An indeterminate/failing second F2 never replays. The first NBL stays
     * successful, the failing NBL completes once as failure, later work waits. */
    FillBurst(3);Credits=3;FailAt=2;
    CHECK(CywTxPump(&TestAdapter,&TestQueue,3,&sent)==STATUS_IO_DEVICE_ERROR && sent==1);
    CHECK(BurstNbl[0].Completions==1 && BurstNbl[0].Status==NDIS_STATUS_SUCCESS);
    CHECK(BurstNbl[1].Completions==1 && BurstNbl[1].Status==NDIS_STATUS_FAILURE);
    CHECK(!BurstNbl[2].Completions);
    CHECK(TestAdapter.TxServiceBurstSecondErrors==1);
    CHECK(!TestAdapter.TxServiceBurstSavedStatusChecks);
    FinishBurst(3);

#if RPI5CYW_TX_GLOM2
    /* Existing pressure-only two-frame glom keeps precedence. Service-burst2
     * is for ordinary singles that would otherwise each perform their own F1. */
    FillBurst(CYW_TX_LIMIT);Credits=4;TestAdapter.TxGlomEnabled=1;
    CHECK(CywTxPump(&TestAdapter,&TestQueue,2,&sent)==STATUS_SUCCESS && sent==2);
    CHECK(PairTransferCalls==1 && !BurstStartCalls && !BurstSecondCalls);
    CHECK(!TestAdapter.TxServiceBurstGrants);
    FinishBurst(CYW_TX_LIMIT);
#endif

    CHECK(!Locks);
    if(Failures)return 1;
    puts("PASS: bounded service-burst2 saves at most one F1, never escapes a pump, preserves glom precedence and exactly-once ownership.");
    return 0;
}
