#pragma once
/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Immutable registry values, never adapter pointers or live packet buffers.
 * Serialization is bounded and allocation-free on the SDIO worker. */
#define CYW_DIAG_CAPACITY (128u*1024u)
typedef struct _CYW_DIAG_RECORD {
    ULONG NameBytes, Type, DataBytes, TotalBytes;
} CYW_DIAG_RECORD;
typedef struct _CYW_DIAG_BUFFER {
    ULONG Used, Failed;
    union { ULONG64 Alignment; UCHAR Data[CYW_DIAG_CAPACITY]; } Storage;
} CYW_DIAG_BUFFER;

static __inline VOID CywDiagReset(CYW_DIAG_BUFFER *B)
{ B->Used=0;B->Failed=0; }
static __inline VOID CywDiagAppend(CYW_DIAG_BUFFER *B,const UNICODE_STRING *Name,
    ULONG Type,const VOID *Data,ULONG Bytes)
{
    CYW_DIAG_RECORD r;ULONG names,body;PUCHAR p;
    if(B->Failed)return;
    names=((ULONG)Name->Length+sizeof(WCHAR)+7u)&~7u;
    if(!Name->Length || (Name->Length&1) || !Name->Buffer ||
       Bytes>CYW_DIAG_CAPACITY || (Bytes && !Data)) {B->Failed=1;return;}
    body=(Bytes+7u)&~7u;
    r.NameBytes=Name->Length;r.Type=Type;r.DataBytes=Bytes;
    r.TotalBytes=(ULONG)sizeof(r)+names+body;
    if(B->Used>CYW_DIAG_CAPACITY || r.TotalBytes>CYW_DIAG_CAPACITY-B->Used) {
        B->Failed=1;return;
    }
    p=B->Storage.Data+B->Used;
    RtlCopyMemory(p,&r,sizeof(r));p+=sizeof(r);
    RtlZeroMemory(p,names);RtlCopyMemory(p,Name->Buffer,Name->Length);p+=names;
    if(Bytes)RtlCopyMemory(p,Data,Bytes);
    if(body>Bytes)RtlZeroMemory(p+Bytes,body-Bytes);
    B->Used+=r.TotalBytes;
}
/* Reject a partial or malformed batch rather than writing past its boundary. */
static __inline BOOLEAN CywDiagRead(const CYW_DIAG_BUFFER *B,ULONG Offset,
    CYW_DIAG_RECORD *R,UNICODE_STRING *Name,const VOID **Data)
{
    ULONG names;
    if(B->Failed || B->Used>CYW_DIAG_CAPACITY || Offset>B->Used ||
       B->Used-Offset<sizeof(*R))return FALSE;
    RtlCopyMemory(R,B->Storage.Data+Offset,sizeof(*R));
    if(!R->NameBytes || (R->NameBytes&1) || R->NameBytes>65532u ||
       R->DataBytes>CYW_DIAG_CAPACITY)return FALSE;
    names=(R->NameBytes+sizeof(WCHAR)+7u)&~7u;
    if(R->TotalBytes!=sizeof(*R)+names+((R->DataBytes+7u)&~7u) ||
       R->TotalBytes>B->Used-Offset)return FALSE;
    Name->Length=(USHORT)R->NameBytes;Name->MaximumLength=(USHORT)(R->NameBytes+sizeof(WCHAR));
    Name->Buffer=(PWCH)(B->Storage.Data+Offset+sizeof(*R));
    *Data=B->Storage.Data+Offset+sizeof(*R)+names;
    return TRUE;
}
