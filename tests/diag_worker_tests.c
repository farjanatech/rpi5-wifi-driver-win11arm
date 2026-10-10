/* Execute production diagnostic-thread lifetime with host event/thread shims. */
#include <windows.h>
#include <winternl.h>
#include <stdio.h>
#include <stdlib.h>
#include "../src/driver/diag_snapshot.h"
typedef struct { HANDLE Handle; } KEVENT;
typedef struct { ULONG DiagAsyncEnabled;NTSTATUS DiagAsyncStatus; } ADAPTER,*PRPI5CYW_ADAPTER;
#define RPI5CYW_TAG 1
#define POOL_FLAG_NON_PAGED 0
#define STATUS_INSUFFICIENT_RESOURCES ((NTSTATUS)0xc000009aL)
#define Executive 0
#define KernelMode 0
#define SynchronizationEvent 0
#define NotificationEvent 1
#define STATUS_SUCCESS ((NTSTATUS)0)
static LONG Allocations,References,FailAllocation,FailThread,Writes,Errors;
static HANDLE WriterEntered,AllowWriter;
static VOID TestFree(PVOID P);
static PVOID ExAllocatePool2(ULONG Flags,size_t Size,ULONG Tag)
{(void)Flags;(void)Tag;if(FailAllocation)return NULL;InterlockedIncrement(&Allocations);return calloc(1,Size);}
#define ExFreePoolWithTag(p,t) TestFree(p)
static VOID KeInitializeEvent(KEVENT *E,int Type,BOOLEAN Set)
{E->Handle=CreateEventW(NULL,Type==NotificationEvent,Set,NULL);}
static VOID KeSetEvent(KEVENT *E,int Increment,BOOLEAN Wait)
{(void)Increment;(void)Wait;SetEvent(E->Handle);}
static VOID KeWaitForSingleObject(PVOID Object,int Reason,int Mode,BOOLEAN Alert,PVOID Timeout)
{(void)Reason;(void)Mode;(void)Alert;(void)Timeout;if(WaitForSingleObject(((KEVENT *)Object)->Handle,10000)!=WAIT_OBJECT_0)abort();}
static PVOID PsGetCurrentThread(void)
{
    KEVENT *e=calloc(1,sizeof(*e));
    if(!e || !DuplicateHandle(GetCurrentProcess(),GetCurrentThread(),GetCurrentProcess(),&e->Handle,0,FALSE,DUPLICATE_SAME_ACCESS))abort();
    InterlockedIncrement(&References);return e;
}
#define ObReferenceObject(p) ((void)(p))
static VOID ObDereferenceObject(PVOID P)
{CloseHandle(((KEVENT *)P)->Handle);free(P);InterlockedDecrement(&References);}
#define ZwClose CloseHandle
#define PsTerminateSystemThread(s) ExitThread((DWORD)(s))
typedef struct { VOID (*Routine)(PVOID);PVOID Context; } START;
static DWORD WINAPI StartThread(PVOID P)
{START start=*(START *)P;free(P);start.Routine(start.Context);return 0;}
static NTSTATUS PsCreateSystemThread(HANDLE *Handle,ULONG Access,OBJECT_ATTRIBUTES *Attr,
    PVOID Process,PVOID Id,VOID (*Routine)(PVOID),PVOID Context)
{
    START *start;(void)Access;(void)Attr;(void)Process;(void)Id;
    if(FailThread)return STATUS_INSUFFICIENT_RESOURCES;
    start=malloc(sizeof(*start));if(!start)abort();start->Routine=Routine;start->Context=Context;
    *Handle=CreateThread(NULL,0,StartThread,start,0,NULL);
    if(!*Handle)abort();return 0;
}
static VOID Rpi5CywWriteDiagnosticBuffer(const CYW_DIAG_BUFFER *B)
{
    ULONG value;memcpy(&value,B->Storage.Data,sizeof(value));
    SetEvent(WriterEntered);if(WaitForSingleObject(AllowWriter,10000)!=WAIT_OBJECT_0)abort();
    if(value!=(ULONG)Writes)InterlockedIncrement(&Errors);
    InterlockedIncrement(&Writes);
}
#include "../src/cyw43455/diagnostics_worker.h"
static VOID TestFree(PVOID P)
{
    CYW_DIAGNOSTICS_WORKER *d=P;
    if(d->Wake.Handle)CloseHandle(d->Wake.Handle);
    if(d->Started.Handle)CloseHandle(d->Started.Handle);
    free(P);InterlockedDecrement(&Allocations);
}
static DWORD WINAPI StopThread(PVOID P) {CywDiagnosticsStop(P);return 0;}
#define CHECK(x) do {if(!(x)){printf("FAIL %d: %s\n",__LINE__,#x);return 1;}}while(0)
int main(void)
{
    ADAPTER a={0};CYW_DIAGNOSTICS_WORKER *d;CYW_DIAG_BUFFER *b;HANDLE stop;ULONG i,v;
    FailAllocation=1;CHECK(!CywDiagnosticsStart(&a) && !a.DiagAsyncEnabled && !Allocations);
    FailAllocation=0;FailThread=1;CHECK(!CywDiagnosticsStart(&a) && !a.DiagAsyncEnabled && !Allocations && !References);
    FailThread=0;
    WriterEntered=CreateEventW(NULL,TRUE,FALSE,NULL);AllowWriter=CreateEventW(NULL,TRUE,FALSE,NULL);
    d=CywDiagnosticsStart(&a);CHECK(d && a.DiagAsyncEnabled);
    b=CywDiagBegin(&d->Mailbox);CHECK(b);v=0;memcpy(b->Storage.Data,&v,sizeof(v));
    CywDiagPublish(&d->Mailbox);KeSetEvent(&d->Wake,0,FALSE);
    CHECK(WaitForSingleObject(WriterEntered,10000)==WAIT_OBJECT_0);
    b=CywDiagBegin(&d->Mailbox);CHECK(b);v=1;memcpy(b->Storage.Data,&v,sizeof(v));CywDiagPublish(&d->Mailbox);
    CHECK(!CywDiagBegin(&d->Mailbox));
    stop=CreateThread(NULL,0,StopThread,d,0,NULL);CHECK(stop);
    CHECK(WaitForSingleObject(stop,20)==WAIT_TIMEOUT && Allocations==1);
    SetEvent(AllowWriter);
    CHECK(WaitForSingleObject(stop,10000)==WAIT_OBJECT_0);CloseHandle(stop);
    CHECK(Writes==2 && !Errors && !Allocations && !References);
    /* Repeated resume/stop, including an empty writer awaiting its event. */
    for(i=0;i<100;++i) {d=CywDiagnosticsStart(&a);CHECK(d);CywDiagnosticsStop(d);}
    CloseHandle(WriterEntered);CloseHandle(AllowWriter);
    CHECK(!Allocations && !References);
    puts("PASS: allocation/thread failures, blocked writer, drain-before-free, and 100 restart/stop lifetimes.");
    return 0;
}
