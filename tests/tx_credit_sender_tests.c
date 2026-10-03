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
    unsigned i;
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
    if(Failures)return 1;
    puts("PASS: actual sender diagnostic counts, F1 busy/error, F2 failure/no replay, zeroization, control exclusion and opt-in timing.");
    return 0;
}
