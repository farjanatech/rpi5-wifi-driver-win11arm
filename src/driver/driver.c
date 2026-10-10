#include "driver.h"
#include "../sdio/sdio.h"
#include "../cyw43455/network.h"
#include "statistics_flags.h"
#include "interrupt_policy.h"

static NDIS_HANDLE gRpi5CywDriverHandle;

MINIPORT_ISR Rpi5CywInterrupt;
MINIPORT_INTERRUPT_DPC Rpi5CywInterruptDpc;
MINIPORT_DISABLE_INTERRUPT Rpi5CywDisableInterrupt;
MINIPORT_ENABLE_INTERRUPT Rpi5CywEnableInterrupt;
MINIPORT_SYNCHRONIZE_INTERRUPT Rpi5CywInterruptRearmSync;
MINIPORT_SYNCHRONIZE_INTERRUPT Rpi5CywInterruptQuiesceSync;

static __forceinline ULONG
Rpi5CywInterruptRead32(PRPI5CYW_ADAPTER Adapter, ULONG Offset)
{
    return READ_REGISTER_ULONG((PULONG)((PUCHAR)Adapter->RegisterBase + Offset));
}

static __forceinline VOID
Rpi5CywInterruptWrite32(PRPI5CYW_ADAPTER Adapter, ULONG Offset, ULONG Value)
{
    WRITE_REGISTER_ULONG((PULONG)((PUCHAR)Adapter->RegisterBase + Offset), Value);
}

/*
 * Linux SDHCI masks SDIO CARD_INT in both Interrupt Status Enable and
 * Interrupt Signal Enable while the SDIO core services the function IRQ.
 * Do the same here. CARD_INT is level-like and is not dismissed by a blind
 * write-one-to-clear; the card/function source must be serviced first.
 */
static VOID
Rpi5CywSetCardInterruptEnable(PRPI5CYW_ADAPTER Adapter, BOOLEAN Enable)
{
    ULONG statusEnable, signalEnable;
    if (!Adapter || !Adapter->RegisterBase) return;

    statusEnable = Rpi5CywInterruptRead32(Adapter, SDHCI_INT_STATUS_ENABLE);
    signalEnable = Rpi5CywInterruptRead32(Adapter, SDHCI_INT_SIGNAL_ENABLE);
    if (Enable)
    {
        statusEnable |= SDHCI_INT_CARD_INT;
        signalEnable |= SDHCI_INT_CARD_INT;
    }
    else
    {
        statusEnable &= ~SDHCI_INT_CARD_INT;
        signalEnable &= ~SDHCI_INT_CARD_INT;
    }

    Rpi5CywInterruptWrite32(Adapter, SDHCI_INT_STATUS_ENABLE, statusEnable);
    Rpi5CywInterruptWrite32(Adapter, SDHCI_INT_SIGNAL_ENABLE, signalEnable);
    KeMemoryBarrier();
    Adapter->InterruptStatusEnable = statusEnable;
    Adapter->InterruptSignalEnable = signalEnable;
}

_Use_decl_annotations_
BOOLEAN
Rpi5CywInterrupt(
    NDIS_HANDLE MiniportInterruptContext,
    PBOOLEAN QueueDefaultInterruptDpc,
    PULONG TargetProcessors
    )
{
    PRPI5CYW_ADAPTER Adapter = (PRPI5CYW_ADAPTER)MiniportInterruptContext;
    ULONG status;

    *QueueDefaultInterruptDpc = FALSE;
    *TargetProcessors = 0;
    if (!Adapter || !Adapter->RegisterBase) return FALSE;

    status = Rpi5CywInterruptRead32(Adapter, SDHCI_INT_STATUS);
    if ((status & SDHCI_INT_CARD_INT) == 0)
    {
        InterlockedIncrement((volatile LONG *)&Adapter->InterruptSpuriousCount);
        return FALSE;
    }

    /*
     * Mask the complete SDIO CARD_INT path before queuing work. Do not clear
     * CARD_INT in INT_STATUS here: the SDIO function source is serviced by the
     * one PASSIVE bus owner, then the worker explicitly rearms the host.
     */
    Rpi5CywSetCardInterruptEnable(Adapter, FALSE);
    InterlockedExchange(&Adapter->InterruptNeedsRearm, 1);
    InterlockedIncrement((volatile LONG *)&Adapter->InterruptIsrCount);

    if (Adapter->InterruptStormFallback)
        return TRUE; /* Polling owns recovery; never queue an IRQ storm. */

    *QueueDefaultInterruptDpc = TRUE;
    return TRUE;
}

_Use_decl_annotations_
VOID
Rpi5CywInterruptDpc(
    NDIS_HANDLE MiniportInterruptContext,
    PVOID MiniportDpcContext,
    PVOID ReceiveThrottleParameters,
    PVOID NdisReserved2
    )
{
    PRPI5CYW_ADAPTER Adapter = (PRPI5CYW_ADAPTER)MiniportInterruptContext;
    UNREFERENCED_PARAMETER(MiniportDpcContext);
    UNREFERENCED_PARAMETER(ReceiveThrottleParameters);
    UNREFERENCED_PARAMETER(NdisReserved2);
    if (!Adapter) return;

    InterlockedIncrement((volatile LONG *)&Adapter->InterruptDpcCount);
    InterlockedExchange(&Adapter->InterruptWakePending, 1);
    if (!Adapter->IoStopped) CywNetworkWake(Adapter);
}

_Use_decl_annotations_
BOOLEAN
Rpi5CywInterruptRearmSync(NDIS_HANDLE SynchronizeContext)
{
    PRPI5CYW_ADAPTER Adapter = (PRPI5CYW_ADAPTER)SynchronizeContext;
    if (!Adapter || !Adapter->RegisterBase || Adapter->IoStopped ||
        !Adapter->Network || Adapter->InterruptStormFallback)
        return FALSE;

    Rpi5CywSetCardInterruptEnable(Adapter, TRUE);
    InterlockedExchange(&Adapter->InterruptNeedsRearm, 0);
    InterlockedIncrement((volatile LONG *)&Adapter->InterruptRearmCount);
    return TRUE;
}

_Use_decl_annotations_
BOOLEAN
Rpi5CywInterruptQuiesceSync(NDIS_HANDLE SynchronizeContext)
{
    PRPI5CYW_ADAPTER Adapter = (PRPI5CYW_ADAPTER)SynchronizeContext;
    if (!Adapter || !Adapter->RegisterBase) return FALSE;

    Rpi5CywSetCardInterruptEnable(Adapter, FALSE);
    InterlockedExchange(&Adapter->InterruptNeedsRearm, 1);
    return TRUE;
}

_Use_decl_annotations_
VOID
Rpi5CywDisableInterrupt(NDIS_HANDLE MiniportInterruptContext)
{
    PRPI5CYW_ADAPTER Adapter = (PRPI5CYW_ADAPTER)MiniportInterruptContext;
    if (!Adapter) return;
    InterlockedIncrement((volatile LONG *)&Adapter->InterruptNdisDisableCalls);
    (VOID)Rpi5CywInterruptQuiesceSync(Adapter);
}

_Use_decl_annotations_
VOID
Rpi5CywEnableInterrupt(NDIS_HANDLE MiniportInterruptContext)
{
    PRPI5CYW_ADAPTER Adapter = (PRPI5CYW_ADAPTER)MiniportInterruptContext;
    if (!Adapter) return;

    /*
     * NDIS can call this callback for diagnostic/troubleshooting control.
     * Never bypass the passive service/INTx check by re-enabling at DIRQL.
     */
    InterlockedIncrement((volatile LONG *)&Adapter->InterruptNdisEnableCalls);
    InterlockedExchange(&Adapter->InterruptNeedsRearm, 1);
    if (Adapter->Network && !Adapter->IoStopped)
        CywNetworkWake(Adapter);
}

