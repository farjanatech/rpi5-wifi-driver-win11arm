/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Run the unchanged ownership fixture first, then exercise the actual wrapper.
 * This regression intentionally FAILS against 0.7.1.5's low-pressure extension.
 * Both build modes must retain exactly the stable four/eight-frame policy. */
#define main baseline_queue_main
#include "tx_queue_tests.c"
#undef main
#include "../src/cyw43455/tx_credit_pump.h"
static NET_BUFFER_LIST DispatchNbl[CYW_TX_LIMIT];
static NET_BUFFER DispatchNb[CYW_TX_LIMIT];
static void Fill(unsigned count,unsigned credits)
{PressureFill(DispatchNbl,DispatchNb,count,(PVOID)(size_t)23);Credits=credits;}
static void Finish(void)
{
    ULONG i;
    CywTxFlush(&TestAdapter,&TestQueue,NDIS_STATUS_PAUSED);
    CHECK(!CywTxOutstanding(&TestQueue) && !TestQueue.Frames && !Locks);
    for(i=0;i<CYW_TX_LIMIT;++i)CHECK(DispatchNbl[i].Completions<=1);
}
int main(void)
{
    ULONG sent,queue,credits,expected,i;
    if(baseline_queue_main())return 1;
    for(queue=0;queue<=64;++queue)for(credits=0;credits<=64;++credits) {
        Fill(queue,credits);expected=queue;
        if(expected>credits)expected=credits;
        if(expected>(queue>=36?8u:4u))expected=(queue>=36?8u:4u);
        CHECK(CywTxCreditPostReceivePump(&TestAdapter,&TestQueue,&sent)==STATUS_SUCCESS);
        CHECK(sent==expected && TransferCalls==expected && sent<=8);
        CHECK(TestQueue.Frames==queue-sent && !TestAdapter.TxQueueFull);
        CHECK(TestAdapter.TxCreditDiag.ExtraFrames==TestAdapter.TxPressureFrames);
        CHECK(TestAdapter.TxCreditDiag.ExtraPasses==TestAdapter.TxPressurePasses);
        Finish();
    }
    /* Fresh post-RX credits NEVER increase the original low-pressure budget. */
    Fill(12,0);CHECK(CywTxPump(&TestAdapter,&TestQueue,4,&sent)==0 && sent==0);
    Credits=8;
    CHECK(CywTxCreditPostReceivePump(&TestAdapter,&TestQueue,&sent)==0 && sent==4);Finish();
    Fill(20,64);TransferTicks=10000;Hook=7;HookAt=5;
    CHECK(CywTxCreditPostReceivePump(&TestAdapter,&TestQueue,&sent)==0 && sent==4);
    CHECK(!PressureBlocked && !TestAdapter.TxCreditDiag.ExtraPasses);Finish();
    /* Existing high-pressure deadline is not reset or extended. */
    Fill(64,64);TransferTicks=20000;
    CHECK(CywTxCreditPostReceivePump(&TestAdapter,&TestQueue,&sent)==0 && sent==5);
    CHECK(TestAdapter.TxCreditDiag.ExtraDeadlineYields==1);Finish();
    Fill(64,64);TransferTicks=1000;Hook=6;HookAt=5;
    CHECK(CywTxCreditPostReceivePump(&TestAdapter,&TestQueue,&sent)==0 && sent==5);
    CHECK(TestAdapter.TxCreditDiag.ExtraDeadlineYields==1);Finish();
    /* BUSY, faults, cancellation, lifecycle changes and flow stop at the same
     * transfer as the original pump; never a second pass or FIFO replay. */
    Fill(64,64);BusyAt=6;
    CHECK(CywTxCreditPostReceivePump(&TestAdapter,&TestQueue,&sent)==0 && sent==5 && TransferCalls==6);
    CHECK(!DispatchNbl[5].Completions);Finish();
    Fill(64,64);FailAt=6;
    CHECK(CywTxCreditPostReceivePump(&TestAdapter,&TestQueue,&sent)==STATUS_IO_DEVICE_ERROR && sent==5);
    CHECK(TransferCalls==6 && DispatchNbl[5].Completions==1 && DispatchNbl[5].Status==NDIS_STATUS_FAILURE);Finish();
    for(i=1;i<=3;i++) {
        Fill(64,64);Hook=i;HookAt=5;
        CHECK(CywTxCreditPostReceivePump(&TestAdapter,&TestQueue,&sent)==0 && sent==5);Finish();
    }
    Fill(64,64);Hook=7;HookAt=5;
    CHECK(CywTxCreditPostReceivePump(&TestAdapter,&TestQueue,&sent)==0 && sent==5);Finish();
    Fill(64,64);Poison=CheckCompleting=1;
    Packet(&Reentrant,&ReentrantNb,100,(PVOID)(size_t)23);Reenter=1;
    CHECK(CywTxCreditPostReceivePump(&TestAdapter,&TestQueue,&sent)==0 && sent==8);
    CHECK(CompletionNbls==8 && LargestCompletion==1 && !Reentrant.Completions);Finish();
    if(Failures)return 1;
    puts("PASS: dispatch-only wrapper, 4225 queue/credit pairs, exact baseline budget, deadlines, faults and ownership.");
    return 0;
}
