/* Exercise production FIFO promotion after one multi-frame NBL releases room. */
#define RPI5CYW_TX_GLOM2 1
#define main baseline_queue_main
#include "tx_queue_tests.c"
#undef main

int main(void)
{
    NET_BUFFER_LIST head, pending[3], bulk;
    NET_BUFFER chain[CYW_TX_LIMIT], small[3], large;
    ULONG i,sent;
    if(baseline_queue_main())return 1;
    Init();
    Packet(&head,&chain[0],100,&head);
    for(i=1;i<CYW_TX_LIMIT;++i) {
        memset(&chain[i],0,sizeof(chain[i]));chain[i].Length=100;
        chain[i-1].Next=&chain[i];
    }
    CHECK(CywTxSubmitWithBacklog(&TestAdapter,&TestQueue,&head)==NDIS_STATUS_PENDING);
    for(i=0;i<3;++i) {
        Packet(&pending[i],&small[i],100,&pending[i]);
        CHECK(CywTxSubmitWithBacklog(&TestAdapter,&TestQueue,&pending[i])==NDIS_STATUS_PENDING);
    }
    Credits=CYW_TX_LIMIT;
    CHECK(CywTxPump(&TestAdapter,&TestQueue,CYW_TX_LIMIT,&sent)==STATUS_SUCCESS);
    CHECK(sent==CYW_TX_LIMIT && head.Completions==1);
    /* All older sends fit. Leaving two stranded in backlog rejects a bulk
     * send despite 61 free active slots, until another packet completes. */
    CHECK(TestQueue.Count==3 && TestQueue.BacklogCount==0);
    for(i=0;i<3;++i)CHECK(TestQueue.Entries[i].Nbl==&pending[i]);
    Packet(&bulk,&large,1514,&bulk);
    CHECK(CywTxSubmitWithBacklog(&TestAdapter,&TestQueue,&bulk)==NDIS_STATUS_PENDING);
    CHECK(TestQueue.Frames==4 && TestAdapter.TxQueueFull==0);
    CywTxCancel(&TestQueue,&pending[1]);
    CywTxFlush(&TestAdapter,&TestQueue,NDIS_STATUS_LOW_POWER_STATE);
    CHECK(!CywTxOutstanding(&TestQueue));
    for(i=0;i<3;++i)CHECK(pending[i].Completions==1);
    CHECK(bulk.Completions==1 && TestAdapter.TxNblAccepted==TestAdapter.TxNblCompleted);
    CHECK(!Locks);
    if(Failures)return 1;
    puts("PASS: refill released capacity in FIFO order without increasing ownership limits.");
    return 0;
}
