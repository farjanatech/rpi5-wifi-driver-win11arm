#include "driver.h"
#include "../sdio/sdio.h"

static NDIS_HANDLE gRpi5CywDriverHandle;

static const NDIS_OID gRpi5CywSupportedOids[] =
{
    OID_GEN_SUPPORTED_LIST,
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

VOID
Rpi5CywWriteDiagnostics(
    _In_ PRPI5CYW_ADAPTER Adapter,
    _In_ ULONG Stage,
    _In_ NTSTATUS Status
    )
{
    OBJECT_ATTRIBUTES Attributes;
    UNICODE_STRING KeyName;
    UNICODE_STRING ValueName;
    HANDLE KeyHandle = NULL;
    ULONG Disposition;
    NTSTATUS KeyStatus;

    if (Adapter == NULL || KeGetCurrentIrql() != PASSIVE_LEVEL)
    {
        return;
    }

    Adapter->DiagStage = Stage;
    Adapter->ProbeStatus = Status;

    RtlInitUnicodeString(&KeyName, L"\\Registry\\Machine\\SOFTWARE\\Rpi5CywDirectDiag");
    InitializeObjectAttributes(&Attributes,
                               &KeyName,
                               OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE,
                               NULL,
                               NULL);

    KeyStatus = ZwCreateKey(&KeyHandle,
                            KEY_SET_VALUE,
                            &Attributes,
                            0,
                            NULL,
                            REG_OPTION_NON_VOLATILE,
                            &Disposition);
    if (!NT_SUCCESS(KeyStatus))
    {
        return;
    }

#define SET_DWORD(_name, _value) do {                                    \
        ULONG _v = (ULONG)(_value);                                      \
        RtlInitUnicodeString(&ValueName, (_name));                        \
        (VOID)ZwSetValueKey(KeyHandle, &ValueName, 0, REG_DWORD,          \
                            &_v, sizeof(_v));                             \
    } while (0)

    SET_DWORD(L"DiagVersion", 3);
    SET_DWORD(L"Stage", Stage);
    SET_DWORD(L"LastStatus", Status);
    SET_DWORD(L"ResourceCount", Adapter->ResourceCount);
    SET_DWORD(L"ResourceTypes", Adapter->ResourceTypes);
    SET_DWORD(L"RegPhysHi", Adapter->RegisterPhysical.HighPart);
    SET_DWORD(L"RegPhysLo", Adapter->RegisterPhysical.LowPart);
    SET_DWORD(L"RegLength", Adapter->RegisterLength);
    SET_DWORD(L"RegMapped", Adapter->RegisterBase != NULL ? 1 : 0);
    SET_DWORD(L"HostVersion", Adapter->HostVersion);
    SET_DWORD(L"Capabilities", Adapter->Capabilities);
    SET_DWORD(L"Capabilities2", Adapter->Capabilities2);
    SET_DWORD(L"PresentState", Adapter->PresentState);
    SET_DWORD(L"ClockControl", Adapter->ClockControl);
    SET_DWORD(L"PowerControl", Adapter->PowerControl);
    SET_DWORD(L"HostControl", Adapter->HostControl);
    SET_DWORD(L"HostControl2", Adapter->HostControl2);
    SET_DWORD(L"TimeoutControl", Adapter->TimeoutControl);
    SET_DWORD(L"SoftwareReset", Adapter->SoftwareReset);
    SET_DWORD(L"LastCommand", Adapter->LastCommand);
    SET_DWORD(L"LastArgument", Adapter->LastArgument);
    SET_DWORD(L"LastInterruptStatus", Adapter->LastInterruptStatus);
    SET_DWORD(L"LastResponse", Adapter->LastResponse);
    SET_DWORD(L"LastCommandResetStatus", Adapter->LastCommandResetStatus);
    SET_DWORD(L"Cmd5AttemptCount", Adapter->Cmd5AttemptCount);
    SET_DWORD(L"Cmd5ValidAttempt", Adapter->Cmd5ValidAttempt);
    SET_DWORD(L"Cmd5SuccessAttempt", Adapter->Cmd5SuccessAttempt);
#define WIDEN2(_value) L##_value
#define WIDEN(_value) WIDEN2(_value)
#define SET_CMD5_ATTEMPT(_number, _index) do {                                             \
        SET_DWORD(L"Cmd5Attempt" WIDEN(#_number) L"ClockKhz",                            \
                  Adapter->Cmd5Attempts[_index].TargetClockKhz);                          \
        SET_DWORD(L"Cmd5Attempt" WIDEN(#_number) L"Argument",                            \
                  Adapter->Cmd5Attempts[_index].Argument);                                \
        SET_DWORD(L"Cmd5Attempt" WIDEN(#_number) L"Status",                             \
                  Adapter->Cmd5Attempts[_index].Status);                                  \
        SET_DWORD(L"Cmd5Attempt" WIDEN(#_number) L"ResetStatus",                        \
                  Adapter->Cmd5Attempts[_index].ResetStatus);                             \
        SET_DWORD(L"Cmd5Attempt" WIDEN(#_number) L"ResponseValid",                       \
                  Adapter->Cmd5Attempts[_index].ResponseValid);                           \
        SET_DWORD(L"Cmd5Attempt" WIDEN(#_number) L"InterruptStatus",                    \
                  Adapter->Cmd5Attempts[_index].InterruptStatus);                         \
        SET_DWORD(L"Cmd5Attempt" WIDEN(#_number) L"Response",                           \
                  Adapter->Cmd5Attempts[_index].Response);                                \
        SET_DWORD(L"Cmd5Attempt" WIDEN(#_number) L"PresentStateBefore",                 \
                  Adapter->Cmd5Attempts[_index].PresentStateBefore);                      \
        SET_DWORD(L"Cmd5Attempt" WIDEN(#_number) L"PresentStateAfter",                  \
                  Adapter->Cmd5Attempts[_index].PresentStateAfter);                       \
        SET_DWORD(L"Cmd5Attempt" WIDEN(#_number) L"ClockControlBefore",                 \
                  Adapter->Cmd5Attempts[_index].ClockControlBefore);                      \
        SET_DWORD(L"Cmd5Attempt" WIDEN(#_number) L"ClockControlAfter",                  \
                  Adapter->Cmd5Attempts[_index].ClockControlAfter);                       \
        SET_DWORD(L"Cmd5Attempt" WIDEN(#_number) L"PowerControlBefore",                 \
                  Adapter->Cmd5Attempts[_index].PowerControlBefore);                      \
        SET_DWORD(L"Cmd5Attempt" WIDEN(#_number) L"PowerControlAfter",                  \
                  Adapter->Cmd5Attempts[_index].PowerControlAfter);                       \
        SET_DWORD(L"Cmd5Attempt" WIDEN(#_number) L"HostControlBefore",                   \
                  Adapter->Cmd5Attempts[_index].HostControlBefore);                       \
        SET_DWORD(L"Cmd5Attempt" WIDEN(#_number) L"HostControlAfter",                    \
                  Adapter->Cmd5Attempts[_index].HostControlAfter);                        \
        SET_DWORD(L"Cmd5Attempt" WIDEN(#_number) L"HostControl2Before",                  \
                  Adapter->Cmd5Attempts[_index].HostControl2Before);                      \
        SET_DWORD(L"Cmd5Attempt" WIDEN(#_number) L"HostControl2After",                   \
                  Adapter->Cmd5Attempts[_index].HostControl2After);                       \
        SET_DWORD(L"Cmd5Attempt" WIDEN(#_number) L"TimeoutControlBefore",                \
                  Adapter->Cmd5Attempts[_index].TimeoutControlBefore);                    \
        SET_DWORD(L"Cmd5Attempt" WIDEN(#_number) L"TimeoutControlAfter",                 \
                  Adapter->Cmd5Attempts[_index].TimeoutControlAfter);                     \
    } while (0)
    SET_CMD5_ATTEMPT(1, 0);
    SET_CMD5_ATTEMPT(2, 1);
    SET_CMD5_ATTEMPT(3, 2);
    SET_CMD5_ATTEMPT(4, 3);
    SET_CMD5_ATTEMPT(5, 4);
    SET_CMD5_ATTEMPT(6, 5);
    SET_CMD5_ATTEMPT(7, 6);
    SET_CMD5_ATTEMPT(8, 7);
    SET_CMD5_ATTEMPT(9, 8);
    SET_CMD5_ATTEMPT(10, 9);
    SET_CMD5_ATTEMPT(11, 10);
    SET_CMD5_ATTEMPT(12, 11);
    SET_CMD5_ATTEMPT(13, 12);
    SET_CMD5_ATTEMPT(14, 13);
    SET_CMD5_ATTEMPT(15, 14);
    SET_CMD5_ATTEMPT(16, 15);
    SET_CMD5_ATTEMPT(17, 16);
    SET_CMD5_ATTEMPT(18, 17);
#undef SET_CMD5_ATTEMPT
#undef WIDEN
#undef WIDEN2
    SET_DWORD(L"Cmd5ProbeResponse", Adapter->Cmd5ProbeResponse);
    SET_DWORD(L"SdioOcr", Adapter->SdioOcr);
    SET_DWORD(L"SdioFunctions", Adapter->SdioFunctions);
    SET_DWORD(L"RelativeAddress", Adapter->RelativeAddress);
    SET_DWORD(L"CccrRevision", Adapter->CccrRevision);
    SET_DWORD(L"IoEnable", Adapter->IoEnable);
    SET_DWORD(L"IoReady", Adapter->IoReady);
    SET_DWORD(L"F1InterfaceCode", Adapter->F1InterfaceCode);
    SET_DWORD(L"F2InterfaceCode", Adapter->F2InterfaceCode);

#undef SET_DWORD

    ZwClose(KeyHandle);
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

        if (Descriptor[Index].Type == CmResourceTypeMemory && Adapter->RegisterBase == NULL)
        {
            Adapter->RegisterPhysical = Descriptor[Index].u.Memory.Start;
            Adapter->RegisterLength = Descriptor[Index].u.Memory.Length;
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
        case OID_GEN_SUPPORTED_LIST:
            return Rpi5CywCopyQuery(OidRequest,
                                    gRpi5CywSupportedOids,
                                    sizeof(gRpi5CywSupportedOids));

        case OID_GEN_HARDWARE_STATUS:
            Data.HardwareStatus = NdisHardwareStatusReady;
            return Rpi5CywCopyQuery(OidRequest, &Data.HardwareStatus, sizeof(Data.HardwareStatus));

        case OID_GEN_MEDIA_SUPPORTED:
        case OID_GEN_MEDIA_IN_USE:
            Data.Medium = NdisMedium802_3;
            return Rpi5CywCopyQuery(OidRequest, &Data.Medium, sizeof(Data.Medium));

        case OID_GEN_PHYSICAL_MEDIUM:
            Data.PhysicalMedium = NdisPhysicalMedium802_3;
            return Rpi5CywCopyQuery(OidRequest, &Data.PhysicalMedium, sizeof(Data.PhysicalMedium));

        case OID_GEN_MAXIMUM_LOOKAHEAD:
        case OID_GEN_CURRENT_LOOKAHEAD:
        case OID_GEN_MAXIMUM_FRAME_SIZE:
            Data.Ulong = RPI5CYW_MTU;
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
        case OID_GEN_VENDOR_DRIVER_VERSION:
            Data.Ushort = RPI5CYW_DRIVER_VERSION;
            return Rpi5CywCopyQuery(OidRequest, &Data.Ushort, sizeof(Data.Ushort));

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
            Data.Ulong64 = Adapter->TxErrors;
            return Rpi5CywCopyQuery(OidRequest, &Data.Ulong64, sizeof(Data.Ulong64));

        case OID_GEN_RCV_ERROR:
            Data.Ulong64 = Adapter->RxErrors;
            return Rpi5CywCopyQuery(OidRequest, &Data.Ulong64, sizeof(Data.Ulong64));

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
        case OID_GEN_CURRENT_PACKET_FILTER:
            if (BufferLength < sizeof(ULONG))
            {
                OidRequest->DATA.SET_INFORMATION.BytesNeeded = sizeof(ULONG);
                return NDIS_STATUS_INVALID_LENGTH;
            }
            Adapter->PacketFilter = *(PULONG)Buffer;
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
            Adapter->MulticastCount = BufferLength / ETH_LENGTH_OF_ADDRESS;
            RtlCopyMemory(Adapter->MulticastList, Buffer, BufferLength);
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
    static const UCHAR FallbackMac[ETH_LENGTH_OF_ADDRESS] =
        { 0x02, 0x52, 0x50, 0x49, 0x35, 0x01 };
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
    Adapter->MiniportHandle = MiniportAdapterHandle;
    Adapter->Lookahead = RPI5CYW_MTU;
    Adapter->LinkSpeed = NDIS_LINK_SPEED_UNKNOWN;
    Adapter->MediaConnectState = MediaConnectStateDisconnected;
    Adapter->MediaDuplexState = MediaDuplexStateUnknown;
    RtlCopyMemory(Adapter->PermanentMacAddress, FallbackMac, sizeof(FallbackMac));
    RtlCopyMemory(Adapter->CurrentMacAddress, FallbackMac, sizeof(FallbackMac));

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

    /*
     * Keep the NDIS device Started even when direct SDIO probing fails. This is
     * intentional: Windows then exposes a disconnected Ethernet adapter and the
     * registry snapshot tells us exactly which real SD command failed. A probe
     * milestone is never reported as working Wi-Fi.
     */
    Status = Rpi5CywSetGeneralAttributes(Adapter);
    if (Status != NDIS_STATUS_SUCCESS)
    {
        Rpi5CywWriteDiagnostics(Adapter, 110, (NTSTATUS)Status);
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

    Rpi5CywUnmapResources(Adapter);
    ExFreePoolWithTag(Adapter, RPI5CYW_TAG);
}

static NDIS_STATUS NTAPI
Rpi5CywPause(
    _In_ NDIS_HANDLE MiniportAdapterContext,
    _In_ PNDIS_MINIPORT_PAUSE_PARAMETERS PauseParameters
    )
{
    UNREFERENCED_PARAMETER(MiniportAdapterContext);
    UNREFERENCED_PARAMETER(PauseParameters);
    return NDIS_STATUS_SUCCESS;
}

static NDIS_STATUS NTAPI
Rpi5CywRestart(
    _In_ NDIS_HANDLE MiniportAdapterContext,
    _In_ PNDIS_MINIPORT_RESTART_PARAMETERS RestartParameters
    )
{
    UNREFERENCED_PARAMETER(MiniportAdapterContext);
    UNREFERENCED_PARAMETER(RestartParameters);
    return NDIS_STATUS_SUCCESS;
}

static VOID NTAPI
Rpi5CywSendNetBufferLists(
    _In_ NDIS_HANDLE MiniportAdapterContext,
    _In_ PNET_BUFFER_LIST NetBufferLists,
    _In_ NDIS_PORT_NUMBER PortNumber,
    _In_ ULONG SendFlags
    )
{
    PRPI5CYW_ADAPTER Adapter = (PRPI5CYW_ADAPTER)MiniportAdapterContext;
    PNET_BUFFER_LIST Nbl;
    ULONG CompleteFlags = 0;

    UNREFERENCED_PARAMETER(PortNumber);

    if (NDIS_TEST_SEND_AT_DISPATCH_LEVEL(SendFlags))
    {
        CompleteFlags = NDIS_SEND_COMPLETE_FLAGS_DISPATCH_LEVEL;
    }

    for (Nbl = NetBufferLists; Nbl != NULL; Nbl = NET_BUFFER_LIST_NEXT_NBL(Nbl))
    {
        NET_BUFFER_LIST_STATUS(Nbl) = NDIS_STATUS_MEDIA_DISCONNECTED;
        if (Adapter != NULL)
        {
            Adapter->TxErrors++;
        }
    }

    NdisMSendNetBufferListsComplete(Adapter->MiniportHandle,
                                    NetBufferLists,
                                    CompleteFlags);
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
    UNREFERENCED_PARAMETER(MiniportAdapterContext);
    UNREFERENCED_PARAMETER(CancelId);
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
    return NDIS_STATUS_SUCCESS;
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
    UNREFERENCED_PARAMETER(MiniportAdapterContext);
    UNREFERENCED_PARAMETER(NetDevicePnPEvent);
}

static VOID NTAPI
Rpi5CywShutdown(
    _In_ NDIS_HANDLE MiniportAdapterContext,
    _In_ NDIS_SHUTDOWN_ACTION ShutdownAction
    )
{
    UNREFERENCED_PARAMETER(MiniportAdapterContext);
    UNREFERENCED_PARAMETER(ShutdownAction);
}

static VOID NTAPI
Rpi5CywUnload(
    _In_ PDRIVER_OBJECT DriverObject
    )
{
    UNREFERENCED_PARAMETER(DriverObject);

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
    return (NTSTATUS)Status;
}
