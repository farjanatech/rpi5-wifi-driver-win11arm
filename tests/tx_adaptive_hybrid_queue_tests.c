/* SPDX-License-Identifier: GPL-3.0-or-later */
/* v0.7.1.19 adaptive TX hybrid: small ACK/control traffic keeps backlog,
 * Glom2 and Burst4; bulk traffic gets main-style active-only/fresh-F1 pacing.
 */
#define RPI5CYW_TX_GLOM2 1
#define main baseline_queue_main
#include "tx_queue_tests.c"
#undef main

static NET_BUFFER_LIST HybridNbl[CYW_TX_LIMIT+8];
static NET_BUFFER HybridNb[CYW_TX_LIMIT+8];

static void FillHybrid(ULONG count,ULONG length)
{
    ULONG i;Init();
    for(i=0;i<count;++i) {
        Packet(&HybridNbl[i],&HybridNb[i],length,&HybridNbl[i]);
        CHECK(CywTxSubmit(&TestAdapter,&TestQueue,&HybridNbl[i])==NDIS_STATUS_PENDING);
    }
}
static void FinishHybrid(ULONG count)
{
    ULONG i;
    CywTxFlush(&TestAdapter,&TestQueue,NDIS_STATUS_PAUSED);
    CHECK(!CywTxOutstanding(&TestQueue));
    for(i=0;i<count;++i)CHECK(HybridNbl[i].Completions<=1);
}
int main(void)
{
    ULONG sent,i;
    if(baseline_queue_main())return 1;

    /* Boundary and admission: 512-byte NBL may backlog; 513-byte bulk NBL
     * gets main-style RESOURCES/backpressure instead of entering backlog. */
    FillHybrid(CYW_TX_LIMIT,100);Credits=0;
    Packet(&HybridNbl[CYW_TX_LIMIT],&HybridNb[CYW_TX_LIMIT],512,&HybridNbl[CYW_TX_LIMIT]);
    CHECK(CywTxSubmitWithBacklog(&TestAdapter,&TestQueue,&HybridNbl[CYW_TX_LIMIT])==NDIS_STATUS_PENDING);
    CHECK(TestQueue.BacklogCount==1 && TestAdapter.TxAdaptiveSmallBacklogAccepted==1);
    Packet(&HybridNbl[CYW_TX_LIMIT+1],&HybridNb[CYW_TX_LIMIT+1],513,&HybridNbl[CYW_TX_LIMIT+1]);
    CHECK(CywTxSubmitWithBacklog(&TestAdapter,&TestQueue,&HybridNbl[CYW_TX_LIMIT+1])==NDIS_STATUS_RESOURCES);
    CHECK(TestAdapter.TxAdaptiveBulkBackpressure==1 && TestQueue.BacklogCount==1);
    FinishHybrid(CYW_TX_LIMIT+2);

    /* Multi-NB classification uses the largest constituent frame, so a mixed
     * NBL containing one bulk frame cannot hide in the small-packet backlog. */
    FillHybrid(CYW_TX_LIMIT,100);Credits=0;
    Packet(&HybridNbl[CYW_TX_LIMIT],&HybridNb[CYW_TX_LIMIT],100,&HybridNbl[CYW_TX_LIMIT]);
    memset(&HybridNb[CYW_TX_LIMIT+1],0,sizeof(HybridNb[0]));
    HybridNb[CYW_TX_LIMIT].Next=&HybridNb[CYW_TX_LIMIT+1];
    HybridNb[CYW_TX_LIMIT+1].Length=513;
    CHECK(CywTxSubmitWithBacklog(&TestAdapter,&TestQueue,&HybridNbl[CYW_TX_LIMIT])==NDIS_STATUS_RESOURCES);
    CHECK(TestAdapter.TxAdaptiveBulkBackpressure==1 && !TestQueue.BacklogCount);
    FinishHybrid(CYW_TX_LIMIT+1);

    /* Small ordinary traffic retains Burst4: one fresh service plus three
     * followers for four small frames. */
    FillHybrid(4,100);Credits=4;TestAdapter.TxGlomEnabled=0;
    CHECK(CywTxPump(&TestAdapter,&TestQueue,4,&sent)==STATUS_SUCCESS && sent==4);
    CHECK(BurstStartCalls==1 && BurstReuseCalls==3);
    CHECK(TestAdapter.TxAdaptiveSmallBurstStarts==1 && TestAdapter.TxAdaptiveSmallFrames==4);
    CHECK(!TestAdapter.TxAdaptiveBulkFreshF1Attempts && !TestAdapter.TxAdaptiveBulkFrames);
    FinishHybrid(4);

    /* Bulk upload traffic under pressure never enters Glom2 or follower reuse.
     * Every frame starts its own fresh-F1 service transaction. */
    FillHybrid(CYW_TX_LIMIT,1514);Credits=8;TestAdapter.TxGlomEnabled=1;
    CHECK(CywTxPump(&TestAdapter,&TestQueue,4,&sent)==STATUS_SUCCESS && sent==4);
    CHECK(!PairTransferCalls && BurstStartCalls==4 && !BurstReuseCalls);
    CHECK(TestAdapter.TxAdaptiveBulkFreshF1Attempts==4 && TestAdapter.TxAdaptiveBulkFrames==4);
    CHECK(!TestAdapter.TxAdaptiveSmallGlomChains && !TestAdapter.TxAdaptiveSmallFrames);
    FinishHybrid(CYW_TX_LIMIT);

    /* Small traffic under the same pressure still keeps Glom2 precedence. */
    FillHybrid(CYW_TX_LIMIT,100);Credits=8;TestAdapter.TxGlomEnabled=1;
    CHECK(CywTxPump(&TestAdapter,&TestQueue,4,&sent)==STATUS_SUCCESS && sent==4);
    CHECK(PairTransferCalls==2 && !BurstStartCalls && !BurstReuseCalls);
    CHECK(TestAdapter.TxAdaptiveSmallGlomChains==2 && TestAdapter.TxAdaptiveSmallFrames==4);
    FinishHybrid(CYW_TX_LIMIT);

    /* Mixed ordering: Burst4 stops before the first 513-byte frame. The bulk
     * frame uses a fresh service, and a following small frame starts a new
     * small burst rather than inheriting stale permission. */
    Init();Credits=4;TestAdapter.TxGlomEnabled=0;
    for(i=0;i<4;++i) {
        ULONG length=(i==0?100:(i==1?512:(i==2?513:100)));
        Packet(&HybridNbl[i],&HybridNb[i],length,&HybridNbl[i]);
        CHECK(CywTxSubmit(&TestAdapter,&TestQueue,&HybridNbl[i])==NDIS_STATUS_PENDING);
    }
    CHECK(CywTxPump(&TestAdapter,&TestQueue,4,&sent)==STATUS_SUCCESS && sent==4);
    CHECK(BurstStartCalls==3 && BurstReuseCalls==1 && BurstReusePosition[2]==1);
    CHECK(TestAdapter.TxAdaptiveSmallBurstStarts==2);
    CHECK(TestAdapter.TxAdaptiveBulkFreshF1Attempts==1);
    CHECK(TestAdapter.TxAdaptiveSmallFrames==3 && TestAdapter.TxAdaptiveBulkFrames==1);
    FinishHybrid(4);

    CHECK(!Locks);
    if(Failures)return 1;
    puts("PASS: adaptive hybrid keeps small backlog/Glom2/Burst4 while bulk uses active-only admission and fresh-F1 per frame.");
    return 0;
}
