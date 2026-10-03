/* Execute the unchanged ownership fixture and baseline tests first. */
#define main baseline_queue_main
#include "tx_queue_tests.c"
#undef main
#include "../src/cyw43455/tx_credit_pump.h"
static NET_BUFFER_LIST CreditNbl[CYW_TX_LIMIT];
static NET_BUFFER CreditNb[CYW_TX_LIMIT];
static void Fill(unsigned count,unsigned credits)
{PressureFill(CreditNbl,CreditNb,count,(PVOID)(size_t)23);Credits=credits;}
static void Finish(void)
{
    ULONG i,count=TestQueue.Count;
    (void)count;
    CywTxFlush(&TestAdapter,&TestQueue,NDIS_STATUS_PAUSED);
    CHECK(!CywTxOutstanding(&TestQueue) && !TestQueue.Frames && !Locks);
    for(i=0;i<CYW_TX_LIMIT;++i)CHECK(CreditNbl[i].Completions<=1);
}
int main(void)
{
    ULONG sent,queue,credits,expected,i;
    if(baseline_queue_main())return 1;
    /* Exhaust all retained queue sizes and usable credit budgets. Transfer
     * callbacks and the original sender contract still decide actual progress. */
    for(queue=0;queue<=64;++queue)for(credits=0;credits<=64;++credits) {
        Fill(queue,credits);expected=queue;
        if(expected>credits)expected=credits;
        if(expected>(RPI5CYW_TX_CREDIT_SCHEDULING || queue>=36?8u:4u))
            expected=(RPI5CYW_TX_CREDIT_SCHEDULING || queue>=36?8u:4u);
        CHECK(CywTxCreditPostReceivePump(&TestAdapter,&TestQueue,&sent)==STATUS_SUCCESS);
        CHECK(sent==expected && TransferCalls==expected && sent<=8);
        CHECK(TestQueue.Frames==queue-sent && !TestAdapter.TxQueueFull);
        Finish();
    }
    /* No new TX before/inside RX is introduced; refreshed credits are spent
     * by the caller's single post-RX invocation, not by receive callbacks. */
    Fill(12,0);CHECK(CywTxPump(&TestAdapter,&TestQueue,4,&sent)==0 && sent==0);
    Credits=8;
    CHECK(CywTxCreditPostReceivePump(&TestAdapter,&TestQueue,&sent)==0);
    CHECK(sent==(RPI5CYW_TX_CREDIT_SCHEDULING?8u:4u));Finish();
    Fill(20,64);TransferTicks=10000;
    CHECK(CywTxCreditPostReceivePump(&TestAdapter,&TestQueue,&sent)==0);
    CHECK(sent==(RPI5CYW_TX_CREDIT_SCHEDULING?6u:4u));
    CHECK(TestAdapter.TxCreditDiag.ExtraDeadlineYields==(RPI5CYW_TX_CREDIT_SCHEDULING?1u:0u));Finish();
    /* Baseline pressure deadline MUST NOT receive a second credit extension. */
    Fill(64,64);TransferTicks=20000;
    CHECK(CywTxCreditPostReceivePump(&TestAdapter,&TestQueue,&sent)==0 && sent==5);
    CHECK(TestAdapter.TxCreditDiag.ExtraDeadlineYields==1);Finish();
#if RPI5CYW_TX_CREDIT_SCHEDULING
    Fill(20,64);TransferTicks=1000;Hook=6;HookAt=5;
    CHECK(CywTxCreditPostReceivePump(&TestAdapter,&TestQueue,&sent)==0 && sent==5);
    CHECK(TestAdapter.TxCreditDiag.ExtraDeadlineYields==1);Finish();
    Fill(20,64);BusyAt=6;
    CHECK(CywTxCreditPostReceivePump(&TestAdapter,&TestQueue,&sent)==0 && sent==5 && TransferCalls==6);
    CHECK(!CreditNbl[5].Completions);Finish();
    Fill(20,64);FailAt=6;
    CHECK(CywTxCreditPostReceivePump(&TestAdapter,&TestQueue,&sent)==STATUS_IO_DEVICE_ERROR && sent==5);
    CHECK(TransferCalls==6 && CreditNbl[5].Completions==1 && CreditNbl[5].Status==NDIS_STATUS_FAILURE);Finish();
    for(i=1;i<=3;i++) { /* Cancel, Pause, D3 during extra frame; exactly-once ownership. */
        Fill(20,64);Hook=i;HookAt=5;
        CHECK(CywTxCreditPostReceivePump(&TestAdapter,&TestQueue,&sent)==0 && sent==5);Finish();
    }
    Fill(20,64);Hook=7;HookAt=5;
    CHECK(CywTxCreditPostReceivePump(&TestAdapter,&TestQueue,&sent)==0 && sent==5);Finish();
    Fill(20,64);Poison=CheckCompleting=1;
    Packet(&Reentrant,&ReentrantNb,100,(PVOID)(size_t)23);Reenter=1;
    CHECK(CywTxCreditPostReceivePump(&TestAdapter,&TestQueue,&sent)==0 && sent==8);
    CHECK(CompletionNbls==8 && LargestCompletion==1 && !Reentrant.Completions);Finish();
#else
    i=0;(void)i;
#endif
    if(Failures)return 1;
    puts("PASS: TX credit scheduler, 4225 queue/credit pairs, bounded post-RX reuse, original ownership and rollback policy.");
    return 0;
}
