#include <windows.h>
#include <winternl.h>
#include <stdio.h>
#include "../src/driver/diag_snapshot.h"
#include "../src/driver/diag_mailbox.h"
static CYW_DIAG_MAILBOX Mailbox;
static volatile LONG Finished,Errors;
#define CHECK(x) do {if(!(x)){printf("FAIL %d: %s\n",__LINE__,#x);return 1;}}while(0)
static DWORD WINAPI Producer(PVOID Context)
{
    ULONG i=0;CYW_DIAG_BUFFER *b;(void)Context;
    while(i<20000) {
        b=CywDiagBegin(&Mailbox);
        if(!b) {SwitchToThread();continue;}
        b->Used=sizeof(i);memcpy(b->Storage.Data,&i,sizeof(i));
        memset(b->Storage.Data+sizeof(i),(UCHAR)i,4096);
        CywDiagPublish(&Mailbox);i++;
    }
    InterlockedExchange(&Finished,1);return 0;
}
static DWORD WINAPI Consumer(PVOID Context)
{
    ULONG expected=0,i,v;CYW_DIAG_BUFFER *b;(void)Context;
    while(expected<20000) {
        b=CywDiagTake(&Mailbox);
        if(!b) {SwitchToThread();continue;}
        memcpy(&v,b->Storage.Data,sizeof(v));
        if(v!=expected || b->Used!=sizeof(v))InterlockedIncrement(&Errors);
        for(i=0;i<4096;++i)if(b->Storage.Data[sizeof(v)+i]!=(UCHAR)expected)InterlockedIncrement(&Errors);
        /* Give the producer a chance to attempt overwrite while we hold it. */
        if((expected&1023)==0)Sleep(1);
        if(*(ULONG *)b->Storage.Data!=expected)InterlockedIncrement(&Errors);
        CywDiagRelease(&Mailbox);expected++;
    }
    return 0;
}
int main(void)
{
    CYW_DIAG_BUFFER *a,*b;HANDLE threads[2];
    a=CywDiagBegin(&Mailbox);CHECK(a && !CywDiagTake(&Mailbox));
    a->Used=8;memset(a->Storage.Data,0x5a,8);CywDiagPublish(&Mailbox);
    CHECK(CywDiagTake(&Mailbox)==a);
    b=CywDiagBegin(&Mailbox);CHECK(b && b!=a);CywDiagPublish(&Mailbox);
    CHECK(!CywDiagBegin(&Mailbox) && Mailbox.Skipped==1);
    CHECK(a->Storage.Data[0]==0x5a);CywDiagRelease(&Mailbox);
    CHECK(CywDiagTake(&Mailbox)==b);CywDiagRelease(&Mailbox);
    CHECK(!CywDiagTake(&Mailbox));
    memset(&Mailbox,0,sizeof(Mailbox));
    threads[0]=CreateThread(NULL,0,Producer,NULL,0,NULL);
    threads[1]=CreateThread(NULL,0,Consumer,NULL,0,NULL);
    CHECK(threads[0] && threads[1]);
    CHECK(WaitForMultipleObjects(2,threads,TRUE,30000)==WAIT_OBJECT_0);
    CloseHandle(threads[0]);CloseHandle(threads[1]);
    CHECK(Finished && !Errors && !CywDiagTake(&Mailbox));
    puts("PASS: slow-writer backpressure, immutable ownership, FIFO and 20,000 concurrent publications.");
    return 0;
}
