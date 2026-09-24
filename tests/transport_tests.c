/* Production transport service/poll/send helpers with scripted SDIO only.
 * Runs on GitHub CI; no real adapter, registry, or driver installation. */
#include <stdio.h>
#include <stdlib.h>
#define RPI5CYW_HOST_TEST 1
#include "../src/driver/driver.h"
#include "../src/cyw43455/network_protocol.h"
#include "../src/cyw43455/rx_performance.h"
#define STATUS_CANCELLED ((NTSTATUS)0xc0000120L)
#define STATUS_UNSUCCESSFUL ((NTSTATUS)0xc0000001L)
#define STATUS_DEVICE_NOT_READY ((NTSTATUS)0xc00000a3L)
#define STATUS_NO_MORE_ENTRIES ((NTSTATUS)0x8000001aL)
#define STATUS_NOT_SUPPORTED ((NTSTATUS)0xc00000bbL)
#define STATUS_INVALID_BUFFER_SIZE ((NTSTATUS)0xc0000206L)
#define STATUS_DEVICE_BUSY ((NTSTATUS)0x80000011L)
#define RtlCopyMemory memcpy
#define RtlSecureZeroMemory(p,n) memset(p,0,n)
#define TRY(x) do {Status=(x);if(!NT_SUCCESS(Status))goto Exit;}while(0)
typedef struct _CYW_NETWORK {
    int Stop;
    BOOLEAN RxPending,RxBatch,RxBatchServiced;
    BOOLEAN Ready,Authorized,Paused;
    UCHAR TxFlow,TxSeq,TxMax;
    PUCHAR Rx,Tx;
    ULONG RxNextLength;
    CYW_RX_GLOM RxGlom;
} CYW_NETWORK;
static unsigned Failures,IoCalls,FailIo,FifoCalls,FailFifo,PendingCalls,StatusReads;
static unsigned AckWrites,MailReads,MailAcks,DataWrites,Delivered,Events,Aborts,Terms,FailCleanup;
static unsigned StatusAt,FrameAt;
static ULONG StatusScript[16],Mail;
static UCHAR Pending,Frames[8][64],Rx[CYW_WIRE_CAPACITY],Tx[CYW_CONTROL_CAPACITY];
static UCHAR RxScript[8192];
static ULONG ScriptOn,ScriptAt,ScriptBytes,LastWriteLength;
static ULONG64 Clock;
static RPI5CYW_ADAPTER TestAdapter;
static CYW_NETWORK TestNetwork;
#define CHECK(x) do {if(!(x)){printf("FAIL line %d: %s\n",__LINE__,#x);++Failures;}}while(0)
ULONG64 KeQueryInterruptTime(void){return Clock;}
static NTSTATUS IoStatus(void){return ++IoCalls==FailIo?STATUS_IO_DEVICE_ERROR:STATUS_SUCCESS;}
static NTSTATUS SdioCmd52Read(PRPI5CYW_ADAPTER Adapter,UCHAR Fn,ULONG Address,PUCHAR Value)
{
    NTSTATUS status=IoStatus();(void)Adapter;CHECK(Fn==0 && Address==5);PendingCalls++;
    if(NT_SUCCESS(status))*Value=Pending;return status;
}
static NTSTATUS SdioCmd52Write(PRPI5CYW_ADAPTER Adapter,UCHAR Fn,ULONG Address,UCHAR Value,UCHAR Verify)
{
    (void)Adapter;CHECK(Verify==0);
    if(Fn==0) {CHECK(Address==6 && Value==2);Aborts++;}
    else {CHECK(Fn==1 && Address==0x1000d && Value==1);Terms++;}
    return FailCleanup?STATUS_IO_DEVICE_ERROR:STATUS_SUCCESS;
}
static NTSTATUS CywBpRead(PRPI5CYW_ADAPTER Adapter,ULONG Address,PULONG Value)
{
    NTSTATUS status=IoStatus();
    if(Address==Adapter->SdioCoreBase+0x20) {
        CHECK(StatusAt<16);StatusReads++;
        if(NT_SUCCESS(status))*Value=StatusScript[StatusAt++];
    } else {
        CHECK(Address==Adapter->SdioCoreBase+0x4c);MailReads++;
        if(NT_SUCCESS(status))*Value=Mail;
    }
    return status;
}
static NTSTATUS CywBpWrite(PRPI5CYW_ADAPTER Adapter,ULONG Address,ULONG Value)
{
    if(Address==Adapter->SdioCoreBase+0x20) {CHECK(Value && !(Value&~CYW_INT_MASK));AckWrites++;}
    else {CHECK(Address==Adapter->SdioCoreBase+0x40 && Value==2);MailAcks++;}
    return IoStatus();
}
static NTSTATUS SdioFifoTransfer(PRPI5CYW_ADAPTER Adapter,PUCHAR Buffer,ULONG Length,BOOLEAN Write)
{
    (void)Adapter;
    if(++FifoCalls==FailFifo)return STATUS_IO_DEVICE_ERROR;
    if(Write) {
        ULONG i,n=CywLe16(Buffer);
        CHECK(Length>=12 && !(Length&3));DataWrites++;LastWriteLength=Length;
        for(i=n;i<Length;++i)CHECK(Buffer[i]==0);
    }
    else if(ScriptOn) {
        CHECK(ScriptAt<=ScriptBytes && Length<=ScriptBytes-ScriptAt);
        if(ScriptAt>ScriptBytes || Length>ScriptBytes-ScriptAt)return STATUS_DEVICE_DATA_ERROR;
        memcpy(Buffer,RxScript+ScriptAt,Length);ScriptAt+=Length;
    }
    else if(Buffer==Rx) {
        CHECK(Length==64 && FrameAt<8);memcpy(Buffer,Frames[FrameAt++],64);
    } else {CHECK(Buffer==Rx+64);memset(Buffer,0,Length);}
    return STATUS_SUCCESS;
}
static VOID CywEvent(PRPI5CYW_ADAPTER Adapter,PUCHAR Buffer,ULONG Length)
{(void)Adapter;(void)Buffer;(void)Length;Events++;}
static VOID CywReceive(PRPI5CYW_ADAPTER Adapter,PUCHAR Buffer,ULONG Length)
{(void)Adapter;(void)Buffer;(void)Length;Delivered++;}
#include "../src/cyw43455/transport_service.h"
#include "../src/cyw43455/transport_poll.h"
#include "../src/cyw43455/transport_send.h"
#include "../src/cyw43455/transport_trace.h"
static void Init(void)
{
    memset(&TestAdapter,0,sizeof(TestAdapter));memset(&TestNetwork,0,sizeof(TestNetwork));memset(StatusScript,0,sizeof(StatusScript));
    memset(Frames,0,sizeof(Frames));memset(Rx,0,sizeof(Rx));memset(Tx,0,sizeof(Tx));
    TestAdapter.Network=&TestNetwork;TestAdapter.SdioCoreBase=0x18002000;TestNetwork.Rx=Rx;TestNetwork.Tx=Tx;TestNetwork.TxMax=32;
    IoCalls=FailIo=FifoCalls=FailFifo=PendingCalls=StatusReads=AckWrites=MailReads=MailAcks=0;
    DataWrites=Delivered=Events=Aborts=Terms=FailCleanup=StatusAt=FrameAt=0;
    Pending=0;Mail=0;Clock=100;
    ScriptOn=ScriptAt=ScriptBytes=LastWriteLength=0;memset(RxScript,0,sizeof(RxScript));
}
static void Frame(unsigned Index,unsigned Sequence,unsigned Channel,unsigned Flow)
{
    CHECK(Index<8);CywPut16(Frames[Index],64);CywPut16(Frames[Index]+2,(uint16_t)((~64u)&0xffffu));
    Frames[Index][4]=(UCHAR)Sequence;Frames[Index][5]=(UCHAR)Channel;Frames[Index][7]=12;
    Frames[Index][8]=(UCHAR)Flow;Frames[Index][9]=32;
}
static NTSTATUS Poll(void){ULONG channel,off,len;return CywPoll(&TestAdapter,&channel,&off,&len);}
#include "rx_config_tests.h"
#include "rx_transport_tests.h"
int main(void)
{
    unsigned i,flow;UCHAR payload[4]={0x20,0,0,0};NTSTATUS status;
    /* No interrupt and no cached RX: no speculative F1/F2 operation. */
    Init();CHECK(Poll()==STATUS_NO_MORE_ENTRIES);CHECK(PendingCalls==1 && !StatusReads && !FifoCalls);
    /* A quiet CCCR starts a grace period, not a speculative FIFO read. At
     * 100 ms, F1 status can independently prove actual pending frame work. */
    Init();CHECK(Poll()==STATUS_NO_MORE_ENTRIES);
    Clock+=CYW_TRANSPORT_FALLBACK_INTERVAL-1;
    CHECK(Poll()==STATUS_NO_MORE_ENTRIES && !StatusReads && !FifoCalls);
    Clock++;StatusScript[0]=CYW_INT_FRAME;Frame(0,0,2,0);
    CHECK(Poll()==STATUS_SUCCESS && FifoCalls==1 && Delivered==1);
    CHECK(TestAdapter.Transport.FallbackReads==1 && TestAdapter.Transport.FallbackFrames==1);
    CHECK(TestAdapter.Transport.LastPending==0 && TestAdapter.Transport.PendingEmpty==3);
    CHECK(TestAdapter.Transport.FrameNotifications==1);
    /* The same fallback with empty status never touches FIFO and remains
     * rate limited. It cannot silently convert FIFO data-available into FRAME. */
    Init();CHECK(Poll()==STATUS_NO_MORE_ENTRIES);Clock+=CYW_TRANSPORT_FALLBACK_INTERVAL;
    StatusScript[0]=0x00800000u;
    CHECK(Poll()==STATUS_NO_MORE_ENTRIES && !FifoCalls && StatusReads==1);
    CHECK(TestAdapter.Transport.StatusNoEvents==1 && !TestAdapter.Transport.FallbackFrames);
    CHECK(Poll()==STATUS_NO_MORE_ENTRIES && StatusReads==1);
    Clock+=CYW_TRANSPORT_FALLBACK_INTERVAL;
    CHECK(Poll()==STATUS_NO_MORE_ENTRIES && StatusReads==2 && !FifoCalls);
    /* A successful TX-side F1 read postpones fallback; active traffic already
     * provides fresh status and does not need another forced transaction. */
    Init();CHECK(Poll()==STATUS_NO_MORE_ENTRIES);Clock+=900000;
    CHECK(CywSendFrame(&TestAdapter,2,payload,4)==STATUS_SUCCESS && StatusReads==1);
    Clock+=100000;CHECK(Poll()==STATUS_NO_MORE_ENTRIES && StatusReads==1);
    Clock+=900000;CHECK(Poll()==STATUS_NO_MORE_ENTRIES && StatusReads==2);
    CHECK(TestAdapter.Transport.FallbackReads==1);
    /* A debounce reread is also a successful freshness observation. */
    Init();StatusScript[0]=CYW_INT_FC_CHANGE;StatusScript[1]=0;
    CHECK(CywTransportService(&TestAdapter,FALSE)==STATUS_SUCCESS && StatusReads==2);
    Clock+=CYW_TRANSPORT_FALLBACK_INTERVAL-1;
    CHECK(Poll()==STATUS_NO_MORE_ENTRIES && StatusReads==2 && !TestAdapter.Transport.FallbackReads);
    /* Mailbox-only fallback performs real mailbox service, never F2. */
    Init();CHECK(Poll()==STATUS_NO_MORE_ENTRIES);Clock+=CYW_TRANSPORT_FALLBACK_INTERVAL;
    StatusScript[0]=CYW_INT_MAIL;Mail=CYW_MAIL_READY|(4u<<16);
    CHECK(Poll()==STATUS_NO_MORE_ENTRIES && MailAcks==1 && !FifoCalls);
    CHECK(TestAdapter.Transport.FallbackMailbox==1 && !TestNetwork.RxPending);
    /* NAK-handled is itself documented frame work, unlike CCCR or raw FIFO
     * availability. Firmware halt and F1 failures retain fail-closed behavior. */
    Init();CHECK(Poll()==STATUS_NO_MORE_ENTRIES);Clock+=CYW_TRANSPORT_FALLBACK_INTERVAL;
    StatusScript[0]=CYW_INT_MAIL;Mail=CYW_MAIL_NAK_HANDLED;Frame(0,0,2,0);
    CHECK(Poll()==STATUS_SUCCESS && Delivered==1 && TestAdapter.Transport.FallbackMailbox==1);
    Init();CHECK(Poll()==STATUS_NO_MORE_ENTRIES);Clock+=CYW_TRANSPORT_FALLBACK_INTERVAL;
    StatusScript[0]=CYW_INT_MAIL;Mail=CYW_MAIL_HALT;
    CHECK(Poll()==STATUS_DEVICE_NOT_READY && !FifoCalls && TestAdapter.Transport.Halted);
    Init();CHECK(Poll()==STATUS_NO_MORE_ENTRIES);Clock+=CYW_TRANSPORT_FALLBACK_INTERVAL;FailIo=3;
    CHECK(Poll()==STATUS_IO_DEVICE_ERROR && !FifoCalls && TestAdapter.Transport.ServiceErrors==1);
    CHECK(TestAdapter.Transport.GlobalFlow && TestAdapter.Transport.FallbackReads==1);
    /* A backwards test clock does not underflow into repeated forced reads. */
    Init();CHECK(Poll()==STATUS_NO_MORE_ENTRIES);Clock=1;
    CHECK(Poll()==STATUS_NO_MORE_ENTRIES && !StatusReads);
    Clock+=CYW_TRANSPORT_FALLBACK_INTERVAL;
    CHECK(Poll()==STATUS_NO_MORE_ENTRIES && StatusReads==1);
    /* A bounded RX batch services pending/status once, yet accepts all frames
     * and wraparound. This is not a new or larger worker packet budget. */
    Init();Pending=2;StatusScript[0]=CYW_INT_FRAME;TestNetwork.RxBatch=TRUE;
    for(i=0;i<4;i++)Frame(i,(254+i)&255u,2,0);
    for(i=0;i<4;i++)CHECK(Poll()==STATUS_SUCCESS);
    CHECK(PendingCalls==1 && StatusReads==1 && AckWrites==1 && Delivered==4);
    CHECK(TestAdapter.Transport.SequenceExpected==2 && !TestAdapter.Transport.SequenceMismatches);
    TestNetwork.RxBatchServiced=FALSE;CHECK(Poll()==STATUS_NO_MORE_ENTRIES);
    CHECK(StatusReads==2 && !TestNetwork.RxPending && !TestNetwork.RxBatchServiced && TestAdapter.Transport.EmptyReads==1);
    Pending=0;CHECK(Poll()==STATUS_NO_MORE_ENTRIES);CHECK(PendingCalls==2 && StatusReads==2);
    /* Control calls are outside batching: pending RX cannot suppress fresh
     * mailbox/status reads between replies. */
    Init();TestNetwork.RxPending=TRUE;
    for(i=0;i<3;i++){Frame(i,i,0,0);CHECK(Poll()==STATUS_SUCCESS);}
    CHECK(!PendingCalls && StatusReads==3 && !Delivered);
    /* A new batch, even with RxPending=true, services an arrived mailbox. */
    Init();TestNetwork.RxPending=TRUE;TestNetwork.RxBatch=TRUE;Frame(0,1,2,0);Frame(1,2,1,0);
    CHECK(Poll()==STATUS_SUCCESS);TestNetwork.RxBatchServiced=FALSE;
    StatusScript[1]=CYW_INT_MAIL;Mail=CYW_MAIL_READY|(4u<<16);
    CHECK(Poll()==STATUS_SUCCESS);CHECK(MailReads==1 && MailAcks==1 && Events==1);
    CHECK(TestAdapter.Transport.MailboxVersion==4);
    /* A mailbox-only CCCR indication does not cause an unadvertised FIFO
     * read. Its metadata is still acknowledged and visible to diagnostics. */
    Init();Pending=2;StatusScript[0]=CYW_INT_MAIL;Mail=CYW_MAIL_READY|(4u<<16);
    CHECK(Poll()==STATUS_NO_MORE_ENTRIES && !FifoCalls && MailAcks==1 && !TestNetwork.RxPending);
    /* FC race: a CHANGE persists after acknowledgement, so data must stop.
     * Mail/frame bits observed on the second read are not thrown away. */
    Init();StatusScript[0]=CYW_INT_FC_STATE|CYW_INT_FC_CHANGE;
    StatusScript[1]=CYW_INT_FC_CHANGE|CYW_INT_FRAME|CYW_INT_MAIL;
    Mail=CYW_MAIL_FLOW|(0x80u<<24);
    CHECK(CywTransportService(&TestAdapter,FALSE)==STATUS_SUCCESS);
    CHECK(TestAdapter.Transport.GlobalFlow && TestAdapter.Transport.FcChanges==1 && TestAdapter.Transport.FcRaces==1);
    CHECK(TestNetwork.RxPending && TestNetwork.TxFlow==0x80 && TestAdapter.Transport.PriorityBlocked && MailAcks==1);
    CHECK(StatusReads==2 && AckWrites==2);
    /* Debounce sees state clear after CHANGE: no invented persistent stop. */
    Init();StatusScript[0]=CYW_INT_FC_STATE|CYW_INT_FC_CHANGE;StatusScript[1]=0;
    CHECK(CywTransportService(&TestAdapter,FALSE)==STATUS_SUCCESS && !TestAdapter.Transport.GlobalFlow);
    /* Fresh F1 before each data frame prevents cached/previously-clear state
     * from admitting a send after the dongle asserts global backpressure. */
    Init();StatusScript[0]=0;StatusScript[1]=CYW_INT_FC_STATE;StatusScript[2]=0;
    CHECK(CywSendFrame(&TestAdapter,2,payload,4)==STATUS_SUCCESS);
    CHECK(CywSendFrame(&TestAdapter,2,payload,4)==STATUS_DEVICE_BUSY);
    CHECK(CywSendFrame(&TestAdapter,2,payload,4)==STATUS_SUCCESS);
    CHECK(DataWrites==2 && TestNetwork.TxSeq==2 && TestAdapter.Transport.TxStatusChecks==3 && StatusReads==3);
    /* Header priority update and mailbox priority update both reach the gate.
     * Production mapping is UNKNOWN: no unverified precedence-bit shortcut. */
    for(flow=0;flow<256;flow++) {
        Init();TestNetwork.TxFlow=(UCHAR)flow;
        CHECK(CywSendFrame(&TestAdapter,2,payload,4)==(flow?STATUS_DEVICE_BUSY:STATUS_SUCCESS));
        CHECK(DataWrites==(flow?0u:1u));
        TestAdapter.Transport.PriorityMaskKnown=1;TestAdapter.Transport.PriorityMask=4;
        CHECK(CywTransportPriorityAllowed(&TestAdapter.Transport,(UCHAR)flow)==((flow&4u)==0));
    }
    TestAdapter.Transport.PriorityMask=0;CHECK(!CywTransportPriorityAllowed(&TestAdapter.Transport,1));
    TestAdapter.Transport.PriorityMask=3;CHECK(!CywTransportPriorityAllowed(&TestAdapter.Transport,4));
    TestAdapter.Transport.PriorityMask=256;CHECK(!CywTransportPriorityAllowed(&TestAdapter.Transport,1));
    Init();TestNetwork.RxPending=TRUE;Frame(0,5,2,0x40);CHECK(Poll()==STATUS_SUCCESS);
    CHECK(TestAdapter.Transport.PriorityFlow==0x40 && TestAdapter.Transport.PriorityBlocked);
    /* Mailbox FC-only notification can release a stopped priority. */
    StatusScript[1]=CYW_INT_MAIL;Mail=CYW_MAIL_FLOW;
    CHECK(CywTransportService(&TestAdapter,FALSE)==STATUS_SUCCESS && !TestNetwork.TxFlow && !TestAdapter.Transport.PriorityBlocked);
    /* Firmware halt is not acknowledged then forgotten, and no F2 transfer
     * follows it. Future calls fail without trying to restart the firmware. */
    Init();StatusScript[0]=CYW_INT_MAIL;Mail=CYW_MAIL_HALT;
    CHECK(CywSendFrame(&TestAdapter,2,payload,4)==STATUS_DEVICE_NOT_READY);
    CHECK(TestAdapter.Transport.Halted && TestAdapter.Transport.FirmwareHalts==1 && MailAcks==1 && !FifoCalls);
    CHECK(CywSendFrame(&TestAdapter,0,payload,4)==STATUS_DEVICE_NOT_READY);
    /* Unknown mailbox bits are counted, not fabricated into a known event. */
    Init();StatusScript[0]=CYW_INT_MAIL;Mail=0x80;
    CHECK(CywTransportService(&TestAdapter,FALSE)==STATUS_SUCCESS && TestAdapter.Transport.MailUnknown==1);
    /* Every status/debounce/mailbox I/O failure propagates before transmission. */
    for(i=1;i<=6;i++) {
        Init();StatusScript[0]=CYW_INT_FC_CHANGE|CYW_INT_MAIL;
        StatusScript[1]=CYW_INT_MAIL;FailIo=i;
        status=CywSendFrame(&TestAdapter,2,payload,4);
        CHECK(status==STATUS_IO_DEVICE_ERROR && !DataWrites && !TestNetwork.TxSeq && TestAdapter.Transport.ServiceErrors==1);
    }
    Init();FailIo=1;CHECK(Poll()==STATUS_IO_DEVICE_ERROR && !FifoCalls);
    /* Malformed/read-failed frames terminate READ (bit1 would be a bug).
     * The original failure is retained; cleanup errors never cause retries. */
    Init();TestNetwork.RxPending=TRUE;Frames[0][0]=1;
    CHECK(Poll()==STATUS_DEVICE_DATA_ERROR && Aborts==1 && Terms==1 && !Delivered && !TestNetwork.RxPending);
    Init();TestNetwork.RxPending=TRUE;FailFifo=1;FailCleanup=1;
    CHECK(Poll()==STATUS_IO_DEVICE_ERROR && Aborts==1 && Terms==1 && TestAdapter.Transport.RxAbortFailures==2);
    Init();TestNetwork.RxPending=TRUE;Frame(0,0,3,0);
    CHECK(Poll()==STATUS_NOT_SUPPORTED && Aborts==1 && Terms==1 && !Delivered);
    Init();TestNetwork.RxPending=TRUE;Frame(0,0,2,0);CywPut16(Frames[0],100);CywPut16(Frames[0]+2,(uint16_t)((~100u)&0xffffu));
    FailFifo=2;CHECK(Poll()==STATUS_IO_DEVICE_ERROR && !Delivered && !TestAdapter.Transport.Frames);
    /* Sequence evidence is passive: count/resync, never silently discard. */
    Init();TestNetwork.RxPending=TRUE;Frame(0,10,2,0);Frame(1,13,2,0);Frame(2,13,2,0);Frame(3,14,2,0);
    for(i=0;i<4;i++)CHECK(Poll()==STATUS_SUCCESS);
    CHECK(TestAdapter.Transport.SequenceMismatches==2 && TestAdapter.Transport.SequenceDuplicates==1 && Delivered==4);
    /* Observed block durations include open intervals, use monotonic ticks,
     * and saturate instead of wrapping diagnostic totals. */
    Init();CywTransportSetGlobal(&TestAdapter.Transport,1,0);CywTransportSetPriority(&TestAdapter.Transport,4,10);
    CHECK(CywTransportBlockedTicks(&TestAdapter.Transport,1,50)==50);
    CHECK(CywTransportBlockedTicks(&TestAdapter.Transport,0,50)==40);
    CywTransportSetGlobal(&TestAdapter.Transport,0,100);CywTransportSetPriority(&TestAdapter.Transport,0,110);
    CHECK(TestAdapter.Transport.GlobalBlocked100ns==100 && TestAdapter.Transport.PriorityBlocked100ns==100);
    TestAdapter.Transport.GlobalBlocked100ns=~0ULL-2;CywTransportSetGlobal(&TestAdapter.Transport,1,200);
    CywTransportSetGlobal(&TestAdapter.Transport,0,205);CHECK(TestAdapter.Transport.GlobalBlocked100ns==~0ULL);
    Init();TestNetwork.Stop=1;CHECK(Poll()==STATUS_CANCELLED && !IoCalls && !FifoCalls);
    /* The actual production sampler is passive and captures at most once per
     * second. It records observed counters, not an invented stall diagnosis. */
    Init();TestNetwork.Ready=TestNetwork.Authorized=TRUE;TestNetwork.RxPending=TRUE;
    TestAdapter.NetworkPhase=600;TestAdapter.TxPackets=0x100000007ULL;TestAdapter.RxPackets=9;
    TestAdapter.TxQueueFrames=3;TestAdapter.TxQueueFull=4;TestAdapter.TxCreditWaits=5;
    TestAdapter.Transport.LastPending=2;TestAdapter.Transport.LastInterrupt=CYW_INT_FRAME;
    TestAdapter.Transport.Frames=11;TestAdapter.PacketProbe.EchoLate=2;TestAdapter.PacketProbe.EchoMaxMs=1234;
    CywTransportSample(&TestAdapter);
    CHECK(TestAdapter.Transport.Trace.Version==1 && TestAdapter.Transport.Trace.EntryBytes==128);
    CHECK(TestAdapter.Transport.Trace.Capacity==128 && TestAdapter.Transport.Trace.Count==1);
    CHECK(TestAdapter.Transport.Trace.Next==1 && TestAdapter.Transport.Trace.Origin100ns==Clock);
    CHECK(TestAdapter.Transport.Trace.Entry[0].Flags==(CYW_TRACE_RX_PENDING|CYW_TRACE_READY|CYW_TRACE_AUTHORIZED));
    CHECK(TestAdapter.Transport.Trace.Entry[0].NetworkPhase==600 && TestAdapter.Transport.Trace.Entry[0].TxPacketsLow==7);
    CHECK(TestAdapter.Transport.Trace.Entry[0].RxPacketsLow==9 && TestAdapter.Transport.Trace.Entry[0].RxFrames==11);
    CHECK(TestAdapter.Transport.Trace.Entry[0].LastPending==2 && TestAdapter.Transport.Trace.Entry[0].LastInterrupt==CYW_INT_FRAME);
    CHECK(TestAdapter.Transport.Trace.Entry[0].QueueDepth==3 && TestAdapter.Transport.Trace.Entry[0].QueueFull==4);
    CHECK(TestAdapter.Transport.Trace.Entry[0].CreditWaits==5 && TestAdapter.Transport.Trace.Entry[0].TxMaximum==32);
    CHECK(TestAdapter.Transport.Trace.Entry[0].EchoLate==2 && TestAdapter.Transport.Trace.Entry[0].EchoMaxMs==1234);
    Clock+=CYW_TRANSPORT_TRACE_INTERVAL-1;CywTransportSample(&TestAdapter);
    CHECK(TestAdapter.Transport.Trace.Count==1);
    Clock++;TestAdapter.Transport.PendingEmpty=17;TestAdapter.Transport.FallbackFrames=2;
    CywTransportSample(&TestAdapter);CHECK(TestAdapter.Transport.Trace.Count==2);
    CHECK(TestAdapter.Transport.Trace.Entry[1].PendingEmpty==17 && TestAdapter.Transport.Trace.Entry[1].FallbackFrames==2);
    CHECK(!IoCalls && !FifoCalls);
    /* Ring retention and counter rollover are explicit in the binary ABI.
     * Every slot is overwritten in place without growing the allocation. */
    Init();
    for(i=0;i<130;i++) {
        TestAdapter.Transport.Frames=i;Clock=100+(ULONG64)i*CYW_TRANSPORT_TRACE_INTERVAL;
        CywTransportSample(&TestAdapter);
    }
    CHECK(TestAdapter.Transport.Trace.Count==128 && TestAdapter.Transport.Trace.Next==2);
    CHECK(TestAdapter.Transport.Trace.Serial==130 && TestAdapter.Transport.Trace.Entry[2].Serial==3);
    CHECK(TestAdapter.Transport.Trace.Entry[1].Serial==130 && TestAdapter.Transport.Trace.Entry[1].RxFrames==129);
    Clock=1;TestAdapter.Transport.Frames=0xffffffffu;CywTransportSample(&TestAdapter);
    CHECK(TestAdapter.Transport.Trace.Entry[2].Time100ns==1 && TestAdapter.Transport.Trace.Entry[2].RxFrames==0xffffffffu);
    Clock+=CYW_TRANSPORT_TRACE_INTERVAL;TestAdapter.Transport.Frames=0;CywTransportSample(&TestAdapter);
    CHECK(TestAdapter.Transport.Trace.Entry[3].RxFrames==0 && !IoCalls && !FifoCalls);
    TestPerformanceRx();
    TestRxConfig();
    printf("%s: production SDPCM service, FIFO poll, and TX gate tests\n",Failures?"FAIL":"PASS");
    return Failures?1:0;
}