BOOLEAN
Rpi5CywInterruptConsumeWake(PRPI5CYW_ADAPTER Adapter)
{
    if (!Adapter) return FALSE;
    return (BOOLEAN)(InterlockedExchange(&Adapter->InterruptWakePending, 0) != 0);
}

VOID
Rpi5CywInterruptResetRuntime(PRPI5CYW_ADAPTER Adapter)
{
    if (!Adapter) return;
    Adapter->InterruptStormFallback = 0;
    Adapter->InterruptEmptyWakeStreak = 0;
    InterlockedExchange(&Adapter->InterruptWakePending, 0);
    InterlockedExchange(&Adapter->InterruptNeedsRearm, 1);
}

VOID
Rpi5CywInterruptRearm(
    PRPI5CYW_ADAPTER Adapter,
    BOOLEAN InterruptWake,
    BOOLEAN UsefulWork
    )
{
    UCHAR pending = 0;
    ULONG functions, nextStreak;
    NTSTATUS status;

    if (!Adapter || !Adapter->InterruptRegistered || !Adapter->InterruptHandle ||
        Adapter->IoStopped || !Adapter->Network || Adapter->InterruptStormFallback ||
        InterlockedCompareExchange(&Adapter->InterruptNeedsRearm, 0, 0) == 0)
        return;
    if (KeGetCurrentIrql() != PASSIVE_LEVEL)
        return;

    /*
     * CCCR INTx is the card-level truth for Function 1/2 pending state. This
     * CMD52 is performed only by the existing PASSIVE bus owner.
     */
    status = SdioCmd52Read(Adapter, 0, CYW_SDIO_CCCR_INT_PENDING, &pending);
    Adapter->InterruptPendingReads++;
    if (!NT_SUCCESS(status))
    {
        Adapter->InterruptPendingReadFailures++;
        if (!Adapter->InterruptStormFallback)
            Adapter->InterruptStormFallbackCount++;
        Adapter->InterruptStormFallback = 1;
        Adapter->InterruptEmptyWakeStreak = 0;
        return; /* Stay masked and use the proven polling path. */
    }

    functions = CywInterruptPendingFunctions(pending);
    if (functions & CYW_INTERRUPT_PENDING_F1) Adapter->InterruptPendingF1++;
    if (functions & CYW_INTERRUPT_PENDING_F2) Adapter->InterruptPendingF2++;
    if (!functions) Adapter->InterruptPendingEmpty++;

    if (InterruptWake)
    {
        if (UsefulWork)
            Adapter->InterruptUsefulWakeCount++;
        else if (!functions)
            Adapter->InterruptEmptyWakeCount++;
    }

    nextStreak = CywInterruptNextEmptyWakeStreak(
        InterruptWake ? 1u : 0u, UsefulWork ? 1u : 0u, pending,
        Adapter->InterruptEmptyWakeStreak);
    Adapter->InterruptEmptyWakeStreak = nextStreak;

    if (CywInterruptUsePollingFallback(nextStreak))
    {
        if (!Adapter->InterruptStormFallback)
            Adapter->InterruptStormFallbackCount++;
        Adapter->InterruptStormFallback = 1;
        return; /* ISR already masked both host CARD_INT enable registers. */
    }

    if (functions)
    {
        /*
         * Function interrupt is still asserted. Keep host CARD_INT masked.
         * The worker's bounded poll loop will service it and try again.
         */
        Adapter->InterruptRearmDeferred++;
        Adapter->InterruptEmptyWakeStreak = 0;
        return;
    }

    (VOID)NdisMSynchronizeWithInterruptEx(
        Adapter->InterruptHandle, 0, Rpi5CywInterruptRearmSync, Adapter);
}

VOID
Rpi5CywInterruptQuiesce(PRPI5CYW_ADAPTER Adapter)
{
    if (!Adapter || !Adapter->InterruptRegistered || !Adapter->InterruptHandle)
        return;
    (VOID)NdisMSynchronizeWithInterruptEx(
        Adapter->InterruptHandle, 0, Rpi5CywInterruptQuiesceSync, Adapter);
}

static VOID
Rpi5CywRegisterInterrupt(PRPI5CYW_ADAPTER Adapter)
{
    NDIS_MINIPORT_INTERRUPT_CHARACTERISTICS Characteristics;
    NDIS_STATUS status;

    Adapter->InterruptRegisterStatus = STATUS_NOT_SUPPORTED;
    Adapter->InterruptRegistered = 0;
    Adapter->InterruptType = 0;
    Adapter->InterruptHandle = NULL;
    Rpi5CywInterruptResetRuntime(Adapter);

    if (!Adapter->InterruptResourceCount) return;

    RtlZeroMemory(&Characteristics, sizeof(Characteristics));
    Characteristics.Header.Type = NDIS_OBJECT_TYPE_MINIPORT_INTERRUPT;
    Characteristics.Header.Revision = NDIS_MINIPORT_INTERRUPT_REVISION_1;
    Characteristics.Header.Size = NDIS_SIZEOF_MINIPORT_INTERRUPT_CHARACTERISTICS_REVISION_1;
    Characteristics.InterruptHandler = Rpi5CywInterrupt;
    Characteristics.InterruptDpcHandler = Rpi5CywInterruptDpc;
    Characteristics.DisableInterruptHandler = Rpi5CywDisableInterrupt;
    Characteristics.EnableInterruptHandler = Rpi5CywEnableInterrupt;
    Characteristics.MsiSupported = FALSE;
    Characteristics.MsiSyncWithAllMessages = FALSE;

    status = NdisMRegisterInterruptEx(
        Adapter->MiniportHandle, Adapter, &Characteristics, &Adapter->InterruptHandle);
    Adapter->InterruptRegisterStatus = (NTSTATUS)status;
    if (status == NDIS_STATUS_SUCCESS)
    {
        Adapter->InterruptRegistered = 1;
        Adapter->InterruptType = (ULONG)Characteristics.InterruptType;

        /* Probe leaves SIGNAL_ENABLE at zero, but also mask CARD_INT in the
         * status-enable register until the PASSIVE worker is fully ready. */
        Rpi5CywSetCardInterruptEnable(Adapter, FALSE);
    }
    else
    {
        Adapter->InterruptHandle = NULL;
    }
}

static VOID
Rpi5CywDeregisterInterrupt(PRPI5CYW_ADAPTER Adapter)
{
    NDIS_HANDLE handle;
    if (!Adapter || !Adapter->InterruptRegistered || !Adapter->InterruptHandle)
        return;
    Rpi5CywInterruptQuiesce(Adapter);
    handle = Adapter->InterruptHandle;
    NdisMDeregisterInterruptEx(handle);
    Adapter->InterruptHandle = NULL;
    Adapter->InterruptRegistered = 0;
}


