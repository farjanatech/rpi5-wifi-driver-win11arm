#pragma once

#include <ntddk.h>
#include <ndis.h>
#include <ifdef.h>
#include <ipifcons.h>

#ifndef ETH_LENGTH_OF_ADDRESS
#define ETH_LENGTH_OF_ADDRESS 6
#endif

#define RPI5CYW_TAG 'W5PR'
#define RPI5CYW_MTU 1500
#define RPI5CYW_FRAME_SIZE 1514
#define RPI5CYW_DRIVER_VERSION 0x0100
#define RPI5CYW_MAX_MULTICAST 32
#define RPI5CYW_MAX_LINK_SPEED 433000000ULL
#define RPI5CYW_CMD5_MAX_ATTEMPTS 18

#define RPI5CYW_MAC_OPTIONS (NDIS_MAC_OPTION_TRANSFERS_NOT_PEND | \
                             NDIS_MAC_OPTION_COPY_LOOKAHEAD_DATA | \
                             NDIS_MAC_OPTION_NO_LOOPBACK)

#define RPI5CYW_SUPPORTED_FILTERS (NDIS_PACKET_TYPE_DIRECTED | \
                                   NDIS_PACKET_TYPE_MULTICAST | \
                                   NDIS_PACKET_TYPE_ALL_MULTICAST | \
                                   NDIS_PACKET_TYPE_BROADCAST | \
                                   NDIS_PACKET_TYPE_PROMISCUOUS)

typedef struct _RPI5CYW_CMD5_ATTEMPT_DIAG
{
    ULONG TargetClockKhz;
    ULONG Argument;
    NTSTATUS Status;
    NTSTATUS ResetStatus;
    ULONG ResponseValid;
    ULONG InterruptStatus;
    ULONG Response;
    ULONG PresentStateBefore;
    ULONG PresentStateAfter;
    ULONG ClockControlBefore;
    ULONG ClockControlAfter;
    ULONG PowerControlBefore;
    ULONG PowerControlAfter;
    ULONG HostControlBefore;
    ULONG HostControlAfter;
    ULONG HostControl2Before;
    ULONG HostControl2After;
    ULONG TimeoutControlBefore;
    ULONG TimeoutControlAfter;
} RPI5CYW_CMD5_ATTEMPT_DIAG, *PRPI5CYW_CMD5_ATTEMPT_DIAG;

typedef struct _RPI5CYW_ADAPTER
{
    NDIS_HANDLE MiniportHandle;
    PDEVICE_OBJECT PhysicalDeviceObject;

    PVOID RegisterBase;
    NDIS_PHYSICAL_ADDRESS RegisterPhysical;
    ULONG RegisterLength;

    ULONG PacketFilter;
    ULONG Lookahead;
    UCHAR PermanentMacAddress[ETH_LENGTH_OF_ADDRESS];
    UCHAR CurrentMacAddress[ETH_LENGTH_OF_ADDRESS];
    UCHAR MulticastList[RPI5CYW_MAX_MULTICAST][ETH_LENGTH_OF_ADDRESS];
    ULONG MulticastCount;

    NDIS_MEDIA_CONNECT_STATE MediaConnectState;
    NDIS_MEDIA_DUPLEX_STATE MediaDuplexState;
    ULONG64 LinkSpeed;

    ULONG64 TxPackets;
    ULONG64 RxPackets;
    ULONG64 TxErrors;
    ULONG64 RxErrors;
    ULONG64 RxNoBuffer;

    ULONG DiagStage;
    NTSTATUS ProbeStatus;
    ULONG ResourceCount;
    ULONG ResourceTypes;

    ULONG HostVersion;
    ULONG Capabilities;
    ULONG Capabilities2;
    ULONG PresentState;
    ULONG ClockControl;
    ULONG PowerControl;
    ULONG HostControl;
    ULONG HostControl2;
    ULONG TimeoutControl;
    ULONG SoftwareReset;

    ULONG LastCommand;
    ULONG LastArgument;
    ULONG LastInterruptStatus;
    ULONG LastResponse;
    NTSTATUS LastCommandResetStatus;

    ULONG Cmd5AttemptCount;
    ULONG Cmd5ValidAttempt;
    ULONG Cmd5SuccessAttempt;
    RPI5CYW_CMD5_ATTEMPT_DIAG Cmd5Attempts[RPI5CYW_CMD5_MAX_ATTEMPTS];

    ULONG Cmd5ProbeResponse;
    ULONG SdioOcr;
    ULONG SdioFunctions;
    ULONG RelativeAddress;
    ULONG CccrRevision;
    ULONG IoEnable;
    ULONG IoReady;
    ULONG F1InterfaceCode;
    ULONG F2InterfaceCode;
} RPI5CYW_ADAPTER, *PRPI5CYW_ADAPTER;

DRIVER_INITIALIZE DriverEntry;

NTSTATUS
Rpi5CywDirectSdioProbe(
    _Inout_ PRPI5CYW_ADAPTER Adapter
    );

VOID
Rpi5CywWriteDiagnostics(
    _In_ PRPI5CYW_ADAPTER Adapter,
    _In_ ULONG Stage,
    _In_ NTSTATUS Status
    );
