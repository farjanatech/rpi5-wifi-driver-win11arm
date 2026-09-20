/* Real control.h + connection.h with simulated transport only. No driver load. */
#include <stdio.h>
#include <stdlib.h>
#define RPI5CYW_HOST_TEST 1
#include "../src/driver/driver.h"
#include "../src/cyw43455/network_protocol.h"
typedef unsigned long long ULONGLONG;
typedef struct _CYW_NETWORK {
    USHORT RequestId;UCHAR TxSeq,TxMax;int Stop;
    PUCHAR Rx;BOOLEAN Associated,Authorized;
} CYW_NETWORK;
#define STATUS_UNSUCCESSFUL ((NTSTATUS)0xc0000001L)
#define STATUS_CANCELLED ((NTSTATUS)0xc0000120L)
#define STATUS_NO_MORE_ENTRIES ((NTSTATUS)0x8000001aL)
#define STATUS_INSUFFICIENT_RESOURCES ((NTSTATUS)0xc000009aL)
#define POOL_FLAG_NON_PAGED 0
#define RtlCopyMemory memcpy
#define RtlSecureZeroMemory(p,n) memset(p,0,n)
#define TRY(x) do {Status=(x);if(!NT_SUCCESS(Status))goto Exit;}while(0)
static unsigned Failures,Outstanding,AllocCalls,FailAlloc,Sent,Polls,Mode,Fault,RadioUp,Joined;
static ULONG PayloadLength,DeclaredLength,ReplyError,RequestCommand,RequestFlags,RequestCapacity;
static ULONG ActualOffset,ActualLength,ClmValue;
static ULONGLONG Clock;
static UCHAR Rx[CYW_WIRE_CAPACITY],Value[64],Country[12];
#define CHECK(x) do {if(!(x)){printf("FAIL line %d: %s\n",__LINE__,#x);++Failures;}}while(0)
static void *TestAlloc(int flags,size_t n,ULONG tag)
{void *p;(void)flags;(void)tag;if(++AllocCalls==FailAlloc)return NULL;p=calloc(1,n);if(p)++Outstanding;return p;}
static void TestFree(void *p,ULONG tag){(void)tag;if(p){--Outstanding;free(p);}}
#define ExAllocatePool2 TestAlloc
#define ExFreePoolWithTag TestFree
static ULONGLONG KeQueryInterruptTime(void){Clock+=1000000;return Clock;}
static void SdioDelayMilliseconds(ULONG n){(void)n;}
static VOID CywLink(PRPI5CYW_ADAPTER A,BOOLEAN up){(void)A;CHECK(!up);}
static NTSTATUS CywSendFrame(PRPI5CYW_ADAPTER A,UCHAR channel,PUCHAR data,ULONG length)
{
    ULONG payload=PayloadLength,declared=DeclaredLength,error=ReplyError;
    PUCHAR p=Rx+12;
    (void)A;++Sent;Polls=0;CHECK(channel==0 && length>=16);
    RequestCommand=CywLe32(data);RequestFlags=CywLe32(data+8);RequestCapacity=CywLe32(data+4);
    CHECK(length==16+RequestCapacity);CHECK(CywLe32(data+12)==0);
    if(Fault==5)return STATUS_IO_DEVICE_ERROR;
    if(Mode) {
        payload=0;declared=RequestCapacity;error=0;
        if(RequestCommand==261){
            CHECK(!(RequestFlags&2) && RequestCapacity==1024 && CywLe32(data+16)==1024);
            CHECK(CywLe32(data+20)==0 && CywLe32(data+24)==0 && CywLe32(data+28)==0);
            memset(Value,0,sizeof(Value));CywPut32(Value,1024);CywPut32(Value+12,2);
            memcpy(Value+16,"US\0\0BD\0\0",8);payload=24;
            if(Fault==9)error=0xffffffe9;
            if(Fault==10)payload=23;
        } else if(RequestCommand==262){
            /* Firmware can return the whole IOVAR buffer, not just the value. */
            payload=RequestCapacity;memset(Value,0,sizeof(Value));
            if(strcmp((char*)data+16,"clmload_status")==0){
                CywPut32(Value,ClmValue);if(Fault==7)payload=3;
            } else {CHECK(strcmp((char*)data+16,"country")==0);memcpy(Value,Country,12);}
        } else if(RequestCommand==263 && strcmp((char*)data+16,"country")==0){
            CHECK(RequestCapacity==20);
            CHECK(data[24]=='B' && data[25]=='D');
            CHECK(data[32]=='B' && data[33]=='D' && !data[26] && !data[27] && !data[34] && !data[35]);
            CHECK(CywLe32(data+28)==0 || CywLe32(data+28)==0xffffffff);
            if(CywLe32(data+28)==0 && Fault==8)error=0xfffffffe;
            else {CHECK(CywCountryRequest(data+24,Country));if(CywLe32(data+28)==0xffffffff)CywPut32(Country+4,7);}
        } else if(RequestCommand==2){RadioUp=1;CHECK(A->CountryApplied==0x4442);}
        else if(RequestCommand==26){Joined=1;CHECK(RadioUp);}
    }
    CHECK(payload<=sizeof(Value));memset(Rx,0,sizeof(Rx));
    CywPut32(p,RequestCommand);CywPut32(p+4,declared);
    CywPut32(p+8,RequestFlags|(error?1:0));CywPut32(p+12,error);
    memcpy(p+16,Value,payload);ActualOffset=12;ActualLength=28+payload;
    if(Fault==1)ActualLength=27;
    if(Fault==2)ActualOffset=ActualLength+1;
    if(Fault==3)CywPut32(p+8,RequestFlags^0x10000);
    if(Fault==4)CywPut32(p,RequestCommand+1);
    if(Fault==6)ActualLength=CYW_WIRE_CAPACITY+1;
    return STATUS_SUCCESS;
}
static NTSTATUS CywPoll(PRPI5CYW_ADAPTER A,PULONG channel,PULONG off,PULONG len)
{
    (void)A;if(Polls++)return STATUS_NO_MORE_ENTRIES;
    *channel=0;*off=ActualOffset;*len=ActualLength;return STATUS_SUCCESS;
}
#include "../src/cyw43455/control.h"
static NTSTATUS CywInt(PRPI5CYW_ADAPTER A,const char *name,ULONG value)
{UCHAR b[4];CywPut32(b,value);return CywIovar(A,name,TRUE,b,4);}
static NTSTATUS CywCmdInt(PRPI5CYW_ADAPTER A,ULONG command,ULONG value)
{UCHAR b[4];CywPut32(b,value);return CywFirmwareCommand(A,command,TRUE,b,4);}
#include "../src/cyw43455/connection.h"
static void Init(PRPI5CYW_ADAPTER A,CYW_NETWORK *N)
{
    CHECK(!Outstanding);memset(A,0,sizeof(*A));memset(N,0,sizeof(*N));A->Network=N;N->Rx=Rx;N->TxMax=1;
    AllocCalls=FailAlloc=Sent=Polls=Mode=Fault=RadioUp=Joined=0;Clock=0;
    PayloadLength=DeclaredLength=4;ReplyError=ClmValue=0;memset(Value,0,sizeof(Value));memset(Country,0,12);
}
int main(void)
{
    RPI5CYW_ADAPTER a;CYW_NETWORK n;UCHAR output[14];unsigned i,j;
    const ULONG metadata[]={0,4,19,0x00130013};
    const ULONG available[]={4,19,20};
    CYW_CONNECT_REQUEST request={0};
    /* A buffer length in the header neither truncates nor fabricates data. */
    for(i=0;i<4;++i)for(j=0;j<3;++j){
        Init(&a,&n);DeclaredLength=metadata[i];PayloadLength=available[j];CywPut32(Value,0x12345678);
        memset(output,0xa5,sizeof(output));
        CHECK(CywIovar(&a,"clmload_status",FALSE,output+1,4)==0);
        CHECK(CywLe32(output+1)==0x12345678 && output[0]==0xa5 && output[5]==0xa5);
        CHECK(a.FirmwareReplyLength==4 && a.FirmwareReplyPayloadLength==available[j]);
        CHECK(a.FirmwareReplyDeclaredLength==metadata[i] && a.FirmwareRequestCapacity==19 && a.FirmwareValueLength==4);
    }
    for(i=0;i<4;++i){
        Init(&a,&n);PayloadLength=i;DeclaredLength=19;memset(output,0xa5,sizeof(output));
        CHECK(CywIovar(&a,"clmload_status",FALSE,output+1,4)==STATUS_DEVICE_DATA_ERROR);
        CHECK(output[1]==0xa5 && output[5]==0xa5 && a.FirmwareReplyLength==i);
    }
    for(i=0;i<12;++i){
        Init(&a,&n);PayloadLength=i;DeclaredLength=19;
        CHECK(CywIovar(&a,"country",FALSE,output+1,12)==STATUS_DEVICE_DATA_ERROR);
    }
    for(i=12;i<=20;++i){
        Init(&a,&n);PayloadLength=i;DeclaredLength=19;CHECK(CywCountryRequest((const UCHAR*)"BD",Value));
        memset(output,0xa5,sizeof(output));CHECK(CywIovar(&a,"country",FALSE,output+1,12)==0);
        CHECK(CywCountryMatches((const UCHAR*)"BD",output+1,a.FirmwareReplyLength));
        CHECK(output[0]==0xa5 && output[13]==0xa5);
    }
    for(i=1;i<=6;++i){
        NTSTATUS expected=i==3 || i==4?STATUS_IO_TIMEOUT:(i==5?STATUS_IO_DEVICE_ERROR:STATUS_DEVICE_DATA_ERROR);
        Init(&a,&n);Fault=i;CHECK(CywIovar(&a,"clmload_status",FALSE,output,4)==expected);
    }
    Init(&a,&n);ReplyError=0xfffffffe;
    CHECK(CywIovar(&a,"clmload_status",FALSE,output,4)==STATUS_UNSUCCESSFUL && a.FirmwareError==ReplyError);
    for(i=1;i<=2;++i){Init(&a,&n);FailAlloc=i;CHECK(CywIovar(&a,"clmload_status",FALSE,output,4)==STATUS_INSUFFICIENT_RESOURCES);}
    Init(&a,&n);CHECK(CywIovar(&a,"country",FALSE,NULL,12)==STATUS_INVALID_PARAMETER && !Sent);
    CHECK(CywIovar(&a,NULL,FALSE,output,4)==STATUS_INVALID_PARAMETER && !Sent);
    /* Integration: actual transport, actual IOVAR and actual connection checks. */
    request.Version=1;request.Country[0]='B';request.Country[1]='D';request.SsidLength=4;memcpy(request.Ssid,"test",4);
    Init(&a,&n);Mode=1;CHECK(CywConnect(&a,&request)==0 && Joined && RadioUp && a.ClmLoadStatus==0);
    CHECK(a.CountryListMembership==1 && a.CountryListCount==2 && a.CountryListReplyLength==24);
    Init(&a,&n);Mode=1;Fault=8;CHECK(CywConnect(&a,&request)==0 && Joined && a.CountrySetMode==4 && a.CountryRevision==7);
    Init(&a,&n);Mode=1;Fault=9;CHECK(CywConnect(&a,&request)==0 && Joined && !a.CountryListMembership && a.CountryListError==0xffffffe9);
    Init(&a,&n);Mode=1;Fault=10;CHECK(CywConnect(&a,&request)==0 && Joined && !a.CountryListMembership && a.CountryListStatus==STATUS_DEVICE_DATA_ERROR);
    Init(&a,&n);Mode=1;ClmValue=1;
    CHECK(CywConnect(&a,&request)==STATUS_DEVICE_DATA_ERROR && !RadioUp && !Joined && a.ClmLoadStatus==1);
    Init(&a,&n);Mode=1;Fault=7;
    CHECK(CywConnect(&a,&request)==STATUS_DEVICE_DATA_ERROR && !RadioUp && !Joined && a.ConnectStep==15);
    CHECK(!Outstanding);if(Failures)return 1;
    puts("PASS: actual control/IOVAR/connection integration, padded replies, truncation, matching, bounds, errors and allocation cleanup");
    return 0;
}
