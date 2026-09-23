/* Actual production memory-only IOCTL gate; deliberately no firmware/SDIO
 * implementations, so accidental hardware work cannot link in this test. */
#include <stdio.h>
#define RPI5CYW_HOST_TEST 1
#include "../src/driver/driver.h"
#include "../src/cyw43455/network_protocol.h"
#include "../src/cyw43455/scan_protocol.h"
typedef int KSPIN_LOCK,KIRQL,KEVENT;
typedef long LONG;
#define STATUS_DEVICE_NOT_READY ((NTSTATUS)0xc00000a3L)
#define STATUS_DEVICE_BUSY ((NTSTATUS)0x80000011L)
#define STATUS_PENDING ((NTSTATUS)0x103L)
#define STATUS_CANCELLED ((NTSTATUS)0xc0000120L)
#define RtlCopyMemory memcpy
#define CYW_IOCTL_SCAN_START 0x12a014u
#define CYW_IOCTL_SCAN_STATUS 0x126018u
#define CYW_IOCTL_SCAN_CANCEL 0x12a01cu
typedef struct _CYW_NETWORK {
    KSPIN_LOCK Lock;KEVENT Wake;
    BOOLEAN Ready,RadioBusy,ScanBusy,ControlBusy,SelectingBand,Associated,Authorized,ScanComplete,ScanAcceptEvents;
    LONG Stop,Paused;volatile LONG ScanCancel;
    ULONG Request,ScanEventStatus;
    UCHAR ScanCountry[2];CYW_SCAN_REPORT ScanReport;
} CYW_NETWORK;
static RPI5CYW_ADAPTER Adapter;static CYW_NETWORK Network;
static unsigned Failures,Locks,Signals;
#define CHECK(x) do {if(!(x)){printf("FAIL line %d: %s\n",__LINE__,#x);Failures++;}}while(0)
static VOID KeAcquireSpinLock(KSPIN_LOCK *Lock,KIRQL *Irql)
{CHECK(Lock==&Network.Lock && !Locks);Locks++;*Irql=0;}
static VOID KeReleaseSpinLock(KSPIN_LOCK *Lock,KIRQL Irql)
{CHECK(Lock==&Network.Lock && Locks==1 && Irql==0);Locks--;}
static LONG InterlockedExchange(volatile LONG *Value,LONG New)
{LONG old=*Value;*Value=New;return old;}
static VOID KeSetEvent(KEVENT *Event,int Increment,BOOLEAN Wait)
{CHECK(Event==&Network.Wake && !Increment && !Wait && Locks==1);Signals++;}
#include "../src/cyw43455/scan_control.h"
static UCHAR Input[8]={1,0,0,0,'B','D',0,0};
static ULONG Bytes;
static VOID Init(void)
{
    CHECK(!Locks);memset(&Adapter,0,sizeof(Adapter));memset(&Network,0,sizeof(Network));
    Adapter.Network=&Network;Adapter.NetworkPhase=500;Network.Ready=TRUE;
    Network.ScanReport.Version=1;Signals=0;
}
static NTSTATUS Start(void)
{return CywScanControl(&Adapter,CYW_IOCTL_SCAN_START,Input,8,0,&Bytes);}
int main(void)
{
    CYW_SCAN_REPORT report;unsigned i;NTSTATUS status;ULONG before;
    Init();CHECK(sizeof(report)==3616);CHECK(Start()==STATUS_SUCCESS);
    CHECK(!Bytes && Signals==1 && Network.Request==4 && Network.ScanBusy);
    CHECK(Network.ScanReport.Version==1 && Network.ScanReport.Generation==1 &&
        Network.ScanReport.State==1 && Network.ScanReport.Status==(unsigned)STATUS_PENDING && !Network.ScanReport.Count);
    CHECK(Network.ScanCountry[0]=='B' && Network.ScanCountry[1]=='D');
    CHECK(Network.ScanReport.Country==0x4442 && Start()==STATUS_DEVICE_BUSY && Signals==1);
    CHECK(CywScanControl(&Adapter,CYW_IOCTL_SCAN_STATUS,(PUCHAR)&report,0,sizeof(report),&Bytes)==0);
    CHECK(Bytes==sizeof(report) && report.Generation==1 && Signals==1);
    CywPut32((PUCHAR)&before,2);
    CHECK(CywScanControl(&Adapter,CYW_IOCTL_SCAN_CANCEL,(PUCHAR)&before,4,0,&Bytes)==STATUS_INVALID_PARAMETER);
    CHECK(!Network.ScanCancel);
    CywPut32((PUCHAR)&before,1);
    CHECK(CywScanControl(&Adapter,CYW_IOCTL_SCAN_CANCEL,(PUCHAR)&before,4,0,&Bytes)==0 && Network.ScanCancel==1);
    CHECK(!Network.ScanBusy && !Network.Request && Network.ScanReport.State==5);
    Init();CHECK(Start()==0);Network.Request=0;Network.ControlBusy=TRUE;
    CHECK(CywScanControl(&Adapter,CYW_IOCTL_SCAN_CANCEL,(PUCHAR)&before,4,0,&Bytes)==0);
    CHECK(Network.ScanBusy && Network.ScanCancel==1); /* Worker owns cleanup. */
    Init();CHECK(Start()==0);CywScanQuiesce(&Adapter);
    CHECK(!Network.ScanBusy && !Network.Request && Network.ScanReport.State==5 && Network.ScanCancel);
    Init();CHECK(Start()==0);Network.ControlBusy=TRUE;Network.Request=0;CywScanQuiesce(&Adapter);
    CHECK(Network.ScanBusy && Network.ScanCancel); /* No premature live-op release. */
    Init();Network.Request=1;CywScanQuiesce(&Adapter);CHECK(Network.Request==1 && !Network.ScanCancel);
    for(i=0;i<13;i++) {
        Init();
        switch(i) {
        case 0:Network.Ready=FALSE;break;case 1:Network.Stop=1;break;
        case 2:Network.Paused=1;break;case 3:Adapter.IoStopped=1;break;
        case 4:Network.Request=1;break;case 5:Network.ControlBusy=TRUE;break;
        case 6:Network.RadioBusy=TRUE;break;case 7:Network.ScanBusy=TRUE;break;
        case 8:Network.SelectingBand=TRUE;break;case 9:Network.Associated=TRUE;break;
        case 10:Network.Authorized=TRUE;break;case 11:Adapter.NetworkPhase=520;break;
        case 12:Adapter.NetworkStatus=STATUS_IO_DEVICE_ERROR;break;
        }
        status=Start();CHECK(status==(i<4?STATUS_DEVICE_NOT_READY:STATUS_DEVICE_BUSY));
        CHECK(!Signals && !Network.ScanReport.Generation);
    }
    /* Link protection covers even a dequeued connect BEFORE association. */
    Init();Network.ControlBusy=TRUE;CHECK(Start()==STATUS_DEVICE_BUSY);
    Network.ControlBusy=FALSE;CHECK(Start()==0);
    /* Buffer/ABI/country validation cannot queue any work. */
    for(i=0;i<8;i++) {Init();CHECK(CywScanControl(&Adapter,CYW_IOCTL_SCAN_START,Input,i,0,&Bytes)==STATUS_INVALID_PARAMETER);CHECK(!Signals);}
    Init();Input[0]=2;CHECK(Start()==STATUS_INVALID_PARAMETER);Input[0]=1;
    Input[4]='b';CHECK(Start()==STATUS_INVALID_PARAMETER);Input[4]='B';
    Input[6]=1;CHECK(Start()==STATUS_INVALID_PARAMETER);Input[6]=0;
    CHECK(CywScanControl(&Adapter,CYW_IOCTL_SCAN_START,NULL,8,0,&Bytes)==STATUS_INVALID_PARAMETER);
    CHECK(CywScanControl(&Adapter,CYW_IOCTL_SCAN_STATUS,(PUCHAR)&report,0,sizeof(report)-1,&Bytes)==STATUS_INVALID_PARAMETER);
    CHECK(!Signals && !Bytes);
    /* Power reset wipes cached results; generation survives to reject an old
     * cancel on the next operation. Reading status never schedules bus work. */
    Init();Network.ScanReport.Generation=9;Network.ScanReport.Count=1;Network.ScanReport.Entries[0].Ssid[0]='X';
    Network.ScanBusy=TRUE;Network.ControlBusy=TRUE;CywScanReset(&Adapter);
    CHECK(Network.ScanReport.Version==1 && Network.ScanReport.Generation==9 && !Network.ScanReport.Count &&
        !Network.ScanReport.Entries[0].Ssid[0] && !Network.ScanBusy && !Network.ControlBusy);
    CHECK(Start()==0 && Network.ScanReport.Generation==10);
    Init();Network.ScanReport.Generation=0xffffffffUL;CHECK(Start()==0 && Network.ScanReport.Generation==1);
    CHECK(!Locks);printf("scan control: %s\n",Failures?"FAIL":"PASS");return Failures?1:0;
}
