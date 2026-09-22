/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
/* Include after kernel types. No clock reads until runtime initialization ends. */
static __inline void CywTimingStart(CYW_TIMING *T)
{
    LARGE_INTEGER Frequency,Now;
    RtlZeroMemory(T,sizeof(*T));
    Now=KeQueryPerformanceCounter(&Frequency);
    T->Snapshot.Version=1;T->Snapshot.BucketCount=CywTimeCount;
    if(Frequency.QuadPart>0) {
        T->Snapshot.Frequency=(CYW_TIMING_U64)Frequency.QuadPart;
        T->Snapshot.SessionQpc=(CYW_TIMING_U64)Now.QuadPart;T->Enabled=1;
    }
}
static __inline CYW_TIMING_U64 CywTimingBegin(const CYW_TIMING *T)
{return T->Enabled?(CYW_TIMING_U64)KeQueryPerformanceCounter(NULL).QuadPart:0;}
static __inline void CywTimingEnd(CYW_TIMING *T,unsigned Bucket,CYW_TIMING_U64 Start)
{
    if(T->Enabled)CywTimingRecord(T,Bucket,Start,CywTimingBegin(T));
}
