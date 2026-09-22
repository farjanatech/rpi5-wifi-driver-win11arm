/* Reuse the real BCDC/IOVAR transport harness, not a substitute radio parser. */
#define main CywPreviousControlMain
#include "control_reply_tests.c"
#undef main
typedef long LONG;
#define STATUS_DEVICE_NOT_READY ((NTSTATUS)0xc00000a3L)
static unsigned ExtCalls,ExtTarget,ExtFault;
static ULONGLONG ExtendedCommandDelay;
static const UCHAR TestBssid[6]={0x02,0x10,0x20,0x30,0x40,0x50};
static void StationReply(UCHAR *data,unsigned version,unsigned length)
{
    unsigned i;
    memset(data,0,521);CywPut16(data,(USHORT)version);CywPut16(data+2,(USHORT)length);
    CywPut32(data+8,0xc032u);memcpy(data+16,TestBssid,6);CywPut32(data+24,16);
    CywPut32(data+52,123);CywPut32(data+56,7);CywPut32(data+60,234);CywPut32(data+64,8);
    CywPut32(data+68,72000);CywPut32(data+72,65000);CywPut32(data+80,9);
    CywPut32(data+160,10);CywPut32(data+164,11);
    for(i=0;i<7;++i)CywPut32(data+172+4*i,100+i);
}
/* The original four GETs still use actual control.h framing. New GET reply
 * transport is mocked here while the real request construction, radio parser,
 * error policy and snapshot formatting execute unchanged below. */
