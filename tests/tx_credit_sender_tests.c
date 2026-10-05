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
    unsigned i;BOOLEAN permit;
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

#if RPI5CYW_TX_SERVICE_BURST2
    /* A fresh first frame may authorize exactly one ordinary second F2 when
     * two real credits are visible after F1. The second frame performs no
     * additional F1 status service. */
    Init();TestNetwork.TxMax=8;permit=FALSE;
    CHECK(CywSendDataBurstStart(&TestAdapter,CreditPayload,sizeof(CreditPayload),TRUE,&permit)==STATUS_SUCCESS);
    CHECK(permit && TestAdapter.TxServiceBurstGrants==1);
    CHECK(d->F1Calls==1 && d->F2Calls==1 && TestNetwork.TxSeq==1 && DataWrites==1);
    CHECK(CywSendDataBurstSecond(&TestAdapter,CreditPayload,sizeof(CreditPayload))==STATUS_SUCCESS);
    CHECK(d->F1Calls==1 && d->F2Calls==2 && TestNetwork.TxSeq==2 && DataWrites==2);
    CHECK(TestAdapter.TxServiceBurstSecondAttempts==1);
    CHECK(TestAdapter.TxServiceBurstSecondSuccess==1);
    CHECK(TestAdapter.TxServiceBurstSavedStatusChecks==1);
    CHECK(!TestAdapter.TxServiceBurstSecondBusy && !TestAdapter.TxServiceBurstSecondErrors);

    /* One real credit never creates a second-frame grant. */
    Init();TestNetwork.TxMax=1;permit=TRUE;
    CHECK(CywSendDataBurstStart(&TestAdapter,CreditPayload,sizeof(CreditPayload),TRUE,&permit)==STATUS_SUCCESS);
    CHECK(!permit && !TestAdapter.TxServiceBurstGrants && TestNetwork.TxSeq==1);
    CHECK(d->F1Calls==1 && d->F2Calls==1);

    /* If the second F2 fails, the successful first frame is never replayed and
     * sequence ownership advances only once. */
    Init();TestNetwork.TxMax=8;permit=FALSE;
    CHECK(CywSendDataBurstStart(&TestAdapter,CreditPayload,sizeof(CreditPayload),TRUE,&permit)==STATUS_SUCCESS && permit);
    FailFifo=1;
    CHECK(CywSendDataBurstSecond(&TestAdapter,CreditPayload,sizeof(CreditPayload))==STATUS_IO_DEVICE_ERROR);
    CHECK(TestNetwork.TxSeq==1 && DataWrites==2);
    CHECK(d->F1Calls==1 && d->F2Calls==2 && d->F2Errors==1);
    CHECK(TestAdapter.TxServiceBurstSecondAttempts==1);
    CHECK(!TestAdapter.TxServiceBurstSecondSuccess);
    CHECK(TestAdapter.TxServiceBurstSecondErrors==1);
    CHECK(!TestAdapter.TxServiceBurstSavedStatusChecks);

    /* Cached flow state is rechecked before the service-reused F2. */
    Init();TestNetwork.TxMax=8;TestAdapter.Transport.GlobalFlow=1;
    CHECK(CywSendDataBurstSecond(&TestAdapter,CreditPayload,sizeof(CreditPayload))==STATUS_DEVICE_BUSY);
    CHECK(!d->F1Calls && !d->F2Calls && !DataWrites && !TestNetwork.TxSeq);
    CHECK(TestAdapter.TxServiceBurstSecondAttempts==1 && TestAdapter.TxServiceBurstSecondBusy==1);
#endif
    if(Failures)return 1;
    puts("PASS: actual sender diagnostics plus bounded two-credit service reuse preserve F1/F2 errors, credits, sequence and no-replay ownership.");
    return 0;
}
