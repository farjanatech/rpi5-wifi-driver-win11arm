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
        *Data=ExAllocatePool2(POOL_FLAG_NON_PAGED,*Size,RPI5CYW_TAG);
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
    for(i=0;i<3;++i) {
        Status=SdioCmd52Write(A,1,0x1000a+i,(UCHAR)(Window>>(8+i*8)),0xff);
        if(!NT_SUCCESS(Status)) return Status;
    }
    return STATUS_SUCCESS;
}
NTSTATUS CywBpRead(PRPI5CYW_ADAPTER A, ULONG Address, PULONG Value)
{
    UCHAR b[4]; NTSTATUS Status;
    if((Address&3) || Address<0x18000000 || Address>0x181ffffc) return STATUS_INVALID_PARAMETER;
    Status=CywWindow(A,Address);
    if(NT_SUCCESS(Status)) Status=SdioCmd53Read(A,1,(Address&0x7fff)|0x8000,b,4);
    if(NT_SUCCESS(Status)) *Value=CywLe32(b);
    return Status;
}
NTSTATUS CywBpWrite(PRPI5CYW_ADAPTER A, ULONG Address, ULONG Value)
{
    UCHAR b[4]; NTSTATUS Status;
    if((Address&3) || Address<0x18000000 || Address>0x181ffffc) return STATUS_INVALID_PARAMETER;
    CywPut32(b,Value); Status=CywWindow(A,Address);
    if(NT_SUCCESS(Status)) Status=SdioCmd53Write(A,1,(Address&0x7fff)|0x8000,b,4);
    return Status;
}
static NTSTATUS CywRam(PRPI5CYW_ADAPTER A, ULONG Address, PUCHAR Data,
                       ULONG Length, BOOLEAN Write)
{
    ULONG n; NTSTATUS Status;
    if(!Length || !Data || !((Address==0 && Length==4) ||
        (Address>=A->RamBase && Address-A->RamBase<=A->RamSize &&
         Length<=A->RamSize-(Address-A->RamBase)))) return STATUS_INVALID_PARAMETER;
    while(Length) {
        n=0x8000-(Address&0x7fff); if(n>512)n=512; if(n>Length)n=Length;
        Status=CywWindow(A,Address); if(!NT_SUCCESS(Status)) return Status;
        Status=Write ? SdioCmd53Write(A,1,Address&0x7fff,Data,n) :
                       SdioCmd53Read(A,1,Address&0x7fff,Data,n);
        if(!NT_SUCCESS(Status)) return Status;
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
        TRY(SdioCmd52Read(A,0,3,&v)); if((v&Bits)==Bits)return STATUS_SUCCESS;
        SdioDelayMilliseconds(1);
    }
    Status=STATUS_IO_TIMEOUT;
Exit: return Status;
}
NTSTATUS CywFirmwareStart(PRPI5CYW_ADAPTER A)
{
    PUCHAR fw=NULL,raw=NULL,nv=NULL;
    ULONG fwSize=0,rawSize=0,cap,bank,v,i,token,address,off,n;
    UCHAR b[4],check[512],byte;
    size_t nvSize=0;
    NTSTATUS Status=STATUS_DEVICE_CONFIGURATION_ERROR;
    if(A->ChipId!=0x4345 || A->ChipRevision!=6 || !A->CoreInventoryComplete ||
        A->SdioFunctions<2 || !A->Cr4WrapperBase) return Status;
    A->NetworkPhase=400;
    /* Read every file before touching the CPU. Firmware is installed by INF,
     * catalog covered, with pinned source hashes recorded in the package. */
    TRY(CywReadFirmwareFile(FW_DIR L"cyfmac43455-sdio.bin",&fw,&fwSize,1024*1024));
    TRY(CywReadFirmwareFile(FW_DIR L"brcmfmac43455-sdio.txt",&raw,&rawSize,16384));
    if(fwSize<4) {Status=STATUS_INVALID_IMAGE_FORMAT;goto Exit;}
    nv=ExAllocatePool2(POOL_FLAG_NON_PAGED,rawSize+8,RPI5CYW_TAG);
    if(!nv) {Status=STATUS_INSUFFICIENT_RESOURCES;goto Exit;}
    if(!CywPackNvram(raw,rawSize,nv,rawSize+8,&nvSize)) {Status=STATUS_INVALID_IMAGE_FORMAT;goto Exit;}
    TRY(CywEnable(A,2)); TRY(CywClock(A,0x28,0x40));
    TRY(SdioCmd52Write(A,1,0x1000e,0x21,0x3f)); KeStallExecutionProcessor(65);
    A->NetworkPhase=410;
    TRY(CywBpRead(A,A->Cr4WrapperBase+0x408,&v));
    TRY(CywCr4Reset(A,v&0x20,0x20,0x20));
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
        fwSize>A->RamSize-nvSize-4) {Status=STATUS_INVALID_IMAGE_FORMAT;goto Exit;}
    A->NetworkPhase=420;
    TRY(CywRam(A,A->RamBase,fw,fwSize,TRUE));
    /* Full readback, not just the first word. Refuse to start a corrupt upload. */
    for(off=0;off<fwSize;off+=n) {
        n=fwSize-off;if(n>sizeof(check))n=sizeof(check);
        TRY(CywRam(A,A->RamBase+off,check,n,FALSE));
        if(RtlCompareMemory(check,fw+off,n)!=n) {Status=STATUS_DEVICE_DATA_ERROR;goto Exit;}
        A->FirmwareBytes=off+n;
    }
    address=A->RamBase+A->RamSize-(ULONG)nvSize-4;
    TRY(CywRam(A,address,nv,(ULONG)nvSize,TRUE));
    token=(ULONG)(nvSize/4);token=((~token&0xffff)<<16)|(token&0xffff);
    CywPut32(b,token);TRY(CywRam(A,A->RamBase+A->RamSize-4,b,4,TRUE));
    TRY(CywRam(A,0,fw,4,TRUE));
    A->NetworkPhase=430;
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
    A->NetworkPhase=440;
Exit:
    if(fw)ExFreePoolWithTag(fw,RPI5CYW_TAG);
    if(raw)ExFreePoolWithTag(raw,RPI5CYW_TAG);
    if(nv)ExFreePoolWithTag(nv,RPI5CYW_TAG);
    A->NetworkStatus=Status;
    return Status;
}
VOID CywFirmwareStop(PRPI5CYW_ADAPTER A)
{
    UCHAR v;
    /* No host disk/boot/UEFI changes. Stop this chip only, best effort. */
    if(A->NetworkPhase>=410) {
        (void)CywBpWrite(A,A->SdioCoreBase+0x24,0);
        (void)CywCr4Reset(A,0,0x20,0x20);
    }
    if(NT_SUCCESS(SdioCmd52Read(A,0,2,&v)))
        (void)SdioCmd52Write(A,0,2,(UCHAR)(v&~6),0);
}