static NTSTATUS RadioCommand(PRPI5CYW_ADAPTER a,ULONG cmd,BOOLEAN set,PUCHAR data,ULONG length)
{
    unsigned which;
    if(cmd!=12 && cmd!=23 && cmd!=137 && !(cmd==262 && length==521))
        return CywFirmwareCommand(a,cmd,set,data,length);
    which=cmd==12?0u:(cmd==23?1u:(cmd==137?3u:4u));++Sent;++ExtCalls;
    Clock+=ExtendedCommandDelay;
    CHECK(!set);a->FirmwareError=0;a->FirmwareReplyLength=length;
    if(cmd==262) {
        CHECK(length==521 && !memcmp(data,"sta_info",9) && !memcmp(data+9,TestBssid,6));
        StationReply(data,4,200);a->FirmwareReplyLength=200;
    } else {
        memset(data,0,length);
        if(cmd==12){CHECK(length==4);CywPut32(data,144);}
        if(cmd==23){CHECK(length==6);memcpy(data,TestBssid,6);}
        if(cmd==137){unsigned i;CHECK(length==20);for(i=0;i<5;++i)CywPut32(data+4*i,1000+i);}
    }
    if(ExtFault && ExtTarget==which) {
        if(ExtFault==1){a->FirmwareError=0xffffffe9UL;return STATUS_UNSUCCESSFUL;}
        if(ExtFault==2){a->FirmwareReplyLength=3;return STATUS_SUCCESS;}
        if(ExtFault==3)return STATUS_IO_TIMEOUT;
        if(ExtFault==4)memset(data,0,length);
    }
    return STATUS_SUCCESS;
}
static NTSTATUS RadioIovar(PRPI5CYW_ADAPTER a,const char *name,BOOLEAN set,PUCHAR data,ULONG length)
{
    if(strcmp(name,"chanspec"))return CywIovar(a,name,set,data,length);
    ++Sent;++ExtCalls;CHECK(!set && length==4);CywPut32(data,0xd024);
    Clock+=ExtendedCommandDelay;
    a->FirmwareError=0;a->FirmwareReplyLength=4;
    if(ExtFault && ExtTarget==2) {
        if(ExtFault==1){a->FirmwareError=0xffffffe9UL;return STATUS_UNSUCCESSFUL;}
        if(ExtFault==2)a->FirmwareReplyLength=3;
        if(ExtFault==3)return STATUS_IO_TIMEOUT;
        if(ExtFault==4)CywPut32(data,0x10000);
    }
    return STATUS_SUCCESS;
}
#define CywFirmwareCommand RadioCommand
#define CywIovar RadioIovar
#include "../src/cyw43455/radio.h"
#undef CywFirmwareCommand
#undef CywIovar
int main(void)
{
    RPI5CYW_ADAPTER a;CYW_NETWORK n;ULONG report[CYW_RADIO_REPORT_WORDS];unsigned i,j;
    UCHAR station[521];const unsigned versions[4]={3,4,5,7},sizes[4]={84,200,252,296};
    CHECK(CywPreviousControlMain()==0);
    for(i=0;i<=9;++i) {
        Init(&a,&n);Mode=2;RadioCase=i;
        a.FirmwareCommand=26;a.FirmwareError=123;a.FirmwareReplyLength=36;
        a.FirmwareReplyDeclaredLength=37;a.FirmwareReplyPayloadLength=38;
        a.FirmwareRequestCapacity=39;a.FirmwareValueLength=40;
        a.NetworkPhase=600;a.NetworkStatus=0;
        CywReadRadio(&a,report);
        CHECK(report[0]==2 && Sent==9 && !Outstanding);
        CHECK(report[20]==511u && !report[21] && report[32]==144 && report[35]==0xd024);
        CHECK(report[36]==1000 && report[40]==1004 && report[41]==4 && report[42]==200);
        CHECK(report[44]==72000 && report[45]==65000 && report[47]==7 && report[54]==101 && report[58]==105);
        CHECK(a.FirmwareCommand==26 && a.FirmwareError==123 && a.FirmwareReplyLength==36);
        CHECK(a.FirmwareReplyDeclaredLength==37 && a.FirmwareReplyPayloadLength==38);
        CHECK(a.FirmwareRequestCapacity==39 && a.FirmwareValueLength==40);
        CHECK(a.NetworkPhase==600 && !a.NetworkStatus);
        if(i<=1 || i==8) {
            CHECK(report[2]==15 && !report[3] && (LONG)report[12]==-55);
            CHECK(report[11]==(i==1?2400u:5000u) && report[14]==(i==8?1u:0u));
        } else {
            unsigned failed=i<=3?0:(i<=5?1:(i==6 || i==9?2:3));
            CHECK(report[2]==(15u & ~(1u<<failed)) && report[3] && report[4+failed]);
        }
        if(i==9)CHECK(report[17]==0xffffffe9UL && report[6]==(ULONG)STATUS_UNSUCCESSFUL);
    }
    Init(&a,&n);Mode=2;Fault=3;CywReadRadio(&a,report);
    CHECK(Sent==1 && report[2]==0 && report[3]==(ULONG)STATUS_IO_TIMEOUT);
    CHECK(report[5]==(ULONG)STATUS_DEVICE_NOT_READY);
    Init(&a,&n);Mode=2;FailAlloc=1;CywReadRadio(&a,report);
    CHECK(!Sent && !report[2] && report[3]==(ULONG)STATUS_INSUFFICIENT_RESOURCES && !Outstanding);
    for(i=0;i<5;++i)for(j=1;j<=3;++j) {
        Init(&a,&n);Mode=2;ExtCalls=0;ExtTarget=i;ExtFault=j;CywReadRadio(&a,report);
        CHECK(report[2]==15 && !report[3] && report[22+i] && report[21]);
        CHECK(!(report[20]&(1u<<i)));
        if(j==3)CHECK(ExtCalls==i+1); /* no continued GETs after transport fault */
        if(j==1)CHECK(report[27+i]==0xffffffe9UL);
        if(i==1)CHECK(report[26]==(ULONG)STATUS_DEVICE_NOT_READY); /* no identity, no station GET */
    }
    for(i=0;i<3;++i) {
        Init(&a,&n);Mode=2;ExtTarget=i;ExtFault=4;CywReadRadio(&a,report);
        CHECK(!(report[20]&(1u<<i)) && report[22+i]==(ULONG)STATUS_DEVICE_DATA_ERROR);
    }
    ExtFault=0;
    Init(&a,&n);Mode=2;ExtCalls=0;ExtendedCommandDelay=40000000ULL;
    a.FirmwareCommand=26;a.FirmwareError=123;a.NetworkPhase=600;a.NetworkStatus=0;
    CywReadRadio(&a,report);ExtendedCommandDelay=0;
    CHECK(Sent==8 && ExtCalls==4 && report[2]==15 && !report[3] && report[20]==15);
    CHECK(report[26]==(ULONG)STATUS_IO_TIMEOUT && report[21]==(ULONG)STATUS_IO_TIMEOUT);
    CHECK(a.FirmwareCommand==26 && a.FirmwareError==123 && a.NetworkPhase==600 && !a.NetworkStatus);
    /* Production station parser: exact known versions, full bounds and MAC
     * matching; truncated/unknown packets must never leak valid zero stats. */
    for(i=0;i<4;++i) {
        StationReply(station,versions[i],sizes[i]);memset(report,0,sizeof(report));
        CHECK(CywRadioParseStation(station,sizes[i],TestBssid,report));
        CHECK((report[20]&CYW_RADIO_STA_STATS_VALID) && report[41]==versions[i]);
        CHECK(!!(report[20]&CYW_RADIO_STA_RETRIES_VALID)==(i!=0));
        for(j=0;j<sizes[i];++j) {
            memset(report,0,sizeof(report));CHECK(!CywRadioParseStation(station,j,TestBssid,report) && !report[20]);
        }
        CywPut16(station+2,(USHORT)(sizes[i]-1));memset(report,0,sizeof(report));
        CHECK(!CywRadioParseStation(station,sizes[i],TestBssid,report) && !report[20]);
    }
    for(i=0;i<=9;++i)if(i!=3 && i!=4 && i!=5 && i!=7) {
        StationReply(station,i,296);memset(report,0,sizeof(report));
        CHECK(!CywRadioParseStation(station,296,TestBssid,report) && !report[20]);
    }
    StationReply(station,4,200);station[16]^=2;memset(report,0,sizeof(report));
    CHECK(!CywRadioParseStation(station,200,TestBssid,report) && !report[20]);
    StationReply(station,4,200);CywPut32(station+24,17);
    CHECK(!CywRadioParseStation(station,200,TestBssid,report));
    StationReply(station,4,200);CywPut32(station+8,2);memset(report,0,sizeof(report));
    CHECK(CywRadioParseStation(station,200,TestBssid,report) && report[20]==CYW_RADIO_STA_VALID);
    StationReply(station,4,200);CywPut32(station+68,0);CywPut32(station+72,0xffffffffUL);memset(report,0,sizeof(report));
    CHECK(CywRadioParseStation(station,200,TestBssid,report));
    CHECK(!(report[20]&(CYW_RADIO_STA_TXRATE_VALID|CYW_RADIO_STA_RXRATE_VALID)) && !report[44] && !report[45]);
    StationReply(station,4,200);CywPut32(station+52,0);CywPut32(station+60,0);CywPut32(station+64,0);memset(report,0,sizeof(report));
    CHECK(CywRadioParseStation(station,200,TestBssid,report));
    CHECK(!(report[20]&(CYW_RADIO_STA_TXRATE_VALID|CYW_RADIO_STA_RXRATE_VALID)));
    CHECK(!CywRadioParseStation(NULL,200,TestBssid,report));
    CHECK(!CywRadioParseStation(station,522,TestBssid,report));
    if(Failures)return 1;
    puts("PASS: radio v2 GETs, station layout/identity/length/rate/stat validity, unsupported/truncated/timeouts, legacy radio fields and diagnostic preservation");return 0;
}
