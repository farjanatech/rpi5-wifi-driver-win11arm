/* Actual production retry gate; no driver, scheduler, or device execution.
 * Real transport-priority policy and queue types, with a mocked queue lock.
 * Deliberately supplies no SDIO or firmware-operation implementation. */
#include <stdio.h>
#define RPI5CYW_HOST_TEST 1
#include "../src/driver/driver.h"
#include "../src/cyw43455/network_protocol.h"
typedef unsigned long NDIS_STATUS;
typedef int KSPIN_LOCK,KIRQL;
typedef struct TEST_NB *PNET_BUFFER;
typedef struct TEST_NBL *PNET_BUFFER_LIST;
#define NDIS_STATUS_SUCCESS 0UL
#define NDIS_STATUS_PAUSED 0xc023002aUL
#define NDIS_STATUS_MEDIA_DISCONNECTED 0xc000020cUL
#define NDIS_STATUS_LOW_POWER_STATE 0xc023002fUL
#include "../src/cyw43455/tx_types.h"
typedef struct _CYW_NETWORK {
    volatile long Stop,Paused;
    volatile BOOLEAN Ready,Associated,Authorized,Published,SelectingBand;
    CYW_TX_STATE Sends;
    UCHAR TxSeq,TxMax,TxFlow;
} CYW_NETWORK;
static RPI5CYW_ADAPTER GateAdapter,AdapterBefore;
static CYW_NETWORK GateNetwork,NetworkBefore;
static unsigned Failures,Locks,Acquires,Releases;
#define CHECK(x) do {if(!(x)){printf("FAIL line %d: %s\n",__LINE__,#x);++Failures;}}while(0)
static void KeAcquireSpinLock(KSPIN_LOCK *Lock,KIRQL *Irql)
{
    CHECK(Lock==&GateNetwork.Sends.Lock && !*Lock && !Locks);
    *Lock=1;Locks++;Acquires++;*Irql=0;
}
static void KeReleaseSpinLock(KSPIN_LOCK *Lock,KIRQL Irql)
{
    CHECK(Lock==&GateNetwork.Sends.Lock && *Lock==1 && Locks==1 && Irql==0);
    *Lock=0;Locks--;Releases++;
}
#include "../src/cyw43455/tx_retry_gate.h"
#include "../src/cyw43455/tx_pressure_gate.h"
static void InitGate(void)
{
    CHECK(!Locks && Acquires==Releases);
    memset(&GateAdapter,0,sizeof(GateAdapter));memset(&GateNetwork,0,sizeof(GateNetwork));
    GateAdapter.Network=&GateNetwork;
    GateNetwork.Ready=GateNetwork.Associated=GateNetwork.Authorized=GateNetwork.Published=TRUE;
    GateNetwork.Sends.Gate=NDIS_STATUS_SUCCESS;
    GateNetwork.Sends.Count=GateNetwork.Sends.Frames=GateNetwork.Sends.Outstanding=1;
}
static void CheckGate(BOOLEAN Expected,unsigned ExpectedLocks)
{
    unsigned acquired=Acquires,released=Releases;
    memcpy(&AdapterBefore,&GateAdapter,sizeof(AdapterBefore));
    memcpy(&NetworkBefore,&GateNetwork,sizeof(NetworkBefore));
    CHECK(CywTxRetryEligible(&GateAdapter)==Expected);
    CHECK(Acquires-acquired==ExpectedLocks && Releases-released==ExpectedLocks && !Locks);
    /* A scheduling predicate must not alter credits, counters, ownership,
     * cancellation, queue gates, diagnostics, or transport state. */
    CHECK(!memcmp(&AdapterBefore,&GateAdapter,sizeof(AdapterBefore)));
    CHECK(!memcmp(&NetworkBefore,&GateNetwork,sizeof(NetworkBefore)));
}
static void CheckPressure(BOOLEAN Expected,unsigned ExpectedLocks,ULONG Threshold)
{
    unsigned acquired=Acquires,released=Releases;
    memcpy(&AdapterBefore,&GateAdapter,sizeof(AdapterBefore));
    memcpy(&NetworkBefore,&GateNetwork,sizeof(NetworkBefore));
    CHECK(CywTxPressureEligible(&GateAdapter,Threshold)==Expected);
    CHECK(Acquires-acquired==ExpectedLocks && Releases-released==ExpectedLocks && !Locks);
    CHECK(!memcmp(&AdapterBefore,&GateAdapter,sizeof(AdapterBefore)));
    CHECK(!memcmp(&NetworkBefore,&GateNetwork,sizeof(NetworkBefore)));
}
static void InitPressure(void)
{
    InitGate();GateNetwork.Sends.Count=GateNetwork.Sends.Frames=32;
    GateNetwork.TxMax=40;
}
static void PressureGateTests(void)
{
    unsigned sequence,window;
    CHECK(!CywTxPressureEligible(NULL,32));
    InitPressure();GateAdapter.Network=NULL;CheckPressure(FALSE,0,32);
    InitPressure();CheckPressure(TRUE,1,32);
    GateNetwork.Sends.Frames=31;CheckPressure(FALSE,1,32);
    GateNetwork.Sends.Frames=16;CheckPressure(TRUE,1,16);
    GateNetwork.Sends.Frames=15;CheckPressure(FALSE,1,16);
    InitPressure();GateAdapter.IoStopped=1;CheckPressure(FALSE,0,32);
    InitPressure();GateAdapter.FifoTransportFailed=1;CheckPressure(FALSE,0,32);
    InitPressure();GateNetwork.Stop=1;CheckPressure(FALSE,0,32);
    InitPressure();GateNetwork.Paused=1;CheckPressure(FALSE,0,32);
    InitPressure();GateNetwork.SelectingBand=1;CheckPressure(FALSE,0,32);
    InitPressure();GateNetwork.Ready=0;CheckPressure(FALSE,0,32);
    InitPressure();GateNetwork.Associated=0;CheckPressure(FALSE,0,32);
    InitPressure();GateNetwork.Authorized=0;CheckPressure(FALSE,0,32);
    InitPressure();GateNetwork.Published=0;CheckPressure(FALSE,0,32);
    InitPressure();GateAdapter.Transport.Halted=1;CheckPressure(FALSE,0,32);
    InitPressure();GateAdapter.Transport.GlobalFlow=1;CheckPressure(FALSE,0,32);
    InitPressure();GateNetwork.Sends.Gate=NDIS_STATUS_PAUSED;CheckPressure(FALSE,1,32);
    InitPressure();GateNetwork.Sends.Gate=NDIS_STATUS_LOW_POWER_STATE;CheckPressure(FALSE,1,32);
    InitPressure();GateNetwork.Sends.Gate=NDIS_STATUS_MEDIA_DISCONNECTED;CheckPressure(FALSE,1,32);
    InitPressure();GateNetwork.Sends.Count=0;GateNetwork.Sends.Completing=1;CheckPressure(FALSE,1,32);
    InitPressure();GateNetwork.TxFlow=1;CheckPressure(FALSE,0,32);
    InitPressure();GateAdapter.Transport.PriorityMaskKnown=1;GateAdapter.Transport.PriorityMask=4;
    GateNetwork.TxFlow=4;CheckPressure(FALSE,0,32);
    GateNetwork.TxFlow=1;CheckPressure(TRUE,1,32);
    /* Exhaustive modulo-256 credit windows, including zero and invalid stale
     * windows. This hint never widens the sender's legal 1..64 credit window. */
    for(sequence=0;sequence<256;++sequence)for(window=0;window<256;++window) {
        BOOLEAN expected=(BOOLEAN)(window>0 && window<=64);
        InitPressure();GateNetwork.TxSeq=(UCHAR)sequence;
        GateNetwork.TxMax=(UCHAR)(sequence+window);
        CheckPressure(expected,expected?1u:0u,32);
    }
}
int main(void)
{
    unsigned sequence;
    CHECK(!CywTxRetryEligible(NULL) && !Acquires);
    InitGate();GateAdapter.Network=NULL;CheckGate(FALSE,0);
    InitGate();CheckGate(TRUE,1);
    InitGate();GateAdapter.IoStopped=1;CheckGate(FALSE,0);
    InitGate();GateNetwork.Stop=1;CheckGate(FALSE,0);
    InitGate();GateNetwork.Paused=1;CheckGate(FALSE,0);
    InitGate();GateNetwork.SelectingBand=1;CheckGate(FALSE,0);
    InitGate();GateNetwork.Ready=0;CheckGate(FALSE,0);
    InitGate();GateNetwork.Associated=0;CheckGate(FALSE,0);
    InitGate();GateNetwork.Authorized=0;CheckGate(FALSE,0);
    InitGate();GateNetwork.Published=0;CheckGate(FALSE,0);
    InitGate();GateAdapter.Transport.Halted=1;CheckGate(FALSE,0);
    InitGate();GateAdapter.Transport.GlobalFlow=1;CheckGate(FALSE,0);
    InitGate();GateNetwork.Sends.Count=0;CheckGate(FALSE,1);
    InitGate();GateNetwork.Sends.Gate=NDIS_STATUS_PAUSED;CheckGate(FALSE,1);
    InitGate();GateNetwork.Sends.Gate=NDIS_STATUS_MEDIA_DISCONNECTED;CheckGate(FALSE,1);
    InitGate();GateNetwork.Sends.Gate=NDIS_STATUS_LOW_POWER_STATE;CheckGate(FALSE,1);
    /* An NBL being completed is not pending send work, even if the pause
     * ownership count is nonzero. Count, not Outstanding, owns retry eligibility. */
    InitGate();GateNetwork.Sends.Count=0;GateNetwork.Sends.Completing=1;CheckGate(FALSE,1);
    /* All exact sequence equalities, including 255, indicate exhaustion.
     * Positive, wrapped-positive, and invalid backwards windows do not. */
    for(sequence=0;sequence<256;sequence++) {
        InitGate();GateNetwork.TxSeq=(UCHAR)sequence;GateNetwork.TxMax=(UCHAR)sequence;
        CheckGate(TRUE,1);
    }
    InitGate();GateNetwork.TxMax=1;CheckGate(FALSE,0);
    InitGate();GateNetwork.TxSeq=255;GateNetwork.TxMax=0;CheckGate(FALSE,0);
    InitGate();GateNetwork.TxSeq=200;GateNetwork.TxMax=8;CheckGate(FALSE,0); /* 64 credits */
    InitGate();GateNetwork.TxSeq=7;GateNetwork.TxMax=72;CheckGate(FALSE,0); /* invalid 65 */
    InitGate();GateNetwork.TxSeq=32;GateNetwork.TxMax=31;CheckGate(FALSE,0); /* invalid 255 */
    /* A known single precedence mask may ignore unrelated flow bits.
     * Unknown/malformed mapping retains production's conservative stop-all. */
    InitGate();GateAdapter.Transport.PriorityMaskKnown=1;GateAdapter.Transport.PriorityMask=4;
    GateNetwork.TxFlow=4;CheckGate(FALSE,0);
    GateNetwork.TxFlow=1;CheckGate(TRUE,1);
    InitGate();GateNetwork.TxFlow=1;CheckGate(FALSE,0);
    InitGate();GateAdapter.Transport.PriorityMask=4;GateNetwork.TxFlow=4;CheckGate(FALSE,0);
    InitGate();GateAdapter.Transport.PriorityMaskKnown=1;GateNetwork.TxFlow=1;CheckGate(FALSE,0);
    InitGate();GateAdapter.Transport.PriorityMaskKnown=1;GateAdapter.Transport.PriorityMask=3;
    GateNetwork.TxFlow=4;CheckGate(FALSE,0);
    InitGate();GateAdapter.Transport.PriorityMaskKnown=1;GateAdapter.Transport.PriorityMask=256;
    GateNetwork.TxFlow=1;CheckGate(FALSE,0);
    GateNetwork.TxFlow=0;CheckGate(TRUE,1);
    PressureGateTests();
    CHECK(!Locks && Acquires==Releases);
    if(Failures)return 1;
    puts("PASS: actual read-only retry eligibility, all stop/link/queue/flow gates, exact wrapped exhaustion, no fabricated credits or I/O.");
    return 0;
}
