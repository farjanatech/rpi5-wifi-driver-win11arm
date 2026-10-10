/* Actual schema capture plus fixed-buffer validation and immutable values. */
#define RPI5CYW_HOST_TEST 1
#include "../src/driver/driver.h"
#include <stdio.h>
#include <stdlib.h>
#include <wchar.h>
typedef wchar_t WCHAR,*PWCH;
typedef const WCHAR *PCWSTR;
typedef struct { USHORT Length,MaximumLength;PWCH Buffer; } UNICODE_STRING;
#define RtlCopyMemory memcpy
#define REG_DWORD 4u
#define REG_QWORD 11u
#define REG_BINARY 3u
static VOID RtlInitUnicodeString(UNICODE_STRING *S,PCWSTR P)
{ S->Length=(USHORT)(wcslen(P)*sizeof(WCHAR));S->MaximumLength=S->Length+2;S->Buffer=(PWCH)P; }
int TestIrql=0;
ULONG64 KeQueryInterruptTime(void) {return 123000000ULL;}
static VOID KeQuerySystemTime(LARGE_INTEGER *T) {T->QuadPart=456000000ULL;}
#include "../src/driver/diag_snapshot.h"
#include "../src/driver/diag_values.h"
static RPI5CYW_ADAPTER Adapter;
static CYW_DIAG_BUFFER Buffer;
#define CHECK(x) do {if(!(x)){printf("FAIL %d: %s\n",__LINE__,#x);return 1;}}while(0)
int main(void)
{
    CYW_DIAG_RECORD record;UNICODE_STRING name;const VOID *data;
    ULONG offset=0,count=0,found=0,v=0;UCHAR source[9]={1,2,3,4,5,6,7,8,9};
    C_ASSERT(sizeof(WCHAR)==2);C_ASSERT(sizeof(ULONG)==4);
    Adapter.TxPackets=1234;Adapter.EromWords=512;
    Adapter.DiagAsyncEnabled=1;Adapter.DiagCaptureSkipped=3;
    memset(Adapter.EromTrace,0x5a,sizeof(Adapter.EromTrace));
    CywDiagReset(&Buffer);CywCaptureDiagnosticValues(&Adapter,120,STATUS_SUCCESS,&Buffer);
    CHECK(!Buffer.Failed && Buffer.Used<CYW_DIAG_CAPACITY-16384);
    /* Writer must see the captured value even as the adapter keeps changing. */
    Adapter.TxPackets=9876;memset(Adapter.EromTrace,0,sizeof(Adapter.EromTrace));
    while(CywDiagRead(&Buffer,offset,&record,&name,&data)) {
        CHECK(record.TotalBytes && (record.TotalBytes&7)==0);
        if(!wcscmp(name.Buffer,L"TxPackets")) {
            memcpy(&v,data,sizeof(v));CHECK(v==1234 && record.Type==REG_DWORD);found|=1;
        }
        if(!wcscmp(name.Buffer,L"SnapshotTimeUtc")) {CHECK(*(const ULONG64 *)data==456000000ULL);found|=2;}
        if(!wcscmp(name.Buffer,L"EromTrace")) {CHECK(record.DataBytes==2048 && *(const UCHAR *)data==0x5a);found|=4;}
        if(!wcscmp(name.Buffer,L"DiagnosticsAsyncEnabled")) {CHECK(*(const ULONG *)data==1);found|=8;}
        offset+=record.TotalBytes;count++;
    }
    CHECK(offset==Buffer.Used && found==15 && count>600);
    printf("PASS: %lu copied registry values use %lu/%u bytes.\n",count,Buffer.Used,CYW_DIAG_CAPACITY);
    /* Non-aligned data is padded; the next record is still aligned. */
    CywDiagReset(&Buffer);RtlInitUnicodeString(&name,L"Odd");
    CywDiagAppend(&Buffer,&name,REG_BINARY,source,sizeof(source));
    memset(source,0,sizeof(source));
    CHECK(CywDiagRead(&Buffer,0,&record,&name,&data));CHECK(((const UCHAR *)data)[8]==9);
    offset=Buffer.Used;CywDiagAppend(&Buffer,&name,REG_BINARY,NULL,0);
    CHECK(CywDiagRead(&Buffer,offset,&record,&name,&data) && record.DataBytes==0);
    /* Overflow invalidates the entire batch, with no partial publication. */
    CywDiagAppend(&Buffer,&name,REG_BINARY,&v,0xffffffffUL);
    CHECK(Buffer.Failed && !CywDiagRead(&Buffer,0,&record,&name,&data));
    CywDiagReset(&Buffer);CywDiagAppend(&Buffer,&name,REG_DWORD,&v,sizeof(v));
    Buffer.Storage.Data[0]=0xff;Buffer.Storage.Data[1]=0xff;
    CHECK(!CywDiagRead(&Buffer,0,&record,&name,&data));
    puts("PASS: snapshot ownership, overflow, zero-length values and malformed bounds.");
    return 0;
}