static const NDIS_OID gRpi5CywSupportedOids[] =
{
    OID_GEN_SUPPORTED_LIST,
    OID_GEN_INTERRUPT_MODERATION,
    OID_PNP_QUERY_POWER,
    OID_PNP_SET_POWER,
    OID_GEN_HARDWARE_STATUS,
    OID_GEN_MEDIA_SUPPORTED,
    OID_GEN_MEDIA_IN_USE,
    OID_GEN_MAXIMUM_LOOKAHEAD,
    OID_GEN_MAXIMUM_FRAME_SIZE,
    OID_GEN_LINK_SPEED,
    OID_GEN_TRANSMIT_BLOCK_SIZE,
    OID_GEN_RECEIVE_BLOCK_SIZE,
    OID_GEN_VENDOR_ID,
    OID_GEN_VENDOR_DESCRIPTION,
    OID_GEN_CURRENT_PACKET_FILTER,
    OID_GEN_CURRENT_LOOKAHEAD,
    OID_GEN_DRIVER_VERSION,
    OID_GEN_MAXIMUM_TOTAL_SIZE,
    OID_GEN_MAC_OPTIONS,
    OID_GEN_MEDIA_CONNECT_STATUS,
    OID_GEN_VENDOR_DRIVER_VERSION,
    OID_GEN_PHYSICAL_MEDIUM,
    OID_GEN_LINK_STATE,
    OID_GEN_STATISTICS,
    OID_GEN_BYTES_RCV,
    OID_GEN_BYTES_XMIT,
    OID_GEN_RCV_DISCARDS,
    OID_GEN_XMIT_DISCARDS,
    OID_GEN_XMIT_OK,
    OID_GEN_RCV_OK,
    OID_GEN_XMIT_ERROR,
    OID_GEN_RCV_ERROR,
    OID_GEN_RCV_NO_BUFFER,
    OID_802_3_PERMANENT_ADDRESS,
    OID_802_3_CURRENT_ADDRESS,
    OID_802_3_MULTICAST_LIST,
    OID_802_3_MAXIMUM_LIST_SIZE
};

static NDIS_STATUS
Rpi5CywCopyQuery(
    _Inout_ PNDIS_OID_REQUEST OidRequest,
    _In_reads_bytes_(Length) const VOID *Source,
    _In_ ULONG Length
    )
{
    PVOID Buffer = OidRequest->DATA.QUERY_INFORMATION.InformationBuffer;
    UINT BufferLength = OidRequest->DATA.QUERY_INFORMATION.InformationBufferLength;

    OidRequest->DATA.QUERY_INFORMATION.BytesWritten = 0;
    OidRequest->DATA.QUERY_INFORMATION.BytesNeeded = 0;

    if (BufferLength < Length)
    {
        OidRequest->DATA.QUERY_INFORMATION.BytesNeeded = Length;
        return NDIS_STATUS_BUFFER_TOO_SHORT;
    }

    RtlCopyMemory(Buffer, Source, Length);
    OidRequest->DATA.QUERY_INFORMATION.BytesWritten = Length;
    return NDIS_STATUS_SUCCESS;
}

#include "statistics_ndis.h"

#include "diag_snapshot.h"
#include "diag_values.h"
/* Serializes complete exports from startup/power and the diagnostic thread.
 * Only writers wait on this mutex; runtime capture never acquires it. */
static KMUTEX CywDiagnosticWriteMutex;

VOID Rpi5CywCaptureDiagnostics(PRPI5CYW_ADAPTER Adapter,ULONG Stage,
    NTSTATUS Status,CYW_DIAG_BUFFER *Buffer)
{
    CywCaptureDiagnosticValues(Adapter,Stage,Status,Buffer);
}

VOID Rpi5CywWriteDiagnosticBuffer(const CYW_DIAG_BUFFER *Buffer)
{
    OBJECT_ATTRIBUTES attributes;UNICODE_STRING keyName,name;
    HANDLE key;ULONG disposition,offset=0;CYW_DIAG_RECORD record;const VOID *data;
    if(Buffer->Failed || !Buffer->Used || KeGetCurrentIrql()!=PASSIVE_LEVEL)return;
    /* Validate the whole batch before the first write. */
    while(offset<Buffer->Used) {
        if(!CywDiagRead(Buffer,offset,&record,&name,&data))return;
        offset+=record.TotalBytes;
    }
    KeWaitForSingleObject(&CywDiagnosticWriteMutex,Executive,KernelMode,FALSE,NULL);
    RtlInitUnicodeString(&keyName,L"\\Registry\\Machine\\SOFTWARE\\Rpi5CywDirectDiag");
    InitializeObjectAttributes(&attributes,&keyName,OBJ_CASE_INSENSITIVE|OBJ_KERNEL_HANDLE,NULL,NULL);
    if(NT_SUCCESS(ZwCreateKey(&key,KEY_SET_VALUE,&attributes,0,NULL,
        REG_OPTION_NON_VOLATILE,&disposition))) {
        offset=0;
        while(CywDiagRead(Buffer,offset,&record,&name,&data)) {
            (VOID)ZwSetValueKey(key,&name,0,record.Type,(PVOID)data,record.DataBytes);
            offset+=record.TotalBytes;
        }
        ZwClose(key);
    }
    KeReleaseMutex(&CywDiagnosticWriteMutex,FALSE);
}

VOID Rpi5CywWriteDiagnostics(PRPI5CYW_ADAPTER Adapter,ULONG Stage,NTSTATUS Status)
{
    CYW_DIAG_BUFFER *buffer;
    if(!Adapter || KeGetCurrentIrql()!=PASSIVE_LEVEL)return;
    /* Startup/power only. Runtime uses its preallocated mailbox instead. */
    Adapter->DiagStage=Stage;Adapter->ProbeStatus=Status;
    if((Stage>=20 && Stage<=90) || (Stage>=200 && Stage<=350))Adapter->ProbePhase=Stage;
    buffer=ExAllocatePool2(POOL_FLAG_NON_PAGED,sizeof(*buffer),RPI5CYW_TAG);
    if(!buffer)return;
    CywDiagReset(buffer);Rpi5CywCaptureDiagnostics(Adapter,Stage,Status,buffer);
    Rpi5CywWriteDiagnosticBuffer(buffer);ExFreePoolWithTag(buffer,RPI5CYW_TAG);
}

static NDIS_STATUS
Rpi5CywSetRegistrationAttributes(
    _In_ PRPI5CYW_ADAPTER Adapter
    )
{
    NDIS_MINIPORT_ADAPTER_REGISTRATION_ATTRIBUTES Registration;

    RtlZeroMemory(&Registration, sizeof(Registration));
    Registration.Header.Type = NDIS_OBJECT_TYPE_MINIPORT_ADAPTER_REGISTRATION_ATTRIBUTES;
    Registration.Header.Revision = NDIS_MINIPORT_ADAPTER_REGISTRATION_ATTRIBUTES_REVISION_1;
    Registration.Header.Size = NDIS_SIZEOF_MINIPORT_ADAPTER_REGISTRATION_ATTRIBUTES_REVISION_1;
    Registration.MiniportAdapterContext = Adapter;
    Registration.AttributeFlags = NDIS_MINIPORT_ATTRIBUTES_HARDWARE_DEVICE |
                                  NDIS_MINIPORT_ATTRIBUTES_SURPRISE_REMOVE_OK;
    Registration.CheckForHangTimeInSeconds = 4;
    Registration.InterfaceType = NdisInterfaceInternal;

    return NdisMSetMiniportAttributes(
        Adapter->MiniportHandle,
        (PNDIS_MINIPORT_ADAPTER_ATTRIBUTES)&Registration);
}

