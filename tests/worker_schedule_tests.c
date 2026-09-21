/* Compile the actual runtime scheduling helper; simulate transport/NDIS. */
#include <stdio.h>
#define RPI5CYW_HOST_TEST 1
#include "../src/driver/driver.h"
#include "../src/cyw43455/network_protocol.h"
typedef struct {ULONG Unused;} TEST_SENDS;
typedef struct {int Stop,Paused;TEST_SENDS Sends;} CYW_NETWORK;
#ifndef STATUS_NO_MORE_ENTRIES
#define STATUS_NO_MORE_ENTRIES ((NTSTATUS)0x8000001aL)
#endif
static ULONG Failures,Pending,Credits,RxRemaining,RxCalls,TxCalls,FailRx,FailTx,StopRx,PauseRx;
static ULONG64 Clock,RxTime,TxTime;
static char Trace[128];static ULONG TraceCount;
static RPI5CYW_ADAPTER Adapter;
static CYW_NETWORK Network;
#define CHECK(x) do {if(!(x)){printf("FAIL %d: %s\n",__LINE__,#x);Failures++;}}while(0)
ULONG64 KeQueryInterruptTime(void){return Clock;}
static NTSTATUS CywTxPump(PRPI5CYW_ADAPTER A,TEST_SENDS *Q,ULONG Budget,PULONG Sent)
{
    (void)A;(void)Q;TxCalls++;*Sent=0;
    CHECK(!Network.Stop && !Network.Paused);
    if(FailTx==TxCalls)return STATUS_IO_DEVICE_ERROR;
    while(*Sent<Budget && Pending && Credits){(*Sent)++;Pending--;Credits--;Clock+=TxTime;Trace[TraceCount++]='T';}
    return STATUS_SUCCESS;
}
static NTSTATUS CywPoll(PRPI5CYW_ADAPTER A,PULONG Channel,PULONG Offset,PULONG Length)
{
    (void)A;*Channel=2;*Offset=12;*Length=64;RxCalls++;
    CHECK(!Network.Stop && !Network.Paused);
    if(FailRx==RxCalls)return STATUS_IO_DEVICE_ERROR;
    if(!RxRemaining)return STATUS_NO_MORE_ENTRIES;
    RxRemaining--;Credits+=2;Clock+=RxTime;Trace[TraceCount++]='R';
    if(StopRx==RxCalls)Network.Stop=1;
    if(PauseRx==RxCalls)Network.Paused=1;
    return STATUS_SUCCESS;
}
#include "../src/cyw43455/worker_io.h"
static void Init(void)
{
    memset(&Adapter,0,sizeof(Adapter));memset(&Network,0,sizeof(Network));memset(Trace,0,sizeof(Trace));
    Pending=Credits=RxRemaining=RxCalls=TxCalls=FailRx=FailTx=StopRx=PauseRx=TraceCount=0;
    Clock=RxTime=TxTime=0;
}
int main(void)
{
    ULONG sent,received;
    Init();Pending=32;RxRemaining=16;
    CHECK(CywServiceIo(&Adapter,&Network,&sent,&received)==0);
    CHECK(sent==8 && received==4 && !strcmp(Trace,"RTTRTTRTTRTT"));
    CHECK(Adapter.TxInterleavedPackets==8 && Adapter.RxBatchYields==1 && RxCalls==4);
    /* TX time counts toward the RX fairness budget; no unbounded receive drain. */
    Init();Pending=32;RxRemaining=16;RxTime=15000;TxTime=3000;
    CHECK(CywServiceIo(&Adapter,&Network,&sent,&received)==0 && received==1 && sent==2 && RxCalls==1);
    Init();Pending=32;Credits=32;
    CHECK(CywServiceIo(&Adapter,&Network,&sent,&received)==0 && sent==8 && !received);
    Init();CHECK(CywServiceIo(&Adapter,&Network,&sent,&received)==0 && !sent && !received);
    Init();Network.Stop=1;CHECK(CywServiceIo(&Adapter,&Network,&sent,&received)==0 && !TxCalls && !RxCalls);
    Init();Network.Paused=1;CHECK(CywServiceIo(&Adapter,&Network,&sent,&received)==0 && !TxCalls && !RxCalls);
    Init();Pending=32;RxRemaining=16;StopRx=1;
    CHECK(CywServiceIo(&Adapter,&Network,&sent,&received)==0 && received==1 && !sent && TxCalls==1);
    Init();Pending=32;RxRemaining=16;PauseRx=1;
    CHECK(CywServiceIo(&Adapter,&Network,&sent,&received)==0 && received==1 && !sent && TxCalls==1);
    Init();FailTx=1;CHECK(CywServiceIo(&Adapter,&Network,&sent,&received)==STATUS_IO_DEVICE_ERROR && !RxCalls);
    Init();RxRemaining=16;FailRx=1;CHECK(CywServiceIo(&Adapter,&Network,&sent,&received)==STATUS_IO_DEVICE_ERROR && TxCalls==1);
    Init();Pending=32;RxRemaining=16;FailTx=2;
    CHECK(CywServiceIo(&Adapter,&Network,&sent,&received)==STATUS_IO_DEVICE_ERROR && received==1 && RxCalls==1);
    if(Failures)return 1;
    puts("PASS: actual worker scheduling: immediate credit service, bounded fairness, idle, pause/stop and bus failure");return 0;
}
