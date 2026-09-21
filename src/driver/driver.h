#pragma once

#ifdef RPI5CYW_HOST_TEST
#include "../../tests/kernel_shim.h"
#else
#include <ntddk.h>
#include <ndis.h>
#include <ifdef.h>
#include <ipifcons.h>
#endif

#include "../cyw43455/packet_probe.h"

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
                                   NDIS_PACKET_TYPE_BROADCAST)

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
    ULONG Cmd53FastPolls, Cmd53WaitSleeps, Cmd53Timeouts;
    ULONG TxQueueHighWater, TxQueueFull, RxBatchYields;
    ULONG TxNblAccepted, TxNblCompleted, TxCancelled, TxExpired, TxCreditWaits;
    ULONG TxCreditSequence, TxCreditMaximum, TxFlowMask;
    ULONG PacketTx[7], PacketRxWire[7], PacketRxHost[7];
    ULONG RxDropState, RxDropFormat, RxDropFilter, RxUnicastOther;
    ULONG RxFilterSnapshot, MacReadbackMatches;
    NTSTATUS MacReadbackStatus;
    ULONG RuntimeF1FastPolls, TxQueueMaxDelayMs;
    CYW_PACKET_PROBE PacketProbe;

    ULONG DiagStage;
    NTSTATUS ProbeStatus;
    ULONG ResourceCount;
    ULONG ResourceTypes;

    ULONG HostVersion;
    ULONG Capabilities;
    ULONG Capabilities2;
    ULONG PresentState;
    ULONG ClockControl;
    ULONG BusModeStage, BusTargetKhz, BusActualKhz, BusWidth;
    ULONG BusCardInterface, BusCardSpeed, BusVerifyReads;
    NTSTATUS BusUpgradeStatus, BusRecoveryStatus, BusVerifyStatus;
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
    ULONG ProbePhase;
    ULONG Function1Ready;
    ULONG ChipClockCsr;
    ULONG ChipIdRaw;
    ULONG ChipId;
    ULONG ChipRevision;
    ULONG Cmd53ReadCount;
    ULONG Cmd53WriteCount;
    ULONG EromAddress;
    ULONG EromWords;
    ULONG EromTrace[512];
    ULONG CoreCount;
    ULONG ChipCommonBase;
    ULONG SdioCoreBase;
    ULONG D11CoreBase;
    ULONG D11WrapperBase;
    ULONG Cr4CoreBase;
    ULONG Cr4WrapperBase;
    ULONG Cr4Capabilities;
    ULONG Cr4IoControl;
    ULONG Cr4ResetControl;
    ULONG RamBankCount;
    ULONG RamBase;
    ULONG CoreInventoryComplete;
    ULONG Cmd53BytesTransferred;
    NTSTATUS Cmd53ResetStatus;
    NTSTATUS ProbeRestoreStatus;
    struct _CYW_NETWORK *Network;
    ULONG NdisPaused;
    volatile long IoStopped;
    ULONG NetworkPhase;
    NTSTATUS NetworkStatus;
    ULONG FirmwareError;
    ULONG FirmwareReplyLength;
    ULONG FirmwareReplyDeclaredLength;
    ULONG FirmwareReplyPayloadLength;
    ULONG FirmwareRequestCapacity;
    ULONG FirmwareValueLength;
    ULONG FirmwareCommand;
    ULONG FirmwareBytes;
    ULONG FirmwareTotalBytes;
    ULONG FirmwareUploadedBytes;
    NTSTATUS RamTransferStatus;
    ULONG RamTransferStage;
    ULONG64 FirmwareNextSnapshot;
    ULONG ConnectStep;
    ULONG CountryRequested;
    ULONG CountryApplied;
    ULONG CountryRevision;
    ULONG CountryBefore;
    ULONG CountryBeforeRevision;
    ULONG CountrySetMode;
    ULONG CountryExplicitError;
    ULONG ClmLoadStatus;
    NTSTATUS ClmQueryStatus;
    NTSTATUS CountryListStatus;
    ULONG CountryListError;
    ULONG CountryListCount;
    ULONG CountryListMembership;
    ULONG CountryListReplyLength;
    ULONG RamTransferAddress;
    ULONG RamTransferLength;
    ULONG RamTransferWrite;
    ULONG RamSize;
    ULONG LinkEvent;
    ULONG LinkReason;
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
