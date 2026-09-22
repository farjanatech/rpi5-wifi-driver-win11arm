/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
/* Do not import user-mode CRT headers into a WDK translation unit. */
typedef unsigned long long CYW_TIMING_U64;
#define CYW_TIMING_MAX (~(CYW_TIMING_U64)0)
#ifndef RPI5CYW_DETAILED_TIMING
#define RPI5CYW_DETAILED_TIMING 0
#endif
#if RPI5CYW_DETAILED_TIMING != 0 && RPI5CYW_DETAILED_TIMING != 1
#error RPI5CYW_DETAILED_TIMING must be 0 or 1
#endif
/* Single runtime worker owns all fields. Raw QPC ticks, not CPU time.
 * Inclusive/nested intervals must NOT be added together. No packet data. */
enum {
    CywTimeTxPump, CywTimeRxBatch, CywTimeWorkerWork, CywTimeIdleWait,
    CywTimeWorkerInterval, CywTimeDiagnostics, CywTimeControl, CywTimeCmd52,
    CywTimeCmd53F1, CywTimeCmd53Rx, CywTimeCmd53Tx, CywTimeReceiveIndication,
    CywTimeCreditRecheck, CywTimeCount
};
#define CYW_TIMING_ALL_MASK 0x1FFFULL
/* Per-command/per-indication clocks are opt-in. Aggregate worker intervals
 * remain available in normal builds; absent detail is NOT zero latency. */
#if RPI5CYW_DETAILED_TIMING
#define CYW_TIMING_ACTIVE_MASK CYW_TIMING_ALL_MASK
#else
#define CYW_TIMING_ACTIVE_MASK 0x107FULL
#endif
typedef struct {
    CYW_TIMING_U64 Count, TotalTicks, MaxTicks, AtLeast10ms, AtLeast100ms;
} CYW_TIMING_BUCKET;
typedef struct {
    CYW_TIMING_U64 Version, BucketCount, Frequency, SessionQpc, SnapshotQpc, ActiveMask;
    CYW_TIMING_BUCKET Bucket[CywTimeCount];
} CYW_TIMING_SNAPSHOT;
typedef struct {
    CYW_TIMING_SNAPSHOT Snapshot;
    CYW_TIMING_U64 TenMsTicks, HundredMsTicks;
    unsigned Enabled;
} CYW_TIMING;
static __inline unsigned CywTimingBucketEnabled(const CYW_TIMING *T,unsigned Bucket)
{
    return T->Enabled && Bucket<CywTimeCount &&
        (CYW_TIMING_ACTIVE_MASK&(1ULL<<Bucket))!=0 &&
        (T->Snapshot.ActiveMask&(1ULL<<Bucket))!=0;
}
static __inline CYW_TIMING_U64 CywTimingAdd(CYW_TIMING_U64 A, CYW_TIMING_U64 B)
{return CYW_TIMING_MAX-A<B?CYW_TIMING_MAX:A+B;}
static __inline void CywTimingRecord(CYW_TIMING *T,unsigned Bucket,CYW_TIMING_U64 Start,CYW_TIMING_U64 End)
{
    CYW_TIMING_BUCKET *B;CYW_TIMING_U64 Delta;
    if(!CywTimingBucketEnabled(T,Bucket) || !T->Snapshot.Frequency || End<Start)return;
    B=&T->Snapshot.Bucket[Bucket];Delta=End-Start;
    B->Count=CywTimingAdd(B->Count,1);B->TotalTicks=CywTimingAdd(B->TotalTicks,Delta);
    if(Delta>B->MaxTicks)B->MaxTicks=Delta;
    /* Threshold conversion happens once at startup, never per command. */
    if(Delta>=T->TenMsTicks)B->AtLeast10ms=CywTimingAdd(B->AtLeast10ms,1);
    if(Delta>=T->HundredMsTicks)B->AtLeast100ms=CywTimingAdd(B->AtLeast100ms,1);
}
