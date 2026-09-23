/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
/* Passive, fixed-size, worker-owned history. No addresses, packet contents,
 * credentials, firmware GETs, allocation, or hot-path registry operations.
 * Times are monotonic 100 ns ticks since boot, not wall-clock timestamps.
 * Entries are observations, not a diagnosis that a quiet link has stalled.
 * Export the entire structure only from the existing serialized diagnostics.
 */
#define CYW_TRANSPORT_TRACE_VERSION 1u
#define CYW_TRANSPORT_TRACE_CAPACITY 128u
#define CYW_TRANSPORT_TRACE_INTERVAL 10000000ULL
#define CYW_TRANSPORT_FALLBACK_INTERVAL 1000000ULL
#define CYW_TRACE_RX_PENDING 1u
#define CYW_TRACE_READY 2u
#define CYW_TRACE_AUTHORIZED 4u
#define CYW_TRACE_PAUSED 8u
#define CYW_TRACE_HALTED 16u
typedef struct {
    unsigned long long Time100ns;
    unsigned int Serial,Flags,NetworkPhase,LastPending,LastInterrupt,LastMailbox;
    unsigned int RxFrames,EmptyReads,TxPacketsLow,RxPacketsLow;
    unsigned int PendingReads,PendingEmpty,StatusReads,StatusNoEvents,FrameNotifications,MailReads;
    unsigned int FallbackReads,FallbackFrames,FallbackMailbox,ServiceErrors,SequenceMismatches;
    unsigned int QueueDepth,QueueFull,CreditWaits,TxSequence,TxMaximum,TxFlow,GlobalFlow;
    unsigned int EchoLate,EchoMaxMs;
} CYW_TRANSPORT_TRACE_ENTRY;
typedef struct {
    unsigned int Version,EntryBytes,Capacity,Count,Next,Serial;
    unsigned long long Origin100ns;
    CYW_TRANSPORT_TRACE_ENTRY Entry[CYW_TRANSPORT_TRACE_CAPACITY];
} CYW_TRANSPORT_TRACE;
/* Stable little-endian Windows ARM64/x64 binary layout: header 32 bytes,
 * entry 128 bytes, total 16416 bytes. Count<=128; Next is the next write slot;
 * oldest entry is slot zero until full, then slot Next. All 32-bit counters
 * are modulo 2^32 snapshots; consumers calculate wrap-aware deltas. */
C_ASSERT(sizeof(unsigned int)==4);
C_ASSERT(sizeof(CYW_TRANSPORT_TRACE_ENTRY)==128);
C_ASSERT(sizeof(CYW_TRANSPORT_TRACE)==16416);
