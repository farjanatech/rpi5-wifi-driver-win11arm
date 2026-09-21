/* Actual firmware.c against a simulated chip. Never loads a Windows driver. */
#include <stdio.h>
#include <stdlib.h>
#include <wchar.h>
#define RPI5CYW_HOST_TEST 1
#define RPI5CYW_FIRMWARE_TEST 1
#include "kernel_shim.h"
typedef const wchar_t *PCWSTR;
#define POOL_FLAG_NON_PAGED 0
#define STATUS_DEVICE_NOT_READY ((NTSTATUS)0xc00000a3L)
#define STATUS_INVALID_IMAGE_FORMAT ((NTSTATUS)0xc000007bL)
#define STATUS_INSUFFICIENT_RESOURCES ((NTSTATUS)0xc000009aL)
#define STATUS_CANCELLED ((NTSTATUS)0xc0000120L)
#define STATUS_PENDING ((NTSTATUS)0x103L)
ULONG64 KeQueryInterruptTime(void);
static size_t TestCompare(const void *a,const void *b,size_t n) {return memcmp(a,b,n)==0?n:0;}
#define RtlCompareMemory TestCompare
#ifndef min
#define min(a,b) ((a)<(b)?(a):(b))
#endif
static unsigned Allocations,Outstanding,FailAlloc;
static void *TestAlloc(int flags,size_t n,unsigned long tag)
{void *p;(void)flags;(void)tag;if(++Allocations==FailAlloc)return NULL;p=calloc(1,n);if(p)++Outstanding;return p;}
static void TestFree(void *p,unsigned long tag) {(void)tag;if(p){--Outstanding;free(p);}}
#define ExAllocatePool2 TestAlloc
#define ExFreePoolWithTag TestFree
#include "../src/cyw43455/firmware.c"
static unsigned Failures,Calls,FailCall,Corrupt,Started,ClockNever,ReadyNever,BadBank;
static ULONG FirmwareLength=6147,MaxRamChunk,NextWrite,NextRead;
static unsigned RamReads,RamWrites,WindowWrites;
static unsigned Snapshots;static ULONG LastUploaded,LastVerified;
static ULONG Window,Bank,Ioctl,Reset,D11Reset;
static unsigned SpeedBadRead,SpeedRestoreFail;
static UCHAR Card[0x10020],Ram[0xc8000],Vector[4];
int TestIrql;
BOOLEAN CywNetworkCancelled(PRPI5CYW_ADAPTER A) {return A->IoStopped!=0;}
#define CHECK(x) do {if(!(x)){printf("FAIL line %d %s\n",__LINE__,#x);++Failures;}}while(0)
ULONG64 KeQueryInterruptTime(void) {return (ULONG64)Calls*100000ULL;}
void Rpi5CywWriteDiagnostics(PRPI5CYW_ADAPTER A,ULONG Stage,NTSTATUS Status)
{
    (void)Stage;(void)Status;++Snapshots;
    CHECK(A->FirmwareUploadedBytes>=LastUploaded && A->FirmwareUploadedBytes<=A->FirmwareTotalBytes);
    CHECK(A->FirmwareBytes>=LastVerified && A->FirmwareBytes<=A->FirmwareUploadedBytes);
    LastUploaded=A->FirmwareUploadedBytes;LastVerified=A->FirmwareBytes;
}
static NTSTATUS Tick(void) {return ++Calls==FailCall?STATUS_IO_DEVICE_ERROR:STATUS_SUCCESS;}
void KeStallExecutionProcessor(ULONG u) {(void)u;}
void SdioDelayMilliseconds(ULONG u) {(void)u;}
NTSTATUS SdioRestoreIdentificationBus(PRPI5CYW_ADAPTER A)
{
    A->BusRecoveryStatus=SpeedRestoreFail?STATUS_IO_DEVICE_ERROR:STATUS_SUCCESS;
    if(!SpeedRestoreFail){A->BusWidth=1;A->BusActualKhz=400;}
    return A->BusRecoveryStatus;
}
NTSTATUS SdioNegotiateOperatingSpeed(PRPI5CYW_ADAPTER A)
{
    NTSTATUS status=Tick();
    CHECK(Started && A->FirmwareBytes==FirmwareLength && A->NetworkPhase==440);
    if(NT_SUCCESS(status)){A->BusWidth=4;A->BusActualKhz=25000;}
    else {(void)SdioRestoreIdentificationBus(A);}
    return status;
}
NTSTATUS CywReadFirmwareFile(PCWSTR Name,PUCHAR *Data,PULONG Size,ULONG Limit)
{
    ULONG i;(void)Limit;
    if(wcsstr(Name,L".bin")) {
        *Size=FirmwareLength;*Data=TestAlloc(0,(*Size+3)&~3UL,0);if(!*Data)return STATUS_INSUFFICIENT_RESOURCES;
        for(i=0;i<*Size;++i)(*Data)[i]=(UCHAR)(i*7);
    } else {
        *Size=8;*Data=TestAlloc(0,*Size,0);if(!*Data)return STATUS_INSUFFICIENT_RESOURCES;
        memcpy(*Data,"a=1\nb=2\n",8);
    }
    return STATUS_SUCCESS;
}
NTSTATUS SdioCmd52Read(PRPI5CYW_ADAPTER A,UCHAR F,ULONG Reg,PUCHAR Value)
{
    NTSTATUS s=Tick();(void)A;(void)F;if(!NT_SUCCESS(s))return s;
    *Value=Card[Reg];if(Reg==3)*Value=ReadyNever?0:Card[2];
    if(Reg==0x1000e && !ClockNever)*Value|=0xc0;return 0;
}
NTSTATUS SdioCmd52Write(PRPI5CYW_ADAPTER A,UCHAR F,ULONG Reg,UCHAR Value,UCHAR Mask)
{
    NTSTATUS s=Tick();(void)A;(void)F;(void)Mask;if(!NT_SUCCESS(s))return s;
    Card[Reg]=Value;
    if(Reg>=0x1000a && Reg<=0x1000c) {
        ++WindowWrites;
        Window=(ULONG)Card[0x1000a]<<8|(ULONG)Card[0x1000b]<<16|(ULONG)Card[0x1000c]<<24;
    }
    return 0;
}
static NTSTATUS Transfer(PRPI5CYW_ADAPTER A,ULONG Address,PUCHAR Data,ULONG Len,BOOLEAN Write)
{
    ULONG addr=Window|(Address&0x7fff),v=0;NTSTATUS s=Tick();
    CHECK(Address&0x8000);
    if(!NT_SUCCESS(s))return s;
    if(addr>=0x198000 && addr-0x198000<sizeof(Ram) && Len<=sizeof(Ram)-(addr-0x198000)) {
        /* Model the hardware constraint absent in exp0.6's permissive stub.
         * This model reproduces the report, not proof of the chip's exact limit. */
        if(Len>64 || Card[0x110]!=64 || Card[0x111]!=0)return STATUS_IO_DEVICE_ERROR;
        CHECK((Len&3)==0 && (addr&3)==0);
        CHECK((Address&0x7fff)+Len<=0x8000);
        if(Len>MaxRamChunk)MaxRamChunk=Len;
        if(Write)++RamWrites;else ++RamReads;
        if(addr-0x198000<((FirmwareLength+3)&~3UL)) {
            if(Write) {CHECK(addr==NextWrite);NextWrite+=Len;}
            else {CHECK(addr==NextRead);NextRead+=Len;}
        }
        if(Write)memcpy(Ram+addr-0x198000,Data,Len);
        else {memcpy(Data,Ram+addr-0x198000,Len);if(Corrupt)Data[0]^=1;}
    } else if(addr==0 && Len==4) {if(Write)memcpy(Vector,Data,4);else memcpy(Data,Vector,4);}
    else if(Len==4) {
        if(Write) {
            v=CywLe32(Data);
            if(addr==A->Cr4CoreBase+0x40)Bank=v;
            if(addr==A->Cr4WrapperBase+0x408) {Ioctl=v;if(!(v&0x20))Started=1;}
            if(addr==A->Cr4WrapperBase+0x800)Reset=v;
            if(addr==A->D11WrapperBase+0x800)D11Reset=v;
        } else {
            if(addr==A->ChipCommonBase)v=SpeedBadRead?0xffffffff:A->ChipIdRaw;
            if(addr==A->Cr4CoreBase+4)v=0xb44;
            if(addr==A->Cr4CoreBase+0x44)v=BadBank?0xffffffff:(Bank<4?15:8);
            if(addr==A->Cr4WrapperBase+0x408)v=Ioctl;
            if(addr==A->Cr4WrapperBase+0x800)v=Reset;
            if(addr==A->D11WrapperBase+0x800)v=D11Reset;
            CywPut32(Data,v);
        }
    } else return STATUS_INVALID_PARAMETER;
    return 0;
}
NTSTATUS SdioCmd53Read(PRPI5CYW_ADAPTER A,UCHAR F,ULONG Reg,PUCHAR Data,ULONG Len)
{CHECK(F==1);return Transfer(A,Reg,Data,Len,FALSE);}
NTSTATUS SdioCmd53Write(PRPI5CYW_ADAPTER A,UCHAR F,ULONG Reg,PUCHAR Data,ULONG Len)
{CHECK(F==1);return Transfer(A,Reg,Data,Len,TRUE);}
static void Init(PRPI5CYW_ADAPTER A)
{
    CHECK(Outstanding==0);memset(A,0,sizeof(*A));memset(Card,0,sizeof(Card));memset(Ram,0,sizeof(Ram));
    Calls=FailCall=Allocations=FailAlloc=Started=Corrupt=ClockNever=ReadyNever=BadBank=Window=Bank=Reset=D11Reset=0;Ioctl=0x21;
    SpeedBadRead=SpeedRestoreFail=0;
    FirmwareLength=6147;MaxRamChunk=RamReads=RamWrites=WindowWrites=0;
    Snapshots=LastUploaded=LastVerified=0;
    NextWrite=NextRead=0x198000;
    A->ChipId=0x4345;A->ChipRevision=6;A->CoreInventoryComplete=1;A->SdioFunctions=3;
    A->RamBase=0x198000;A->Cr4CoreBase=0x18002000;A->Cr4WrapperBase=0x18102000;A->SdioCoreBase=0x18004000;
    A->D11WrapperBase=0x18101000;
    A->ChipCommonBase=0x18000000;A->ChipIdRaw=0x15264345;
}
int main(void)
{
    RPI5CYW_ADAPTER a;unsigned count,i,j;UCHAR rejected[512]={0};
    ULONG value;
    /* Real CywBpRead/Write: a verified window replaces six redundant CMD52
     * write/readback operations per subsequent register access in production. */
    Init(&a);a.BusModeStage=6;a.BusWidth=4;a.BusActualKhz=25000;
    CHECK(CywBpRead(&a,a.ChipCommonBase,&value)==0 && value==a.ChipIdRaw);
    CHECK(WindowWrites==3 && a.BpWindowValid && a.BpWindowSelections==1);
    for(i=0;i<100;++i)CHECK(CywBpRead(&a,a.ChipCommonBase,&value)==0 && value==a.ChipIdRaw);
    CHECK(WindowWrites==3 && a.BpWindowCacheHits==100);
    CHECK(CywBpWrite(&a,a.SdioCoreBase+0x20,0)==0 && WindowWrites==3);
    CHECK(CywBpRead(&a,a.Cr4WrapperBase+0x408,&value)==0 && WindowWrites==6);
    FailCall=Calls+1;
    CHECK(!NT_SUCCESS(CywBpRead(&a,a.Cr4WrapperBase+0x408,&value)) && !a.BpWindowValid);
    FailCall=0;CHECK(CywBpRead(&a,a.Cr4WrapperBase+0x408,&value)==0 && WindowWrites==9);
    for(i=1;i<=3;++i) {
        Init(&a);a.BusModeStage=6;a.BusWidth=4;a.BusActualKhz=25000;FailCall=i;
        CHECK(!NT_SUCCESS(CywBpRead(&a,a.ChipCommonBase,&value)) && !a.BpWindowValid);
        FailCall=0;CHECK(CywBpRead(&a,a.ChipCommonBase,&value)==0 && a.BpWindowValid);
    }
    Init(&a);CHECK(CywBpRead(&a,a.ChipCommonBase,&value)==0);
    CHECK(CywBpRead(&a,a.ChipCommonBase,&value)==0 && WindowWrites==6 && !a.BpWindowValid);
    a.BpWindowValid=1;a.IoStopped=1;CHECK(CywBpRead(&a,a.ChipCommonBase,&value)==STATUS_CANCELLED && !a.BpWindowValid);
    const ULONG packageSizes[]={609309,631467};
    Init(&a);Window=0x198000;Card[0x110]=64;
    CHECK(SdioCmd53Write(&a,1,0x8000,rejected,512)==STATUS_IO_DEVICE_ERROR);
    CHECK(RamWrites==0 && !Started);
    Init(&a);CHECK(CywFirmwareStart(&a)==0);CHECK(a.NetworkPhase==450 && a.RamSize==0xc8000);
    CHECK(a.BusModeStage==6 && a.BusVerifyReads==16 && a.BusWidth==4);
    CHECK(Started && a.FirmwareBytes==6147 && Outstanding==0);count=Calls;
    CHECK(a.FirmwareUploadedBytes==6147 && a.FirmwareTotalBytes==6147 && Snapshots>=7);
    CHECK(MaxRamChunk==64 && RamReads==97 && RamWrites==99);
    CHECK(NextWrite==0x198000+6148 && NextRead==NextWrite);
    CHECK(Card[0x110]==64 && Card[0x111]==0);
    CHECK(CywLe32(Ram+sizeof(Ram)-4)==0xfffc0003);
    CywFirmwareStop(&a);CHECK((Card[2]&6)==0 && Ioctl==0x21 && D11Reset==1);
    Init(&a);Card[2]=6;Card[4]=7;CHECK(CywFirmwareStart(&a)==0);CHECK(a.NetworkPhase==450);
    Init(&a);SpeedBadRead=1;
    CHECK(CywFirmwareStart(&a)==STATUS_DEVICE_DATA_ERROR);
    CHECK(a.BusModeStage==90 && a.BusWidth==1 && a.BusActualKhz==400 && Outstanding==0);
    Init(&a);SpeedBadRead=SpeedRestoreFail=1;
    CHECK(CywFirmwareStart(&a)==STATUS_DEVICE_DATA_ERROR && a.BusModeStage==99);
    i=Calls;CywFirmwareStop(&a);CHECK(Calls==i); /* no unsafe cleanup transfers */
    for(i=1;i<=count;++i) {
        Init(&a);FailCall=i;CHECK(!NT_SUCCESS(CywFirmwareStart(&a)));CHECK(Outstanding==0);
        CHECK(a.FirmwareUploadedBytes==min(NextWrite-0x198000,a.FirmwareTotalBytes));
        CHECK(LastUploaded==a.FirmwareUploadedBytes && LastVerified==a.FirmwareBytes);
    }
    for(i=1;i<=3;++i) {Init(&a);FailAlloc=i;CHECK(!NT_SUCCESS(CywFirmwareStart(&a)));CHECK(Outstanding==0 && Calls==0);}
    Init(&a);Corrupt=1;CHECK(CywFirmwareStart(&a)==STATUS_DEVICE_DATA_ERROR);CHECK(!Started);
    CHECK(a.NetworkPhase==421 && a.FirmwareBytes==0 && a.RamTransferWrite==0);
    Init(&a);BadBank=1;CHECK(CywFirmwareStart(&a)==STATUS_DEVICE_DATA_ERROR);CHECK(!Started);
    Init(&a);ClockNever=1;CHECK(CywFirmwareStart(&a)==STATUS_IO_TIMEOUT);CHECK(!Started);
    Init(&a);ReadyNever=1;CHECK(CywFirmwareStart(&a)==STATUS_IO_TIMEOUT);CHECK(!Started);
    Init(&a);a.ChipRevision=7;CHECK(CywFirmwareStart(&a)==STATUS_DEVICE_CONFIGURATION_ERROR);CHECK(Calls==0);
    /* Old and ReactOS package sizes cross multiple 32KiB windows and end in
     * partial words. These are synthetic bytes, not firmware execution. */
    for(j=0;j<2;++j) {
    Init(&a);FirmwareLength=packageSizes[j];CHECK(CywFirmwareStart(&a)==0);
    CHECK(a.FirmwareBytes==FirmwareLength && MaxRamChunk==64 && Started);
    CHECK(a.FirmwareUploadedBytes==FirmwareLength && LastVerified==FirmwareLength);
    CHECK(Snapshots>10 && Snapshots<Calls/100); /* throttled, not per chunk */
    CHECK(NextWrite==0x198000+((FirmwareLength+3)&~3u) && NextRead==NextWrite);
    for(i=0;i<FirmwareLength;++i)CHECK(Ram[i]==(UCHAR)(i*7));
    for(i=FirmwareLength;i<((FirmwareLength+3)&~3u);++i)CHECK(Ram[i]==0);
    CHECK(WindowWrites<5000); /* no reselect for every 64-byte RAM chunk */
    CHECK(a.RamTransferAddress==0 && a.RamTransferLength==4 && a.RamTransferWrite==1);
    CHECK(Outstanding==0);
    }
    CHECK(Outstanding==0);if(Failures)return 1;
    printf("PASS: actual firmware startup, RAM sizing/readback, allocation errors and %u injected bus faults\n",count);return 0;
}
