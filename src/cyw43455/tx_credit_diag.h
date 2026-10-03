/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
/* Worker-owned observations only; NEVER used to grant credits or own NBLs.
 * All snapshot words are 64-bit little endian. Counters saturate, not wrap.
 * F1/F2 detailed QPC timing is opt-in; disabled timing is unavailable, not zero.
 * RX credit observations are per completed batch, not per firmware header.
 */
#ifndef RPI5CYW_TX_CREDIT_SCHEDULING
#define RPI5CYW_TX_CREDIT_SCHEDULING 1
#endif
#if RPI5CYW_TX_CREDIT_SCHEDULING != 0 && RPI5CYW_TX_CREDIT_SCHEDULING != 1
#error RPI5CYW_TX_CREDIT_SCHEDULING must be 0 or 1
#endif
#define CYW_TX_DIAG_VERSION 1ULL
typedef unsigned long long CYW_TXD_U64;
typedef struct {
    CYW_TXD_U64 Version,Bytes,SessionQpc,SnapshotQpc,Frequency,WorkerStart;
    CYW_TXD_U64 SchedulingEnabled,DetailedTimingEnabled;
    CYW_TXD_U64 PumpCalls,PumpFrames,PumpRequestedFrames,PumpEmptyStarts;
    CYW_TXD_U64 PumpNoProgress,PumpBudgetHits,PumpPendingEnds,PumpMaxFrames;
    CYW_TXD_U64 QueueEntriesSampleMax,QueueRetainedSampleMax,PumpTicks,PumpMaxTicks;
    CYW_TXD_U64 CreditSamples,CreditZero,CreditInvalid,Credit1To4,Credit5To16,Credit17To64;
    CYW_TXD_U64 GateLifecycleBlocked,GateCreditZero,GateCreditInvalid,GatePriorityBlocked;
    CYW_TXD_U64 F1Calls,F1Errors,F1FlowBusy,F1StatusReads,F1StatusAcks,F1MailboxReads;
    CYW_TXD_U64 F1Ticks,F1MaxTicks,F2Calls,F2Errors,F2PayloadBytes,F2PaddedBytes;
    CYW_TXD_U64 F2Cmd53Writes,F2Ticks,F2MaxTicks,ClockRegressions;
    CYW_TXD_U64 RxBatches,RxWindowChanges,RxCreditReopens,RxWindowGrows;
    CYW_TXD_U64 PostRxCalls,PostRxFrames,PostRxMaxFrames,ExtraPasses,ExtraFrames,ExtraDeadlineYields;
    CYW_TXD_U64 RxEndToPostPumpTicks,RxEndToPostPumpMaxTicks;
} CYW_TX_CREDIT_DIAG;
#define CYW_TX_DIAG_WORDS 58u
static __inline CYW_TXD_U64 CywTxDiagAdd(CYW_TXD_U64 A,CYW_TXD_U64 B)
{return ~0ULL-A<B?~0ULL:A+B;}
static __inline void CywTxDiagInc(CYW_TXD_U64 *Value)
{*Value=CywTxDiagAdd(*Value,1);}
static __inline unsigned CywTxDiagWindow(unsigned char Sequence,unsigned char Maximum)
{return (unsigned char)(Maximum-Sequence);}
static __inline void CywTxDiagCredit(CYW_TX_CREDIT_DIAG *D,unsigned Window)
{
    CywTxDiagInc(&D->CreditSamples);
    if(!Window)CywTxDiagInc(&D->CreditZero);
    else if(Window>64)CywTxDiagInc(&D->CreditInvalid);
    else if(Window<=4)CywTxDiagInc(&D->Credit1To4);
    else if(Window<=16)CywTxDiagInc(&D->Credit5To16);
    else CywTxDiagInc(&D->Credit17To64);
}
static __inline void CywTxDiagDuration(CYW_TX_CREDIT_DIAG *D,
    CYW_TXD_U64 *Total,CYW_TXD_U64 *Maximum,CYW_TXD_U64 Start,CYW_TXD_U64 End)
{
    CYW_TXD_U64 elapsed;
    if(End<Start){CywTxDiagInc(&D->ClockRegressions);return;}
    elapsed=End-Start;*Total=CywTxDiagAdd(*Total,elapsed);
    if(elapsed>*Maximum)*Maximum=elapsed;
}
static __inline void CywTxDiagRx(CYW_TX_CREDIT_DIAG *D,unsigned Before,unsigned After)
{
    CywTxDiagInc(&D->RxBatches);
    if(Before!=After)CywTxDiagInc(&D->RxWindowChanges);
    if(!Before && After && After<=64)CywTxDiagInc(&D->RxCreditReopens);
    if(Before<=64 && After<=64 && After>Before)CywTxDiagInc(&D->RxWindowGrows);
}
/* Expansion is after the adapter definition. Disabled detail makes NO QPC
 * calls in the sender, including in host tests and firmware initialization. */
#define CYW_TXD_CLOCK(A) ((RPI5CYW_DETAILED_TIMING && (A)->Timing.Enabled) ? \
    (CYW_TXD_U64)KeQueryPerformanceCounter(NULL).QuadPart : 0ULL)
