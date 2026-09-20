#pragma once
/* Bounded pending-NBL ownership. Only the bus worker removes entries or
 * completes accepted NBLs; submission/cancellation use Lock. No NBL/NB chain
 * is modified except the NBL next link, detached by the send callback. */
#define CYW_TX_LIMIT 64u
#define CYW_TX_MAX_AGE 300000000ULL /* 30 seconds, interrupt-time units */
typedef struct _CYW_PENDING_SEND {
    PNET_BUFFER_LIST Nbl;
    PNET_BUFFER Next;
    PVOID CancelId;
    ULONG Frames, HeldFrames, Bytes;
    ULONG64 Submitted;
    BOOLEAN Cancelled;
} CYW_PENDING_SEND;
typedef struct _CYW_TX_STATE {
    KSPIN_LOCK Lock;
    NDIS_STATUS Gate;
    ULONG Count, Frames, Bytes, Outstanding;
    CYW_PENDING_SEND Entries[CYW_TX_LIMIT];
    /* Nonpaged, worker-only staging buffer, including BCDC header. */
    UCHAR Frame[1518];
} CYW_TX_STATE;
