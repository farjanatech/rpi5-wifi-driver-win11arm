/* Compile the actual recovery callback extracted from network.c by the runner. */
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
typedef void VOID,*PVOID,*NDIS_HANDLE;
typedef uint32_t ULONG;
typedef int32_t NTSTATUS;
#define TRUE 1
#define FALSE 0
#define STATUS_SUCCESS ((NTSTATUS)0)
#define STATUS_IO_TIMEOUT ((NTSTATUS)0xc00000b5)
#define STATUS_IO_DEVICE_ERROR ((NTSTATUS)0xc0000185)
#define STATUS_INSUFFICIENT_RESOURCES ((NTSTATUS)0xc000009a)
#define NT_SUCCESS(s) ((s)>=0)
#define RPI5CYW_TAG 123u
typedef struct {
    NTSTATUS NetworkStatus,RuntimeRecoveryLastStatus;
    ULONG NetworkPhase,RuntimeRecoveryRestarts,RuntimeRecoveryFailures,RuntimeRecoveryInProgress;
} ADAPTER,*PRPI5CYW_ADAPTER;
static NTSTATUS DownResult,UpResult,ChildResult,CapturedStatus;
static ULONG Calls,Frees,WorkFrees,Captures;
static NTSTATUS CywNetworkPower(PRPI5CYW_ADAPTER A,int On)
{
    Calls++;
    if(On && NT_SUCCESS(UpResult))A->NetworkStatus=ChildResult;
    return On?UpResult:DownResult;
}
static VOID Rpi5CywWriteDiagnostics(PRPI5CYW_ADAPTER A,ULONG Stage,NTSTATUS Status)
{(void)A;if(Stage!=120)abort();CapturedStatus=Status;Captures++;}
static VOID ExFreePoolWithTag(PVOID P,ULONG Tag)
{if(!P || Tag!=RPI5CYW_TAG)abort();Frees++;}
static VOID NdisFreeIoWorkItem(NDIS_HANDLE Item)
{if(!Item)abort();WorkFrees++;}
#include "runtime_recovery_work.inc"
#define CHECK(x) do {if(!(x)){printf("FAIL %d: %s\n",__LINE__,#x);return 1;}}while(0)
static int Run(NTSTATUS Down,NTSTATUS Up,NTSTATUS Child,NTSTATUS Expected,ULONG PowerCalls)
{
    ADAPTER a={0};CYW_RUNTIME_RECOVERY_WORK work={&a};NTSTATUS result;
    a.NetworkPhase=400;a.RuntimeRecoveryInProgress=1;
    DownResult=Down;UpResult=Up;ChildResult=Child;Calls=Frees=WorkFrees=Captures=0;
    result=NT_SUCCESS(Down)?Up:Down;
    CywRuntimeRecoveryWork(&work,&a);
    CHECK(a.NetworkStatus==Expected);
    CHECK(a.NetworkPhase==400 && !a.RuntimeRecoveryInProgress);
    CHECK(a.RuntimeRecoveryLastStatus==result && CapturedStatus==result);
    CHECK(a.RuntimeRecoveryRestarts==(ULONG)NT_SUCCESS(result));
    CHECK(a.RuntimeRecoveryFailures==(ULONG)!NT_SUCCESS(result));
    CHECK(Calls==PowerCalls && Captures==1 && Frees==1 && WorkFrees==1);
    return 0;
}
int main(void)
{
    int failures=0;
    failures+=Run(STATUS_IO_DEVICE_ERROR,0,0,STATUS_IO_DEVICE_ERROR,1);
    failures+=Run(0,STATUS_IO_TIMEOUT,0,STATUS_IO_TIMEOUT,2);
    failures+=Run(0,STATUS_INSUFFICIENT_RESOURCES,0,STATUS_INSUFFICIENT_RESOURCES,2);
    failures+=Run(0,0,0,0,2);
    /* A successfully created worker can already have failed firmware startup.
     * Its real error must never be overwritten by thread-creation success. */
    failures+=Run(0,0,STATUS_IO_DEVICE_ERROR,STATUS_IO_DEVICE_ERROR,2);
    if(failures)return 1;
    puts("PASS: recovery failures reach live GUI status; successful launch preserves worker status; cleanup exactly once.");
    return 0;
}
