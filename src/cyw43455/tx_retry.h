/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
/* Worker-owned scheduling policy only: no bus access, credits, packet changes,
 * allocations, or timer-resolution changes. Times are monotonic 100 ns ticks.
 * Eligible is supplied by the caller after checking a live, open TX queue,
 * lifecycle and flow gates, and EXACT credit exhaustion (TxSeq == TxMax).
 * Returned milliseconds request an event wait; Windows may wait longer.
 */
#define CYW_TX_RETRY_FAST_LIMIT 4u
#define CYW_TX_RETRY_EPISODE_100NS 200000ULL

typedef struct {
    unsigned int EpisodeActive,FastAttempts,BackoffLatched;
    unsigned int HaveNow,PendingFastWait,FollowUp;
    unsigned long long EpisodeStart100ns,LastNow100ns;
    unsigned int FastRequests,BackoffRequests,IdleRequests,FastResumes;
    unsigned int FastTimeouts,FastWakes;
    unsigned long long ActualFastWait100ns,MaxFastWait100ns;
} CYW_TX_RETRY;

static __inline void CywTxRetryIncrement(unsigned int *Value)
{
    if(*Value != ~0u)++*Value;
}

/* Accumulated diagnostics deliberately survive all policy resets. */
static __inline void CywTxRetryReset(CYW_TX_RETRY *State)
{
    State->EpisodeActive=0;State->FastAttempts=0;State->BackoffLatched=0;
    State->HaveNow=0;State->PendingFastWait=0;State->FollowUp=0;
    State->EpisodeStart100ns=0;State->LastNow100ns=0;
}

static __inline unsigned int CywTxRetrySelect(CYW_TX_RETRY *State,
    unsigned long long Now100ns,unsigned int TxProgress,unsigned int RxProgress,
    unsigned int Eligible)
{
    unsigned int rollback=State->HaveNow && Now100ns<State->LastNow100ns;
    if(State->FollowUp && TxProgress)CywTxRetryIncrement(&State->FastResumes);
    /* A follow-up describes this next worker iteration only, not a later TX. */
    State->FollowUp=0;State->PendingFastWait=0;
    /* A positive credit window ends exhaustion even if the current iteration
     * only received a credit update. An inactive queue or closed lifecycle /
     * flow gate also ends this episode. RX alone with exhaustion does not. */
    if(TxProgress || !Eligible)CywTxRetryReset(State);
    State->HaveNow=1;State->LastNow100ns=Now100ns;
    if(rollback) {
        State->EpisodeActive=1;State->BackoffLatched=1;
        State->EpisodeStart100ns=Now100ns;
    }
    /* RX progress with unchanged exhaustion neither waits nor replenishes
     * the retry budget. */
    if(TxProgress || RxProgress)return 0;
    if(!Eligible) {
        CywTxRetryIncrement(&State->IdleRequests);return 10;
    }
    if(!State->EpisodeActive) {
        State->EpisodeActive=1;State->EpisodeStart100ns=Now100ns;
    }
    if(State->BackoffLatched || State->FastAttempts>=CYW_TX_RETRY_FAST_LIMIT ||
        Now100ns-State->EpisodeStart100ns>=CYW_TX_RETRY_EPISODE_100NS) {
        State->BackoffLatched=1;
        CywTxRetryIncrement(&State->BackoffRequests);return 10;
    }
    State->FastAttempts++;
    State->PendingFastWait=1;
    CywTxRetryIncrement(&State->FastRequests);return 1;
}

/* Call around the selected event wait. Nonfast waits are intentionally ignored.
 * TimedOut is the wait's STATUS_TIMEOUT result, not an elapsed-time estimate.
 * FastResumes means subsequent TX progress, not proof this wait caused it. */
static __inline void CywTxRetryRecordWait(CYW_TX_RETRY *State,
    unsigned long long Start100ns,unsigned long long End100ns,unsigned int TimedOut)
{
    unsigned long long elapsed;
    if(!State->PendingFastWait)return;
    State->PendingFastWait=0;State->FollowUp=1;
    if(TimedOut)CywTxRetryIncrement(&State->FastTimeouts);
    else CywTxRetryIncrement(&State->FastWakes);
    if(End100ns<Start100ns || (State->HaveNow && Start100ns<State->LastNow100ns)) {
        State->BackoffLatched=1;State->HaveNow=1;State->LastNow100ns=End100ns;
        return;
    }
    elapsed=End100ns-Start100ns;
    if(elapsed>~0ULL-State->ActualFastWait100ns)State->ActualFastWait100ns=~0ULL;
    else State->ActualFastWait100ns+=elapsed;
    if(elapsed>State->MaxFastWait100ns)State->MaxFastWait100ns=elapsed;
    State->HaveNow=1;State->LastNow100ns=End100ns;
    if(State->EpisodeActive &&
        (End100ns<State->EpisodeStart100ns ||
         End100ns-State->EpisodeStart100ns>=CYW_TX_RETRY_EPISODE_100NS))
        State->BackoffLatched=1;
}
