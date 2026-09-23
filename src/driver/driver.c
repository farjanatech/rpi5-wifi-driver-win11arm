#include "driver.h"
#include "../sdio/sdio.h"
#include "../cyw43455/network.h"
#include "statistics_flags.h"

static NDIS_HANDLE gRpi5CywDriverHandle;

static const NDIS_OID gRpi5CywSupportedOids[] =
{
    OID_GEN_SUPPORTED_LIST,
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
    if ((Stage >= 20 && Stage <= 90) || (Stage >= 200 && Stage <= 350))
        Adapter->ProbePhase = Stage;

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

    SET_DWORD(L"DiagVersion", 29);
    /* Remove stale prior-session timing evidence while firmware is starting.
     * A zero-size snapshot is deliberately invalid to all timing readers. */
    if(!Adapter->Timing.Enabled) {
        RtlInitUnicodeString(&ValueName,L"TimingV2");
        (VOID)ZwSetValueKey(KeyHandle,&ValueName,0,REG_BINARY,NULL,0);
    }
    SET_DWORD(L"JoinPreferenceAccepted", Adapter->JoinPreferenceAccepted);
    SET_DWORD(L"JoinPreferenceStatus", Adapter->JoinPreferenceStatus);
    SET_DWORD(L"JoinPreferenceError", Adapter->JoinPreferenceError);
    /* Retire .19-only counters so persistent registry values cannot look live. */
    SET_DWORD(L"RuntimeCmd52Commands", 0);
    SET_DWORD(L"RuntimeCmd52FastPolls", 0);
    SET_DWORD(L"RuntimeCmd52WaitSleeps", 0);
    SET_DWORD(L"RuntimeCmd52Timeouts", 0);
    SET_DWORD(L"NetworkPhase", Adapter->NetworkPhase);
    SET_DWORD(L"NetworkStatus", Adapter->NetworkStatus);
    SET_DWORD(L"FirmwareCommand", Adapter->FirmwareCommand);
    SET_DWORD(L"FirmwareError", Adapter->FirmwareError);
    SET_DWORD(L"FirmwareBytes", Adapter->FirmwareBytes);
    SET_DWORD(L"FirmwareReplyLength", Adapter->FirmwareReplyLength);
    SET_DWORD(L"FirmwareReplyDeclaredLength", Adapter->FirmwareReplyDeclaredLength);
    SET_DWORD(L"FirmwareReplyPayloadLength", Adapter->FirmwareReplyPayloadLength);
    SET_DWORD(L"FirmwareRequestCapacity", Adapter->FirmwareRequestCapacity);
    SET_DWORD(L"FirmwareValueLength", Adapter->FirmwareValueLength);
    SET_DWORD(L"FirmwareTotalBytes", Adapter->FirmwareTotalBytes);
    SET_DWORD(L"FirmwareStartupBusKhz", Adapter->FirmwareStartupBusKhz);
    SET_DWORD(L"FirmwareStartupFallback", Adapter->FirmwareStartupFallback);
    SET_DWORD(L"FirmwareStartupElapsedMs", Adapter->FirmwareStartupElapsedMs);
    SET_DWORD(L"FirmwareStartupStatus", Adapter->FirmwareStartupStatus);
    SET_DWORD(L"FirmwareUploadedBytes", Adapter->FirmwareUploadedBytes);
    SET_DWORD(L"RamTransferStatus", Adapter->RamTransferStatus);
    SET_DWORD(L"RamTransferStage", Adapter->RamTransferStage);
    SET_DWORD(L"ConnectStep", Adapter->ConnectStep);
    RtlInitUnicodeString(&ValueName,L"BandSelectionV1");
    (VOID)ZwSetValueKey(KeyHandle,&ValueName,0,REG_BINARY,
                        Adapter->BandSelection,sizeof(Adapter->BandSelection));
    SET_DWORD(L"CountryRequested", Adapter->CountryRequested);
    SET_DWORD(L"CountryApplied", Adapter->CountryApplied);
    SET_DWORD(L"CountryRevision", Adapter->CountryRevision);
    SET_DWORD(L"CountryBefore", Adapter->CountryBefore);
    SET_DWORD(L"CountryBeforeRevision", Adapter->CountryBeforeRevision);
    SET_DWORD(L"CountrySetMode", Adapter->CountrySetMode);
    SET_DWORD(L"CountryExplicitError", Adapter->CountryExplicitError);
    SET_DWORD(L"ClmLoadStatus", Adapter->ClmLoadStatus);
    SET_DWORD(L"ClmQueryStatus", Adapter->ClmQueryStatus);
    SET_DWORD(L"CountryListStatus", Adapter->CountryListStatus);
    SET_DWORD(L"CountryListError", Adapter->CountryListError);
    SET_DWORD(L"CountryListCount", Adapter->CountryListCount);
    SET_DWORD(L"CountryListMembership", Adapter->CountryListMembership);
    SET_DWORD(L"CountryListReplyLength", Adapter->CountryListReplyLength);
    SET_DWORD(L"RamTransferAddress", Adapter->RamTransferAddress);
    SET_DWORD(L"RamTransferLength", Adapter->RamTransferLength);
    SET_DWORD(L"RamTransferWrite", Adapter->RamTransferWrite);
    SET_DWORD(L"RamSize", Adapter->RamSize);
    SET_DWORD(L"LinkEvent", Adapter->LinkEvent);
    SET_DWORD(L"LinkReason", Adapter->LinkReason);
    SET_DWORD(L"TxPackets", Adapter->TxPackets);
    SET_DWORD(L"RxPackets", Adapter->RxPackets);
    SET_DWORD(L"TxErrors", Adapter->TxErrors);
    SET_DWORD(L"RxErrors", Adapter->RxErrors);
    SET_DWORD(L"RxNoBuffer", Adapter->RxNoBuffer);
    SET_DWORD(L"RxDropState", Adapter->RxDropState);
    SET_DWORD(L"RxDropFormat", Adapter->RxDropFormat);
    SET_DWORD(L"RxDropFilter", Adapter->RxDropFilter);
    SET_DWORD(L"RxUnicastOther", Adapter->RxUnicastOther);
    SET_DWORD(L"RxFilterSnapshot", Adapter->RxFilterSnapshot);
    SET_DWORD(L"MacReadbackStatus", Adapter->MacReadbackStatus);
    SET_DWORD(L"MacReadbackMatches", Adapter->MacReadbackMatches);
    SET_DWORD(L"RuntimeF1FastPolls", Adapter->RuntimeF1FastPolls);
    SET_DWORD(L"BpWindowCacheHits", Adapter->BpWindowCacheHits);
    SET_DWORD(L"BpWindowSelections", Adapter->BpWindowSelections);
    SET_DWORD(L"RuntimeCmd53CommandSleeps", Adapter->RuntimeCmd53SleepPhase[0]);
    SET_DWORD(L"RuntimeCmd53BufferSleeps", Adapter->RuntimeCmd53SleepPhase[1]);
    SET_DWORD(L"RuntimeCmd53CompleteSleeps", Adapter->RuntimeCmd53SleepPhase[2]);
    SET_DWORD(L"RuntimeF1WaitSleeps", Adapter->RuntimeF1WaitSleeps);
    SET_DWORD(L"RuntimeF2WaitSleeps", Adapter->RuntimeF2WaitSleeps);
    SET_DWORD(L"RuntimeCmd53SleepMs", Adapter->RuntimeCmd53Sleep100ns / 10000ULL);
    SET_DWORD(L"TxQueueMaxDelayMs", Adapter->TxQueueMaxDelayMs);
    SET_DWORD(L"TxIpv4ChecksumBad", Adapter->PacketProbe.TxIpBad);
    SET_DWORD(L"RxIpv4ChecksumBad", Adapter->PacketProbe.RxIpBad);
    SET_DWORD(L"TxTransportChecksumBad", Adapter->PacketProbe.TxTransportBad);
    SET_DWORD(L"RxTransportChecksumBad", Adapter->PacketProbe.RxTransportBad);
    SET_DWORD(L"RxIpv4Malformed", Adapter->PacketProbe.RxMalformed);
    SET_DWORD(L"RxIpv4Fragment", Adapter->PacketProbe.RxFragment);
    SET_DWORD(L"RxUdpNoChecksum", Adapter->PacketProbe.RxUdpNoChecksum);
    SET_DWORD(L"TxEchoRequest", Adapter->PacketProbe.TxEcho);
    SET_DWORD(L"RxEchoRequest", Adapter->PacketProbe.RxEchoRequest);
    SET_DWORD(L"RxEchoReply", Adapter->PacketProbe.RxEchoReply);
    SET_DWORD(L"RxEchoMatched", Adapter->PacketProbe.RxEchoMatched);
    SET_DWORD(L"RxEchoUnmatched", Adapter->PacketProbe.RxEchoUnmatched);
    SET_DWORD(L"EchoLate1000ms", Adapter->PacketProbe.EchoLate);
    SET_DWORD(L"EchoMaxRttMs", Adapter->PacketProbe.EchoMaxMs);
    SET_DWORD(L"EchoTrackingEvicted", Adapter->PacketProbe.EchoEvicted);
    SET_DWORD(L"RxIcmpUnreachable", Adapter->PacketProbe.RxIcmpUnreachable);
    SET_DWORD(L"RxIcmpOther", Adapter->PacketProbe.RxIcmpOther);
    {
        static const PCWSTR TxNames[7]={L"TxOther",L"TxArpRequest",L"TxArpReply",L"TxIpv4Icmp",L"TxIpv4Udp",L"TxIpv4Tcp",L"TxIpv6"};
        static const PCWSTR WireNames[7]={L"RxWireOther",L"RxWireArpRequest",L"RxWireArpReply",L"RxWireIpv4Icmp",L"RxWireIpv4Udp",L"RxWireIpv4Tcp",L"RxWireIpv6"};
        static const PCWSTR HostNames[7]={L"RxHostOther",L"RxHostArpRequest",L"RxHostArpReply",L"RxHostIpv4Icmp",L"RxHostIpv4Udp",L"RxHostIpv4Tcp",L"RxHostIpv6"};
        ULONG PacketIndex;
        for(PacketIndex=0;PacketIndex<7;++PacketIndex) {
            SET_DWORD(TxNames[PacketIndex],Adapter->PacketTx[PacketIndex]);
            SET_DWORD(WireNames[PacketIndex],Adapter->PacketRxWire[PacketIndex]);
            SET_DWORD(HostNames[PacketIndex],Adapter->PacketRxHost[PacketIndex]);
        }
    }
    SET_DWORD(L"Cmd53FastPolls", Adapter->Cmd53FastPolls);
    SET_DWORD(L"Cmd53WaitSleeps", Adapter->Cmd53WaitSleeps);
    SET_DWORD(L"Cmd53Timeouts", Adapter->Cmd53Timeouts);
    SET_DWORD(L"TxQueueHighWater", Adapter->TxQueueHighWater);
    SET_DWORD(L"TxQueueFull", Adapter->TxQueueFull);
    SET_DWORD(L"TxQueueLimit", RPI5CYW_TX_LIMIT);
    SET_DWORD(L"RadioVersion", Adapter->RadioReport[0]);
    SET_DWORD(L"RadioGeneration", Adapter->RadioReport[1]);
    SET_DWORD(L"RadioValidMask", Adapter->RadioReport[2]);
    SET_DWORD(L"RadioStatus", Adapter->RadioReport[3]);
    SET_DWORD(L"RadioChannelStatus", Adapter->RadioReport[4]);
    SET_DWORD(L"RadioRssiStatus", Adapter->RadioReport[5]);
    SET_DWORD(L"RadioPmStatus", Adapter->RadioReport[6]);
    SET_DWORD(L"RadioMpcStatus", Adapter->RadioReport[7]);
    SET_DWORD(L"RadioHardwareChannel", Adapter->RadioReport[8]);
    SET_DWORD(L"RadioTargetChannel", Adapter->RadioReport[9]);
    SET_DWORD(L"RadioScanChannel", Adapter->RadioReport[10]);
    SET_DWORD(L"RadioBandMHz", Adapter->RadioReport[11]);
    SET_DWORD(L"RadioRssiRaw", Adapter->RadioReport[12]);
    SET_DWORD(L"RadioPmMode", Adapter->RadioReport[13]);
    SET_DWORD(L"RadioMpc", Adapter->RadioReport[14]);
    SET_DWORD(L"TxQueueFrames", Adapter->TxQueueFrames);
    SET_DWORD(L"TxBurstAdmissions", Adapter->TxBurstAdmissions);
    SET_DWORD(L"TxOversizedNbl", Adapter->TxOversizedNbl);
    SET_DWORD(L"TxInterleavedPackets", Adapter->TxInterleavedPackets);
    SET_DWORD(L"TxNblAccepted", Adapter->TxNblAccepted);
    SET_DWORD(L"TxNblCompleted", Adapter->TxNblCompleted);
    SET_DWORD(L"TxCancelled", Adapter->TxCancelled);
    SET_DWORD(L"TxExpired", Adapter->TxExpired);
    SET_DWORD(L"TxCreditWaits", Adapter->TxCreditWaits);
    SET_DWORD(L"TxCompletionBatchCalls", Adapter->TxCompletionBatchCalls);
    SET_DWORD(L"TxCompletionBatchNbls", Adapter->TxCompletionBatchNbls);
    SET_DWORD(L"TxCompletionBatchMax", Adapter->TxCompletionBatchMax);
    SET_DWORD(L"TxRetryVersion", 1);
    SET_DWORD(L"TxRetryFastRequests", Adapter->TxRetry.FastRequests);
    SET_DWORD(L"TxRetryBackoffRequests", Adapter->TxRetry.BackoffRequests);
    SET_DWORD(L"TxRetryIdleRequests", Adapter->TxRetry.IdleRequests);
    SET_DWORD(L"TxRetryFastResumes", Adapter->TxRetry.FastResumes);
    SET_DWORD(L"TxRetryFastTimeouts", Adapter->TxRetry.FastTimeouts);
    SET_DWORD(L"TxRetryFastWakes", Adapter->TxRetry.FastWakes);
    RtlInitUnicodeString(&ValueName,L"TxRetryActualFastWait100ns");
    (VOID)ZwSetValueKey(KeyHandle,&ValueName,0,REG_QWORD,
        &Adapter->TxRetry.ActualFastWait100ns,sizeof(Adapter->TxRetry.ActualFastWait100ns));
    RtlInitUnicodeString(&ValueName,L"TxRetryMaxFastWait100ns");
    (VOID)ZwSetValueKey(KeyHandle,&ValueName,0,REG_QWORD,
        &Adapter->TxRetry.MaxFastWait100ns,sizeof(Adapter->TxRetry.MaxFastWait100ns));
    SET_DWORD(L"TxCreditSequence", Adapter->TxCreditSequence);
    SET_DWORD(L"TxCreditMaximum", Adapter->TxCreditMaximum);
    SET_DWORD(L"TxFlowMask", Adapter->TxFlowMask);
    {
        const CYW_TRANSPORT_STATE *T=&Adapter->Transport;
        ULONG64 Now=KeQueryInterruptTime(),Blocked;
        SET_DWORD(L"TransportGlobalFlow",T->GlobalFlow);
        SET_DWORD(L"TransportLastInterrupt",T->LastInterrupt);
        SET_DWORD(L"TransportLastMailbox",T->LastMailbox);
        SET_DWORD(L"TransportMailboxVersion",T->MailboxVersion);
        SET_DWORD(L"TransportFirmwareHalted",T->Halted);
        SET_DWORD(L"TransportPendingReads",T->PendingReads);
        SET_DWORD(L"TransportLastPending",T->LastPending);
        SET_DWORD(L"TransportPendingEmpty",T->PendingEmpty);
        SET_DWORD(L"TransportStatusNoEvents",T->StatusNoEvents);
        SET_DWORD(L"TransportFrameNotifications",T->FrameNotifications);
        SET_DWORD(L"TransportFallbackReads",T->FallbackReads);
        SET_DWORD(L"TransportFallbackFrames",T->FallbackFrames);
        SET_DWORD(L"TransportFallbackMailbox",T->FallbackMailbox);
        SET_DWORD(L"TransportStatusReads",T->StatusReads);
        SET_DWORD(L"TransportStatusAcks",T->StatusAcks);
        SET_DWORD(L"TransportFcChanges",T->FcChanges);
        SET_DWORD(L"TransportFcRaces",T->FcRaces);
        SET_DWORD(L"TransportFcStops",T->FcStops);
        SET_DWORD(L"TransportMailboxReads",T->MailReads);
        SET_DWORD(L"TransportMailboxUnknown",T->MailUnknown);
        SET_DWORD(L"TransportFirmwareHalts",T->FirmwareHalts);
        SET_DWORD(L"TransportRxSequenceValid",T->SequenceValid);
        SET_DWORD(L"TransportRxSequenceExpected",T->SequenceExpected);
        SET_DWORD(L"TransportRxSequenceLast",T->SequenceLast);
        SET_DWORD(L"TransportRxSequenceMismatches",T->SequenceMismatches);
        SET_DWORD(L"TransportRxSequenceDuplicates",T->SequenceDuplicates);
        SET_DWORD(L"TransportRxFrames",T->Frames);
        SET_DWORD(L"TransportRxEmptyReads",T->EmptyReads);
        SET_DWORD(L"TransportServiceErrors",T->ServiceErrors);
        SET_DWORD(L"TransportRxAbortFailures",T->RxAbortFailures);
        SET_DWORD(L"TransportPriorityStops",T->PriorityStops);
        SET_DWORD(L"TransportTxStatusChecks",T->TxStatusChecks);
        SET_DWORD(L"TransportPriorityMaskKnown",T->PriorityMaskKnown);
        SET_DWORD(L"TransportPriorityMask",T->PriorityMask);
        SET_DWORD(L"TransportPriorityFlow",T->PriorityFlow);
        SET_DWORD(L"TransportPriorityBlocked",T->PriorityBlocked);
        Blocked=CywTransportBlockedTicks(T,1,Now)/10000ULL;
        RtlInitUnicodeString(&ValueName,L"TransportGlobalBlockedMs");
        (VOID)ZwSetValueKey(KeyHandle,&ValueName,0,REG_QWORD,&Blocked,sizeof(Blocked));
        Blocked=CywTransportBlockedTicks(T,0,Now)/10000ULL;
        RtlInitUnicodeString(&ValueName,L"TransportPriorityBlockedMs");
        (VOID)ZwSetValueKey(KeyHandle,&ValueName,0,REG_QWORD,&Blocked,sizeof(Blocked));
        RtlInitUnicodeString(&ValueName,L"TransportTraceV1");
        (VOID)ZwSetValueKey(KeyHandle,&ValueName,0,REG_BINARY,
                            (PVOID)&T->Trace,sizeof(T->Trace));
    }
    /* Worker publishes one complete explicit-query report. Unsupported
     * extension fields remain invalid, never inferred from stale registry data. */
    RtlInitUnicodeString(&ValueName,L"RadioReportV2");
    (VOID)ZwSetValueKey(KeyHandle,&ValueName,0,REG_BINARY,
                        Adapter->RadioReport,sizeof(Adapter->RadioReport));
    SET_DWORD(L"RxBatchYields", Adapter->RxBatchYields);
    SET_DWORD(L"RxHeaderReads", 0);
    SET_DWORD(L"RxReadAheadAttempts", 0);
    SET_DWORD(L"RxReadAheadFrames", 0);
    SET_DWORD(L"RxReadAheadSavedCommands", 0);
    SET_DWORD(L"RxReadAheadMismatch", 0);
    SET_DWORD(L"RxReadAheadHintIgnored", 0);
    {
        LARGE_INTEGER Now;
        KeQuerySystemTime(&Now);
        RtlInitUnicodeString(&ValueName, L"SnapshotTimeUtc");
        (VOID)ZwSetValueKey(KeyHandle, &ValueName, 0, REG_QWORD, &Now, sizeof(Now));
    }
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
    SET_DWORD(L"BusModeStage", Adapter->BusModeStage);
    SET_DWORD(L"BusTargetKhz", Adapter->BusTargetKhz);
    SET_DWORD(L"BusActualKhz", Adapter->BusActualKhz);
    SET_DWORD(L"BusWidth", Adapter->BusWidth);
    SET_DWORD(L"BusCardInterface", Adapter->BusCardInterface);
    SET_DWORD(L"BusCardSpeed", Adapter->BusCardSpeed);
    SET_DWORD(L"BusVerifyReads", Adapter->BusVerifyReads);
    SET_DWORD(L"BusUpgradeStatus", Adapter->BusUpgradeStatus);
    SET_DWORD(L"BusRecoveryStatus", Adapter->BusRecoveryStatus);
    SET_DWORD(L"BusVerifyStatus", Adapter->BusVerifyStatus);
    SET_DWORD(L"BusHighSpeedEligible", Adapter->BusHighSpeedEligible);
    SET_DWORD(L"BusHighSpeedAttempted", Adapter->BusHighSpeedAttempted);
    SET_DWORD(L"BusHighSpeedActive", Adapter->BusHighSpeedActive);
    SET_DWORD(L"BusHighSpeedRejectMask", Adapter->BusHighSpeedRejectMask);
    SET_DWORD(L"BusHighSpeedStatus", Adapter->BusHighSpeedStatus);
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
    SET_DWORD(L"ProbePhase", Adapter->ProbePhase);
    SET_DWORD(L"Function1Ready", Adapter->Function1Ready);
    SET_DWORD(L"ChipClockCsr", Adapter->ChipClockCsr);
    SET_DWORD(L"ChipIdRaw", Adapter->ChipIdRaw);
    SET_DWORD(L"ChipId", Adapter->ChipId);
    SET_DWORD(L"ChipRevision", Adapter->ChipRevision);
    SET_DWORD(L"Cmd53ReadCount", Adapter->Cmd53ReadCount);
    SET_DWORD(L"Cmd53WriteCount", Adapter->Cmd53WriteCount);
    SET_DWORD(L"EromAddress", Adapter->EromAddress);
    SET_DWORD(L"EromWords", Adapter->EromWords);
    {
        ULONG Words = Adapter->EromWords > 512 ? 512 : Adapter->EromWords;
        RtlInitUnicodeString(&ValueName, L"EromTrace");
        (VOID)ZwSetValueKey(KeyHandle, &ValueName, 0, REG_BINARY,
                           Adapter->EromTrace, (ULONG)(Words * sizeof(ULONG)));
    }
    SET_DWORD(L"CoreCount", Adapter->CoreCount);
    SET_DWORD(L"ChipCommonBase", Adapter->ChipCommonBase);
    SET_DWORD(L"SdioCoreBase", Adapter->SdioCoreBase);
    SET_DWORD(L"D11CoreBase", Adapter->D11CoreBase);
    SET_DWORD(L"Cr4CoreBase", Adapter->Cr4CoreBase);
    SET_DWORD(L"Cr4WrapperBase", Adapter->Cr4WrapperBase);
    SET_DWORD(L"Cr4Capabilities", Adapter->Cr4Capabilities);
    SET_DWORD(L"Cr4IoControl", Adapter->Cr4IoControl);
    SET_DWORD(L"Cr4ResetControl", Adapter->Cr4ResetControl);
    SET_DWORD(L"RamBankCount", Adapter->RamBankCount);
    SET_DWORD(L"RamBase", Adapter->RamBase);
    SET_DWORD(L"CoreInventoryComplete", Adapter->CoreInventoryComplete);
    SET_DWORD(L"Cmd53BytesTransferred", Adapter->Cmd53BytesTransferred);
    SET_DWORD(L"Cmd53ResetStatus", Adapter->Cmd53ResetStatus);
    SET_DWORD(L"ProbeRestoreStatus", Adapter->ProbeRestoreStatus);

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
            Data.Ulong = 0x0006001d;
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
    NTSTATUS Status = CywNetworkPower(Adapter, Work->State == NdisDeviceStateD0);
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

    if (NT_SUCCESS(ProbeStatus)) ProbeStatus = CywNetworkInitialize(Adapter);
    Adapter->NetworkStatus = ProbeStatus;
    if (!NT_SUCCESS(ProbeStatus))
    {
        Rpi5CywWriteDiagnostics(Adapter, 120, ProbeStatus);
        Rpi5CywUnmapResources(Adapter);
        ExFreePoolWithTag(Adapter, RPI5CYW_TAG);
        return NDIS_STATUS_FAILURE;
    }
    Status = Rpi5CywSetGeneralAttributes(Adapter);
    if (Status != NDIS_STATUS_SUCCESS)
    {
        Rpi5CywWriteDiagnostics(Adapter, 110, (NTSTATUS)Status);
        CywNetworkStop(Adapter);
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
    Rpi5CywUnmapResources(Adapter);
    ExFreePoolWithTag(Adapter, RPI5CYW_TAG);
}

static NDIS_STATUS NTAPI
Rpi5CywPause(
    _In_ NDIS_HANDLE MiniportAdapterContext,
    _In_ PNDIS_MINIPORT_PAUSE_PARAMETERS PauseParameters
    )
{
    UNREFERENCED_PARAMETER(PauseParameters);
    CywNetworkPause((PRPI5CYW_ADAPTER)MiniportAdapterContext, TRUE);
    return NDIS_STATUS_SUCCESS;
}

static NDIS_STATUS NTAPI
Rpi5CywRestart(
    _In_ NDIS_HANDLE MiniportAdapterContext,
    _In_ PNDIS_MINIPORT_RESTART_PARAMETERS RestartParameters
    )
{
    UNREFERENCED_PARAMETER(RestartParameters);
    CywNetworkPause((PRPI5CYW_ADAPTER)MiniportAdapterContext, FALSE);
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
    if (NetDevicePnPEvent->DevicePnPEvent == NdisDevicePnPEventSurpriseRemoved)
        CywNetworkShutdown((PRPI5CYW_ADAPTER)MiniportAdapterContext);
}

static VOID NTAPI
Rpi5CywShutdown(
    _In_ NDIS_HANDLE MiniportAdapterContext,
    _In_ NDIS_SHUTDOWN_ACTION ShutdownAction
    )
{
    CywNetworkShutdown((PRPI5CYW_ADAPTER)MiniportAdapterContext);
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
VOID Rpi5CywWriteTimingDiagnostics(PRPI5CYW_ADAPTER Adapter)
{
    OBJECT_ATTRIBUTES Attributes;UNICODE_STRING KeyName,ValueName;HANDLE Key;
    CYW_TIMING_SNAPSHOT Snapshot;
    if(!Adapter->Timing.Enabled || KeGetCurrentIrql()!=PASSIVE_LEVEL)return;
    C_ASSERT(sizeof(CYW_TIMING_BUCKET)==40);
    C_ASSERT(sizeof(CYW_TIMING_SNAPSHOT)==48+40*CywTimeCount);
    Snapshot=Adapter->Timing.Snapshot;
    Snapshot.SnapshotQpc=(CYW_TIMING_U64)KeQueryPerformanceCounter(NULL).QuadPart;
    RtlInitUnicodeString(&KeyName,L"\\Registry\\Machine\\SOFTWARE\\Rpi5CywDirectDiag");
    InitializeObjectAttributes(&Attributes,&KeyName,OBJ_CASE_INSENSITIVE|OBJ_KERNEL_HANDLE,NULL,NULL);
    if(!NT_SUCCESS(ZwOpenKey(&Key,KEY_SET_VALUE,&Attributes)))return;
    RtlInitUnicodeString(&ValueName,L"TimingV2");
    (VOID)ZwSetValueKey(Key,&ValueName,0,REG_BINARY,&Snapshot,sizeof(Snapshot));
    ZwClose(Key);
}
