/* SPDX-License-Identifier: GPL-3.0-or-later
 * v0.7.1.9 production-backlog tests. First run the unchanged active-queue
 * ownership suite, then exercise the new bounded deferred-NBL path.
 */
#define main baseline_queue_main
#include "tx_queue_tests.c"
#undef main

#define TOTAL_PENDING (CYW_TX_LIMIT + CYW_TX_BACKLOG_LIMIT)
static NET_BUFFER_LIST BacklogNbl[TOTAL_PENDING + 1];
static NET_BUFFER BacklogNb[TOTAL_PENDING + 8];

static void FillProduction(ULONG count,PVOID id)
{
    ULONG i;
    for(i=0;i<count;++i) {
        Packet(&BacklogNbl[i],&BacklogNb[i],100,id);
        CHECK(CywTxSubmitWithBacklog(&TestAdapter,&TestQueue,&BacklogNbl[i])==NDIS_STATUS_PENDING);
    }
}
static void FlushAndCheck(ULONG count)
{
    ULONG i;
    CywTxFlush(&TestAdapter,&TestQueue,NDIS_STATUS_PAUSED);
    CHECK(!CywTxOutstanding(&TestQueue));
    CHECK(!TestQueue.Count && !TestQueue.Frames && !TestQueue.Bytes);
    CHECK(!TestQueue.BacklogCount && !TestQueue.BacklogFrames && !TestQueue.BacklogBytes);
    CHECK(!TestAdapter.TxBacklogCurrent && !TestAdapter.TxBacklogNblCurrent);
    for(i=0;i<count;++i)CHECK(BacklogNbl[i].Completions==1);
}
static void TestBoundedBacklog(void)
{
    ULONG sent,i;PVOID id=&TestAdapter;
    Init();Credits=0;FillProduction(TOTAL_PENDING,id);
    CHECK(CYW_TX_LIMIT==64 && CYW_TX_BACKLOG_LIMIT==128);
    CHECK(TestQueue.Count==CYW_TX_LIMIT && TestQueue.Frames==CYW_TX_LIMIT);
    CHECK(TestQueue.BacklogCount==CYW_TX_BACKLOG_LIMIT);
    CHECK(TestQueue.BacklogFrames==CYW_TX_BACKLOG_LIMIT);
    CHECK(CywTxOutstanding(&TestQueue)==TOTAL_PENDING);
    CHECK(TestAdapter.TxNblAccepted==TOTAL_PENDING);
    CHECK(TestAdapter.TxBacklogAccepted==CYW_TX_BACKLOG_LIMIT);
    CHECK(TestAdapter.TxBacklogHighWater==CYW_TX_BACKLOG_LIMIT);
    CHECK(TestAdapter.TxBacklogCurrent==CYW_TX_BACKLOG_LIMIT);
    CHECK(TestAdapter.TxBacklogNblCurrent==CYW_TX_BACKLOG_LIMIT);
    CHECK(!TestAdapter.TxQueueFull && !TestAdapter.TxBacklogFull);
    Packet(&BacklogNbl[TOTAL_PENDING],&BacklogNb[TOTAL_PENDING],100,id);
    CHECK(CywTxSubmitWithBacklog(&TestAdapter,&TestQueue,&BacklogNbl[TOTAL_PENDING])==NDIS_STATUS_RESOURCES);
    CHECK(TestAdapter.TxQueueFull==1 && TestAdapter.TxBacklogFull==1);
    CHECK(CywTxOutstanding(&TestQueue)==TOTAL_PENDING);

    Credits=TOTAL_PENDING;TransferTicks=10000;
    CHECK(CywTxPump(&TestAdapter,&TestQueue,TOTAL_PENDING,&sent)==STATUS_SUCCESS);
    CHECK(sent==TOTAL_PENDING && TransferCalls==TOTAL_PENDING);
    CHECK(!CywTxOutstanding(&TestQueue));
    CHECK(TestAdapter.TxBacklogPromoted==CYW_TX_BACKLOG_LIMIT);
    CHECK(!TestAdapter.TxBacklogCurrent && !TestAdapter.TxBacklogNblCurrent);
    CHECK(TestAdapter.TxBacklogMaxDelayMs>0);
    CHECK(TestAdapter.TxQueueHighWater==CYW_TX_LIMIT);
    CHECK(TestAdapter.TxNblCompleted==TOTAL_PENDING);
    for(i=0;i<TOTAL_PENDING;++i)
        CHECK(BacklogNbl[i].Completions==1 && BacklogNbl[i].Status==NDIS_STATUS_SUCCESS);
    CHECK(!BacklogNbl[TOTAL_PENDING].Completions);
}
static void TestFifoPromotion(void)
{
    ULONG sent;PVOID activeId=&TestQueue,backId=&TestAdapter;
    Init();Credits=1;FillProduction(CYW_TX_LIMIT,activeId);
    Packet(&BacklogNbl[CYW_TX_LIMIT],&BacklogNb[CYW_TX_LIMIT],100,backId);
    Packet(&BacklogNbl[CYW_TX_LIMIT+1],&BacklogNb[CYW_TX_LIMIT+1],100,backId);
    CHECK(CywTxSubmitWithBacklog(&TestAdapter,&TestQueue,&BacklogNbl[CYW_TX_LIMIT])==NDIS_STATUS_PENDING);
    CHECK(CywTxSubmitWithBacklog(&TestAdapter,&TestQueue,&BacklogNbl[CYW_TX_LIMIT+1])==NDIS_STATUS_PENDING);
    CHECK(TestQueue.BacklogCount==2);
    CHECK(CywTxPump(&TestAdapter,&TestQueue,1,&sent)==STATUS_SUCCESS && sent==1);
    CHECK(TestQueue.Count==CYW_TX_LIMIT && TestQueue.Frames==CYW_TX_LIMIT);
    CHECK(TestQueue.Entries[CYW_TX_LIMIT-1].Nbl==&BacklogNbl[CYW_TX_LIMIT]);
    CHECK(TestQueue.BacklogCount==1 && TestQueue.Backlog[0].Nbl==&BacklogNbl[CYW_TX_LIMIT+1]);

    Packet(&BacklogNbl[CYW_TX_LIMIT+2],&BacklogNb[CYW_TX_LIMIT+2],100,backId);
    CHECK(CywTxSubmitWithBacklog(&TestAdapter,&TestQueue,&BacklogNbl[CYW_TX_LIMIT+2])==NDIS_STATUS_PENDING);
    CHECK(TestQueue.BacklogCount==2);
    CHECK(TestQueue.Backlog[0].Nbl==&BacklogNbl[CYW_TX_LIMIT+1]);
    CHECK(TestQueue.Backlog[1].Nbl==&BacklogNbl[CYW_TX_LIMIT+2]);
    FlushAndCheck(CYW_TX_LIMIT+3);
}
static void TestCancellationAndExpiry(void)
{
    ULONG sent,i;PVOID activeId=&TestQueue,backId=&TestAdapter;
    Init();Credits=0;CheckCompleting=1;FillProduction(CYW_TX_LIMIT,activeId);
    for(i=0;i<2;++i) {
        Packet(&BacklogNbl[CYW_TX_LIMIT+i],&BacklogNb[CYW_TX_LIMIT+i],100,backId);
        CHECK(CywTxSubmitWithBacklog(&TestAdapter,&TestQueue,&BacklogNbl[CYW_TX_LIMIT+i])==NDIS_STATUS_PENDING);
    }
    CywTxCancel(&TestQueue,backId);
    CHECK(CywTxPump(&TestAdapter,&TestQueue,4,&sent)==STATUS_SUCCESS && !sent);
    CHECK(!TestQueue.BacklogCount && TestAdapter.TxBacklogCancelled==2);
    CHECK(TestAdapter.TxCancelled==2 && CywTxOutstanding(&TestQueue)==CYW_TX_LIMIT);
    CHECK(BacklogNbl[CYW_TX_LIMIT].Status==NDIS_STATUS_SEND_ABORTED);
    CHECK(BacklogNbl[CYW_TX_LIMIT+1].Status==NDIS_STATUS_SEND_ABORTED);
    FlushAndCheck(CYW_TX_LIMIT+2);

    Init();Credits=0;CheckCompleting=1;FillProduction(CYW_TX_LIMIT,activeId);
    Packet(&BacklogNbl[CYW_TX_LIMIT],&BacklogNb[CYW_TX_LIMIT],100,backId);
    CHECK(CywTxSubmitWithBacklog(&TestAdapter,&TestQueue,&BacklogNbl[CYW_TX_LIMIT])==NDIS_STATUS_PENDING);
    Clock=CYW_TX_MAX_AGE;
    for(i=0;i<TestQueue.Count;++i)TestQueue.Entries[i].Submitted=Clock;
    TestQueue.Backlog[0].Submitted=0;
    CHECK(CywTxPump(&TestAdapter,&TestQueue,4,&sent)==STATUS_SUCCESS && !sent);
    CHECK(!TestQueue.BacklogCount && TestAdapter.TxBacklogExpired==1 && TestAdapter.TxExpired==1);
    CHECK(BacklogNbl[CYW_TX_LIMIT].Completions==1);
    CHECK(BacklogNbl[CYW_TX_LIMIT].Status==NDIS_STATUS_FAILURE);
    CHECK(TestAdapter.Traffic.Errors[1]==1);
    FlushAndCheck(CYW_TX_LIMIT+1);
}
static void TestMultiFramePromotion(void)
{
    ULONG sent,i;PVOID id=&TestAdapter;
    Init();Credits=4;FillProduction(60,id);
    memset(&BacklogNb[60],0,sizeof(BacklogNb[0])*8);
    Packet(&BacklogNbl[60],&BacklogNb[60],100,id);
    for(i=0;i<7;++i) {
        BacklogNb[60+i].Length=100;
        BacklogNb[60+i].Next=&BacklogNb[61+i];
    }
    BacklogNb[67].Length=100;BacklogNb[67].Next=NULL;
    BacklogNbl[60].First=&BacklogNb[60];
    CHECK(CywTxSubmitWithBacklog(&TestAdapter,&TestQueue,&BacklogNbl[60])==NDIS_STATUS_PENDING);
    CHECK(TestQueue.Frames==60 && TestQueue.BacklogFrames==8 && TestQueue.BacklogCount==1);
    CHECK(CywTxPump(&TestAdapter,&TestQueue,4,&sent)==STATUS_SUCCESS && sent==4);
    CHECK(!TestQueue.BacklogCount && TestQueue.Frames==64);
    CHECK(TestQueue.Entries[TestQueue.Count-1].Nbl==&BacklogNbl[60]);
    CHECK(TestQueue.Entries[TestQueue.Count-1].HeldFrames==8);
    CHECK(TestAdapter.TxBacklogPromoted==1);
    FlushAndCheck(61);
}
int main(void)
{
    if(baseline_queue_main())return 1;
    TestBoundedBacklog();
    TestFifoPromotion();
    TestCancellationAndExpiry();
    TestMultiFramePromotion();
    CHECK(!Locks);
    if(Failures)return 1;
    puts("PASS: v0.7.1.9 bounded 128-frame backlog, FIFO promotion, cancellation, expiry, lifecycle flush and unchanged 64-frame active window");
    return 0;
}
