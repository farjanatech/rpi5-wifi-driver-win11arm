/* SPDX-License-Identifier: GPL-3.0-or-later
 * SDPCM/BCDC and WPA2 sequences adapted from Ahmed ARIF, GPL-2.0-or-later,
 * ReactOS cyw43455/fwil.c at 9bb45e56 (Copyright 2026 Ahmed ARIF), and
 * Linux v6.12 brcmfmac (ISC, Broadcom). See THIRD_PARTY_NOTICES.md.
 * One PASSIVE_LEVEL worker owns ALL runtime SDIO traffic. NDIS callbacks and
 * IOCTL dispatch copies credentials; pending NDIS-owned sends stay in a bounded
 * queue until completion. No user-mode pointers are retained.
 */
#include "network.h"
#include "../sdio/sdio.h"
#include "tx_types.h"

#define TRY(x) do { Status=(x); if(!NT_SUCCESS(Status)) goto Exit; } while(0)
#define CYW_IOCTL_CONNECT CTL_CODE(FILE_DEVICE_NETWORK,0x800,METHOD_BUFFERED,FILE_WRITE_DATA)
#define CYW_IOCTL_STATUS CTL_CODE(FILE_DEVICE_NETWORK,0x801,METHOD_BUFFERED,FILE_READ_DATA)
#define CYW_IOCTL_DISCONNECT CTL_CODE(FILE_DEVICE_NETWORK,0x802,METHOD_BUFFERED,FILE_WRITE_DATA)
struct _CYW_NETWORK {
    PRPI5CYW_ADAPTER Adapter;
    KSPIN_LOCK Lock;
    KEVENT Wake, PauseAck, ThreadStarted;
    PVOID Thread;
    volatile LONG Stop, Paused;
    volatile BOOLEAN Ready, Associated, Authorized, Published;
    BOOLEAN RxPending, Powered;
    ULONG Request;
    CYW_CONNECT_REQUEST Connect;
    CYW_TX_STATE Sends;
    UCHAR TxSeq, TxMax, TxFlow;
    USHORT RequestId;
    NDIS_HANDLE RxPool;
    PUCHAR Rx, Tx;
};
static KSPIN_LOCK ControlLock;
static PRPI5CYW_ADAPTER ControlAdapter;
static NDIS_HANDLE ControlHandle;
static PDEVICE_OBJECT ControlDevice;
static VOID CywRefreshTxGate(PRPI5CYW_ADAPTER A);
BOOLEAN CywNetworkCancelled(PRPI5CYW_ADAPTER A)
{return A->IoStopped || (A->Network && A->Network->Stop);}