static NDIS_STATUS
Rpi5CywSetGeneralAttributes(
    _In_ PRPI5CYW_ADAPTER Adapter
    )
{
    NDIS_MINIPORT_ADAPTER_GENERAL_ATTRIBUTES General;

    RtlZeroMemory(&General, sizeof(General));
    General.Header.Type = NDIS_OBJECT_TYPE_MINIPORT_ADAPTER_GENERAL_ATTRIBUTES;
    General.Header.Revision = NDIS_MINIPORT_ADAPTER_GENERAL_ATTRIBUTES_REVISION_1;
    General.Header.Size = NDIS_SIZEOF_MINIPORT_ADAPTER_GENERAL_ATTRIBUTES_REVISION_1;
    General.MediaType = NdisMedium802_3;
    General.PhysicalMediumType = NdisPhysicalMedium802_3;
    General.MtuSize = RPI5CYW_MTU;
    General.MaxXmitLinkSpeed = RPI5CYW_MAX_LINK_SPEED;
    General.MaxRcvLinkSpeed = RPI5CYW_MAX_LINK_SPEED;
    General.XmitLinkSpeed = Adapter->LinkSpeed;
    General.RcvLinkSpeed = Adapter->LinkSpeed;
    General.MediaConnectState = Adapter->MediaConnectState;
    General.MediaDuplexState = Adapter->MediaDuplexState;
    General.LookaheadSize = Adapter->Lookahead;
    General.MacOptions = RPI5CYW_MAC_OPTIONS;
    General.SupportedPacketFilters = RPI5CYW_SUPPORTED_FILTERS;
    General.MaxMulticastListSize = RPI5CYW_MAX_MULTICAST;
    General.MacAddressLength = ETH_LENGTH_OF_ADDRESS;
    RtlCopyMemory(General.PermanentMacAddress,
                  Adapter->PermanentMacAddress,
                  ETH_LENGTH_OF_ADDRESS);
    RtlCopyMemory(General.CurrentMacAddress,
                  Adapter->CurrentMacAddress,
                  ETH_LENGTH_OF_ADDRESS);
    General.AccessType = NET_IF_ACCESS_BROADCAST;
    General.DirectionType = NET_IF_DIRECTION_SENDRECEIVE;
    General.ConnectionType = NET_IF_CONNECTION_DEDICATED;
    General.IfType = IF_TYPE_ETHERNET_CSMACD;
    General.IfConnectorPresent = FALSE;
    General.SupportedPauseFunctions = NdisPauseFunctionsUnsupported;
    General.SupportedOidList = (PNDIS_OID)gRpi5CywSupportedOids;
    General.SupportedOidListLength = sizeof(gRpi5CywSupportedOids);
    General.SupportedStatistics =
        CYW_STATISTICS_SUPPORTED |
        NDIS_STATISTICS_XMIT_OK_SUPPORTED |
        NDIS_STATISTICS_RCV_OK_SUPPORTED |
        NDIS_STATISTICS_XMIT_ERROR_SUPPORTED |
        NDIS_STATISTICS_RCV_ERROR_SUPPORTED |
        NDIS_STATISTICS_RCV_NO_BUFFER_SUPPORTED;

    return NdisMSetMiniportAttributes(
        Adapter->MiniportHandle,
        (PNDIS_MINIPORT_ADAPTER_ATTRIBUTES)&General);
}

static NDIS_STATUS
Rpi5CywMapResources(
    _Inout_ PRPI5CYW_ADAPTER Adapter,
    _In_opt_ PNDIS_RESOURCE_LIST ResourceList
    )
{
    PCM_PARTIAL_RESOURCE_DESCRIPTOR Descriptor;
    ULONG Index;
    NDIS_STATUS Status;

    if (ResourceList == NULL || ResourceList->Count == 0)
    {
        return NDIS_STATUS_RESOURCES;
    }

    Adapter->ResourceCount = ResourceList->Count;
    Adapter->ResourceTypes = 0;
    Descriptor = &ResourceList->PartialDescriptors[0];

    for (Index = 0; Index < ResourceList->Count; Index++)
    {
        if (Index < 4)
        {
            Adapter->ResourceTypes |= ((ULONG)Descriptor[Index].Type & 0xFFUL) << (Index * 8);
        }

        if (Descriptor[Index].Type == CmResourceTypeInterrupt)
        {
            Adapter->InterruptResourceCount++;
            if (Adapter->InterruptResourceCount == 1)
            {
                Adapter->InterruptResourceFlags = Descriptor[Index].Flags;
                Adapter->InterruptVector = Descriptor[Index].u.Interrupt.Vector;
                Adapter->InterruptLevel = Descriptor[Index].u.Interrupt.Level;
            }
        }

        if (Descriptor[Index].Type == CmResourceTypeMemory && Adapter->RegisterBase == NULL)
        {
            Adapter->RegisterPhysical = Descriptor[Index].u.Memory.Start;
            Adapter->RegisterLength = Descriptor[Index].u.Memory.Length;
            if (Adapter->RegisterPhysical.HighPart != 0x10 ||
                Adapter->RegisterPhysical.LowPart != 0x01100000 ||
                Adapter->RegisterLength < 0x100 || Adapter->RegisterLength > 0x1000)
                return NDIS_STATUS_RESOURCES;
            Status = NdisMMapIoSpace(&Adapter->RegisterBase,
                                     Adapter->MiniportHandle,
                                     Adapter->RegisterPhysical,
                                     Adapter->RegisterLength);
            if (Status != NDIS_STATUS_SUCCESS)
            {
                Adapter->RegisterBase = NULL;
                return Status;
            }
        }
    }

    return Adapter->RegisterBase != NULL ? NDIS_STATUS_SUCCESS : NDIS_STATUS_RESOURCES;
}

static VOID
Rpi5CywUnmapResources(
    _Inout_ PRPI5CYW_ADAPTER Adapter
    )
{
    if (Adapter->RegisterBase != NULL)
    {
        NdisMUnmapIoSpace(Adapter->MiniportHandle,
                          Adapter->RegisterBase,
                          Adapter->RegisterLength);
        Adapter->RegisterBase = NULL;
        Adapter->RegisterLength = 0;
    }
}

