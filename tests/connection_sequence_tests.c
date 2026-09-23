/* Compile the actual connection sequence with deterministic firmware replies.
 * No Windows driver is loaded; all commands terminate in these stubs. */
#include <stdio.h>
#define RPI5CYW_HOST_TEST 1
#include "../src/driver/driver.h"
#include "../src/cyw43455/network_protocol.h"
#define STATUS_UNSUCCESSFUL ((NTSTATUS)0xc0000001L)
#define STATUS_CANCELLED ((NTSTATUS)0xc0000120L)
#define STATUS_NO_MORE_ENTRIES ((NTSTATUS)0x8000001aL)
typedef unsigned long long ULONGLONG;
#define RtlCopyMemory memcpy
#define RtlSecureZeroMemory(p,n) memset(p,0,n)
#define TRY(x) do {Status=(x);if(!NT_SUCCESS(Status))goto Exit;}while(0)
struct _CYW_NETWORK { BOOLEAN Associated,Authorized,SelectingBand,Powered;int Stop,Paused;unsigned Request; };
static unsigned Failures,Calls,FailStep,WrongCountry,ShortReply,RadioUp,Joined;
static unsigned AutoCalls,SetCalls,RejectExplicit,RejectAuto,ClmBad,ClmUnsupported,CountryReads;
static unsigned ListMode,NegativeRevision;
static unsigned PreferenceCalls,PreferenceFault;
static unsigned BandMode,BandPolls,BandReads,BandJoins,Published,Disassociated;
static ULONGLONG Clock;
static ULONG ExplicitError;
static NTSTATUS ExplicitStatus;
static UCHAR Country[12];
#define CHECK(x) do {if(!(x)){printf("FAIL line %d: %s\n",__LINE__,#x);++Failures;}}while(0)
static VOID CywLink(PRPI5CYW_ADAPTER A,BOOLEAN Up)
{if(Up){CHECK(!A->Network->SelectingBand && A->Network->Associated && A->Network->Authorized);Published++;}}
static BOOLEAN CywBandRequestPending(PRPI5CYW_ADAPTER A){return A->Network->Request!=0;}
ULONGLONG KeQueryInterruptTime(void){Clock+=1000000ULL;return Clock;}
static void SdioDelayMilliseconds(ULONG n){Clock+=(ULONGLONG)n*10000ULL;}
static NTSTATUS CywPoll(PRPI5CYW_ADAPTER A,PULONG channel,PULONG off,PULONG len)
{
    (void)channel;(void)off;(void)len;++BandPolls;
    if(BandMode==8)return STATUS_IO_DEVICE_ERROR;
    if(BandMode==9)A->Network->Stop=1;
    if(BandMode==10)A->Network->Paused=1;
    if(BandMode==11)A->IoStopped=1;
    if(BandMode==12)A->Network->Powered=FALSE;
    if(BandMode==18)A->Network->Request=2;
    return STATUS_NO_MORE_ENTRIES;
}
static NTSTATUS Call(PRPI5CYW_ADAPTER A,ULONG Command)
{
    ++Calls;A->FirmwareCommand=Command;A->FirmwareError=0;
    if(A->ConnectStep==FailStep){A->FirmwareError=0xfffffffe;return STATUS_UNSUCCESSFUL;}
    return 0;
}
static NTSTATUS CywCmdInt(PRPI5CYW_ADAPTER A,ULONG Command,ULONG Value)
{
    NTSTATUS s=Call(A,Command);(void)Value;if(!NT_SUCCESS(s))return s;
    if(Command==2){CHECK(A->CountryApplied==0x4442 && A->CountryRevision<0x80000000UL);RadioUp=1;}
    return 0;
}
static NTSTATUS CywIovar(PRPI5CYW_ADAPTER A,const char *Name,BOOLEAN Set,PUCHAR Data,ULONG Length)
{
    NTSTATUS s=Call(A,Set?263:262);if(!NT_SUCCESS(s))return s;
    if(strcmp(Name,"clmload_status")==0) {
        CHECK(!Set && Length==4);
        if(ClmUnsupported){A->FirmwareError=0xffffffe9;return STATUS_UNSUCCESSFUL;}
        CywPut32(Data,ClmBad);A->FirmwareReplyLength=ShortReply==15?2:4;
    } else if(strcmp(Name,"country")==0) {
        if(Set){
            ++SetCalls;CHECK(Data[0]=='B' && Data[1]=='D' && !Data[2] && !Data[3]);
            CHECK(Length==12 && Data[8]=='B' && Data[9]=='D' && !Data[10] && !Data[11]);
            if(CywLe32(Data+4)==0xffffffffUL){
                ++AutoCalls;CHECK(RejectExplicit && ExplicitError==0xfffffffe);
                if(RejectAuto){A->FirmwareError=0xfffffffe;return STATUS_UNSUCCESSFUL;}
                CHECK(CywCountryRequest(Data,Country));CywPut32(Country+4,NegativeRevision?0xffffffffUL:7);
            } else {
                CHECK(Length==12 && CywLe32(Data+4)==0);
                if(RejectExplicit){A->FirmwareError=ExplicitError;return ExplicitStatus;}
                memcpy(Country,Data,12);
            }
        } else {
            ++CountryReads;CHECK(Length==12);memcpy(Data,Country,12);
            if(WrongCountry && CountryReads>1)Data[8]='U';
            A->FirmwareReplyLength=ShortReply==A->ConnectStep?10:12;
        }
    } else if(strcmp(Name,"join_pref")==0) {
        const UCHAR expected[8]={3,2,0,1,1,2,0,0},rssi[4]={1,2,0,0};
        CHECK(Set && ((Length==8 && !memcmp(Data,expected,8)) || (Length==4 && !memcmp(Data,rssi,4))));
        CHECK(RadioUp && A->CountryApplied==0x4442 && A->Network->SelectingBand);
        CHECK(A->ConnectStep==(PreferenceCalls?20u:18u));
        PreferenceCalls++;
        if(BandMode==13 && PreferenceCalls==2){A->FirmwareError=0xfffffffe;return STATUS_UNSUCCESSFUL;}
        if(PreferenceFault==1){A->FirmwareError=0xffffffe9;return STATUS_UNSUCCESSFUL;}
        if(PreferenceFault==2){A->FirmwareError=0xfffffffe;return STATUS_UNSUCCESSFUL;}
        if(PreferenceFault==3)return STATUS_IO_TIMEOUT;
        if(PreferenceFault==4)return STATUS_IO_DEVICE_ERROR;
    } else CHECK(Set && strcmp(Name,"wpaie")==0 && Length==22);
    return 0;
}
static NTSTATUS CywInt(PRPI5CYW_ADAPTER A,const char *Name,ULONG Value)
{(void)Name;(void)Value;return Call(A,263);}
static NTSTATUS CywFirmwareCommand(PRPI5CYW_ADAPTER A,ULONG Command,BOOLEAN Set,PUCHAR Data,ULONG Length)
{
    NTSTATUS s=Call(A,Command);if(!NT_SUCCESS(s))return s;
    if(Command==23 || Command==29 || Command==127) {
        const UCHAR mac[6]={2,0x10,0x20,0x30,0x40,0x50};
        unsigned channel=BandMode==1 || (BandJoins==2 && BandMode!=16)?6u:36u;
        CHECK(!Set && A->Network->SelectingBand);++BandReads;
        memset(Data,0,Length);A->FirmwareReplyLength=Length;
        if(Command==23){CHECK(Length==6);memcpy(Data,mac,6);if(BandMode==6 && BandJoins==1 && BandReads==4)Data[5]++;}
        else if(Command==29){CHECK(Length==12);CywPut32(Data,channel);CywPut32(Data+4,channel);}
        else {CHECK(Length==12);CywPut32(Data,(ULONG)(BandMode==2 && BandJoins==1?-80:-55));}
        if(BandMode==5 && BandJoins==1)A->FirmwareReplyLength=3;
        if(BandMode==7 && BandJoins==1)A->Network->Authorized=FALSE;
        if(BandMode==16 && BandJoins==2)A->FirmwareReplyLength=3;
        return 0;
    }
    if(Command==262) {
        const UCHAR mac[6]={2,0x10,0x20,0x30,0x40,0x50};
        CHECK(!Set && Length==521 && !memcmp(Data,"sta_info",9) && !memcmp(Data+9,mac,6));
        memset(Data,0,Length);CywPut16(Data,4);CywPut16(Data+2,200);memcpy(Data+16,mac,6);
        CywPut32(Data+8,(BandMode==19 && BandJoins==1)?0x10u:0x30u);
        A->FirmwareReplyLength=200;return 0;
    }
    if(Command==261) {
        CHECK(!Set && Length==1024 && CywLe32(Data)==1024 && CywLe32(Data+4)==0 && CywLe32(Data+8)==0 && CywLe32(Data+12)==0);
        if(ListMode==1){A->FirmwareError=0xffffffe9;return STATUS_UNSUCCESSFUL;}
        if(ListMode==4)return STATUS_IO_TIMEOUT;
        memset(Data,0,Length);CywPut32(Data,1024);CywPut32(Data+12,2);
        memcpy(Data+16,"US\0\0BD\0\0",8);
        if(ListMode==2)Data[20]='U';
        if(ListMode==3)CywPut32(Data+12,0xffffffff);
        if(ListMode==5)CywPut32(Data+12,0);
        A->FirmwareReplyLength=24;return 0;
    }
    CHECK(Set);
    if(Command==268){CHECK(Length==132 && CywLe16(Data)==32 && CywLe16(Data+2)==0);}
    else if(Command==52) {
        CHECK(Length==12 && CywLe32(Data)==3 && A->Network->SelectingBand);
        Disassociated++;if(BandMode==14)return STATUS_IO_DEVICE_ERROR;
        if(BandMode==17){A->FirmwareError=0xffffffef;return STATUS_UNSUCCESSFUL;}
        A->Network->Associated=A->Network->Authorized=FALSE;
    } else {
        CHECK(Command==26 && RadioUp && Length==36 && CywLe32(Data)==4);Joined=1;++BandJoins;
        if(BandMode==15 && BandJoins==2)return STATUS_IO_DEVICE_ERROR;
        A->Network->Associated=A->Network->Authorized=
            !(BandMode==4 || (BandJoins==1 && (BandMode==3 || (BandMode>=8 && BandMode!=19))));
    }
    return 0;
}
#include "../src/cyw43455/connection.h"
static void Init(PRPI5CYW_ADAPTER A,struct _CYW_NETWORK *N)
{
    memset(A,0,sizeof(*A));memset(N,0,sizeof(*N));A->Network=N;
    N->Powered=TRUE;BandMode=BandPolls=BandReads=BandJoins=Published=Disassociated=0;Clock=0;
    Calls=FailStep=WrongCountry=ShortReply=RadioUp=Joined=0;memset(Country,0,sizeof(Country));
    AutoCalls=SetCalls=RejectExplicit=RejectAuto=ClmBad=ClmUnsupported=CountryReads=0;
    ListMode=NegativeRevision=0;
    PreferenceCalls=PreferenceFault=0;
    ExplicitError=0xfffffffe;
    ExplicitStatus=STATUS_UNSUCCESSFUL;
}
int main(void)
{
    RPI5CYW_ADAPTER a;struct _CYW_NETWORK n;CYW_CONNECT_REQUEST r={0};unsigned i;
    r.Version=1;r.Country[0]='B';r.Country[1]='D';r.SsidLength=4;memcpy(r.Ssid,"test",4);
    Init(&a,&n);CHECK(CywConnect(&a,&r)==0);
    CHECK(Calls==28 && Joined && a.NetworkPhase==600 && a.CountryRequested==0x4442 && a.CountrySetMode==2);
    CHECK(PreferenceCalls==2 && a.JoinPreferenceAccepted==1 && !a.JoinPreferenceStatus && !a.JoinPreferenceError);
    CHECK(Published==1 && a.BandSelection[1]==2 && a.BandSelection[2]==27 && a.BandSelection[9]==36);
    CHECK(a.CountryListStatus==0 && a.CountryListCount==2 && a.CountryListMembership==1);
    for(i=1;i<=16;++i) {
        if(i==2 || i==13)continue; /* Country BADARG and first join have explicit fallback. */
        Init(&a,&n);FailStep=i;
        if(i==16)RejectExplicit=1;
        CHECK(CywConnect(&a,&r)==STATUS_UNSUCCESSFUL);
        CHECK(a.ConnectStep==i && a.NetworkPhase==510 && !Joined);
        CHECK(a.FirmwareError==0xfffffffe);if(i!=13)CHECK(!RadioUp);
    }
    Init(&a,&n);WrongCountry=1;CHECK(CywConnect(&a,&r)==STATUS_DEVICE_DATA_ERROR);
    CHECK(a.ConnectStep==3 && !RadioUp && !Joined);
    for(i=0;i<3;++i){
        Init(&a,&n);ShortReply=i==0?3:(i==1?14:15);
        CHECK(CywConnect(&a,&r)==STATUS_DEVICE_DATA_ERROR);CHECK(!RadioUp && !Joined);
    }
    Init(&a,&n);CHECK(CywCountryRequest(r.Country,Country));CywPut32(Country+4,7);
    CHECK(CywConnect(&a,&r)==0 && Joined && !SetCalls && a.CountrySetMode==1 && a.CountryRevision==7);
    Init(&a,&n);RejectExplicit=1;
    CHECK(CywConnect(&a,&r)==0 && Joined && AutoCalls==1 && SetCalls==2);
    CHECK(a.CountrySetMode==4 && a.CountryExplicitError==0xfffffffe && a.CountryRevision==7);
    Init(&a,&n);RejectExplicit=RejectAuto=1;
    CHECK(CywConnect(&a,&r)==STATUS_UNSUCCESSFUL && !RadioUp && !Joined && AutoCalls==1);
    Init(&a,&n);RejectExplicit=WrongCountry=1;
    CHECK(CywConnect(&a,&r)==STATUS_DEVICE_DATA_ERROR && !RadioUp && !Joined);
    Init(&a,&n);RejectExplicit=NegativeRevision=1;
    CHECK(CywConnect(&a,&r)==STATUS_DEVICE_DATA_ERROR && !RadioUp && !Joined);
    for(i=1;i<=5;++i){
        Init(&a,&n);ListMode=i;
        CHECK(CywConnect(&a,&r)==0 && Joined);
        CHECK(a.CountryListMembership==(i==2?2u:0u));
        if(i==1)CHECK(a.CountryListStatus==STATUS_UNSUCCESSFUL && a.CountryListError==0xffffffe9);
        if(i==3)CHECK(a.CountryListStatus==STATUS_DEVICE_DATA_ERROR);
        if(i==4)CHECK(a.CountryListStatus==STATUS_IO_TIMEOUT && !a.CountryListReplyLength);
        if(i==5)CHECK(a.CountryListStatus==0 && !a.CountryListCount);
    }
    Init(&a,&n);RejectExplicit=1;ExplicitError=0xfffffff9;
    CHECK(CywConnect(&a,&r)==STATUS_UNSUCCESSFUL && !AutoCalls && !RadioUp);
    Init(&a,&n);RejectExplicit=1;ExplicitStatus=STATUS_IO_TIMEOUT;
    CHECK(CywConnect(&a,&r)==STATUS_IO_TIMEOUT && !AutoCalls && !RadioUp);
    Init(&a,&n);ClmBad=1;
    CHECK(CywConnect(&a,&r)==STATUS_DEVICE_DATA_ERROR && !SetCalls && !RadioUp && a.ClmLoadStatus==1);
    Init(&a,&n);ClmUnsupported=1;
    CHECK(CywConnect(&a,&r)==0 && Joined && a.ClmLoadStatus==0xffffffff && a.ClmQueryStatus==STATUS_UNSUCCESSFUL);
    Init(&a,&n);PreferenceFault=1;
    CHECK(CywConnect(&a,&r)==0 && Joined && !a.JoinPreferenceAccepted && PreferenceCalls==1);
    CHECK(a.JoinPreferenceStatus==STATUS_UNSUCCESSFUL && a.JoinPreferenceError==0xffffffe9);
    for(i=2;i<=4;i++) {
        NTSTATUS expected=i==2?STATUS_UNSUCCESSFUL:(i==3?STATUS_IO_TIMEOUT:STATUS_IO_DEVICE_ERROR);
        Init(&a,&n);PreferenceFault=i;
        CHECK(CywConnect(&a,&r)==expected && !Joined && !a.JoinPreferenceAccepted);
        CHECK(a.ConnectStep==18 && a.JoinPreferenceStatus==expected && PreferenceCalls==1);
    }
    Init(&a,&n);CHECK(CywConnect(&a,&r)==0 && a.JoinPreferenceAccepted);
    FailStep=1;CHECK(CywConnect(&a,&r)==STATUS_UNSUCCESSFUL);
    CHECK(!a.JoinPreferenceAccepted && a.JoinPreferenceStatus==0x103 && !a.JoinPreferenceError);
    /* Actual bounded selector: no invented 5 GHz success, exactly one safe
     * fallback, and no publication on timeout/cancellation/transport errors. */
    Init(&a,&n);BandMode=1;CHECK(CywConnect(&a,&r)==0 && a.BandSelection[1]==3 && a.BandSelection[9]==6 && Published==1);
    for(i=2;i<=7;i++) {
        Init(&a,&n);BandMode=i;
        if(i==4) {
            CHECK(CywConnect(&a,&r)==STATUS_IO_TIMEOUT && !Published && n.SelectingBand);
            CHECK(a.BandSelection[1]==6 && a.BandSelection[13]==2);
        } else {
            CHECK(CywConnect(&a,&r)==0 && Published==1 && !n.SelectingBand);
            CHECK(a.BandSelection[1]==5 && a.BandSelection[9]==6 && a.BandSelection[13]==2);
        }
        CHECK(BandJoins==2 && Disassociated==1 && PreferenceCalls==2);
    }
    for(i=8;i<=12;i++) {
        Init(&a,&n);BandMode=i;
        CHECK(CywConnect(&a,&r)==(i==8?STATUS_IO_DEVICE_ERROR:STATUS_CANCELLED));
        CHECK(!Published && n.SelectingBand && BandJoins==1 && !Disassociated && BandPolls==1);
    }
    for(i=13;i<=16;i++) {
        Init(&a,&n);BandMode=i;
        CHECK(!NT_SUCCESS(CywConnect(&a,&r)) && !Published && n.SelectingBand);
        CHECK(a.BandSelection[1]==6 && BandJoins<=2);
    }
    Init(&a,&n);FailStep=13;CHECK(CywConnect(&a,&r)==0 && Published==1 && a.BandSelection[15]==4 && a.BandSelection[13]==2);
    Init(&a,&n);BandMode=17;CHECK(CywConnect(&a,&r)==0 && Published==1 && BandJoins==2 && Disassociated==1);
    Init(&a,&n);BandMode=18;CHECK(CywConnect(&a,&r)==STATUS_CANCELLED && !Published && BandJoins==1 && !Disassociated);
    Init(&a,&n);BandMode=19;CHECK(CywConnect(&a,&r)==0 && Published==1 && BandJoins==2 && a.BandSelection[15]==3);
    if(Failures)return 1;
    puts("PASS: actual connection sequence, existing country reuse, same-country revision fallback, CLM checks, fail-closed readback");
    return 0;
}