static VOID CywLink(PRPI5CYW_ADAPTER A, BOOLEAN Up)
{
    NDIS_LINK_STATE Link;
    NDIS_STATUS_INDICATION Indication;
    NDIS_MEDIA_CONNECT_STATE State=Up?MediaConnectStateConnected:MediaConnectStateDisconnected;
    if(A->MediaConnectState==State)return;
    A->MediaConnectState=State;
    CywRefreshTxGate(A);
    A->MediaDuplexState=Up?MediaDuplexStateFull:MediaDuplexStateUnknown;
    A->LinkSpeed=NDIS_LINK_SPEED_UNKNOWN; /* Never invent a negotiated rate. */
    RtlZeroMemory(&Link,sizeof(Link));
    Link.Header.Type=NDIS_OBJECT_TYPE_DEFAULT; Link.Header.Revision=NDIS_LINK_STATE_REVISION_1;
    Link.Header.Size=NDIS_SIZEOF_LINK_STATE_REVISION_1;
    Link.MediaConnectState=State;Link.MediaDuplexState=A->MediaDuplexState;
    Link.XmitLinkSpeed=Link.RcvLinkSpeed=A->LinkSpeed;
    Link.PauseFunctions=NdisPauseFunctionsUnsupported;
    RtlZeroMemory(&Indication,sizeof(Indication));
    Indication.Header.Type=NDIS_OBJECT_TYPE_STATUS_INDICATION;
    Indication.Header.Revision=NDIS_STATUS_INDICATION_REVISION_1;
    Indication.Header.Size=NDIS_SIZEOF_STATUS_INDICATION_REVISION_1;
    Indication.SourceHandle=A->MiniportHandle;
    Indication.StatusCode=NDIS_STATUS_LINK_STATE;
    Indication.StatusBuffer=&Link;Indication.StatusBufferSize=sizeof(Link);
    if(A->Network->Published)NdisMIndicateStatusEx(A->MiniportHandle,&Indication);
}
static VOID CywEvent(PRPI5CYW_ADAPTER A, PUCHAR p, ULONG n)
{
    CYW_NETWORK *N=A->Network;
    ULONG skip,type,status,reason;
    PUCHAR eth,msg;
    if(n<4 || p[0]>>4!=2 || (p[2]&15)!=0)return;
    skip=4+(ULONG)p[3]*4;
    if(skip>n || n-skip<72)return;
    eth=p+skip;
    if(eth[12]!=0x88 || eth[13]!=0x6c || eth[19]!=0 || eth[20]!=0x10 ||
        eth[21]!=0x18 || eth[22]!=0 || eth[23]!=1)return;
    msg=eth+24;
    if(CywBe32(msg+20)>n-skip-72)return;
    type=CywBe32(msg+4);status=CywBe32(msg+8);reason=CywBe32(msg+12);
    A->LinkEvent=type;A->LinkReason=reason;
    if(type==16) {
        N->Associated=(msg[3]&1)!=0 && status==0;
        if(!N->Associated)N->Authorized=FALSE;
    } else if(type==46) {
        N->Authorized=status==6;
    } else if(type==5 || type==6 || type==11 || type==12 || (type==0 && status!=0)) {
        N->Associated=N->Authorized=FALSE;
    }
    CywLink(A,N->Associated && N->Authorized);
    A->NetworkPhase=N->Associated && N->Authorized?600:(N->Associated?520:500);
}
static VOID CywReceive(PRPI5CYW_ADAPTER A, PUCHAR p, ULONG n)
{
    CYW_NETWORK *N=A->Network;
    size_t off,len;
    PMDL Mdl; PNET_BUFFER_LIST Nbl;KIRQL irql;BOOLEAN accept;
    unsigned kind;
    if(!CywEthernetBody(p,n,&off,&len)) {A->RxDropFormat++;return;}
    kind=CywPacketKind(p+off,len);A->PacketRxWire[kind]++;
    if(N->Paused || !N->Published || !N->Authorized || !N->Associated) {
        A->RxDropState++;return;
    }
    KeAcquireSpinLock(&N->Lock,&irql);
    A->RxFilterSnapshot=A->PacketFilter;
    accept=(BOOLEAN)CywAcceptEthernet(p+off,A->CurrentMacAddress,A->PacketFilter,
                                      &A->MulticastList[0][0],A->MulticastCount);
    KeReleaseSpinLock(&N->Lock,irql);
    if(!(p[off]&1) && RtlCompareMemory(p+off,A->CurrentMacAddress,6)!=6)A->RxUnicastOther++;
    if(!accept) {A->RxDropFilter++;return;}
    /* RESOURCES forces synchronous consumption: Pause/Halt cannot race an
     * outstanding return callback or a retained pointer into the RX buffer. */
    Mdl=IoAllocateMdl(p+off,(ULONG)len,FALSE,FALSE,NULL);
    if(!Mdl) {A->RxNoBuffer++;return;}
    MmBuildMdlForNonPagedPool(Mdl);
    Nbl=NdisAllocateNetBufferAndNetBufferList(N->RxPool,0,0,Mdl,0,(ULONG)len);
    if(Nbl) {
        Nbl->SourceHandle=A->MiniportHandle;
        NET_BUFFER_LIST_STATUS(Nbl)=NDIS_STATUS_SUCCESS;
        NET_BUFFER_LIST_NEXT_NBL(Nbl)=NULL;
        NdisMIndicateReceiveNetBufferLists(A->MiniportHandle,Nbl,0,1,NDIS_RECEIVE_FLAGS_RESOURCES);
        A->PacketRxHost[kind]++;
        NdisFreeNetBufferList(Nbl);A->RxPackets++;
    } else A->RxNoBuffer++;
    IoFreeMdl(Mdl);
}
/* STATUS_NO_MORE_ENTRIES is not an I/O error: there is no frame this poll. */
static NTSTATUS CywPoll(PRPI5CYW_ADAPTER A, PULONG Channel, PULONG Offset, PULONG Length)
{
    CYW_NETWORK *N=A->Network;
    UCHAR pending; ULONG ist,mail; uint32_t len,off;
    NTSTATUS Status;
    if(N->Stop)return STATUS_CANCELLED;
    TRY(SdioCmd52Read(A,0,5,&pending));
    if(!N->RxPending && !(pending&6))return STATUS_NO_MORE_ENTRIES;
    TRY(CywBpRead(A,A->SdioCoreBase+0x20,&ist));
    ist&=0x200000f0;
    if(ist)TRY(CywBpWrite(A,A->SdioCoreBase+0x20,ist));
    if(ist&0x80) {
        TRY(CywBpRead(A,A->SdioCoreBase+0x4c,&mail));
        TRY(CywBpWrite(A,A->SdioCoreBase+0x40,2));
        UNREFERENCED_PARAMETER(mail);
    }
    TRY(SdioFifoTransfer(A,N->Rx,64,FALSE));
    if(CywLe32(N->Rx)==0) {N->RxPending=FALSE;return STATUS_NO_MORE_ENTRIES;}
    if(!CywSdpcmHeader(N->Rx,64,&len,&off)) {Status=STATUS_DEVICE_DATA_ERROR;goto Exit;}
    if(len>64)TRY(SdioFifoTransfer(A,N->Rx+64,(len-64+3)&~3UL,FALSE));
    N->RxPending=TRUE;
    if((UCHAR)(N->Rx[9]-N->TxSeq)<=0x40)N->TxMax=N->Rx[9];
    N->TxFlow=N->Rx[8];
    *Channel=N->Rx[5]&15;*Offset=off;*Length=len;
    /* rxglom is explicitly disabled during configuration. Unknown/glom
     * frames fail closed instead of parsing untrusted nested lengths. */
    if(*Channel==3 || (N->Rx[5]&0x80))return STATUS_NOT_SUPPORTED;
    if(*Channel==1)CywEvent(A,N->Rx+off,len-off);
    if(*Channel==2)CywReceive(A,N->Rx+off,len-off);
    return STATUS_SUCCESS;
Exit:
    N->RxPending=FALSE;
    /* Terminate bad FIFO frame. A failed transaction is not silently retried
     * as if the next read were aligned with a fresh SDPCM header. */
    (void)SdioCmd52Write(A,0,6,2,0);
    (void)SdioCmd52Write(A,1,0x1000d,2,0);
    return Status;
}
static NTSTATUS CywSendFrame(PRPI5CYW_ADAPTER A, UCHAR Channel, PUCHAR Data, ULONG Length)
{
    CYW_NETWORK *N=A->Network;
    ULONG total=Length+12, padded=(total+3)&~3UL;
    NTSTATUS Status;
    if(Length>CYW_CONTROL_CAPACITY-12)return STATUS_INVALID_BUFFER_SIZE;
    if(!CywTxCredit(N->TxSeq,N->TxMax,Channel==2?N->TxFlow:0))return STATUS_DEVICE_BUSY;
    RtlZeroMemory(N->Tx,padded);
    CywPut16(N->Tx,(USHORT)total);CywPut16(N->Tx+2,(USHORT)~total);
    N->Tx[4]=N->TxSeq;N->Tx[5]=Channel;N->Tx[7]=12;
    RtlCopyMemory(N->Tx+12,Data,Length);
    Status=SdioFifoTransfer(A,N->Tx,padded,TRUE);
    RtlSecureZeroMemory(N->Tx,padded);
    if(NT_SUCCESS(Status))N->TxSeq++;
    return Status;
}
static BOOLEAN CywTxCanTransfer(PRPI5CYW_ADAPTER A)
{
    CYW_NETWORK *N=A->Network;
    A->TxCreditSequence=N->TxSeq;A->TxCreditMaximum=N->TxMax;A->TxFlowMask=N->TxFlow;
    return !A->IoStopped && !N->Stop && !N->Paused && N->Ready &&
        N->Authorized && N->Associated && CywTxCredit(N->TxSeq,N->TxMax,N->TxFlow);
}
static NTSTATUS CywTxTransfer(PRPI5CYW_ADAPTER A,PUCHAR Data,ULONG Length)
{
    NTSTATUS Status=CywSendFrame(A,2,Data,Length);
    /* Transfer success is not proof that the AP received/acknowledged a frame. */
    if(NT_SUCCESS(Status) && Length>=4)A->PacketTx[CywPacketKind(Data+4,Length-4)]++;
    return Status;
}
#include "tx_queue.h"
static VOID CywRefreshTxGate(PRPI5CYW_ADAPTER A)
{
    CYW_NETWORK *N=A->Network;KIRQL irql;
    KeAcquireSpinLock(&N->Sends.Lock,&irql);
    N->Sends.Gate=(A->IoStopped || N->Stop)?NDIS_STATUS_LOW_POWER_STATE:
        N->Paused?NDIS_STATUS_PAUSED:
        (!N->Ready || !N->Associated || !N->Authorized)?NDIS_STATUS_MEDIA_DISCONNECTED:
        NDIS_STATUS_SUCCESS;
    KeReleaseSpinLock(&N->Sends.Lock,irql);
}
#include "control.h"
static NTSTATUS CywInt(PRPI5CYW_ADAPTER A,const char *Name,ULONG Value)
{UCHAR b[4];CywPut32(b,Value);return CywIovar(A,Name,TRUE,b,4);}
static NTSTATUS CywCmdInt(PRPI5CYW_ADAPTER A,ULONG Command,ULONG Value)
{UCHAR b[4];CywPut32(b,Value);return CywFirmwareCommand(A,Command,TRUE,b,4);}
static NTSTATUS CywConfigure(PRPI5CYW_ADAPTER A)
{
    PUCHAR clm=NULL;ULONG size,off,n;UCHAR chunk[460],events[16]={0},mac[6]={0};
    NTSTATUS Status;
    TRY(CywReadFirmwareFile(L"\\SystemRoot\\System32\\drivers\\rpi5cyw\\cyfmac43455-sdio.clm_blob",&clm,&size,65536));
    for(off=0;off<size;off+=n) {
        n=size-off;if(n>448)n=448;
        RtlZeroMemory(chunk,sizeof(chunk));
        CywPut16(chunk,(USHORT)(0x1000|(off==0?2:0)|(off+n==size?4:0)));
        CywPut16(chunk+2,2);CywPut32(chunk+4,n);RtlCopyMemory(chunk+12,clm+off,n);
        TRY(CywIovar(A,"clmload",TRUE,chunk,n+12));
    }
    TRY(CywCmdInt(A,3,0)); /* radio DOWN until user supplies a country */
    TRY(CywInt(A,"bus:txglom",0));TRY(CywInt(A,"bus:rxglom",0));
    TRY(CywInt(A,"mpc",0));TRY(CywCmdInt(A,86,0));
    TRY(CywInt(A,"allmulti",1)); /* software applies NDIS multicast filters */
    TRY(CywIovar(A,"cur_etheraddr",TRUE,A->CurrentMacAddress,6));
    /* Read-only evidence: distinguish an accepted SET from matching readback.
     * An unavailable GET is recorded, not used to rewrite or invent a MAC. */
    A->MacReadbackStatus=CywIovar(A,"cur_etheraddr",FALSE,mac,sizeof(mac));
    A->MacReadbackMatches=NT_SUCCESS(A->MacReadbackStatus) &&
        RtlCompareMemory(mac,A->CurrentMacAddress,6)==6?1:0;
    events[0]=(1<<0)|(1<<5)|(1<<6);events[1]=(1<<3)|(1<<4);
    events[2]=1;events[5]=1<<6;
    TRY(CywIovar(A,"event_msgs",TRUE,events,sizeof(events)));
    A->NetworkPhase=500;
Exit: if(clm)ExFreePoolWithTag(clm,RPI5CYW_TAG);return Status;
}
#include "connection.h"
static VOID CywWorker(PVOID Context)
{
    PRPI5CYW_ADAPTER A=Context;CYW_NETWORK *N=A->Network;
    CYW_CONNECT_REQUEST request;
    KIRQL irql;ULONG op,channel,off,len,i,lastPhase=0,sentBefore,sentAfter;
    ULONGLONG nextSnapshot=0, rxStart;
    LARGE_INTEGER wait;NTSTATUS Status;
    N->Thread=PsGetCurrentThread();ObReferenceObject(N->Thread);
    KeSetEvent(&N->ThreadStarted,0,FALSE);
    wait.QuadPart=-100000; /* 10 ms polling, no DISPATCH_LEVEL busy wait */
    /* Firmware upload/readback can take tens of seconds at the conservative
     * clock. Never block MiniportInitializeEx on that work. */
    Status=CywFirmwareStart(A);
    if(NT_SUCCESS(Status) && !N->Stop)Status=CywConfigure(A);
    if(!NT_SUCCESS(Status))goto Failed;
    if(N->Stop)goto Exit;
    N->Ready=TRUE;A->NetworkStatus=STATUS_SUCCESS;
    CywRefreshTxGate(A);
    Rpi5CywWriteDiagnostics(A,120,STATUS_SUCCESS);
    while(!N->Stop) {
        if(N->Paused) {
            CywTxFlush(A,&N->Sends,NDIS_STATUS_PAUSED);
            KeSetEvent(&N->PauseAck,0,FALSE);
            KeWaitForSingleObject(&N->Wake,Executive,KernelMode,FALSE,&wait);continue;
        }
        KeClearEvent(&N->PauseAck);
        KeAcquireSpinLock(&N->Lock,&irql);op=N->Request;N->Request=0;
        RtlCopyMemory(&request,&N->Connect,sizeof(request));RtlSecureZeroMemory(&N->Connect,sizeof(request));
        KeReleaseSpinLock(&N->Lock,irql);
        if(op) {
            CywTxFlush(A,&N->Sends,NDIS_STATUS_MEDIA_DISCONNECTED);
            Status=op==1?CywConnect(A,&request):CywCmdInt(A,3,0);
            RtlSecureZeroMemory(&request,sizeof(request));
            if(op==2 || !NT_SUCCESS(Status)) {
                N->Associated=N->Authorized=FALSE;CywLink(A,FALSE);
                if(op==2 && NT_SUCCESS(Status))A->NetworkPhase=500;
            }
            A->NetworkStatus=Status;Rpi5CywWriteDiagnostics(A,120,Status);
            CywRefreshTxGate(A);
        }
        Status=CywTxPump(A,&N->Sends,4,&sentBefore);
        if(!NT_SUCCESS(Status))goto Failed;
        rxStart=KeQueryInterruptTime();
        for(i=0;CywReceiveBudget(i,KeQueryInterruptTime()-rxStart) && !N->Stop && !N->Paused;++i) {
            Status=CywPoll(A,&channel,&off,&len);
            if(Status==STATUS_NO_MORE_ENTRIES)break;
            if(!NT_SUCCESS(Status))goto Failed;
        }
        if(i && !CywReceiveBudget(i,KeQueryInterruptTime()-rxStart))A->RxBatchYields++;
        if(A->NetworkPhase!=lastPhase || KeQueryInterruptTime()>=nextSnapshot) {
            Rpi5CywWriteDiagnostics(A,120,A->NetworkStatus);
            lastPhase=A->NetworkPhase;nextSnapshot=KeQueryInterruptTime()+300000000ULL;
        }
        Status=CywTxPump(A,&N->Sends,4,&sentAfter);
        if(!NT_SUCCESS(Status))goto Failed;
        if(!i && !sentBefore && !sentAfter)
            KeWaitForSingleObject(&N->Wake,Executive,KernelMode,FALSE,&wait);
    }
    goto Exit;
Failed:
    A->NetworkStatus=Status;N->Ready=FALSE;
    N->Associated=N->Authorized=FALSE;CywLink(A,FALSE);
    Rpi5CywWriteDiagnostics(A,120,Status);
    CywFirmwareStop(A);
Exit:
    RtlSecureZeroMemory(&request,sizeof(request));
    N->Ready=FALSE;
    CywTxFlush(A,&N->Sends,A->IoStopped?NDIS_STATUS_LOW_POWER_STATE:NDIS_STATUS_MEDIA_DISCONNECTED);
    KeSetEvent(&N->PauseAck,0,FALSE);
    PsTerminateSystemThread(STATUS_SUCCESS);
}
NTSTATUS CywNetworkInitialize(PRPI5CYW_ADAPTER A)
{
    CYW_NETWORK *N;NET_BUFFER_LIST_POOL_PARAMETERS Pool;NTSTATUS Status;
    OBJECT_ATTRIBUTES Attr;KIRQL irql;HANDLE threadHandle;
    N=ExAllocatePool2(POOL_FLAG_NON_PAGED,sizeof(*N),RPI5CYW_TAG);
    if(!N)return STATUS_INSUFFICIENT_RESOURCES;
    A->Network=N;N->Adapter=A;N->TxMax=1;N->Paused=1;N->Powered=TRUE;
    KeInitializeSpinLock(&N->Sends.Lock);N->Sends.Gate=NDIS_STATUS_PAUSED;
    KeInitializeSpinLock(&N->Lock);KeInitializeEvent(&N->Wake,SynchronizationEvent,FALSE);
    KeInitializeEvent(&N->PauseAck,NotificationEvent,TRUE);
    KeInitializeEvent(&N->ThreadStarted,NotificationEvent,FALSE);
    N->Rx=ExAllocatePool2(POOL_FLAG_NON_PAGED,CYW_WIRE_CAPACITY,RPI5CYW_TAG);
    N->Tx=ExAllocatePool2(POOL_FLAG_NON_PAGED,CYW_CONTROL_CAPACITY,RPI5CYW_TAG);
    if(!N->Rx || !N->Tx) {Status=STATUS_INSUFFICIENT_RESOURCES;goto Exit;}
    RtlZeroMemory(&Pool,sizeof(Pool));Pool.Header.Type=NDIS_OBJECT_TYPE_DEFAULT;
    Pool.Header.Revision=NET_BUFFER_LIST_POOL_PARAMETERS_REVISION_1;
    Pool.Header.Size=NDIS_SIZEOF_NET_BUFFER_LIST_POOL_PARAMETERS_REVISION_1;
    Pool.ProtocolId=NDIS_PROTOCOL_ID_DEFAULT;Pool.fAllocateNetBuffer=TRUE;Pool.PoolTag=RPI5CYW_TAG;
    N->RxPool=NdisAllocateNetBufferListPool(A->MiniportHandle,&Pool);
    if(!N->RxPool) {Status=STATUS_INSUFFICIENT_RESOURCES;goto Exit;}
    InitializeObjectAttributes(&Attr,NULL,OBJ_KERNEL_HANDLE,NULL,NULL);
    TRY(PsCreateSystemThread(&threadHandle,THREAD_ALL_ACCESS,&Attr,NULL,NULL,CywWorker,A));
    KeWaitForSingleObject(&N->ThreadStarted,Executive,KernelMode,FALSE,NULL);
    ZwClose(threadHandle);
    KeAcquireSpinLock(&ControlLock,&irql);
    if(ControlAdapter)Status=STATUS_DEVICE_BUSY;else ControlAdapter=A;
    KeReleaseSpinLock(&ControlLock,irql);
Exit:
    A->NetworkStatus=Status;
    if(!NT_SUCCESS(Status))CywNetworkStop(A);
    return Status;
}
VOID CywNetworkStop(PRPI5CYW_ADAPTER A)
{
    CYW_NETWORK *N=A->Network;KIRQL irql;
    if(!N)return;
    KeAcquireSpinLock(&ControlLock,&irql);if(ControlAdapter==A)ControlAdapter=NULL;
    KeReleaseSpinLock(&ControlLock,irql);
    InterlockedExchange(&N->Stop,1);KeSetEvent(&N->Wake,0,FALSE);
    CywRefreshTxGate(A);
    if(N->Thread) {
        KeWaitForSingleObject(N->Thread,Executive,KernelMode,FALSE,NULL);
        ObDereferenceObject(N->Thread);
    }
    CywTxFlush(A,&N->Sends,NDIS_STATUS_MEDIA_DISCONNECTED);
    if(N->Powered)CywFirmwareStop(A);
    if(N->RxPool)NdisFreeNetBufferListPool(N->RxPool);
    if(N->Rx)ExFreePoolWithTag(N->Rx,RPI5CYW_TAG);
    if(N->Tx) {RtlSecureZeroMemory(N->Tx,CYW_CONTROL_CAPACITY);ExFreePoolWithTag(N->Tx,RPI5CYW_TAG);}
    RtlSecureZeroMemory(N,sizeof(*N));ExFreePoolWithTag(N,RPI5CYW_TAG);A->Network=NULL;
}
VOID CywNetworkPause(PRPI5CYW_ADAPTER A,BOOLEAN Paused)
{
    CYW_NETWORK *N=A->Network;
    A->NdisPaused=Paused;
    if(!N)return;
    if(Paused)KeClearEvent(&N->PauseAck);
    InterlockedExchange(&N->Paused,Paused);N->Published=TRUE;KeSetEvent(&N->Wake,0,FALSE);
    CywRefreshTxGate(A);
    if(Paused && (N->Ready || CywTxOutstanding(&N->Sends)))
        KeWaitForSingleObject(&N->PauseAck,Executive,KernelMode,FALSE,NULL);
}
/* Called on an NDIS work item. Keep the network allocation alive in D3 so
 * concurrent rejected sends never race freed memory. No SDIO access in D3. */