static NDIS_STATUS
Rpi5CywQueryInformation(
    _In_ PRPI5CYW_ADAPTER Adapter,
    _Inout_ PNDIS_OID_REQUEST OidRequest
    )
{
    NDIS_OID Oid = OidRequest->DATA.QUERY_INFORMATION.Oid;
    union
    {
        ULONG Ulong;
        USHORT Ushort;
        ULONG64 Ulong64;
        NDIS_MEDIUM Medium;
        NDIS_HARDWARE_STATUS HardwareStatus;
        NDIS_MEDIA_STATE MediaState;
        NDIS_PHYSICAL_MEDIUM PhysicalMedium;
        NDIS_LINK_STATE LinkState;
        UCHAR Mac[ETH_LENGTH_OF_ADDRESS];
    } Data;

    RtlZeroMemory(&Data, sizeof(Data));

    switch (Oid)
    {
        case OID_GEN_STATISTICS:
        {
            CYW_TRAFFIC_STATS s;NDIS_STATISTICS_INFO v;
            CywTrafficSnapshot(Adapter,&s);CywNdisStatistics(&s,&v);
            return Rpi5CywCopyQuery(OidRequest,&v,sizeof(v));
        }
        case OID_GEN_BYTES_RCV:
        case OID_GEN_BYTES_XMIT:
        case OID_GEN_RCV_DISCARDS:
        case OID_GEN_XMIT_DISCARDS:
        {
            CYW_TRAFFIC_STATS s;CywTrafficSnapshot(Adapter,&s);
            Data.Ulong64=Oid==OID_GEN_BYTES_RCV?CywTrafficTotal(s.Bytes[0]):
                Oid==OID_GEN_BYTES_XMIT?CywTrafficTotal(s.Bytes[1]):
                Oid==OID_GEN_RCV_DISCARDS?s.Discards[0]:s.Discards[1];
            return Rpi5CywCopyQuery(OidRequest,&Data.Ulong64,sizeof(Data.Ulong64));
        }
        case OID_GEN_SUPPORTED_LIST:
            return Rpi5CywCopyQuery(OidRequest,
                                    gRpi5CywSupportedOids,
                                    sizeof(gRpi5CywSupportedOids));

        case OID_GEN_INTERRUPT_MODERATION:
        {
            NDIS_INTERRUPT_MODERATION_PARAMETERS Parameters;
            RtlZeroMemory(&Parameters, sizeof(Parameters));
            Parameters.Header.Type = NDIS_OBJECT_TYPE_DEFAULT;
            Parameters.Header.Revision = NDIS_INTERRUPT_MODERATION_PARAMETERS_REVISION_1;
            Parameters.Header.Size = NDIS_SIZEOF_INTERRUPT_MODERATION_PARAMETERS_REVISION_1;
            Parameters.InterruptModeration = NdisInterruptModerationNotSupported;
            return Rpi5CywCopyQuery(OidRequest, &Parameters, (ULONG)sizeof(Parameters));
        }

        case OID_GEN_HARDWARE_STATUS:
            Data.HardwareStatus = NdisHardwareStatusReady;
            return Rpi5CywCopyQuery(OidRequest, &Data.HardwareStatus, sizeof(Data.HardwareStatus));

        case OID_PNP_QUERY_POWER:
            return NDIS_STATUS_SUCCESS;

        case OID_GEN_MEDIA_SUPPORTED:
        case OID_GEN_MEDIA_IN_USE:
            Data.Medium = NdisMedium802_3;
            return Rpi5CywCopyQuery(OidRequest, &Data.Medium, sizeof(Data.Medium));

        case OID_GEN_PHYSICAL_MEDIUM:
            Data.PhysicalMedium = NdisPhysicalMedium802_3;
            return Rpi5CywCopyQuery(OidRequest, &Data.PhysicalMedium, sizeof(Data.PhysicalMedium));

        case OID_GEN_MAXIMUM_LOOKAHEAD:
        case OID_GEN_MAXIMUM_FRAME_SIZE:
            Data.Ulong = RPI5CYW_MTU;
            return Rpi5CywCopyQuery(OidRequest, &Data.Ulong, sizeof(Data.Ulong));

        case OID_GEN_CURRENT_LOOKAHEAD:
            Data.Ulong = Adapter->Lookahead;
            return Rpi5CywCopyQuery(OidRequest, &Data.Ulong, sizeof(Data.Ulong));

        case OID_GEN_MAXIMUM_TOTAL_SIZE:
        case OID_GEN_TRANSMIT_BLOCK_SIZE:
        case OID_GEN_RECEIVE_BLOCK_SIZE:
            Data.Ulong = RPI5CYW_FRAME_SIZE;
            return Rpi5CywCopyQuery(OidRequest, &Data.Ulong, sizeof(Data.Ulong));

        case OID_GEN_LINK_SPEED:
            Data.Ulong = (ULONG)(Adapter->LinkSpeed / 100ULL);
            return Rpi5CywCopyQuery(OidRequest, &Data.Ulong, sizeof(Data.Ulong));

        case OID_GEN_VENDOR_ID:
            Data.Ulong = ((ULONG)Adapter->PermanentMacAddress[0] << 16) |
                         ((ULONG)Adapter->PermanentMacAddress[1] << 8) |
                         Adapter->PermanentMacAddress[2];
            return Rpi5CywCopyQuery(OidRequest, &Data.Ulong, sizeof(Data.Ulong));

        case OID_GEN_VENDOR_DESCRIPTION:
        {
            static const CHAR Description[] = "Raspberry Pi 5 CYW43455 Direct SDIO Ethernet";
            return Rpi5CywCopyQuery(OidRequest, Description, sizeof(Description));
        }

        case OID_GEN_DRIVER_VERSION:
            Data.Ushort = RPI5CYW_DRIVER_VERSION;
            return Rpi5CywCopyQuery(OidRequest, &Data.Ushort, sizeof(Data.Ushort));

        case OID_GEN_VENDOR_DRIVER_VERSION:
            Data.Ulong = 0x00070104;
            return Rpi5CywCopyQuery(OidRequest, &Data.Ulong, sizeof(Data.Ulong));

        case OID_GEN_CURRENT_PACKET_FILTER:
            Data.Ulong = Adapter->PacketFilter;
            return Rpi5CywCopyQuery(OidRequest, &Data.Ulong, sizeof(Data.Ulong));

        case OID_GEN_MAC_OPTIONS:
            Data.Ulong = RPI5CYW_MAC_OPTIONS;
            return Rpi5CywCopyQuery(OidRequest, &Data.Ulong, sizeof(Data.Ulong));

        case OID_GEN_MEDIA_CONNECT_STATUS:
            Data.MediaState = Adapter->MediaConnectState == MediaConnectStateConnected ?
                              NdisMediaStateConnected : NdisMediaStateDisconnected;
            return Rpi5CywCopyQuery(OidRequest, &Data.MediaState, sizeof(Data.MediaState));

        case OID_GEN_LINK_STATE:
            Data.LinkState.Header.Type = NDIS_OBJECT_TYPE_DEFAULT;
            Data.LinkState.Header.Revision = NDIS_LINK_STATE_REVISION_1;
            Data.LinkState.Header.Size = NDIS_SIZEOF_LINK_STATE_REVISION_1;
            Data.LinkState.MediaConnectState = Adapter->MediaConnectState;
            Data.LinkState.MediaDuplexState = Adapter->MediaDuplexState;
            Data.LinkState.XmitLinkSpeed = Adapter->LinkSpeed;
            Data.LinkState.RcvLinkSpeed = Adapter->LinkSpeed;
            Data.LinkState.PauseFunctions = NdisPauseFunctionsUnsupported;
            return Rpi5CywCopyQuery(OidRequest, &Data.LinkState, sizeof(Data.LinkState));

        case OID_GEN_XMIT_OK:
            Data.Ulong64 = Adapter->TxPackets;
            return Rpi5CywCopyQuery(OidRequest, &Data.Ulong64, sizeof(Data.Ulong64));

        case OID_GEN_RCV_OK:
            Data.Ulong64 = Adapter->RxPackets;
            return Rpi5CywCopyQuery(OidRequest, &Data.Ulong64, sizeof(Data.Ulong64));

        case OID_GEN_XMIT_ERROR:
        case OID_GEN_RCV_ERROR:
        {
            CYW_TRAFFIC_STATS s;CywTrafficSnapshot(Adapter,&s);
            Data.Ulong64=s.Errors[Oid==OID_GEN_XMIT_ERROR?1:0];
            return Rpi5CywCopyQuery(OidRequest, &Data.Ulong64, sizeof(Data.Ulong64));
        }

        case OID_GEN_RCV_NO_BUFFER:
            Data.Ulong64 = Adapter->RxNoBuffer;
            return Rpi5CywCopyQuery(OidRequest, &Data.Ulong64, sizeof(Data.Ulong64));

        case OID_802_3_PERMANENT_ADDRESS:
            RtlCopyMemory(Data.Mac, Adapter->PermanentMacAddress, ETH_LENGTH_OF_ADDRESS);
            return Rpi5CywCopyQuery(OidRequest, Data.Mac, ETH_LENGTH_OF_ADDRESS);

        case OID_802_3_CURRENT_ADDRESS:
            RtlCopyMemory(Data.Mac, Adapter->CurrentMacAddress, ETH_LENGTH_OF_ADDRESS);
            return Rpi5CywCopyQuery(OidRequest, Data.Mac, ETH_LENGTH_OF_ADDRESS);

        case OID_802_3_MULTICAST_LIST:
            return Rpi5CywCopyQuery(OidRequest,
                                    Adapter->MulticastList,
                                    Adapter->MulticastCount * ETH_LENGTH_OF_ADDRESS);

        case OID_802_3_MAXIMUM_LIST_SIZE:
            Data.Ulong = RPI5CYW_MAX_MULTICAST;
            return Rpi5CywCopyQuery(OidRequest, &Data.Ulong, sizeof(Data.Ulong));

        default:
            return NDIS_STATUS_NOT_SUPPORTED;
    }
}

