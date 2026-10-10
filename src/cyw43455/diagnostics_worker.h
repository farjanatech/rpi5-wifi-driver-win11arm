/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Lifetime is nested inside the SDIO worker. The writer owns only copied
 * values; it cannot access an adapter, SDIO, credentials, or NBLs. */
#include "../driver/diag_mailbox.h"
typedef struct _CYW_DIAGNOSTICS_WORKER {
    CYW_DIAG_MAILBOX Mailbox;
    KEVENT Wake,Started;
    PVOID Thread;
    volatile LONG Stop;
} CYW_DIAGNOSTICS_WORKER;

static VOID CywDiagnosticsThread(PVOID Context)
{
    CYW_DIAGNOSTICS_WORKER *D=Context;CYW_DIAG_BUFFER *buffer;
    D->Thread=PsGetCurrentThread();ObReferenceObject(D->Thread);
    KeSetEvent(&D->Started,0,FALSE);
    for(;;) {
        buffer=CywDiagTake(&D->Mailbox);
        if(buffer) {
            Rpi5CywWriteDiagnosticBuffer(buffer);CywDiagRelease(&D->Mailbox);
            continue;
        }
        /* Stop is set only after the sole producer has finished publishing.
         * Drain pending snapshots before acknowledging thread termination. */
        if(InterlockedCompareExchange(&D->Stop,0,0)) {
            /* Recheck after the acquire barrier: the producer may have
             * published its last buffer between our empty read and Stop. */
            buffer=CywDiagTake(&D->Mailbox);
            if(!buffer)break;
            Rpi5CywWriteDiagnosticBuffer(buffer);CywDiagRelease(&D->Mailbox);
            continue;
        }
        KeWaitForSingleObject(&D->Wake,Executive,KernelMode,FALSE,NULL);
    }
    PsTerminateSystemThread(STATUS_SUCCESS);
}
static CYW_DIAGNOSTICS_WORKER *CywDiagnosticsStart(PRPI5CYW_ADAPTER A)
{
    CYW_DIAGNOSTICS_WORKER *D;OBJECT_ATTRIBUTES attr;HANDLE handle;
    A->DiagAsyncEnabled=0;
    A->DiagAsyncStatus=STATUS_INSUFFICIENT_RESOURCES;
    D=ExAllocatePool2(POOL_FLAG_NON_PAGED,sizeof(*D),RPI5CYW_TAG);
    if(!D)return NULL;
    KeInitializeEvent(&D->Wake,SynchronizationEvent,FALSE);
    KeInitializeEvent(&D->Started,NotificationEvent,FALSE);
    InitializeObjectAttributes(&attr,NULL,OBJ_KERNEL_HANDLE,NULL,NULL);
    A->DiagAsyncStatus=PsCreateSystemThread(&handle,THREAD_ALL_ACCESS,&attr,NULL,NULL,CywDiagnosticsThread,D);
    if(!NT_SUCCESS(A->DiagAsyncStatus)) {ExFreePoolWithTag(D,RPI5CYW_TAG);return NULL;}
    KeWaitForSingleObject(&D->Started,Executive,KernelMode,FALSE,NULL);
    ZwClose(handle);A->DiagAsyncEnabled=1;return D;
}
static VOID CywDiagnosticsStop(CYW_DIAGNOSTICS_WORKER *D)
{
    if(!D)return;
    InterlockedExchange(&D->Stop,1);KeSetEvent(&D->Wake,0,FALSE);
    KeWaitForSingleObject(D->Thread,Executive,KernelMode,FALSE,NULL);
    ObDereferenceObject(D->Thread);ExFreePoolWithTag(D,RPI5CYW_TAG);
}
