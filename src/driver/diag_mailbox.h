#pragma once
/* Two-slot single-producer/single-consumer FIFO. State transitions use full
 * interlocked barriers. A slow writer can skip diagnostics, never stall SDIO
 * or allocate an unbounded backlog. Producer/consumer each own their index. */
typedef struct _CYW_DIAG_MAILBOX {
    volatile LONG State[2]; /* 0 free, 1 capturing, 2 ready, 3 writing */
    ULONG Produce, Consume, Skipped;
    CYW_DIAG_BUFFER Buffer[2];
} CYW_DIAG_MAILBOX;
static __inline CYW_DIAG_BUFFER *CywDiagBegin(CYW_DIAG_MAILBOX *M)
{
    if(InterlockedCompareExchange(&M->State[M->Produce],1,0)!=0) {
        M->Skipped++;return NULL;
    }
    CywDiagReset(&M->Buffer[M->Produce]);return &M->Buffer[M->Produce];
}
static __inline VOID CywDiagPublish(CYW_DIAG_MAILBOX *M)
{
    InterlockedExchange(&M->State[M->Produce],2);M->Produce^=1;
}
static __inline CYW_DIAG_BUFFER *CywDiagTake(CYW_DIAG_MAILBOX *M)
{
    if(InterlockedCompareExchange(&M->State[M->Consume],3,2)!=2)return NULL;
    return &M->Buffer[M->Consume];
}
static __inline VOID CywDiagRelease(CYW_DIAG_MAILBOX *M)
{
    InterlockedExchange(&M->State[M->Consume],0);M->Consume^=1;
}