NTSTATUS CywNetworkPower(PRPI5CYW_ADAPTER A,BOOLEAN On)
{
    CYW_NETWORK *N=A->Network;KIRQL irql;OBJECT_ATTRIBUTES attr;
    HANDLE handle;NTSTATUS status;
    if(!N)return STATUS_DEVICE_NOT_READY;
    if(!On) {
        N->Ready=FALSE;InterlockedExchange(&N->Stop,1);KeSetEvent(&N->Wake,0,FALSE);
        CywRefreshTxGate(A);
        if(N->Thread) {
            KeWaitForSingleObject(N->Thread,Executive,KernelMode,FALSE,NULL);
            ObDereferenceObject(N->Thread);N->Thread=NULL;
        }
        CywTxFlush(A,&N->Sends,NDIS_STATUS_LOW_POWER_STATE);
        if(N->Powered)CywFirmwareStop(A);
        N->Powered=FALSE;A->IoStopped=1;
        N->Associated=N->Authorized=FALSE;CywLink(A,FALSE);
        KeAcquireSpinLock(&N->Lock,&irql);N->Request=0;
        RtlSecureZeroMemory(&N->Connect,sizeof(N->Connect));KeReleaseSpinLock(&N->Lock,irql);
        return STATUS_SUCCESS;
    }
    if(N->Thread)return STATUS_SUCCESS;
    A->IoStopped=0;N->Stop=0;N->Ready=FALSE;N->Powered=TRUE;
    N->Paused=(LONG)A->NdisPaused;N->TxSeq=0;N->TxMax=1;N->TxFlow=0;N->RxPending=FALSE;
    status=Rpi5CywDirectSdioProbe(A);
    if(!NT_SUCCESS(status))return status;
    KeClearEvent(&N->ThreadStarted);
    InitializeObjectAttributes(&attr,NULL,OBJ_KERNEL_HANDLE,NULL,NULL);
    status=PsCreateSystemThread(&handle,THREAD_ALL_ACCESS,&attr,NULL,NULL,CywWorker,A);
    if(NT_SUCCESS(status)) {
        KeWaitForSingleObject(&N->ThreadStarted,Executive,KernelMode,FALSE,NULL);ZwClose(handle);
    }
    return status;
}
VOID CywNetworkShutdown(PRPI5CYW_ADAPTER A)
{
    InterlockedExchange(&A->IoStopped,1);
    if(A->Network)InterlockedExchange(&A->Network->Stop,1);
    /* Shutdown may run at HIGH_LEVEL: no waiting, allocation, or SDIO calls. */
}
NDIS_STATUS CywNetworkSend(PRPI5CYW_ADAPTER A,PNET_BUFFER_LIST Nbl)
{
    CYW_NETWORK *N=A->Network;NDIS_STATUS status;
    if(A->IoStopped)return NDIS_STATUS_LOW_POWER_STATE;
    if(!N)return NDIS_STATUS_MEDIA_DISCONNECTED;
    status=CywTxSubmit(A,&N->Sends,Nbl);
    if(status==NDIS_STATUS_PENDING)KeSetEvent(&N->Wake,0,FALSE);
    return status;
}
VOID CywNetworkCancelSend(PRPI5CYW_ADAPTER A,PVOID CancelId)
{
    CYW_NETWORK *N=A->Network;if(!N)return;
    CywTxCancel(&N->Sends,CancelId);KeSetEvent(&N->Wake,0,FALSE);
}
VOID CywNetworkSetFilter(PRPI5CYW_ADAPTER A,ULONG Filter)
{
    KIRQL irql;KeAcquireSpinLock(&A->Network->Lock,&irql);
    A->PacketFilter=Filter;KeReleaseSpinLock(&A->Network->Lock,irql);
}
VOID CywNetworkSetMulticast(PRPI5CYW_ADAPTER A,PUCHAR List,ULONG Length)
{
    KIRQL irql;KeAcquireSpinLock(&A->Network->Lock,&irql);
    A->MulticastCount=Length/6;RtlCopyMemory(A->MulticastList,List,Length);
    KeReleaseSpinLock(&A->Network->Lock,irql);
}
static NTSTATUS CywDispatch(PDEVICE_OBJECT Device,PIRP Irp)
{
    PIO_STACK_LOCATION Stack=IoGetCurrentIrpStackLocation(Irp);
    NTSTATUS Status=STATUS_INVALID_DEVICE_REQUEST;ULONG bytes=0,code;
    PRPI5CYW_ADAPTER A;CYW_NETWORK *N;KIRQL irql;
    UNREFERENCED_PARAMETER(Device);
    if(Stack->MajorFunction==IRP_MJ_CREATE || Stack->MajorFunction==IRP_MJ_CLOSE ||
        Stack->MajorFunction==IRP_MJ_CLEANUP)Status=STATUS_SUCCESS;
    else if(Stack->MajorFunction==IRP_MJ_DEVICE_CONTROL) {
        code=Stack->Parameters.DeviceIoControl.IoControlCode;
        KeAcquireSpinLock(&ControlLock,&irql);A=ControlAdapter;N=A?A->Network:NULL;
        if(!N)Status=STATUS_DEVICE_NOT_READY;
        else if(code==CYW_IOCTL_STATUS && Stack->Parameters.DeviceIoControl.OutputBufferLength>=32) {
            ULONG *out=Irp->AssociatedIrp.SystemBuffer;
            out[0]=1;out[1]=A->NetworkPhase;out[2]=(ULONG)A->NetworkStatus;
            out[3]=A->FirmwareCommand;out[4]=A->FirmwareError;out[5]=A->LinkEvent;
            out[6]=A->LinkReason;out[7]=A->MediaConnectState==MediaConnectStateConnected;
            bytes=32;Status=STATUS_SUCCESS;
            /* Keep the original 32-byte ABI usable by older utilities. */
            if(Stack->Parameters.DeviceIoControl.OutputBufferLength>=48) {
                out[0]=2;out[8]=A->ConnectStep;out[9]=A->CountryRequested;
                out[10]=A->CountryApplied;out[11]=A->CountryRevision;bytes=48;
            }
            if(Stack->Parameters.DeviceIoControl.OutputBufferLength>=96) {
                out[0]=3;out[12]=A->FirmwareTotalBytes;out[13]=A->FirmwareUploadedBytes;
                out[14]=A->FirmwareBytes;out[15]=A->RamTransferAddress;
                out[16]=A->RamTransferLength;out[17]=A->RamTransferWrite;
                out[18]=(ULONG)A->RamTransferStatus;out[19]=A->RamTransferStage;
                out[20]=A->LastCommand;out[21]=A->LastArgument;
                out[22]=A->LastResponse;out[23]=A->LastInterruptStatus;bytes=96;
            }
        } else if((code==CYW_IOCTL_CONNECT && Stack->Parameters.DeviceIoControl.InputBufferLength==sizeof(CYW_CONNECT_REQUEST) &&
                    CywValidConnect(Irp->AssociatedIrp.SystemBuffer)) ||
                  (code==CYW_IOCTL_DISCONNECT && Stack->Parameters.DeviceIoControl.InputBufferLength==0)) {
            KeAcquireSpinLockAtDpcLevel(&N->Lock);
            if(!N->Ready)Status=STATUS_DEVICE_NOT_READY;
            else if(N->Request)Status=STATUS_DEVICE_BUSY;
            else {
                RtlSecureZeroMemory(&N->Connect,sizeof(N->Connect));
                if(code==CYW_IOCTL_CONNECT)RtlCopyMemory(&N->Connect,Irp->AssociatedIrp.SystemBuffer,sizeof(N->Connect));
                N->Request=code==CYW_IOCTL_CONNECT?1:2;KeSetEvent(&N->Wake,0,FALSE);Status=STATUS_SUCCESS;
            }
            KeReleaseSpinLockFromDpcLevel(&N->Lock);
        } else Status=STATUS_INVALID_PARAMETER;
        KeReleaseSpinLock(&ControlLock,irql);
        if(code==CYW_IOCTL_CONNECT && Irp->AssociatedIrp.SystemBuffer)
            RtlSecureZeroMemory(Irp->AssociatedIrp.SystemBuffer,Stack->Parameters.DeviceIoControl.InputBufferLength);
    }
    Irp->IoStatus.Status=Status;Irp->IoStatus.Information=bytes;IoCompleteRequest(Irp,IO_NO_INCREMENT);return Status;
}
NTSTATUS CywControlRegister(NDIS_HANDLE DriverHandle)
{
    UNICODE_STRING name=RTL_CONSTANT_STRING(L"\\Device\\Rpi5CywControl");
    UNICODE_STRING link=RTL_CONSTANT_STRING(L"\\DosDevices\\Rpi5CywControl");
    UNICODE_STRING sddl=RTL_CONSTANT_STRING(L"D:P(A;;GA;;;SY)(A;;GA;;;BA)");
    PDRIVER_DISPATCH dispatch[IRP_MJ_MAXIMUM_FUNCTION+1]={0};NDIS_DEVICE_OBJECT_ATTRIBUTES attr;
    KeInitializeSpinLock(&ControlLock);
    dispatch[IRP_MJ_CREATE]=dispatch[IRP_MJ_CLOSE]=dispatch[IRP_MJ_CLEANUP]=dispatch[IRP_MJ_DEVICE_CONTROL]=CywDispatch;
    RtlZeroMemory(&attr,sizeof(attr));attr.Header.Type=NDIS_OBJECT_TYPE_DEVICE_OBJECT_ATTRIBUTES;
    attr.Header.Revision=NDIS_DEVICE_OBJECT_ATTRIBUTES_REVISION_1;attr.Header.Size=sizeof(attr);
    attr.DeviceName=&name;attr.SymbolicName=&link;attr.DefaultSDDLString=&sddl;attr.MajorFunctions=dispatch;
    return (NTSTATUS)NdisRegisterDeviceEx(DriverHandle,&attr,&ControlDevice,&ControlHandle);
}
VOID CywControlDeregister(VOID)
{if(ControlHandle) {NdisDeregisterDeviceEx(ControlHandle);ControlHandle=NULL;}}
