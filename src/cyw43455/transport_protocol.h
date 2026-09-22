/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
/* SDPCM interrupt/mailbox definitions follow Linux v6.12 brcmfmac sdio.c
 * (ISC, Broadcom). All state is owned by the one PASSIVE SDIO worker. */
#define CYW_INT_FC_STATE 0x10u
#define CYW_INT_FC_CHANGE 0x20u
#define CYW_INT_FRAME 0x40u
#define CYW_INT_MAIL 0x80u
#define CYW_INT_MASK 0x200000f0u
#define CYW_MAIL_NAK_HANDLED 0x01u
#define CYW_MAIL_READY 0x0au
#define CYW_MAIL_FLOW 0x04u
#define CYW_MAIL_HALT 0x10u
#define CYW_MAIL_KNOWN 0xffff001fu
typedef struct {
    unsigned int GlobalFlow,LastInterrupt,LastMailbox,MailboxVersion,Halted;
    unsigned int PendingReads,StatusReads,StatusAcks,FcChanges,FcRaces,FcStops;
    unsigned int MailReads,MailUnknown,FirmwareHalts;
    unsigned int SequenceValid,SequenceExpected,SequenceLast;
    unsigned int SequenceMismatches,SequenceDuplicates,Frames,EmptyReads;
    unsigned int ServiceErrors,RxAbortFailures,PriorityStops,TxStatusChecks;
    unsigned int PriorityMaskKnown,PriorityMask,PriorityFlow,PriorityBlocked;
    unsigned long long GlobalBlocked100ns,GlobalStarted100ns;
    unsigned long long PriorityBlocked100ns,PriorityStarted100ns;
} CYW_TRANSPORT_STATE;
static __inline unsigned long long CywTransportAddTicks(unsigned long long A,unsigned long long B)
{return ~0ULL-A<B?~0ULL:A+B;}
static __inline unsigned long long CywTransportElapsed(unsigned long long Start,unsigned long long Now)
{return Now>=Start?Now-Start:0;}
static __inline void CywTransportSetGlobal(CYW_TRANSPORT_STATE *T,unsigned int State,unsigned long long Now)
{
    State=State?1u:0u;
    if(State && !T->GlobalFlow) {T->FcStops++;T->GlobalStarted100ns=Now;}
    if(!State && T->GlobalFlow)T->GlobalBlocked100ns=CywTransportAddTicks(T->GlobalBlocked100ns,
        CywTransportElapsed(T->GlobalStarted100ns,Now));
    T->GlobalFlow=State;
}
static __inline void CywTransportSequence(CYW_TRANSPORT_STATE *T,unsigned char Sequence)
{
    if(T->SequenceValid && (unsigned int)Sequence!=T->SequenceExpected) {
        T->SequenceMismatches++;
        if((unsigned int)Sequence==((T->SequenceExpected-1u)&255u))T->SequenceDuplicates++;
    }
    T->SequenceLast=Sequence;T->SequenceExpected=((unsigned int)Sequence+1u)&255u;
    T->SequenceValid=1;T->Frames++;
}
static __inline int CywTransportPriorityAllowed(const CYW_TRANSPORT_STATE *T,unsigned char Flow)
{
    /* Firmware flow bits are precedence, NOT the BCDC priority number. Linux
     * maps priority 0 to precedence 2 by default, and may remap after reading
     * AP WMM parameters. Until a mapping is verified, retain the safe stop-all
     * fallback instead of incorrectly treating bit 0 as best effort. */
    return T->PriorityMaskKnown && T->PriorityMask && T->PriorityMask<=128u &&
        !(T->PriorityMask&(T->PriorityMask-1u)) ? (Flow&T->PriorityMask)==0 : Flow==0;
}
static __inline void CywTransportSetPriority(CYW_TRANSPORT_STATE *T,unsigned char Flow,unsigned long long Now)
{
    unsigned int Blocked=CywTransportPriorityAllowed(T,Flow)?0u:1u;
    if(Blocked && !T->PriorityBlocked) {T->PriorityStops++;T->PriorityStarted100ns=Now;}
    if(!Blocked && T->PriorityBlocked)T->PriorityBlocked100ns=CywTransportAddTicks(T->PriorityBlocked100ns,
        CywTransportElapsed(T->PriorityStarted100ns,Now));
    T->PriorityFlow=Flow;T->PriorityBlocked=Blocked;
}
static __inline unsigned long long CywTransportBlockedTicks(const CYW_TRANSPORT_STATE *T,int Global,unsigned long long Now)
{
    unsigned long long Total=Global?T->GlobalBlocked100ns:T->PriorityBlocked100ns;
    unsigned int Active=Global?T->GlobalFlow:T->PriorityBlocked;
    unsigned long long Start=Global?T->GlobalStarted100ns:T->PriorityStarted100ns;
    return Active?CywTransportAddTicks(Total,CywTransportElapsed(Start,Now)):Total;
}