typedef struct _CYW_POWER_WORK {
    PRPI5CYW_ADAPTER Adapter;
    PNDIS_OID_REQUEST Request;
    NDIS_DEVICE_POWER_STATE State;
} CYW_POWER_WORK;

static VOID CywPowerWork(PVOID Context, NDIS_HANDLE WorkItem)
{
    CYW_POWER_WORK *Work = Context;
    PRPI5CYW_ADAPTER Adapter = Work->Adapter;
    PNDIS_OID_REQUEST Request = Work->Request;
    NDIS_HANDLE Miniport = Adapter->MiniportHandle;
    NTSTATUS Status;
    Adapter->PowerTransitionCount++;
    Adapter->LastPowerState=(ULONG)Work->State;
    Adapter->LastPowerTransition100ns=KeQueryInterruptTime();
    if(Work->State==NdisDeviceStateD0)Adapter->PowerD0Count++;
    else if(Work->State==NdisDeviceStateD1)Adapter->PowerD1Count++;
    else if(Work->State==NdisDeviceStateD2)Adapter->PowerD2Count++;
    else if(Work->State==NdisDeviceStateD3)Adapter->PowerD3Count++;
    Status=CywNetworkPower(Adapter, Work->State == NdisDeviceStateD0);
    Adapter->NetworkStatus = Status;
    Rpi5CywWriteDiagnostics(Adapter, 120, Status);
    Request->DATA.SET_INFORMATION.BytesRead = sizeof(NDIS_DEVICE_POWER_STATE);
    ExFreePoolWithTag(Work, RPI5CYW_TAG);
    NdisFreeIoWorkItem(WorkItem);
    /* A failed resume is reported through disconnected media + diagnostics,
     * not by blocking the system's power transition. */
    NdisMOidRequestComplete(Miniport, Request, NDIS_STATUS_SUCCESS);
}

static NDIS_STATUS
Rpi5CywSetInformation(
    _Inout_ PRPI5CYW_ADAPTER Adapter,
    _Inout_ PNDIS_OID_REQUEST OidRequest
    )
{
    NDIS_OID Oid = OidRequest->DATA.SET_INFORMATION.Oid;
    PVOID Buffer = OidRequest->DATA.SET_INFORMATION.InformationBuffer;
    UINT BufferLength = OidRequest->DATA.SET_INFORMATION.InformationBufferLength;

    OidRequest->DATA.SET_INFORMATION.BytesRead = 0;
    OidRequest->DATA.SET_INFORMATION.BytesNeeded = 0;

    switch (Oid)
    {
        case OID_GEN_INTERRUPT_MODERATION:
        {
            ULONG required = (ULONG)sizeof(NDIS_INTERRUPT_MODERATION_PARAMETERS);
            if (BufferLength < required)
            {
                OidRequest->DATA.SET_INFORMATION.BytesNeeded = required;
                return NDIS_STATUS_INVALID_LENGTH;
            }
            OidRequest->DATA.SET_INFORMATION.BytesRead = required;
            return NDIS_STATUS_INVALID_DATA;
        }

        case OID_PNP_SET_POWER:
        {
            CYW_POWER_WORK *Work;
            NDIS_HANDLE Item;
            NDIS_DEVICE_POWER_STATE State;
            if (BufferLength != sizeof(State)) return NDIS_STATUS_INVALID_LENGTH;
            RtlCopyMemory(&State, Buffer, sizeof(State));
            if (State < NdisDeviceStateD0 || State > NdisDeviceStateD3)
                return NDIS_STATUS_INVALID_DATA;
            Work = ExAllocatePool2(POOL_FLAG_NON_PAGED, sizeof(*Work), RPI5CYW_TAG);
            if (!Work) return NDIS_STATUS_RESOURCES;
            Item = NdisAllocateIoWorkItem(Adapter->MiniportHandle);
            if (!Item) { ExFreePoolWithTag(Work, RPI5CYW_TAG); return NDIS_STATUS_RESOURCES; }
            Work->Adapter = Adapter; Work->Request = OidRequest; Work->State = State;
            NdisQueueIoWorkItem(Item, CywPowerWork, Work);
            return NDIS_STATUS_PENDING;
        }
        case OID_GEN_CURRENT_PACKET_FILTER:
            if (BufferLength < sizeof(ULONG))
            {
                OidRequest->DATA.SET_INFORMATION.BytesNeeded = sizeof(ULONG);
                return NDIS_STATUS_INVALID_LENGTH;
            }
            if ((*(PULONG)Buffer & ~RPI5CYW_SUPPORTED_FILTERS) != 0)
                return NDIS_STATUS_NOT_SUPPORTED;
            CywNetworkSetFilter(Adapter, *(PULONG)Buffer);
            OidRequest->DATA.SET_INFORMATION.BytesRead = sizeof(ULONG);
            return NDIS_STATUS_SUCCESS;

        case OID_GEN_CURRENT_LOOKAHEAD:
            if (BufferLength < sizeof(ULONG))
            {
                OidRequest->DATA.SET_INFORMATION.BytesNeeded = sizeof(ULONG);
                return NDIS_STATUS_INVALID_LENGTH;
            }
            Adapter->Lookahead = min(*(PULONG)Buffer, RPI5CYW_MTU);
            OidRequest->DATA.SET_INFORMATION.BytesRead = sizeof(ULONG);
            return NDIS_STATUS_SUCCESS;

        case OID_802_3_MULTICAST_LIST:
            if ((BufferLength % ETH_LENGTH_OF_ADDRESS) != 0 ||
                BufferLength > sizeof(Adapter->MulticastList))
            {
                OidRequest->DATA.SET_INFORMATION.BytesNeeded = sizeof(Adapter->MulticastList);
                return NDIS_STATUS_INVALID_LENGTH;
            }
            CywNetworkSetMulticast(Adapter, Buffer, BufferLength);
            OidRequest->DATA.SET_INFORMATION.BytesRead = BufferLength;
            return NDIS_STATUS_SUCCESS;

        default:
            return NDIS_STATUS_NOT_SUPPORTED;
    }
}

