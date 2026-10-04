#pragma once
/* Bounded pending-NBL ownership. Only the bus worker removes active entries or
 * completes accepted NBLs; submission/cancellation use Lock. No NBL/NB chain
 * is modified except the NBL next link, detached by the send callback.
 *
 * v0.7.1.9 keeps the proven 64-frame active transport window and adds a
 * separate bounded 128-frame pending backlog. Backlogged NBLs remain owned by
 * the miniport but cannot consume firmware credits or SDIO budget until they
 * are promoted into the unchanged active queue.
 */
#define CYW_TX_LIMIT RPI5CYW_TX_LIMIT
#define CYW_TX_BACKLOG_LIMIT RPI5CYW_TX_BACKLOG_LIMIT
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
    ULONG Count, Frames, Bytes, Outstanding, Completing;
    CYW_PENDING_SEND Entries[CYW_TX_LIMIT];
    ULONG BacklogCount, BacklogFrames, BacklogBytes;
    CYW_PENDING_SEND Backlog[CYW_TX_BACKLOG_LIMIT];
    /* Nonpaged, worker-only staging buffers, including BCDC header. */
    UCHAR Frame[1518];
#if RPI5CYW_TX_GLOM2
    UCHAR Frame2[1518];
#endif
} CYW_TX_STATE;
