/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
#include <stdint.h>
/* Single runtime worker owns all fields. Raw QPC ticks, not CPU time.
 * Inclusive/nested intervals must NOT be added together. No packet data. */
enum {
    CywTimeTxPump, CywTimeRxBatch, CywTimeWorkerWork, CywTimeIdleWait,
    CywTimeWorkerInterval, CywTimeDiagnostics, CywTimeControl, CywTimeCmd52,
    CywTimeCmd53F1, CywTimeCmd53Rx, CywTimeCmd53Tx, CywTimeReceiveIndication,
    CywTimeCreditRecheck, CywTimeCount
};
typedef struct {
    uint64_t Count, TotalTicks, MaxTicks, AtLeast10ms, AtLeast100ms;
} CYW_TIMING_BUCKET;
typedef struct {
    uint64_t Version, BucketCount, Frequency, SessionQpc, SnapshotQpc;
    CYW_TIMING_BUCKET Bucket[CywTimeCount];
} CYW_TIMING_SNAPSHOT;
typedef struct {
    CYW_TIMING_SNAPSHOT Snapshot;
    unsigned Enabled;
} CYW_TIMING;
static __inline uint64_t CywTimingAdd(uint64_t A, uint64_t B)
{return UINT64_MAX-A<B?UINT64_MAX:A+B;}
static __inline void CywTimingRecord(CYW_TIMING *T,unsigned Bucket,uint64_t Start,uint64_t End)
{
    CYW_TIMING_BUCKET *B;uint64_t Delta,Ten,Hundred;
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