static NDIS_STATUS NTAPI
Rpi5CywInitializeEx(
    _In_ NDIS_HANDLE MiniportAdapterHandle,
    _In_ NDIS_HANDLE MiniportDriverContext,
    _In_ PNDIS_MINIPORT_INIT_PARAMETERS MiniportInitParameters
    )
{
    UUID MacSeed;
    PRPI5CYW_ADAPTER Adapter;
    NDIS_STATUS Status;
    NTSTATUS ProbeStatus;

    UNREFERENCED_PARAMETER(MiniportDriverContext);

    Adapter = (PRPI5CYW_ADAPTER)ExAllocatePool2(
        POOL_FLAG_NON_PAGED,
        sizeof(*Adapter),
        RPI5CYW_TAG);
    if (Adapter == NULL)
    {
        return NDIS_STATUS_RESOURCES;
    }

    RtlZeroMemory(Adapter, sizeof(*Adapter));
    KeInitializeSpinLock(&Adapter->TrafficLock);
    Adapter->MiniportHandle = MiniportAdapterHandle;
    Adapter->NdisPaused = TRUE;
    Adapter->Lookahead = RPI5CYW_MTU;
    Adapter->LinkSpeed = NDIS_LINK_SPEED_UNKNOWN;
    Adapter->MediaConnectState = MediaConnectStateDisconnected;
    Adapter->MediaDuplexState = MediaDuplexStateUnknown;
    ProbeStatus = ExUuidCreate(&MacSeed);
    if (!NT_SUCCESS(ProbeStatus))
    {
        ExFreePoolWithTag(Adapter, RPI5CYW_TAG);
        return NDIS_STATUS_RESOURCES;
    }
    RtlCopyMemory(Adapter->PermanentMacAddress, &MacSeed, ETH_LENGTH_OF_ADDRESS);
    Adapter->PermanentMacAddress[0] = (Adapter->PermanentMacAddress[0] & 0xFC) | 2;
    RtlCopyMemory(Adapter->CurrentMacAddress, Adapter->PermanentMacAddress, ETH_LENGTH_OF_ADDRESS);

    Status = Rpi5CywSetRegistrationAttributes(Adapter);
    if (Status != NDIS_STATUS_SUCCESS)
    {
        ExFreePoolWithTag(Adapter, RPI5CYW_TAG);
        return Status;
    }

    NdisMGetDeviceProperty(MiniportAdapterHandle,
                           &Adapter->PhysicalDeviceObject,
                           NULL,
                           NULL,
                           NULL,
                           NULL);

    Status = Rpi5CywMapResources(
        Adapter,
        (PNDIS_RESOURCE_LIST)MiniportInitParameters->AllocatedResources);
    if (Status != NDIS_STATUS_SUCCESS)
    {
        Rpi5CywWriteDiagnostics(Adapter, 10, (NTSTATUS)Status);
        Rpi5CywUnmapResources(Adapter);
        ExFreePoolWithTag(Adapter, RPI5CYW_TAG);
        return Status;
    }

    Rpi5CywWriteDiagnostics(Adapter, 11, STATUS_SUCCESS);

    ProbeStatus = Rpi5CywDirectSdioProbe(Adapter);
    Adapter->ProbeStatus = ProbeStatus;
    Rpi5CywWriteDiagnostics(Adapter, 100, ProbeStatus);

    if (NT_SUCCESS(ProbeStatus))
    {
        Rpi5CywRegisterInterrupt(Adapter);
        Rpi5CywWriteDiagnostics(Adapter, 101, ProbeStatus);
        ProbeStatus = CywNetworkInitialize(Adapter);
    }
    Adapter->NetworkStatus = ProbeStatus;
    if (!NT_SUCCESS(ProbeStatus))
    {
        Rpi5CywWriteDiagnostics(Adapter, 120, ProbeStatus);
        Rpi5CywDeregisterInterrupt(Adapter);
        Rpi5CywUnmapResources(Adapter);
        ExFreePoolWithTag(Adapter, RPI5CYW_TAG);
        return NDIS_STATUS_FAILURE;
    }
    Status = Rpi5CywSetGeneralAttributes(Adapter);
    if (Status != NDIS_STATUS_SUCCESS)
    {
        Rpi5CywWriteDiagnostics(Adapter, 110, (NTSTATUS)Status);
        CywNetworkStop(Adapter);
        Rpi5CywDeregisterInterrupt(Adapter);
        Rpi5CywUnmapResources(Adapter);
        ExFreePoolWithTag(Adapter, RPI5CYW_TAG);
        return Status;
    }

    Rpi5CywWriteDiagnostics(Adapter, 120, ProbeStatus);
    return NDIS_STATUS_SUCCESS;
}

static VOID NTAPI
Rpi5CywHaltEx(
    _In_ NDIS_HANDLE MiniportAdapterContext,
    _In_ NDIS_HALT_ACTION HaltAction
    )
{
    PRPI5CYW_ADAPTER Adapter = (PRPI5CYW_ADAPTER)MiniportAdapterContext;

    UNREFERENCED_PARAMETER(HaltAction);
    if (Adapter == NULL)
    {
        return;
    }

    CywNetworkStop(Adapter);
    Rpi5CywDeregisterInterrupt(Adapter);
    Rpi5CywUnmapResources(Adapter);
    ExFreePoolWithTag(Adapter, RPI5CYW_TAG);
}

static NDIS_STATUS NTAPI
Rpi5CywPause(
    _In_ NDIS_HANDLE MiniportAdapterContext,
    _In_ PNDIS_MINIPORT_PAUSE_PARAMETERS PauseParameters
    )
{
    PRPI5CYW_ADAPTER Adapter=(PRPI5CYW_ADAPTER)MiniportAdapterContext;
    UNREFERENCED_PARAMETER(PauseParameters);
    Adapter->NdisPauseCount++;
    Adapter->LastPause100ns=KeQueryInterruptTime();
    CywNetworkPause(Adapter, TRUE);
    return NDIS_STATUS_SUCCESS;
}

static NDIS_STATUS NTAPI
Rpi5CywRestart(
    _In_ NDIS_HANDLE MiniportAdapterContext,
    _In_ PNDIS_MINIPORT_RESTART_PARAMETERS RestartParameters
    )
{
    PRPI5CYW_ADAPTER Adapter=(PRPI5CYW_ADAPTER)MiniportAdapterContext;
    UNREFERENCED_PARAMETER(RestartParameters);
    Adapter->NdisRestartCount++;
    Adapter->LastRestart100ns=KeQueryInterruptTime();
    CywNetworkPause(Adapter, FALSE);
    return NDIS_STATUS_SUCCESS;
}

#include "../cyw43455/tx_dispatch.h"
static VOID NTAPI
Rpi5CywSendNetBufferLists(
    _In_ NDIS_HANDLE MiniportAdapterContext,
    _In_ PNET_BUFFER_LIST NetBufferLists,
    _In_ NDIS_PORT_NUMBER PortNumber,
    _In_ ULONG SendFlags
    )
{
    PRPI5CYW_ADAPTER Adapter = (PRPI5CYW_ADAPTER)MiniportAdapterContext;
    UNREFERENCED_PARAMETER(PortNumber);
    CywDispatchSendChain(Adapter,NetBufferLists,SendFlags);
}

static VOID NTAPI
Rpi5CywReturnNetBufferLists(
    _In_ NDIS_HANDLE MiniportAdapterContext,
    _In_ PNET_BUFFER_LIST NetBufferLists,
    _In_ ULONG ReturnFlags
    )
{
    UNREFERENCED_PARAMETER(MiniportAdapterContext);
    UNREFERENCED_PARAMETER(NetBufferLists);
    UNREFERENCED_PARAMETER(ReturnFlags);
}

