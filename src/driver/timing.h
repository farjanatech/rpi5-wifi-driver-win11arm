/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
/* Do not import user-mode CRT headers into a WDK translation unit. */
typedef unsigned long long CYW_TIMING_U64;
#define CYW_TIMING_MAX (~(CYW_TIMING_U64)0)
/* Single runtime worker owns all fields. Raw QPC ticks, not CPU time.
 * Inclusive/nested intervals must NOT be added together. No packet data. */
enum {
    CywTimeTxPump, CywTimeRxBatch, CywTimeWorkerWork, CywTimeIdleWait,
    CywTimeWorkerInterval, CywTimeDiagnostics, CywTimeControl, CywTimeCmd52,
    CywTimeCmd53F1, CywTimeCmd53Rx, CywTimeCmd53Tx, CywTimeReceiveIndication,
    CywTimeCreditRecheck, CywTimeCount
};
typedef struct {
    CYW_TIMING_U64 Count, TotalTicks, MaxTicks, AtLeast10ms, AtLeast100ms;
} CYW_TIMING_BUCKET;
typedef struct {
    CYW_TIMING_U64 Version, BucketCount, Frequency, SessionQpc, SnapshotQpc;
    CYW_TIMING_BUCKET Bucket[CywTimeCount];
} CYW_TIMING_SNAPSHOT;
typedef struct {
    CYW_TIMING_SNAPSHOT Snapshot;
    unsigned Enabled;
} CYW_TIMING;
static __inline CYW_TIMING_U64 CywTimingAdd(CYW_TIMING_U64 A, CYW_TIMING_U64 B)
{return CYW_TIMING_MAX-A<B?CYW_TIMING_MAX:A+B;}
static __inline void CywTimingRecord(CYW_TIMING *T,unsigned Bucket,CYW_TIMING_U64 Start,CYW_TIMING_U64 End)
{
    CYW_TIMING_BUCKET *B;CYW_TIMING_U64 Delta,Ten,Hundred;
    if(!T->Enabled || !T->Snapshot.Frequency || Bucket>=CywTimeCount || End<Start)return;
    B=&T->Snapshot.Bucket[Bucket];Delta=End-Start;
    B->Count=CywTimingAdd(B->Count,1);B->TotalTicks=CywTimingAdd(B->TotalTicks,Delta);
    if(Delta>B->MaxTicks)B->MaxTicks=Delta;
    /* Ceil thresholds; division is integer-only, no hot-path tick conversion. */
    Ten=T->Snapshot.Frequency/100+(T->Snapshot.Frequency%100!=0);
    Hundred=T->Snapshot.Frequency/10+(T->Snapshot.Frequency%10!=0);
    if(Delta>=Ten)B->AtLeast10ms=CywTimingAdd(B->AtLeast10ms,1);
    if(Delta>=Hundred)B->AtLeast100ms=CywTimingAdd(B->AtLeast100ms,1);
}
