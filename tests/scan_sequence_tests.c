/* Execute the actual production scan sequence against bounded firmware mocks.
 * All device operations end here. This binary is run on GitHub CI only. */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "../src/cyw43455/network_protocol.h"
#include "../src/cyw43455/scan_protocol.h"
typedef unsigned char UCHAR,BOOLEAN,*PUCHAR;
typedef unsigned short USHORT;
typedef unsigned ULONG,*PULONG;
typedef unsigned long long ULONGLONG;
typedef int NTSTATUS,LONG,KIRQL;
typedef void VOID;
#define TRUE 1
#define FALSE 0
#define STATUS_SUCCESS ((NTSTATUS)0)
#define STATUS_INVALID_PARAMETER ((NTSTATUS)0xc000000du)
#define STATUS_DEVICE_DATA_ERROR ((NTSTATUS)0xc000009cu)
#define STATUS_IO_TIMEOUT ((NTSTATUS)0xc00000b5u)
#define STATUS_CANCELLED ((NTSTATUS)0xc0000120u)
#define STATUS_UNSUCCESSFUL ((NTSTATUS)0xc0000001u)
#define STATUS_IO_DEVICE_ERROR ((NTSTATUS)0xc0000185u)
#define STATUS_DEVICE_BUSY ((NTSTATUS)0x80000011u)
#define STATUS_NO_MORE_ENTRIES ((NTSTATUS)0x8000001au)
#define NT_SUCCESS(s) ((NTSTATUS)(s)>=0)
#define RtlCopyMemory memcpy
#define RtlZeroMemory(p,n) memset(p,0,n)
typedef struct _CYW_NETWORK {
    int Lock;volatile LONG Stop,Paused,ScanCancel;
    BOOLEAN Powered,Ready,Associated,Authorized,SelectingBand,ScanBusy,ScanComplete,ScanAcceptEvents;
    USHORT ScanSyncId;ULONG ScanEventStatus;
    UCHAR ScanCountry[2];CYW_SCAN_REPORT ScanReport;
} CYW_NETWORK;
typedef struct _ADAPTER {
    CYW_NETWORK *Network;BOOLEAN IoStopped;NTSTATUS NetworkStatus;ULONG NetworkPhase;
    ULONG FirmwareCommand,FirmwareError,FirmwareReplyLength,FirmwareReplyDeclaredLength;
    ULONG FirmwareReplyPayloadLength,FirmwareRequestCapacity,FirmwareValueLength;
} ADAPTER,*PRPI5CYW_ADAPTER;
static ADAPTER TestAdapter;static CYW_NETWORK TestNetwork;
static unsigned Failures,Mode,Calls,CountrySets,MaskSets,EscanCalls,AbortCalls,UpCalls,DownCalls;
static unsigned ActivePolls,RefreshCalls,FailCall,RestoreReads;
static ULONGLONG TestClock;
static UCHAR FirmwareCountry[12],FirmwareMask[16],SavedCountry[12],SavedMask[16];
static int RadioUp;
#define CHECK(x) do {if(!(x)){printf("FAIL line %d mode %u: %s\n",__LINE__,Mode,#x);++Failures;}} while(0)
static VOID KeAcquireSpinLock(int *lock,KIRQL *level){CHECK(!*lock);*lock=1;*level=0;}
static VOID KeReleaseSpinLock(int *lock,KIRQL level){(void)level;CHECK(*lock);*lock=0;}
static LONG InterlockedCompareExchange(volatile LONG *p,LONG exchange,LONG compare)
{LONG old=*p;if(old==compare)*p=exchange;return old;}
static ULONGLONG KeQueryInterruptTime(void){TestClock+=100000ULL;return TestClock;}
static VOID SdioDelayMilliseconds(ULONG ms){TestClock+=(ULONGLONG)ms*10000ULL;}
static VOID CywRefreshTxGate(PRPI5CYW_ADAPTER A){CHECK(!A->Network->Ready);++RefreshCalls;}
static VOID CywScanEvent(PRPI5CYW_ADAPTER A,ULONG status,PUCHAR data,ULONG length);
static NTSTATUS Call(PRPI5CYW_ADAPTER A,ULONG command)
{
    CHECK(!A->Network->Lock && A->Network->ScanBusy);++Calls;
    A->FirmwareCommand=command;A->FirmwareError=0;A->FirmwareReplyLength=0;
    A->FirmwareReplyDeclaredLength=123;A->FirmwareReplyPayloadLength=124;
    A->FirmwareRequestCapacity=125;A->FirmwareValueLength=126;
    if(Calls==FailCall)return STATUS_IO_DEVICE_ERROR;
    if(Mode==17 && Calls==2)TestClock+=310000000ULL;
    return STATUS_SUCCESS;
}
static NTSTATUS CywCmdInt(PRPI5CYW_ADAPTER A,ULONG command,ULONG value)
{
    NTSTATUS s=Call(A,command);CHECK(!value);if(!NT_SUCCESS(s))return s;
    CHECK(command==2 || command==3);
    if(command==2) {
        CHECK(FirmwareCountry[0]=='B' && FirmwareCountry[1]=='D');
        CHECK(FirmwareMask[8]&32u);RadioUp=1;++UpCalls;
    } else {RadioUp=0;++DownCalls;}
    return s;
}
static VOID Partial(PRPI5CYW_ADAPTER A,unsigned sync,unsigned malformed)
{
    UCHAR p[140]={0};UCHAR *b=p+12;
    CywScanPut32(p,sizeof(p));CywScanPut32(p+4,109);CywScanPut16(p+8,sync);CywScanPut16(p+10,1);
    CywScanPut32(b,109);CywScanPut32(b+4,128);b[8]=2;b[13]=3;
    CywScanPut16(b+16,1);b[18]=4;memcpy(b+19,"test",4);
    CywScanPut16(b+72,0xd024);CywScanPut16(b+78,65536u-55u);b[88]=36;
    CywScanPut16(b+116,128);if(malformed)CywScanPut32(b+120,0xffffffffu);
    CywScanEvent(A,8,p,sizeof(p));
}
static NTSTATUS CywIovar(PRPI5CYW_ADAPTER A,const char *name,BOOLEAN set,PUCHAR data,ULONG length)
{
    NTSTATUS s=Call(A,set?263u:262u);if(!NT_SUCCESS(s))return s;
    if(!strcmp(name,"clmload_status")) {
        CHECK(!set && length==4);
        if(Mode==7){A->FirmwareError=0xffffffe9u;return STATUS_UNSUCCESSFUL;}
        memset(data,0,length);A->FirmwareReplyLength=length;
    } else if(!strcmp(name,"country")) {
        CHECK(length==12 && !RadioUp);
        if(set) {
            ++CountrySets;
            if(data[0]=='B') {
                CHECK(data[1]=='D' && data[8]=='B' && data[9]=='D');
                if(Mode==8 && CywLe32(data+4)==0) {
                    A->FirmwareError=0xfffffffeu;return STATUS_UNSUCCESSFUL;
                }
                memcpy(FirmwareCountry,data,12);
                if(CywLe32(data+4)==0xffffffffu)CywPut32(FirmwareCountry+4,7);
            } else {CHECK(!memcmp(data,SavedCountry,12));memcpy(FirmwareCountry,data,12);}
        } else {
            memcpy(data,FirmwareCountry,12);A->FirmwareReplyLength=12;
            if(Mode==6 && FirmwareCountry[0]=='B')data[8]='U';
        }
    } else if(!strcmp(name,"event_msgs")) {
        CHECK(length==16);
        if(set) {
            ++MaskSets;
            if(MaskSets==1)CHECK((data[8]&32u) && !A->Network->ScanAcceptEvents);
            else {CHECK(!memcmp(data,SavedMask,16));}
            if(Mode==5 && MaskSets==2) {A->FirmwareError=99;return STATUS_UNSUCCESSFUL;}
            memcpy(FirmwareMask,data,16);
        } else {
            memcpy(data,FirmwareMask,16);A->FirmwareReplyLength=16;
            if(MaskSets>=2) {++RestoreReads;if(Mode==10)data[0]^=1;}
        }
    } else if(!strcmp(name,"escan")) {
        UCHAR expected[72];++EscanCalls;CHECK(set && length==72 && RadioUp);
        CywScanBuildRequest(expected,A->Network->ScanSyncId);CHECK(!memcmp(data,expected,72));
        CHECK(A->Network->ScanAcceptEvents && A->Network->ScanReport.State==CYW_SCAN_RUNNING);
        if(Mode==9) {A->FirmwareError=0xffffffe9u;return STATUS_UNSUCCESSFUL;}
        if(Mode==13) {Partial(A,A->Network->ScanSyncId,0);CywScanEvent(A,0,NULL,0);}
    } else CHECK(0);
    return STATUS_SUCCESS;
}
static NTSTATUS CywFirmwareCommand(PRPI5CYW_ADAPTER A,ULONG command,BOOLEAN set,PUCHAR data,ULONG length)
{
    UCHAR expected[68];NTSTATUS s=Call(A,command);
    CHECK(command==50 && set && length==68 && !A->Network->ScanAcceptEvents);
    CywScanBuildAbort(expected);CHECK(!memcmp(data,expected,68));++AbortCalls;return s;
}
static NTSTATUS CywPoll(PRPI5CYW_ADAPTER A,PULONG channel,PULONG off,PULONG length)
{
    (void)channel;(void)off;(void)length;CHECK(!A->Network->Lock);
    if(!A->Network->ScanAcceptEvents) {
        /* Even same-sync terminal/partial must be ignored outside active scan. */
        Partial(A,A->Network->ScanSyncId,0);CywScanEvent(A,0,NULL,0);
        if(Mode==18)return STATUS_SUCCESS;
        return STATUS_NO_MORE_ENTRIES;
    }
    ++ActivePolls;
    if(Mode==2)return STATUS_NO_MORE_ENTRIES;
    if(Mode==3) {A->Network->ScanCancel=1;return STATUS_NO_MORE_ENTRIES;}
    if(Mode==11) {A->Network->Paused=1;return STATUS_NO_MORE_ENTRIES;}
    if(Mode==12) {A->Network->Stop=1;return STATUS_NO_MORE_ENTRIES;}
    if(Mode==1) {
        Partial(A,(A->Network->ScanSyncId+1u)&65535u,0);
        CHECK(A->Network->ScanReport.Count==0 && !A->Network->ScanComplete);
    }
    if(Mode==14) {
        UCHAR p[12]={0};CywScanPut32(p,12);CywScanPut16(p+8,A->Network->ScanSyncId+1u);
        CywScanEvent(A,0,p,12);CHECK(!A->Network->ScanComplete);
    }
    if(Mode!=15)Partial(A,A->Network->ScanSyncId,Mode==16);
    CywScanEvent(A,Mode==4?1u:0u,NULL,0);
    return STATUS_SUCCESS;
}
#include "../src/cyw43455/scan_sequence.h"
static void Init(unsigned mode)
{
    Mode=mode;Calls=CountrySets=MaskSets=EscanCalls=AbortCalls=UpCalls=DownCalls=0;
    ActivePolls=RefreshCalls=FailCall=RestoreReads=0;TestClock=0;RadioUp=0;
    memset(&TestAdapter,0,sizeof(TestAdapter));memset(&TestNetwork,0,sizeof(TestNetwork));
    TestAdapter.Network=&TestNetwork;TestNetwork.Powered=TestNetwork.Ready=TestNetwork.ScanBusy=TRUE;
    TestAdapter.NetworkPhase=500;
    TestNetwork.ScanCountry[0]='B';TestNetwork.ScanCountry[1]='D';
    TestNetwork.ScanReport.Version=1;TestNetwork.ScanReport.Generation=7;
    TestNetwork.ScanReport.State=CYW_SCAN_QUEUED;
    TestAdapter.FirmwareCommand=1;TestAdapter.FirmwareError=2;TestAdapter.FirmwareReplyLength=3;
    TestAdapter.FirmwareReplyDeclaredLength=4;TestAdapter.FirmwareReplyPayloadLength=5;
    TestAdapter.FirmwareRequestCapacity=6;TestAdapter.FirmwareValueLength=7;
    CHECK(CywCountryRequest((const UCHAR *)"US",FirmwareCountry));memcpy(SavedCountry,FirmwareCountry,12);
    memset(FirmwareMask,0,16);FirmwareMask[0]=0x61;FirmwareMask[2]=1;FirmwareMask[5]=64;
    memcpy(SavedMask,FirmwareMask,16);
}
static void EndChecks(void)
{
    CHECK(!TestNetwork.ScanBusy && !TestNetwork.ScanAcceptEvents && !TestNetwork.Lock);
    CHECK(!TestNetwork.Associated && !TestNetwork.Authorized);
    CHECK(TestAdapter.FirmwareCommand==1 && TestAdapter.FirmwareError==2 && TestAdapter.FirmwareReplyLength==3);
    CHECK(TestAdapter.FirmwareReplyDeclaredLength==4 && TestAdapter.FirmwareReplyPayloadLength==5);
    CHECK(TestAdapter.FirmwareRequestCapacity==6 && TestAdapter.FirmwareValueLength==7);
    if(TestNetwork.ScanReport.State!=CYW_SCAN_COMPLETE)CHECK(!TestNetwork.ScanReport.Count);
}
int main(void)
{
    unsigned i;
    for(i=0;i<=18;++i) {
        Init(i);CywScanRequest(&TestAdapter);EndChecks();
        if(i==0 || i==1 || i==7 || i==8 || i==13 || i==14 || i==15) {
            CHECK(TestNetwork.ScanReport.State==CYW_SCAN_COMPLETE && TestNetwork.ScanReport.Status==0);
            CHECK(TestNetwork.ScanReport.Count==(i==15?0u:1u));
            CHECK(UpCalls==1 && DownCalls==2 && !RadioUp && RestoreReads==1 && !RefreshCalls);
            CHECK(!memcmp(FirmwareCountry,SavedCountry,12) && !memcmp(FirmwareMask,SavedMask,16));
        } else {
            CHECK(TestNetwork.ScanReport.State==((i==3 || i==11 || i==12)?CYW_SCAN_CANCELLED:CYW_SCAN_FAILED));
            CHECK(!TestNetwork.ScanReport.Count);
        }
        if(i==2)CHECK(AbortCalls==1 && (NTSTATUS)TestNetwork.ScanReport.Status==STATUS_IO_TIMEOUT);
        if(i==5 || i==10 || i==12 || i==18)CHECK(!TestNetwork.Ready && RefreshCalls==1);
        if(i==6)CHECK(!UpCalls && !EscanCalls);
        if(i==8)CHECK(CountrySets==3);
        if(i==13)CHECK(!ActivePolls); /* events during command reply */
        if(i==17)CHECK(!UpCalls && !EscanCalls);
    }
    for(i=1;i<=14;++i) {
        Init(0);FailCall=i;CywScanRequest(&TestAdapter);EndChecks();
        CHECK(TestNetwork.ScanReport.State!=CYW_SCAN_COMPLETE);
    }
    Init(0);TestNetwork.ScanCancel=1;CywScanRequest(&TestAdapter);EndChecks();
    CHECK(!Calls && TestNetwork.Ready && TestNetwork.ScanReport.State==CYW_SCAN_CANCELLED);
    Init(0);TestNetwork.Paused=1;CywScanRequest(&TestAdapter);EndChecks();CHECK(!Calls);
    Init(0);TestAdapter.IoStopped=1;CywScanRequest(&TestAdapter);EndChecks();CHECK(!Calls);
    Init(0);TestNetwork.Associated=TestNetwork.Authorized=TRUE;TestAdapter.NetworkPhase=600;
    CywScanRequest(&TestAdapter);
    CHECK(!Calls && TestNetwork.Associated && TestNetwork.Authorized && TestAdapter.NetworkPhase==600);
    CHECK(TestNetwork.ScanReport.State==CYW_SCAN_FAILED && (NTSTATUS)TestNetwork.ScanReport.Status==STATUS_DEVICE_BUSY);
    Init(0);TestNetwork.SelectingBand=TRUE;CywScanRequest(&TestAdapter);CHECK(!Calls && TestNetwork.SelectingBand);
    Init(0);TestNetwork.ScanReport.Generation=65536;CywScanRequest(&TestAdapter);CHECK(TestNetwork.ScanSyncId==1);
    Init(0);TestNetwork.ScanReport.Generation=65537;CywScanRequest(&TestAdapter);CHECK(TestNetwork.ScanSyncId==2);
    printf("scan sequence tests: %u failures\n",Failures);return Failures?1:0;
}