static VOID NTAPI
Rpi5CywCancelSend(
    _In_ NDIS_HANDLE MiniportAdapterContext,
    _In_ PVOID CancelId
    )
{
    CywNetworkCancelSend((PRPI5CYW_ADAPTER)MiniportAdapterContext,CancelId);
}

static BOOLEAN NTAPI
Rpi5CywCheckForHang(
    _In_ NDIS_HANDLE MiniportAdapterContext
    )
{
    UNREFERENCED_PARAMETER(MiniportAdapterContext);
    return FALSE;
}

static NDIS_STATUS NTAPI
Rpi5CywReset(
    _In_ NDIS_HANDLE MiniportAdapterContext,
    _Out_ PBOOLEAN AddressingReset
    )
{
    UNREFERENCED_PARAMETER(MiniportAdapterContext);
    if (AddressingReset != NULL)
    {
        *AddressingReset = FALSE;
    }
    /* Recovery needs a controlled adapter restart; never falsely report an
     * unperformed hardware reset as successful. */
    return NDIS_STATUS_HARD_ERRORS;
}

static NDIS_STATUS NTAPI
Rpi5CywOidRequest(
    _In_ NDIS_HANDLE MiniportAdapterContext,
    _Inout_ PNDIS_OID_REQUEST OidRequest
    )
{
    PRPI5CYW_ADAPTER Adapter = (PRPI5CYW_ADAPTER)MiniportAdapterContext;

    switch (OidRequest->RequestType)
    {
        case NdisRequestQueryInformation:
        case NdisRequestQueryStatistics:
            return Rpi5CywQueryInformation(Adapter, OidRequest);

        case NdisRequestSetInformation:
            return Rpi5CywSetInformation(Adapter, OidRequest);

        default:
            return NDIS_STATUS_NOT_SUPPORTED;
    }
}

static VOID NTAPI
Rpi5CywCancelOidRequest(
    _In_ NDIS_HANDLE MiniportAdapterContext,
    _In_ PVOID RequestId
    )
{
    UNREFERENCED_PARAMETER(MiniportAdapterContext);
    UNREFERENCED_PARAMETER(RequestId);
}

static VOID NTAPI
Rpi5CywDevicePnPEventNotify(
    _In_ NDIS_HANDLE MiniportAdapterContext,
    _In_ PNET_DEVICE_PNP_EVENT NetDevicePnPEvent
    )
{
    PRPI5CYW_ADAPTER Adapter=(PRPI5CYW_ADAPTER)MiniportAdapterContext;
    if (NetDevicePnPEvent->DevicePnPEvent == NdisDevicePnPEventSurpriseRemoved)
    {
        Adapter->SurpriseRemoveCount++;
        CywNetworkShutdown(Adapter);
    }
}

static VOID NTAPI
Rpi5CywShutdown(
    _In_ NDIS_HANDLE MiniportAdapterContext,
    _In_ NDIS_SHUTDOWN_ACTION ShutdownAction
    )
{
    PRPI5CYW_ADAPTER Adapter=(PRPI5CYW_ADAPTER)MiniportAdapterContext;
    Adapter->ShutdownCount++;
    CywNetworkShutdown(Adapter);
    UNREFERENCED_PARAMETER(ShutdownAction);
}

static VOID NTAPI
Rpi5CywUnload(
    _In_ PDRIVER_OBJECT DriverObject
    )
{
    UNREFERENCED_PARAMETER(DriverObject);
    CywControlDeregister();

    if (gRpi5CywDriverHandle != NULL)
    {
        NdisMDeregisterMiniportDriver(gRpi5CywDriverHandle);
        gRpi5CywDriverHandle = NULL;
    }
}

NTSTATUS NTAPI
DriverEntry(
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PUNICODE_STRING RegistryPath
    )
{
    NDIS_MINIPORT_DRIVER_CHARACTERISTICS Characteristics;
    NDIS_STATUS Status;

    KeInitializeMutex(&CywDiagnosticWriteMutex,0);
    RtlZeroMemory(&Characteristics, sizeof(Characteristics));
    Characteristics.Header.Type = NDIS_OBJECT_TYPE_MINIPORT_DRIVER_CHARACTERISTICS;
    Characteristics.Header.Revision = NDIS_MINIPORT_DRIVER_CHARACTERISTICS_REVISION_2;
    Characteristics.Header.Size = NDIS_SIZEOF_MINIPORT_DRIVER_CHARACTERISTICS_REVISION_2;
    Characteristics.MajorNdisVersion = 6;
    Characteristics.MinorNdisVersion = 30;
    Characteristics.MajorDriverVersion = 1;
    Characteristics.MinorDriverVersion = 0;
    Characteristics.InitializeHandlerEx = Rpi5CywInitializeEx;
    Characteristics.HaltHandlerEx = Rpi5CywHaltEx;
    Characteristics.UnloadHandler = Rpi5CywUnload;
    Characteristics.PauseHandler = Rpi5CywPause;
    Characteristics.RestartHandler = Rpi5CywRestart;
    Characteristics.OidRequestHandler = Rpi5CywOidRequest;
    Characteristics.SendNetBufferListsHandler = Rpi5CywSendNetBufferLists;
    Characteristics.ReturnNetBufferListsHandler = Rpi5CywReturnNetBufferLists;
    Characteristics.CancelSendHandler = Rpi5CywCancelSend;
    Characteristics.CheckForHangHandlerEx = Rpi5CywCheckForHang;
    Characteristics.ResetHandlerEx = Rpi5CywReset;
    Characteristics.DevicePnPEventNotifyHandler = Rpi5CywDevicePnPEventNotify;
    Characteristics.ShutdownHandlerEx = Rpi5CywShutdown;
    Characteristics.CancelOidRequestHandler = Rpi5CywCancelOidRequest;

    Status = NdisMRegisterMiniportDriver(DriverObject,
                                         RegistryPath,
                                         NULL,
                                         &Characteristics,
                                         &gRpi5CywDriverHandle);
    if (Status == NDIS_STATUS_SUCCESS)
    {
        Status = (NDIS_STATUS)CywControlRegister(gRpi5CywDriverHandle);
        if (Status != NDIS_STATUS_SUCCESS)
        {
            NdisMDeregisterMiniportDriver(gRpi5CywDriverHandle);
            gRpi5CywDriverHandle = NULL;
        }
    }
    return (NTSTATUS)Status;
}

/* Worker-owned metrics: one registry value is a coherent, fixed-layout snapshot.
 * This routine never runs from an NDIS callback and never performs SDIO I/O. */
VOID Rpi5CywCaptureTimingDiagnostics(PRPI5CYW_ADAPTER Adapter,CYW_DIAG_BUFFER *Buffer)
{
    UNICODE_STRING ValueName;
    CYW_TIMING_SNAPSHOT Snapshot;
    if(!Adapter->Timing.Enabled || KeGetCurrentIrql()!=PASSIVE_LEVEL)return;
    C_ASSERT(sizeof(CYW_TIMING_BUCKET)==40);
    C_ASSERT(sizeof(CYW_TIMING_SNAPSHOT)==48+40*CywTimeCount);
    Snapshot=Adapter->Timing.Snapshot;
    Snapshot.SnapshotQpc=(CYW_TIMING_U64)KeQueryPerformanceCounter(NULL).QuadPart;
    RtlInitUnicodeString(&ValueName,L"TimingV2");
    CywDiagAppend(Buffer,&ValueName,REG_BINARY,&Snapshot,sizeof(Snapshot));
}
