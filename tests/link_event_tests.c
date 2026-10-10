/* Exercise production event parsing/state transitions, with only OS-facing
 * notification and diagnostic sinks replaced by observations. */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "../src/cyw43455/stability_diag.h"
typedef void VOID;
typedef uint8_t UCHAR,*PUCHAR;
typedef uint32_t ULONG;
typedef int BOOLEAN;
#define TRUE 1
#define FALSE 0
typedef struct {
    BOOLEAN Associated,Authorized,SelectingBand,ScanBusy;
} CYW_NETWORK;
typedef struct {
    CYW_NETWORK *Network;
    ULONG LinkEvent,LinkReason,NetworkPhase,BandSelection[16];
    BOOLEAN Connected;
    ULONG Up,Down,Disconnects,Class,Event,Status,Reason,Scans;
} ADAPTER,*PRPI5CYW_ADAPTER;
static ULONG CywBe32(const UCHAR *p)
{return ((ULONG)p[0]<<24)|((ULONG)p[1]<<16)|((ULONG)p[2]<<8)|p[3];}
static BOOLEAN CywConnectionWasUp(PRPI5CYW_ADAPTER A,CYW_NETWORK *N)
{return A->Connected || (N->Associated && N->Authorized);}
static VOID CywLink(PRPI5CYW_ADAPTER A,BOOLEAN Up)
{
    if(Up && A->Network->SelectingBand)return;
    if(A->Connected!=Up) {if(Up)A->Up++;else A->Down++;A->Connected=Up;}
}
static VOID CywRecordFirmwareDisconnect(PRPI5CYW_ADAPTER A,ULONG Class,
                                        ULONG Event,ULONG Status,ULONG Reason)
{A->Disconnects++;A->Class=Class;A->Event=Event;A->Status=Status;A->Reason=Reason;}
static VOID CywScanEvent(PRPI5CYW_ADAPTER A,ULONG Status,PUCHAR Payload,ULONG Length)
{(void)Status;(void)Payload;(void)Length;A->Scans++;}
#include "link_event.inc"
#define CHECK(x) do {if(!(x)){printf("FAIL %d: %s\n",__LINE__,#x);return 1;}}while(0)
static void Be32(UCHAR *p,ULONG v)
{p[0]=(UCHAR)(v>>24);p[1]=(UCHAR)(v>>16);p[2]=(UCHAR)(v>>8);p[3]=(UCHAR)v;}
static void Packet(UCHAR *p,ULONG event,ULONG status,ULONG flags,ULONG reason)
{
    memset(p,0,76);p[0]=0x20;p[16]=0x88;p[17]=0x6c;
    p[24]=0x10;p[25]=0x18;p[27]=1;p[29]=2;p[31]=(UCHAR)flags;
    Be32(p+32,event);Be32(p+36,status);Be32(p+40,reason);
}
static void Event(ADAPTER *a,ULONG event,ULONG status,ULONG flags,ULONG reason)
{UCHAR p[76];Packet(p,event,status,flags,reason);CywEvent(a,p,sizeof(p));}
static void Init(ADAPTER *a,CYW_NETWORK *n,int connected)
{
    memset(a,0,sizeof(*a));memset(n,0,sizeof(*n));a->Network=n;
    a->Connected=n->Associated=n->Authorized=connected;
    a->NetworkPhase=connected?600:500;
}
static int RekeyProgress(void)
{
    const ULONG progress[]={4,5,8,9,10,11};
    ADAPTER a;CYW_NETWORK n;unsigned i,cycle;
    Init(&a,&n,TRUE);
    for(cycle=0;cycle<1000;cycle++) {
        for(i=0;i<sizeof(progress)/sizeof(progress[0]);i++) {
            Event(&a,46,progress[i],0,0);
            CHECK(n.Associated && n.Authorized && a.Connected);
            CHECK(a.NetworkPhase==600 && !a.Down && !a.Disconnects);
        }
        Event(&a,46,6,0,0);
        CHECK(a.Connected && !a.Down && !a.Up);
    }
    return 0;
}
static int InitialHandshake(void)
{
    const ULONG progress[]={4,5,8,9,10,11};
    ADAPTER a;CYW_NETWORK n;unsigned i;
    Init(&a,&n,FALSE);
    Event(&a,16,0,1,0);
    CHECK(n.Associated && !n.Authorized && !a.Connected && a.NetworkPhase==520);
    for(i=0;i<sizeof(progress)/sizeof(progress[0]);i++) {
        Event(&a,46,progress[i],0,0);
        CHECK(!n.Authorized && !a.Connected && !a.Up);
    }
    Event(&a,46,6,0,0);
    CHECK(n.Authorized && a.Connected && a.Up==1 && a.NetworkPhase==600);
    Init(&a,&n,FALSE);
    Event(&a,46,6,0,0); /* Completion may precede association. */
    CHECK(n.Authorized && !n.Associated && !a.Connected);
    Event(&a,16,0,1,0);
    CHECK(a.Connected && a.Up==1);
    return 0;
}
static int RealFailures(void)
{
    const ULONG failures[]={0,1,2,3,7,12,0xffffffffu};
    const ULONG disconnects[]={5,6,11,12,16,0};
    const ULONG progress[]={4,5,8,9,10,11};
    ADAPTER a;CYW_NETWORK n;unsigned i;
    for(i=0;i<sizeof(failures)/sizeof(failures[0]);i++) {
        Init(&a,&n,TRUE);Event(&a,46,failures[i],0,0);
        CHECK(!n.Authorized && !a.Connected && a.Down==1 && a.Disconnects==1);
        CHECK(a.Class==CYW_FW_DISCONNECT_AUTH_LOSS);
        Event(&a,46,failures[i],0,0);CHECK(a.Disconnects==1 && a.Down==1);
    }
    for(i=0;i<sizeof(progress)/sizeof(progress[0]);i++) {
        Init(&a,&n,TRUE);Event(&a,46,progress[i],0,15); /* Explicit failure reason. */
        CHECK(!a.Connected && !n.Authorized && a.Reason==15 && a.Disconnects==1);
    }
    Init(&a,&n,TRUE);Event(&a,46,6,0,15); /* Contradictory completion/error. */
    CHECK(!a.Connected && !n.Authorized && a.Disconnects==1);
    Init(&a,&n,FALSE);Event(&a,16,0,1,0);Event(&a,46,6,0,15);
    CHECK(!n.Authorized && !a.Connected && !a.Up);
    for(i=0;i<sizeof(disconnects)/sizeof(disconnects[0]);i++) {
        Init(&a,&n,TRUE);Event(&a,46,10,0,0); /* Drop during rekey. */
        Event(&a,disconnects[i],disconnects[i]==0?1:0,0,4);
        CHECK(!n.Associated && !n.Authorized && !a.Connected);
        CHECK(a.Down==1 && a.Disconnects==1 && a.Event==disconnects[i]);
        Event(&a,16,0,1,0);CHECK(!a.Connected); /* No authorization carryover. */
        Event(&a,46,6,0,0);CHECK(a.Connected && a.Up==1);
    }
    return 0;
}
static int UnrelatedAndMalformed(void)
{
    UCHAR p[76];ADAPTER a;CYW_NETWORK n;ULONG length;
    Init(&a,&n,TRUE);
    Packet(p,16,0,0,4);
    for(length=0;length<sizeof(p);length++)CywEvent(&a,p,length);
    CHECK(a.Connected && !a.Down && !a.Disconnects);
    p[2]=1;CywEvent(&a,p,sizeof(p));p[2]=0; /* Other BCDC interface. */
    p[3]=255;CywEvent(&a,p,sizeof(p));p[3]=0;
    Be32(p+48,1);CywEvent(&a,p,sizeof(p));Be32(p+48,0); /* Truncated payload. */
    p[24]=0xff;CywEvent(&a,p,sizeof(p));
    CHECK(a.Connected && !a.Down && !a.Disconnects);
    Event(&a,69,0,0,0);CHECK(a.Scans==1 && a.Connected);
    Event(&a,123,0,0,0);CHECK(a.Connected && !a.Down);
    Init(&a,&n,FALSE);n.ScanBusy=TRUE;
    Event(&a,16,0,1,0);Event(&a,46,6,0,0);
    CHECK(!n.Associated && !n.Authorized && !a.Up);
    n.ScanBusy=FALSE;n.SelectingBand=TRUE;a.BandSelection[1]=6;
    Event(&a,16,0,1,0);Event(&a,46,6,0,0);
    CHECK(!a.Connected && a.NetworkPhase==510);
    return 0;
}
int main(void)
{
    int failed=RekeyProgress()+InitialHandshake()+RealFailures()+UnrelatedAndMalformed();
    if(failed)return 1;
    puts("PASS: actual event handler: 1000 rekeys, initial authorization, real failures, reconnect, scan isolation and malformed packets.");
    return 0;
}
