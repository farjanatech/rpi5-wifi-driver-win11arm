/* SPDX-License-Identifier: GPL-3.0-or-later
 * Adapted from Ahmed ARIF's ReactOS CYW chip.c, GPL-2.0-or-later,
 * Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>, revision 9bb45e56.
 * CR4 RAM sizing: Linux v6.12 brcmfmac/chip.c, ISC Broadcom 2014.
 * See THIRD_PARTY_NOTICES.md. Only CYW43455 revision 6 is accepted.
 */
#include "network.h"
#include "../sdio/sdio.h"

#define TRY(x) do { Status=(x); if(!NT_SUCCESS(Status)) goto Exit; } while(0)
#define FW_DIR L"\\SystemRoot\\System32\\drivers\\rpi5cyw\\"
/* Linux brcmfmac configures F1 for 64 bytes. exp0.6's first 512-byte
 * byte-mode RAM write was rejected on the Pi with R5 OUT_OF_RANGE (0x1100).
 * Use conservative nonzero byte counts, not a blind retry or new block engine. */
#define CYW_F1_RAM_CHUNK 64UL

/* PASSIVE_LEVEL worker only. Persist at most once per five seconds during
 * transfer, plus phase/failure boundaries. Live status never reads hardware. */
static VOID CywFirmwareSnapshot(PRPI5CYW_ADAPTER A,BOOLEAN Force)
{
    ULONG64 now=KeQueryInterruptTime();
    if(Force || now>=A->FirmwareNextSnapshot) {
        A->FirmwareNextSnapshot=now+50000000ULL;
        Rpi5CywWriteDiagnostics(A,120,A->NetworkStatus);
    }
}
static VOID CywFirmwarePhase(PRPI5CYW_ADAPTER A,ULONG Phase)
{ A->NetworkPhase=Phase;CywFirmwareSnapshot(A,TRUE); }

#ifndef RPI5CYW_FIRMWARE_TEST
NTSTATUS CywReadFirmwareFile(PCWSTR Name, PUCHAR *Data, PULONG Size, ULONG Limit)
{
    UNICODE_STRING Path;
    OBJECT_ATTRIBUTES Attr;
    IO_STATUS_BLOCK Io;
    FILE_STANDARD_INFORMATION Info;
    HANDLE File;
    NTSTATUS Status;
    *Data=NULL; *Size=0;
    RtlInitUnicodeString(&Path,Name);
    InitializeObjectAttributes(&Attr,&Path,OBJ_KERNEL_HANDLE|OBJ_CASE_INSENSITIVE,NULL,NULL);
    Status=ZwCreateFile(&File,GENERIC_READ|SYNCHRONIZE,&Attr,&Io,NULL,
        FILE_ATTRIBUTE_NORMAL,FILE_SHARE_READ,FILE_OPEN,
        FILE_SYNCHRONOUS_IO_NONALERT|FILE_NON_DIRECTORY_FILE,NULL,0);
    if(!NT_SUCCESS(Status)) return Status;
    Status=ZwQueryInformationFile(File,&Io,&Info,sizeof(Info),FileStandardInformation);
    if(NT_SUCCESS(Status) && (Info.EndOfFile.QuadPart<=0 || Info.EndOfFile.QuadPart>Limit))
        Status=STATUS_INVALID_IMAGE_FORMAT;
    if(NT_SUCCESS(Status)) {
        *Size=Info.EndOfFile.LowPart;
        *Data=ExAllocatePool2(POOL_FLAG_NON_PAGED,(*Size+3)&~3UL,RPI5CYW_TAG);
        if(!*Data) Status=STATUS_INSUFFICIENT_RESOURCES;
        else {
            Status=ZwReadFile(File,NULL,NULL,NULL,&Io,*Data,*Size,NULL,NULL);
            if(NT_SUCCESS(Status) && Io.Information!=*Size) Status=STATUS_END_OF_FILE;
        }
    }
    ZwClose(File);
    if(!NT_SUCCESS(Status) && *Data) {ExFreePoolWithTag(*Data,RPI5CYW_TAG);*Data=NULL;}
    return Status;
}
#endif

