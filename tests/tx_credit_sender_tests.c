/* Execute real transport helpers through the existing scripted SDIO fixture. */
#define main baseline_transport_main
#include "transport_tests.c"
#undef main
static unsigned QpcCalls;
LARGE_INTEGER KeQueryPerformanceCounter(LARGE_INTEGER *Frequency)
{
    LARGE_INTEGER value;
    if(Frequency)Frequency->QuadPart=1000000;
    value.QuadPart=(LONGLONG)++QpcCalls;return value;
}
static UCHAR CreditPayload[100];
int main(void)
{
    CYW_TX_CREDIT_DIAG *d=&TestAdapter.TxCreditDiag;
    unsigned i;ULONG permit;
    if(baseline_transport_main())return 1;
    Init();TestNetwork.TxMax=8;TestAdapter.Timing.Enabled=1;
    CHECK(CywSendFrame(&TestAdapter,2,CreditPayload,sizeof(CreditPayload))==STATUS_SUCCESS);
    CHECK(d->F1Calls==1 && d->F1StatusReads==1 && !d->F1Errors && !d->F1FlowBusy);
    CHECK(d->F2Calls==1 && !d->F2Errors && d->F2PayloadBytes==100 && d->F2PaddedBytes==112);
    CHECK(TestNetwork.TxSeq==1 && DataWrites==1);
    if(RPI5CYW_DETAILED_TIMING)CHECK(d->F1Ticks==1 && d->F2Ticks==1 && QpcCalls==4);
    else CHECK(!d->F1Ticks && !d->F2Ticks && !QpcCalls);
    Init();TestNetwork.TxMax=8;StatusScript[0]=CYW_INT_FC_STATE;
    CHECK(CywSendFrame(&TestAdapter,2,CreditPayload,sizeof(CreditPayload))==STATUS_DEVICE_BUSY);
    CHECK(d->F1Calls==1 && d->F1FlowBusy==1 && !d->F2Calls && !DataWrites && !TestNetwork.TxSeq);
    Init();TestNetwork.TxMax=8;FailIo=1;
    CHECK(CywSendFrame(&TestAdapter,2,CreditPayload,sizeof(CreditPayload))==STATUS_IO_DEVICE_ERROR);
    CHECK(d->F1Errors==1 && !d->F2Calls && !DataWrites && !TestNetwork.TxSeq);
    Init();TestNetwork.TxMax=8;FailFifo=1;
    CHECK(CywSendFrame(&TestAdapter,2,CreditPayload,sizeof(CreditPayload))==STATUS_IO_DEVICE_ERROR);
    CHECK(d->F2Calls==1 && d->F2Errors==1 && !d->F2PayloadBytes && !d->F2PaddedBytes && !TestNetwork.TxSeq);
    for(i=0;i<112;i++)CHECK(Tx[i]==0);
    Init();TestNetwork.TxMax=8;
    CHECK(CywSendFrame(&TestAdapter,0,CreditPayload,sizeof(CreditPayload))==STATUS_SUCCESS);
    CHECK(!d->F1Calls && !d->F2Calls); /* Firmware/control traffic is not data TX. */

#if RPI5CYW_TX_SERVICE_BURST4
    /* Four real credits, including sequence wrap, authorize three followers
     * after one fresh F1. Every follower is still a separate F2 transfer. */
    Init();TestNetwork.TxSeq=254;TestNetwork.TxMax=2;permit=0;
    CHECK(CywSendDataBurstStart(&TestAdapter,CreditPayload,sizeof(CreditPayload),4,&permit)==STATUS_SUCCESS);
    CHECK(permit==3 && TestAdapter.TxServiceBurstGrants==1);
    CHECK(TestAdapter.TxServiceBurstGrantedFollowers==3 && TestAdapter.TxServiceBurstMaxFollowers==3);
    CHECK(CywSendDataBurstReuse(&TestAdapter,CreditPayload,sizeof(CreditPayload),2)==STATUS_SUCCESS);
    CHECK(CywSendDataBurstReuse(&TestAdapter,CreditPayload,sizeof(CreditPayload),3)==STATUS_SUCCESS);
    CHECK(CywSendDataBurstReuse(&TestAdapter,CreditPayload,sizeof(CreditPayload),4)==STATUS_SUCCESS);
    CHECK(d->F1Calls==1 && d->F2Calls==4 && TestNetwork.TxSeq==2 && DataWrites==4);
    CHECK(TestAdapter.TxServiceBurstSecondSuccess==1 && TestAdapter.TxServiceBurstThirdSuccess==1);
    CHECK(TestAdapter.TxServiceBurstFourthSuccess==1 && TestAdapter.TxServiceBurstSavedStatusChecks==3);

    /* Three credits cap the grant at two followers even when four are wanted. */
    Init();TestNetwork.TxMax=3;permit=0;
    CHECK(CywSendDataBurstStart(&TestAdapter,CreditPayload,sizeof(CreditPayload),4,&permit)==STATUS_SUCCESS);
    CHECK(permit==2 && TestAdapter.TxServiceBurstGrantedFollowers==2);
    CHECK(CywSendDataBurstReuse(&TestAdapter,CreditPayload,sizeof(CreditPayload),2)==STATUS_SUCCESS);
    CHECK(CywSendDataBurstReuse(&TestAdapter,CreditPayload,sizeof(CreditPayload),3)==STATUS_SUCCESS);
    CHECK(TestNetwork.TxSeq==3 && d->F1Calls==1 && d->F2Calls==3);

    /* One real credit never creates a follower grant. */
    Init();TestNetwork.TxMax=1;permit=99;
    CHECK(CywSendDataBurstStart(&TestAdapter,CreditPayload,sizeof(CreditPayload),4,&permit)==STATUS_SUCCESS);
    CHECK(!permit && !TestAdapter.TxServiceBurstGrants && TestNetwork.TxSeq==1);

    /* Cached flow is rechecked before every follower. Stop before position 3:
     * no third F2 occurs and the already successful frames are not replayed. */
    Init();TestNetwork.TxMax=4;permit=0;
    CHECK(CywSendDataBurstStart(&TestAdapter,CreditPayload,sizeof(CreditPayload),4,&permit)==STATUS_SUCCESS && permit==3);
    CHECK(CywSendDataBurstReuse(&TestAdapter,CreditPayload,sizeof(CreditPayload),2)==STATUS_SUCCESS);
    TestAdapter.Transport.GlobalFlow=1;
    CHECK(CywSendDataBurstReuse(&TestAdapter,CreditPayload,sizeof(CreditPayload),3)==STATUS_DEVICE_BUSY);
    CHECK(TestNetwork.TxSeq==2 && DataWrites==2 && d->F2Calls==2);
    CHECK(TestAdapter.TxServiceBurstThirdAttempts==1 && TestAdapter.TxServiceBurstThirdBusy==1);

    /* A failing fourth F2 is terminal for that frame and is never replayed. */
    Init();TestNetwork.TxMax=4;permit=0;
    CHECK(CywSendDataBurstStart(&TestAdapter,CreditPayload,sizeof(CreditPayload),4,&permit)==STATUS_SUCCESS && permit==3);
    CHECK(CywSendDataBurstReuse(&TestAdapter,CreditPayload,sizeof(CreditPayload),2)==STATUS_SUCCESS);
    CHECK(CywSendDataBurstReuse(&TestAdapter,CreditPayload,sizeof(CreditPayload),3)==STATUS_SUCCESS);
    FailFifo=4;
    CHECK(CywSendDataBurstReuse(&TestAdapter,CreditPayload,sizeof(CreditPayload),4)==STATUS_IO_DEVICE_ERROR);
    CHECK(TestNetwork.TxSeq==3 && FifoCalls==4 && DataWrites==3);
    CHECK(TestAdapter.TxServiceBurstFourthAttempts==1 && TestAdapter.TxServiceBurstFourthErrors==1);
    CHECK(TestAdapter.TxServiceBurstSavedStatusChecks==2);
#endif
    if(Failures)return 1;
    puts("PASS: actual sender diagnostics plus bounded four-credit service reuse preserve F1/F2 errors, credits, wrap, flow gates and no-replay ownership.");
    return 0;
}
