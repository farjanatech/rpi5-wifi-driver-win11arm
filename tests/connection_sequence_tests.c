/* Compile the actual connection sequence with deterministic firmware replies.
 * No Windows driver is loaded; all commands terminate in these stubs. */
#include <stdio.h>
#define RPI5CYW_HOST_TEST 1
#include "../src/driver/driver.h"
#include "../src/cyw43455/network_protocol.h"
#define STATUS_UNSUCCESSFUL ((NTSTATUS)0xc0000001L)
#define RtlCopyMemory memcpy
#define RtlSecureZeroMemory(p,n) memset(p,0,n)
#define TRY(x) do {Status=(x);if(!NT_SUCCESS(Status))goto Exit;}while(0)
struct _CYW_NETWORK { BOOLEAN Associated,Authorized; };
static unsigned Failures,Calls,FailStep,WrongCountry,ShortReply,RadioUp,Joined;
static UCHAR Country[12];
#define CHECK(x) do {if(!(x)){printf("FAIL line %d: %s\n",__LINE__,#x);++Failures;}}while(0)
static VOID CywLink(PRPI5CYW_ADAPTER A,BOOLEAN Up) {(void)A;CHECK(!Up);}
static NTSTATUS Call(PRPI5CYW_ADAPTER A,ULONG Command)
{
    ++Calls;CHECK(A->ConnectStep==Calls);A->FirmwareCommand=Command;A->FirmwareError=0;
    if(Calls==FailStep){A->FirmwareError=0xfffffffe;return STATUS_UNSUCCESSFUL;}
    return 0;
}
static NTSTATUS CywCmdInt(PRPI5CYW_ADAPTER A,ULONG Command,ULONG Value)
{
    NTSTATUS s=Call(A,Command);(void)Value;if(!NT_SUCCESS(s))return s;
    if(Command==2){CHECK(A->CountryApplied==0x4442 && A->CountryRevision==0);RadioUp=1;}
    return 0;
}
static NTSTATUS CywIovar(PRPI5CYW_ADAPTER A,const char *Name,BOOLEAN Set,PUCHAR Data,ULONG Length)
{
    NTSTATUS s=Call(A,Set?263:262);if(!NT_SUCCESS(s))return s;
    if(strcmp(Name,"country")==0) {
        CHECK(Length==12);
        if(Set){CHECK(Data[0]=='B' && Data[1]=='D' && CywLe32(Data+4)==0);memcpy(Country,Data,12);}
        else {memcpy(Data,Country,12);if(WrongCountry)Data[8]='U';A->FirmwareReplyLength=ShortReply?10:12;}
    } else CHECK(Set && strcmp(Name,"wpaie")==0 && Length==22);
    return 0;
}
static NTSTATUS CywInt(PRPI5CYW_ADAPTER A,const char *Name,ULONG Value)
{(void)Name;(void)Value;return Call(A,263);}
static NTSTATUS CywFirmwareCommand(PRPI5CYW_ADAPTER A,ULONG Command,BOOLEAN Set,PUCHAR Data,ULONG Length)
{
    NTSTATUS s=Call(A,Command);CHECK(Set);if(!NT_SUCCESS(s))return s;
    if(Command==268){CHECK(Length==132 && CywLe16(Data)==32 && CywLe16(Data+2)==0);}
    else {CHECK(Command==26 && RadioUp && Length==36 && CywLe32(Data)==4);Joined=1;}
    return 0;
}
#include "../src/cyw43455/connection.h"
static void Init(PRPI5CYW_ADAPTER A,struct _CYW_NETWORK *N)
{
    memset(A,0,sizeof(*A));memset(N,0,sizeof(*N));A->Network=N;
    Calls=FailStep=WrongCountry=ShortReply=RadioUp=Joined=0;memset(Country,0,sizeof(Country));
}
int main(void)
{
    RPI5CYW_ADAPTER a;struct _CYW_NETWORK n;CYW_CONNECT_REQUEST r={0};unsigned i;
    r.Version=1;r.Country[0]='B';r.Country[1]='D';r.SsidLength=4;memcpy(r.Ssid,"test",4);
    Init(&a,&n);CHECK(CywConnect(&a,&r)==0);
    CHECK(Calls==13 && Joined && a.NetworkPhase==520 && a.CountryRequested==0x4442);
    for(i=1;i<=13;++i) {
        Init(&a,&n);FailStep=i;CHECK(CywConnect(&a,&r)==STATUS_UNSUCCESSFUL);
        CHECK(Calls==i && a.ConnectStep==i && a.NetworkPhase==510 && !Joined);
        CHECK(a.FirmwareError==0xfffffffe);if(i<=12)CHECK(!RadioUp);
    }
    Init(&a,&n);WrongCountry=1;CHECK(CywConnect(&a,&r)==STATUS_DEVICE_DATA_ERROR);
    CHECK(Calls==3 && a.ConnectStep==3 && !RadioUp && !Joined);
    Init(&a,&n);ShortReply=1;CHECK(CywConnect(&a,&r)==STATUS_DEVICE_DATA_ERROR);
    CHECK(Calls==3 && !RadioUp && !Joined);
    if(Failures)return 1;
    puts("PASS: actual connection sequence, BD revision zero/readback, all 13 failures, no radio-up after country rejection");
    return 0;
}