static NTSTATUS CywWindow(PRPI5CYW_ADAPTER A, ULONG Address)
{
    ULONG i, Window=Address&0xffff8000UL;
    NTSTATUS Status;
    BOOLEAN cache=A->BusModeStage==6 && A->BusWidth==4 &&
        A->BusActualKhz>400 && ((!A->BusHighSpeedActive && A->BusActualKhz<=25000) ||
        (A->BusActualKhz<=50000 && A->BusHighSpeedActive &&
         A->BusHighSpeedStatus==STATUS_SUCCESS &&
         (A->BusCardSpeed&CYW_SDIO_SPEED_BSS_MASK)==CYW_SDIO_SPEED_ENABLE_HS &&
         (A->HostControl&SDHCI_HC_HIGH_SPEED_ENABLE)!=0));
    if(A->IoStopped)return STATUS_CANCELLED;
    /* The single bus worker owns this cache. Never reuse a partial selection.
     * Low-level window writes and failed transfers invalidate it as well. */
    if(cache && A->BpWindowValid && A->BpWindow==Window) {
        A->BpWindowCacheHits++;return STATUS_SUCCESS;
    }
    A->BpWindowValid=0;
    for(i=0;i<3;++i) {
        Status=SdioCmd52Write(A,1,0x1000a+i,(UCHAR)(Window>>(8+i*8)),0xff);
        if(!NT_SUCCESS(Status)) return Status;
    }
    if(cache) {A->BpWindow=Window;A->BpWindowValid=1;A->BpWindowSelections++;}
    return STATUS_SUCCESS;
}
NTSTATUS CywBpRead(PRPI5CYW_ADAPTER A, ULONG Address, PULONG Value)
{
    UCHAR b[4]; NTSTATUS Status;
    if((Address&3) || Address<0x18000000 || Address>0x181ffffc) return STATUS_INVALID_PARAMETER;
    Status=CywWindow(A,Address);
    if(NT_SUCCESS(Status)) Status=SdioCmd53Read(A,1,(Address&0x7fff)|0x8000,b,4);
    if(!NT_SUCCESS(Status))A->BpWindowValid=0;
    if(NT_SUCCESS(Status)) *Value=CywLe32(b);
    return Status;
}
NTSTATUS CywBpWrite(PRPI5CYW_ADAPTER A, ULONG Address, ULONG Value)
{
    UCHAR b[4]; NTSTATUS Status;
    if((Address&3) || Address<0x18000000 || Address>0x181ffffc) return STATUS_INVALID_PARAMETER;
    CywPut32(b,Value); Status=CywWindow(A,Address);
    if(NT_SUCCESS(Status)) Status=SdioCmd53Write(A,1,(Address&0x7fff)|0x8000,b,4);
    if(!NT_SUCCESS(Status))A->BpWindowValid=0;
    return Status;
}
static NTSTATUS CywRam(PRPI5CYW_ADAPTER A, ULONG Address, PUCHAR Data,
                       ULONG Length, BOOLEAN Write)
{
    ULONG n, window=0xffffffffUL; NTSTATUS Status;
    if(!Length || !Data || !((Address==0 && Length==4) ||
        (Address>=A->RamBase && Address-A->RamBase<=A->RamSize &&
         Length<=A->RamSize-(Address-A->RamBase)))) return STATUS_INVALID_PARAMETER;
    while(Length) {
        if(CywNetworkCancelled(A)) {
            A->RamTransferStatus=STATUS_CANCELLED;A->RamTransferStage=4;
            return STATUS_CANCELLED;
        }
        n=0x8000-(Address&0x7fff);
        if(n>CYW_F1_RAM_CHUNK)n=CYW_F1_RAM_CHUNK; if(n>Length)n=Length;
        A->RamTransferAddress=Address; A->RamTransferLength=n;
        A->RamTransferWrite=Write;
        A->RamTransferStatus=STATUS_PENDING;A->RamTransferStage=1;
        CywFirmwareSnapshot(A,FALSE);
        /* The sole SDIO worker owns the window throughout this call. Select
         * once per 32KiB boundary, not six CMD52 operations per small chunk. */
        if(window!=(Address&0xffff8000UL)) {
            Status=CywWindow(A,Address);
            if(!NT_SUCCESS(Status)) {A->RamTransferStatus=Status;return Status;}
            window=Address&0xffff8000UL;
        }
        A->RamTransferStage=2;
        Status=Write ? SdioCmd53Write(A,1,(Address&0x7fff)|0x8000,Data,n) :
                       SdioCmd53Read(A,1,(Address&0x7fff)|0x8000,Data,n);
        A->RamTransferStatus=Status;
        if(!NT_SUCCESS(Status)) return Status;
        A->RamTransferStage=3;
        /* Count only successful firmware payload writes, never padding,
         * register, NVRAM or vector writes. Readback counts remain separate. */
        if(Write && A->NetworkPhase==420)
            A->FirmwareUploadedBytes=min(Address-A->RamBase+n,A->FirmwareTotalBytes);
        CywFirmwareSnapshot(A,FALSE);
        Address+=n; Data+=n; Length-=n;
    }
    return STATUS_SUCCESS;
}
static NTSTATUS CywCr4Reset(PRPI5CYW_ADAPTER A, ULONG Pre, ULONG Held, ULONG Post)
{
    ULONG v,i,w=A->Cr4WrapperBase; NTSTATUS Status;
    TRY(CywBpRead(A,w+0x800,&v));
    if(!(v&1)) {
        TRY(CywBpWrite(A,w+0x408,Pre|3));
        TRY(CywBpRead(A,w+0x408,&v));
        TRY(CywBpWrite(A,w+0x800,1)); KeStallExecutionProcessor(10);
        TRY(CywBpRead(A,w+0x800,&v));
        if(!(v&1)) {Status=STATUS_DEVICE_NOT_READY;goto Exit;}
    }
    TRY(CywBpWrite(A,w+0x408,Held|3));
    TRY(CywBpRead(A,w+0x408,&v));
    for(i=0;i<50;++i) {
        TRY(CywBpWrite(A,w+0x800,0)); KeStallExecutionProcessor(50);
        TRY(CywBpRead(A,w+0x800,&v)); if(!(v&1))break;
    }
    if(i==50) {Status=STATUS_IO_TIMEOUT;goto Exit;}
    TRY(CywBpWrite(A,w+0x408,Post|1));
    TRY(CywBpRead(A,w+0x408,&v)); KeStallExecutionProcessor(10);
Exit: return Status;
}
static NTSTATUS CywClock(PRPI5CYW_ADAPTER A, UCHAR Request, UCHAR Mask)
{
    UCHAR v; ULONG i; NTSTATUS Status;
    Status=SdioCmd52Write(A,1,0x1000e,Request,0x3f);
    if(!NT_SUCCESS(Status))return Status;
    for(i=0;i<1000;++i) {
        if(CywNetworkCancelled(A))return STATUS_CANCELLED;
        Status=SdioCmd52Read(A,1,0x1000e,&v); if(!NT_SUCCESS(Status))return Status;
        if((v&Mask)==Mask)return STATUS_SUCCESS;
        SdioDelayMilliseconds(1);
    }
    return STATUS_IO_TIMEOUT;
}
static NTSTATUS CywEnable(PRPI5CYW_ADAPTER A, UCHAR Bits)
{
    UCHAR v; ULONG i; NTSTATUS Status;
    TRY(SdioCmd52Read(A,0,2,&v)); TRY(SdioCmd52Write(A,0,2,(UCHAR)(v|Bits),0xfe));
    for(i=0;i<1000;++i) {
        if(CywNetworkCancelled(A))return STATUS_CANCELLED;
        TRY(SdioCmd52Read(A,0,3,&v)); if((v&Bits)==Bits)return STATUS_SUCCESS;
        SdioDelayMilliseconds(1);
    }
    Status=STATUS_IO_TIMEOUT;
Exit: return Status;
}
static NTSTATUS CywD11Hold(PRPI5CYW_ADAPTER A)
{
    ULONG v,w=A->D11WrapperBase;NTSTATUS Status;
    if(w<0x18100000 || w>0x181ff000 || w==A->Cr4WrapperBase)
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    TRY(CywBpRead(A,w+0x800,&v));
    if(!(v&1)) {
        TRY(CywBpWrite(A,w+0x408,0xf));
        TRY(CywBpRead(A,w+0x408,&v));
        TRY(CywBpWrite(A,w+0x800,1));KeStallExecutionProcessor(10);
        TRY(CywBpRead(A,w+0x800,&v));
        if(!(v&1)) {Status=STATUS_DEVICE_NOT_READY;goto Exit;}
    }
    TRY(CywBpWrite(A,w+0x408,7));TRY(CywBpRead(A,w+0x408,&v));
Exit:return Status;
}
NTSTATUS CywFirmwareStart(PRPI5CYW_ADAPTER A)
{
    PUCHAR fw=NULL,raw=NULL,nv=NULL;
    ULONG fwSize=0,fwPadded,rawSize=0,cap,bank,v,i,token,address,off,n;
    BOOLEAN busRetried=FALSE;
    UCHAR b[4],check[512],byte;
    size_t nvSize=0;
    NTSTATUS Status=STATUS_DEVICE_CONFIGURATION_ERROR;
    A->BpWindowValid=0;
    if(A->ChipId!=0x4345 || A->ChipRevision!=6 || !A->CoreInventoryComplete ||
        A->SdioFunctions<2 || !A->Cr4WrapperBase) return Status;
    A->NetworkStatus=STATUS_SUCCESS;A->FirmwareNextSnapshot=0;
    A->FirmwareTotalBytes=0;A->FirmwareUploadedBytes=0;
    A->RamTransferStatus=STATUS_SUCCESS;A->RamTransferStage=0;
    A->FirmwareBytes=0; A->RamTransferAddress=0; A->RamTransferLength=0;
    A->RamTransferWrite=0;
    CywFirmwarePhase(A,400);
    /* Read every file before touching the CPU. Firmware is installed by INF,
     * catalog covered, with pinned source hashes recorded in the package. */
    TRY(CywReadFirmwareFile(FW_DIR L"cyfmac43455-sdio.bin",&fw,&fwSize,1024*1024));
    A->FirmwareTotalBytes=fwSize;
    TRY(CywReadFirmwareFile(FW_DIR L"brcmfmac43455-sdio.txt",&raw,&rawSize,16384));
    if(fwSize<4) {Status=STATUS_INVALID_IMAGE_FORMAT;goto Exit;}
    fwPadded=(fwSize+3)&~3UL;
    nv=ExAllocatePool2(POOL_FLAG_NON_PAGED,rawSize+8,RPI5CYW_TAG);
    if(!nv) {Status=STATUS_INSUFFICIENT_RESOURCES;goto Exit;}
    if(!CywPackNvram(raw,rawSize,nv,rawSize+8,&nvSize)) {Status=STATUS_INVALID_IMAGE_FORMAT;goto Exit;}
    /* A warm Windows restart may leave the previous firmware/F2 enabled.
     * Quiesce its FIFO and interrupt source before replacing chip RAM. */
    TRY(SdioCmd52Read(A,0,2,&byte));
    TRY(SdioCmd52Write(A,0,2,(UCHAR)(byte&~4),0xfe));
    TRY(SdioCmd52Write(A,0,4,0,7));
    TRY(SdioCmd52Write(A,0,6,2,0));
    /* FBR1 block-size bytes are in function 0, independent of host BLKSIZE.
     * Verify both before firmware transfers; never depend on warm-boot state. */
    TRY(SdioCmd52Write(A,0,0x110,(UCHAR)CYW_F1_RAM_CHUNK,0xff));
    TRY(SdioCmd52Write(A,0,0x111,0,0xff));
    TRY(CywEnable(A,2)); TRY(CywClock(A,0x28,0x40));
    TRY(SdioCmd52Write(A,1,0x1000e,0x21,0x3f)); KeStallExecutionProcessor(65);
    CywFirmwarePhase(A,410);
    TRY(CywBpRead(A,A->Cr4WrapperBase+0x408,&v));
    TRY(CywCr4Reset(A,v&0x20,0x20,0x20));
    TRY(CywD11Hold(A)); /* firmware, not host, releases D11 reset */
    TRY(CywBpRead(A,A->Cr4CoreBase+4,&cap));
    bank=(cap&15)+((cap>>4)&15);
    if(!bank || bank>30) {Status=STATUS_DEVICE_DATA_ERROR;goto Exit;}
    A->RamSize=0;
    for(i=0;i<bank;++i) {
        TRY(CywBpWrite(A,A->Cr4CoreBase+0x40,i));
        TRY(CywBpRead(A,A->Cr4CoreBase+0x44,&v));
        if(v==0xffffffff) {Status=STATUS_DEVICE_DATA_ERROR;goto Exit;}
        A->RamSize+=((v&0x7f)+1)*((v&0x200)?1024:8192);
    }
    if(A->RamSize>4*1024*1024 || nvSize+4>A->RamSize ||
        fwPadded>A->RamSize-nvSize-4) {Status=STATUS_INVALID_IMAGE_FORMAT;goto Exit;}
    CywFirmwarePhase(A,420);
    TRY(CywRam(A,A->RamBase,fw,fwPadded,TRUE));
    CywFirmwarePhase(A,421);
    /* Full readback, not just the first word. Refuse to start a corrupt upload. */
    for(off=0;off<fwPadded;off+=n) {
        n=fwPadded-off;if(n>sizeof(check))n=sizeof(check);
        TRY(CywRam(A,A->RamBase+off,check,n,FALSE));
        if(RtlCompareMemory(check,fw+off,n)!=n) {Status=STATUS_DEVICE_DATA_ERROR;goto Exit;}
        A->FirmwareBytes=min(off+n,fwSize);
        CywFirmwareSnapshot(A,FALSE);
    }
    CywFirmwarePhase(A,422);
    address=A->RamBase+A->RamSize-(ULONG)nvSize-4;
    TRY(CywRam(A,address,nv,(ULONG)nvSize,TRUE));
    token=(ULONG)(nvSize/4);token=((~token&0xffff)<<16)|(token&0xffff);
    CywPut32(b,token);TRY(CywRam(A,A->RamBase+A->RamSize-4,b,4,TRUE));
    TRY(CywRam(A,0,fw,4,TRUE));
    TRY(CywBpWrite(A,A->SdioCoreBase+0x20,0xffffffff));
    CywFirmwarePhase(A,430);
    TRY(CywCr4Reset(A,0x20,0,0));TRY(CywClock(A,0x10,0xc0));
    TRY(SdioCmd52Read(A,1,0x10009,&byte));
    TRY(SdioCmd52Write(A,1,0x10009,(UCHAR)(byte|0x10),0x10));
    TRY(SdioCmd52Write(A,1,0x10008,0x60,0xff));
    TRY(SdioCmd52Write(A,1,0x1001d,0xd0,0xff));
    TRY(SdioCmd52Write(A,0,0x210,0,0xff));
    TRY(SdioCmd52Write(A,0,0x211,2,0xff));
    TRY(CywBpWrite(A,A->SdioCoreBase+0x48,0x40000));
    TRY(CywEnable(A,4));
    TRY(SdioCmd52Write(A,0,4,7,7));
    TRY(CywBpWrite(A,A->SdioCoreBase+0x24,0x200000f0));
    CywFirmwarePhase(A,440);
    /* Keep upload/readback unchanged. First validate the faster bus using
     * read-only chip-ID CMD53 transfers, not writes into running firmware RAM.
     * Association/control traffic then exercises F2 at this same speed. */
    TRY(SdioNegotiateOperatingSpeed(A));
VerifyOperatingBus:
    A->BusModeStage=5; A->BusVerifyReads=0;
    for(i=0;i<16;++i) {
        Status=CywNetworkCancelled(A) ? STATUS_CANCELLED :
            CywBpRead(A,A->ChipCommonBase,&v);
        if(NT_SUCCESS(Status) && v!=A->ChipIdRaw)Status=STATUS_DEVICE_DATA_ERROR;
        if(!NT_SUCCESS(Status)) {
            A->BusVerifyStatus=Status;
            /* A selected high-speed mode is not yet a verified data path.
             * Restore both ends to default timing and recheck all 16 reads.
             * Never retry cancellation, or associate after inconsistent state. */
            if(A->BusHighSpeedActive && !busRetried && Status!=STATUS_CANCELLED) {
                busRetried=TRUE;A->BusHighSpeedStatus=Status;
                Status=SdioRestoreDefaultOperatingBus(A);
                if(NT_SUCCESS(Status))
                    goto VerifyOperatingBus;
                goto Exit; /* Restore helper already attempted safe recovery. */
            }
            (void)SdioRestoreIdentificationBus(A);
            A->BusModeStage=NT_SUCCESS(A->BusRecoveryStatus)?90:99;
            goto Exit; /* Never label recovered slow mode a speed success. */
        }
        ++A->BusVerifyReads;
    }
    A->BusVerifyStatus=STATUS_SUCCESS;A->BusModeStage=6;
    CywFirmwarePhase(A,450);
Exit:
    if(fw)ExFreePoolWithTag(fw,RPI5CYW_TAG);
    if(raw)ExFreePoolWithTag(raw,RPI5CYW_TAG);
    if(nv)ExFreePoolWithTag(nv,RPI5CYW_TAG);
    A->NetworkStatus=Status;
    CywFirmwareSnapshot(A,TRUE);
    return Status;
}
VOID CywFirmwareStop(PRPI5CYW_ADAPTER A)
{
    UCHAR v;
    A->BpWindowValid=0;
    if(A->IoStopped || !NT_SUCCESS(A->BusRecoveryStatus))return;
    /* No host disk/boot/UEFI changes. Stop this chip only, best effort. */
    if(A->NetworkPhase>=410) {
        (void)CywBpWrite(A,A->SdioCoreBase+0x24,0);
        (void)CywCr4Reset(A,0,0x20,0x20);
        (void)CywD11Hold(A);
    }
    if(NT_SUCCESS(SdioCmd52Read(A,0,2,&v)))
        (void)SdioCmd52Write(A,0,2,(UCHAR)(v&~6),0);
    A->BpWindowValid=0;
}
