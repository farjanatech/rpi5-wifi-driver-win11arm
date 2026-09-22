/* Compile the production RX poller; only hardware and NDIS delivery are mocked. */
#include <stdio.h>
#define RPI5CYW_HOST_TEST 1
#include "../src/driver/driver.h"
#include "../src/cyw43455/network_protocol.h"
typedef struct _CYW_NETWORK {
    int Stop;BOOLEAN RxPending;ULONG RxNextLength;
    UCHAR TxSeq,TxMax,TxFlow;PUCHAR Rx;
} CYW_NETWORK;
#define STATUS_CANCELLED ((NTSTATUS)0xc0000120L)
#define STATUS_NO_MORE_ENTRIES ((NTSTATUS)0x8000001aL)
#define STATUS_NOT_SUPPORTED ((NTSTATUS)0xc00000bbL)
#define UNREFERENCED_PARAMETER(x) (void)(x)
#define TRY(x) do {Status=(x);if(!NT_SUCCESS(Status))goto Exit;}while(0)
static unsigned Failures,Operations,FailOperation,FifoReads,Cmd53Reads,Aborts,Delivered,Events;
static ULONG Consumed,ReadSizes[8],Pending,Interrupt,DeliveredLength;
static UCHAR Wire[CYW_WIRE_CAPACITY],Rx[CYW_WIRE_CAPACITY];
static RPI5CYW_ADAPTER Adapter;static CYW_NETWORK Network;
#define CHECK(x) do {if(!(x)){printf("FAIL %d: %s\n",__LINE__,#x);++Failures;}}while(0)
static NTSTATUS Bus(void){return ++Operations==FailOperation?STATUS_IO_DEVICE_ERROR:STATUS_SUCCESS;}
static NTSTATUS SdioCmd52Read(PRPI5CYW_ADAPTER a,UCHAR fn,ULONG addr,PUCHAR p)
{CHECK(a==&Adapter && fn==0 && addr==5);*p=(UCHAR)Pending;return Bus();}
static NTSTATUS SdioCmd52Write(PRPI5CYW_ADAPTER a,UCHAR fn,ULONG addr,UCHAR v,BOOLEAN raw)
{CHECK(a==&Adapter && v==2 && !raw && ((fn==0 && addr==6)||(fn==1 && addr==0x1000d)));++Aborts;return STATUS_SUCCESS;}
static NTSTATUS CywBpRead(PRPI5CYW_ADAPTER a,ULONG addr,PULONG v)
{CHECK(a==&Adapter && (addr==0x1020 || addr==0x104c));*v=addr==0x1020?Interrupt:0;return Bus();}
static NTSTATUS CywBpWrite(PRPI5CYW_ADAPTER a,ULONG addr,ULONG v)
{CHECK(a==&Adapter && ((addr==0x1020 && v==(Interrupt&0x200000f0))||(addr==0x1040 && v==2)));return Bus();}
static NTSTATUS SdioFifoTransfer(PRPI5CYW_ADAPTER a,PUCHAR p,ULONG n,BOOLEAN write)
{
    NTSTATUS status=Bus();
    CHECK(a==&Adapter && !write && n && !(n&3) && Consumed+n<=sizeof(Wire));
    CHECK(p==Rx+Consumed && FifoReads<8);
    ReadSizes[FifoReads++]=n;Cmd53Reads+=(n+511)/512;
    if(NT_SUCCESS(status)){memcpy(p,Wire+Consumed,n);Consumed+=n;}
    return status;
}
static VOID CywReceive(PRPI5CYW_ADAPTER a,PUCHAR p,ULONG n)
{CHECK(a==&Adapter && p==Rx+12 && n==(ULONG)CywLe16(Wire)-12u);CHECK(memcmp(p,Wire+12,n)==0);++Delivered;DeliveredLength=n;}
static VOID CywEvent(PRPI5CYW_ADAPTER a,PUCHAR p,ULONG n)
{CHECK(a==&Adapter && p==Rx+12 && n==(ULONG)CywLe16(Wire)-12u);++Events;}
#include "../src/cyw43455/rx_poll.h"
static VOID Reset(ULONG length,UCHAR channel,UCHAR hint)
{
    memset(&Adapter,0,sizeof(Adapter));memset(&Network,0,sizeof(Network));
    memset(Wire,0xab,sizeof(Wire));memset(Rx,0xcd,sizeof(Rx));
    Adapter.Network=&Network;Adapter.SdioCoreBase=0x1000;Network.Rx=Rx;
    Network.TxSeq=250;Pending=4;Interrupt=0x80;
    Operations=FailOperation=FifoReads=Cmd53Reads=Aborts=Delivered=Events=0;
    Consumed=DeliveredLength=0;memset(ReadSizes,0,sizeof(ReadSizes));
    CywPut16(Wire,(USHORT)length);CywPut16(Wire+2,(USHORT)~length);
    Wire[4]=5;Wire[5]=channel;Wire[6]=hint;Wire[7]=12;Wire[8]=3;Wire[9]=2;
}
static NTSTATUS Poll(BOOLEAN ahead)
{
    ULONG channel=99,off=99,len=99;NTSTATUS status;
    status=ahead?CywPollFrame(&Adapter,&channel,&off,&len,TRUE):CywPoll(&Adapter,&channel,&off,&len);
    if(NT_SUCCESS(status)){CHECK(len==CywLe16(Wire) && off==12 && channel==(Wire[5]&15u));}
    else CHECK(channel==0 && off==0 && len==0);
    return status;
}
int main(void)
{
    ULONG n,normalCommands,first,op;unsigned ignored;
    /* Every non-glom read-ahead length, including exact command boundaries. */
    for(n=49;n<=2048;++n) {
        Reset(n,2,96);CHECK(Poll(TRUE)==STATUS_SUCCESS);
        normalCommands=Cmd53Reads;CHECK(ReadSizes[0]==64 && Delivered==1);
        Reset(n,2,96);Network.RxPending=TRUE;Network.RxNextLength=(n+15)&~15UL;
        CHECK(Poll(TRUE)==STATUS_SUCCESS && Delivered==1 && DeliveredLength==n-12);
        CHECK(FifoReads==1 && ReadSizes[0]==((n+15)&~15UL));
        CHECK(Adapter.RxReadAheadFrames==1 && Adapter.RxReadAheadAttempts==1);
        CHECK(Adapter.RxReadAheadSavedCommands==normalCommands-Cmd53Reads);
        CHECK(Network.RxNextLength==1536 && Network.TxMax==2 && Network.TxFlow==3);
    }
    /* Full Ethernet payload: four CMD53s become three, same delivery bytes. */
    Reset(1530,2,96);CHECK(Poll(TRUE)==0 && Cmd53Reads==4);
    Reset(1530,2,96);Network.RxPending=TRUE;Network.RxNextLength=1536;
    CHECK(Poll(TRUE)==0 && Cmd53Reads==3 && Adapter.RxReadAheadSavedCommands==1);
    /* Same four-frame batch as production: 16 commands -> 13; then reset. */
    Reset(1530,2,96);
    for(n=0;n<4;++n) {
        Consumed=0;FifoReads=0;
        CHECK(Poll(TRUE)==0 && Delivered==n+1);
    }
    CHECK(Cmd53Reads==13 && Adapter.RxReadAheadSavedCommands==3);
    Network.RxNextLength=0;Consumed=0;FifoReads=0;
    CHECK(Poll(TRUE)==0 && ReadSizes[0]==64 && Cmd53Reads==17);
    /* No/short/oversized hints always take the established header-first path. */
    for(n=0;n<256;++n) {
        Reset(1530,2,(UCHAR)n);CHECK(Poll(TRUE)==0);
        first=n*16;ignored=first && (first<64 || first>2048)?1u:0u;
        CHECK(Network.RxNextLength==(first>=64 && first<=2048?first:0));
        CHECK(Adapter.RxReadAheadHintIgnored==ignored);
    }
    for(n=0;n<5;++n) {
        static const ULONG invalid[]={16,63,65,2064,0xffffffffUL};
        Reset(1530,2,0);Network.RxPending=TRUE;Network.RxNextLength=invalid[n];
        CHECK(Poll(TRUE)==0 && ReadSizes[0]==64 && Adapter.RxReadAheadAttempts==0);
    }
    /* Pending control transaction suppresses both use and storage of hints. */
    Reset(1530,0,96);Network.RxPending=TRUE;Network.RxNextLength=1536;
    CHECK(Poll(FALSE)==0 && ReadSizes[0]==64 && Network.RxNextLength==0 && !Delivered);
    Reset(1530,2,96);Network.RxPending=TRUE;Network.RxNextLength=1536;
    CHECK(Poll(FALSE)==0 && Delivered==1 && Network.RxNextLength==0);
    /* Event and unsolicited control frames remain correctly classified. */
    Reset(100,1,0);Network.RxPending=TRUE;Network.RxNextLength=112;
    CHECK(Poll(TRUE)==0 && Events==1 && !Delivered);
    Reset(100,0,0);Network.RxPending=TRUE;Network.RxNextLength=112;
    CHECK(Poll(TRUE)==0 && !Events && !Delivered);
    /* Hint/length mismatch is never delivered or retried from a mid-frame. */
    for(n=0;n<2;++n) {
        Reset(n?1600:1400,2,96);Network.RxPending=TRUE;Network.RxNextLength=1536;
        CHECK(Poll(TRUE)==STATUS_DEVICE_DATA_ERROR && !Delivered && Aborts==2);
        CHECK(Adapter.RxReadAheadMismatch==1 && !Network.RxPending && !Network.RxNextLength);
    }
    /* Every I/O failure before and during normal/two-part and hinted reads. */
    for(n=0;n<2;++n)for(op=1;op<=(n?6u:7u);++op) {
        Reset(1530,2,96);if(n){Network.RxPending=TRUE;Network.RxNextLength=1536;}
        FailOperation=op;CHECK(Poll(TRUE)==STATUS_IO_DEVICE_ERROR);
        CHECK(!Delivered && Aborts==2 && !Network.RxPending && !Network.RxNextLength);
    }
    Reset(1530,2,96);Wire[2]^=1;CHECK(Poll(TRUE)==STATUS_DEVICE_DATA_ERROR && !Delivered && Aborts==2);
    Reset(1530,2,96);Wire[7]=11;CHECK(Poll(TRUE)==STATUS_DEVICE_DATA_ERROR && !Delivered);
    Reset(1530,3,96);CHECK(Poll(TRUE)==STATUS_NOT_SUPPORTED && !Network.RxNextLength && Aborts==2);
    Reset(1530,0x82,96);CHECK(Poll(TRUE)==STATUS_NOT_SUPPORTED && !Delivered);
    Reset(1530,2,96);Network.RxPending=TRUE;Network.RxNextLength=1536;memset(Wire,0,4);
    CHECK(Poll(TRUE)==STATUS_NO_MORE_ENTRIES && !Network.RxPending && !Network.RxNextLength && !Aborts);
    Reset(1530,2,96);Pending=0;Network.RxNextLength=1536;
    CHECK(Poll(TRUE)==STATUS_NO_MORE_ENTRIES && !FifoReads && !Network.RxNextLength);
    Reset(1530,2,96);Network.Stop=1;Network.RxNextLength=1536;
    CHECK(Poll(TRUE)==STATUS_CANCELLED && !Operations && !Network.RxNextLength);
    /* Maximum legacy header-first frame stays bounded; no read-ahead of it. */
    Reset(65535,0,255);CHECK(Poll(FALSE)==0 && Consumed==65536 && !Network.RxNextLength);
    printf("RX poll / read-ahead production-code tests: %s\n",Failures?"FAIL":"PASS");
    return Failures?1:0;
}
