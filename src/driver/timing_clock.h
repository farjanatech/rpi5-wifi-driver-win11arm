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
        T->Snapshot.Frequency=(uint64_t)Frequency.QuadPart;
        T->Snapshot.SessionQpc=(uint64_t)Now.QuadPart;T->Enabled=1;
    }
}
static __inline uint64_t CywTimingBegin(const CYW_TIMING *T)
{return T->Enabled?(uint64_t)KeQueryPerformanceCounter(NULL).QuadPart:0;}
static __inline void CywTimingEnd(CYW_TIMING *T,unsigned Bucket,uint64_t Start)
{
    if(T->Enabled)CywTimingRecord(T,Bucket,Start,CywTimingBegin(T));
}
