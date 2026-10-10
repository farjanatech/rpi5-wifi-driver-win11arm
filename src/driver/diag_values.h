/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Shared by the actual driver and host snapshot tests. Captures only the
 * existing diagnostic schema; no registry I/O or retained adapter pointers. */
static VOID CywCaptureDiagnosticValues(PRPI5CYW_ADAPTER Adapter,ULONG Stage,
    NTSTATUS Status,CYW_DIAG_BUFFER *Buffer)
{
    UNICODE_STRING ValueName;
    Adapter->DiagStage=Stage;Adapter->ProbeStatus=Status;
    if((Stage>=20 && Stage<=90) || (Stage>=200 && Stage<=350))Adapter->ProbePhase=Stage;
#define SET_DWORD(_name, _value) do {                                    \
        ULONG _v = (ULONG)(_value);                                      \
        RtlInitUnicodeString(&ValueName, (_name));                        \
        (VOID)CywDiagAppend(Buffer, &ValueName, REG_DWORD,          \
                            &_v, sizeof(_v));                             \
    } while (0)

#define SET_QWORD(_name, _value) do {                                    \
        ULONG64 _v = (ULONG64)(_value);                                  \
        RtlInitUnicodeString(&ValueName, (_name));                        \
        (VOID)CywDiagAppend(Buffer, &ValueName, REG_QWORD,          \
                            &_v, sizeof(_v));                             \
    } while (0)

    SET_DWORD(L"DiagVersion", 47);
    SET_DWORD(L"DiagnosticsAsyncEnabled", Adapter->DiagAsyncEnabled);
    SET_DWORD(L"DiagnosticsAsyncStatus", Adapter->DiagAsyncStatus);
    SET_DWORD(L"DiagnosticsCaptureSkipped", Adapter->DiagCaptureSkipped);
    SET_DWORD(L"DiagnosticsCaptureOverflow", Adapter->DiagCaptureOverflow);
    /* Remove stale prior-session timing evidence while firmware is starting.
     * A zero-size snapshot is deliberately invalid to all timing readers. */
    if(!Adapter->Timing.Enabled) {
        RtlInitUnicodeString(&ValueName,L"TimingV2");
        (VOID)CywDiagAppend(Buffer, &ValueName, REG_BINARY,NULL,0);
    }
    SET_DWORD(L"JoinPreferenceAccepted", Adapter->JoinPreferenceAccepted);
    SET_DWORD(L"JoinPreferenceStatus", Adapter->JoinPreferenceStatus);
    SET_DWORD(L"JoinPreferenceError", Adapter->JoinPreferenceError);
    /* Retire .19-only counters so persistent registry values cannot look live. */
    SET_DWORD(L"RuntimeCmd52Commands", 0);
    SET_DWORD(L"RuntimeCmd52FastPolls", 0);
    SET_DWORD(L"RuntimeCmd52WaitSleeps", 0);
    SET_DWORD(L"RuntimeCmd52Timeouts", 0);
    /* Retire stale pre-v0.7.1.11 experimental glom registry names so old
     * persistent values cannot be mistaken for counters owned by this build. */
    SET_DWORD(L"TxGlomConfigAttempts", 0);
    SET_DWORD(L"TxGlomConfigFallbacks", 0);
    SET_DWORD(L"TxGlomConfigFirmwareError", 0);
    SET_DWORD(L"TxGlomTransfers", 0);
    SET_DWORD(L"TxGlomBytes", 0);
    SET_DWORD(L"TxGlomTransferFailures", 0);
    SET_DWORD(L"TxGlomPairAttempts", 0);
    SET_DWORD(L"TxGlomQueueFallbacks", 0);
    SET_DWORD(L"TxGlomMapFallbacks", 0);
    SET_DWORD(L"TxGlomCreditStops", 0);
    SET_DWORD(L"TxGlomBusyStops", 0);
    SET_DWORD(L"TxGlomSingleDataFrames", 0);
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
    (VOID)CywDiagAppend(Buffer, &ValueName, REG_BINARY,
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
    SET_DWORD(L"DisconnectCount", Adapter->DisconnectCount);
    SET_DWORD(L"LastDisconnectSource", Adapter->LastDisconnectSource);
    SET_DWORD(L"LastDisconnectEvent", Adapter->LastDisconnectEvent);
    SET_DWORD(L"LastDisconnectStatus", Adapter->LastDisconnectStatus);
    SET_DWORD(L"LastDisconnectReason", Adapter->LastDisconnectReason);
    SET_DWORD(L"LastDisconnectNetworkPhase", Adapter->LastDisconnectNetworkPhase);
    SET_DWORD(L"LastDisconnectPowerState", Adapter->LastDisconnectPowerState);
    SET_QWORD(L"LastDisconnect100ns", Adapter->LastDisconnect100ns);
    SET_DWORD(L"FirmwareDisconnectCount", Adapter->FirmwareDisconnectCount);
    SET_DWORD(L"FirmwareDeauthCount", Adapter->FirmwareDeauthCount);
    SET_DWORD(L"FirmwareDisassocCount", Adapter->FirmwareDisassocCount);
    SET_DWORD(L"FirmwareLinkDownCount", Adapter->FirmwareLinkDownCount);
    SET_DWORD(L"FirmwareAuthLossCount", Adapter->FirmwareAuthLossCount);
    SET_DWORD(L"FirmwareOtherDisconnectCount", Adapter->FirmwareOtherDisconnectCount);
    SET_DWORD(L"ExplicitDisconnectCount", Adapter->ExplicitDisconnectCount);
    SET_DWORD(L"WorkerStartCount", Adapter->WorkerStartCount);
    SET_DWORD(L"WorkerRestartCount", Adapter->WorkerRestartCount);
    SET_DWORD(L"WorkerFailureCount", Adapter->WorkerFailureCount);
    SET_DWORD(L"WorkerExitCount", Adapter->WorkerExitCount);
    SET_DWORD(L"LastWorkerFailureStatus", Adapter->LastWorkerFailureStatus);
    SET_QWORD(L"LastWorkerFailure100ns", Adapter->LastWorkerFailure100ns);
    SET_DWORD(L"RuntimeRecoveryAttempts", Adapter->RuntimeRecoveryAttempts);
    SET_DWORD(L"RuntimeRecoveryRestarts", Adapter->RuntimeRecoveryRestarts);
    SET_DWORD(L"RuntimeRecoveryFailures", Adapter->RuntimeRecoveryFailures);
    SET_DWORD(L"RuntimeRecoveryInProgress", Adapter->RuntimeRecoveryInProgress);
    SET_DWORD(L"RuntimeRecoveryTriggerStatus", Adapter->RuntimeRecoveryTriggerStatus);
    SET_DWORD(L"RuntimeRecoveryLastStatus", Adapter->RuntimeRecoveryLastStatus);
    SET_DWORD(L"WarmCardResetAttempts", Adapter->WarmCardResetAttempts);
    SET_DWORD(L"WarmCardResetStatus", Adapter->WarmCardResetStatus);
    SET_DWORD(L"PowerTransitionCount", Adapter->PowerTransitionCount);
    SET_DWORD(L"PowerD0Count", Adapter->PowerD0Count);
    SET_DWORD(L"PowerD1Count", Adapter->PowerD1Count);
    SET_DWORD(L"PowerD2Count", Adapter->PowerD2Count);
    SET_DWORD(L"PowerD3Count", Adapter->PowerD3Count);
    SET_DWORD(L"LastPowerState", Adapter->LastPowerState);
    SET_QWORD(L"LastPowerTransition100ns", Adapter->LastPowerTransition100ns);
    SET_DWORD(L"NdisPauseCount", Adapter->NdisPauseCount);
    SET_DWORD(L"NdisRestartCount", Adapter->NdisRestartCount);
    SET_QWORD(L"LastPause100ns", Adapter->LastPause100ns);
    SET_QWORD(L"LastRestart100ns", Adapter->LastRestart100ns);
    SET_DWORD(L"SurpriseRemoveCount", Adapter->SurpriseRemoveCount);
    SET_DWORD(L"ShutdownCount", Adapter->ShutdownCount);
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
    SET_DWORD(L"FifoBlockReady", Adapter->FifoBlockReady);
    SET_DWORD(L"FifoBlockCommands", Adapter->FifoBlockCommands);
    SET_DWORD(L"FifoBlockBytes", Adapter->FifoBlockBytes);
    SET_DWORD(L"FifoBlockFailures", Adapter->FifoBlockFailures);
    SET_DWORD(L"FifoLateCompletionAccepted", Adapter->FifoLateCompletionAccepted);
    SET_DWORD(L"FifoLastFailureStatus", Adapter->FifoLastFailureStatus);
    SET_DWORD(L"FifoLastFailureWrite", Adapter->FifoLastFailureWrite);
    SET_DWORD(L"FifoLastFailureBlocks", Adapter->FifoLastFailureBlocks);
    SET_DWORD(L"FifoLastFailureCompletedBlocks", Adapter->FifoLastFailureCompletedBlocks);
    SET_DWORD(L"FifoLastFailureWaitEvent", Adapter->FifoLastFailureWaitEvent);
    SET_DWORD(L"FifoLastFailureInterruptStatus", Adapter->FifoLastFailureInterruptStatus);
    SET_DWORD(L"FifoLastFailurePresentState", Adapter->FifoLastFailurePresentState);
    SET_DWORD(L"FifoLastFailureBytesTransferred", Adapter->FifoLastFailureBytesTransferred);
    SET_DWORD(L"FifoTransportFailed", Adapter->FifoTransportFailed);
    SET_DWORD(L"RxReadAhead", Adapter->RxReadAhead);
    SET_DWORD(L"RxReadAheadRejected", Adapter->RxReadAheadRejected);
    SET_DWORD(L"RxGlomEnabled", Adapter->RxGlomEnabled);
    SET_DWORD(L"RxGlomGroups", Adapter->RxGlomGroups);
    SET_DWORD(L"RxGlomFrames", Adapter->RxGlomFrames);
    SET_DWORD(L"RxGlomErrors", Adapter->RxGlomErrors);
    SET_DWORD(L"TxPressurePasses", Adapter->TxPressurePasses);
    SET_DWORD(L"TxPressureFrames", Adapter->TxPressureFrames);
    SET_DWORD(L"TxPressureDeadlineYields", Adapter->TxPressureDeadlineYields);
    SET_DWORD(L"Cmd53WaitSleeps", Adapter->Cmd53WaitSleeps);
    SET_DWORD(L"Cmd53Timeouts", Adapter->Cmd53Timeouts);
    SET_DWORD(L"TxQueueHighWater", Adapter->TxQueueHighWater);
    SET_DWORD(L"TxQueueFull", Adapter->TxQueueFull);
    SET_DWORD(L"TxQueueLimit", RPI5CYW_TX_LIMIT);
    SET_DWORD(L"TxBacklogLimit", RPI5CYW_TX_BACKLOG_LIMIT);
    SET_DWORD(L"TxBacklogCurrent", Adapter->TxBacklogCurrent);
    SET_DWORD(L"TxBacklogNblCurrent", Adapter->TxBacklogNblCurrent);
    SET_DWORD(L"TxBacklogHighWater", Adapter->TxBacklogHighWater);
    SET_DWORD(L"TxBacklogAccepted", Adapter->TxBacklogAccepted);
    SET_DWORD(L"TxBacklogPromoted", Adapter->TxBacklogPromoted);
    SET_DWORD(L"TxBacklogFull", Adapter->TxBacklogFull);
    SET_DWORD(L"TxBacklogExpired", Adapter->TxBacklogExpired);
    SET_DWORD(L"TxBacklogCancelled", Adapter->TxBacklogCancelled);
    SET_DWORD(L"TxBacklogMaxDelayMs", Adapter->TxBacklogMaxDelayMs);
    SET_DWORD(L"TxGlomRequested", Adapter->TxGlomRequested);
    SET_DWORD(L"TxGlomEnabled", Adapter->TxGlomEnabled);
    SET_DWORD(L"TxGlomConfigStatus", Adapter->TxGlomConfigStatus);
    SET_DWORD(L"TxGlomAttempts", Adapter->TxGlomAttempts);
    SET_DWORD(L"TxGlomChains", Adapter->TxGlomChains);
    SET_DWORD(L"TxGlomFrames", Adapter->TxGlomFrames);
    SET_DWORD(L"TxGlomBusyFallbacks", Adapter->TxGlomBusyFallbacks);
    SET_DWORD(L"TxGlomErrors", Adapter->TxGlomErrors);
    SET_DWORD(L"TxGlomPayloadBytes", Adapter->TxGlomPayloadBytes);
    SET_DWORD(L"TxGlomPaddedBytes", Adapter->TxGlomPaddedBytes);
    SET_DWORD(L"TxGlomExtendedDataSingles", Adapter->TxGlomExtendedDataSingles);
    SET_DWORD(L"TxGlomExtendedControlSingles", Adapter->TxGlomExtendedControlSingles);
    SET_DWORD(L"TxGlomPressureThreshold", RPI5CYW_TX_GLOM_PRESSURE_THRESHOLD);
    SET_DWORD(L"TxServiceBurstEnabled", RPI5CYW_TX_SERVICE_BURST4);
    SET_DWORD(L"TxServiceBurstMax", RPI5CYW_TX_SERVICE_BURST_MAX);
    SET_DWORD(L"TxServiceBurstGrants", Adapter->TxServiceBurstGrants);
    SET_DWORD(L"TxServiceBurstGrantedFollowers", Adapter->TxServiceBurstGrantedFollowers);
    SET_DWORD(L"TxServiceBurstMaxFollowers", Adapter->TxServiceBurstMaxFollowers);
    SET_DWORD(L"TxServiceBurstSecondAttempts", Adapter->TxServiceBurstSecondAttempts);
    SET_DWORD(L"TxServiceBurstSecondSuccess", Adapter->TxServiceBurstSecondSuccess);
    SET_DWORD(L"TxServiceBurstSecondBusy", Adapter->TxServiceBurstSecondBusy);
    SET_DWORD(L"TxServiceBurstSecondErrors", Adapter->TxServiceBurstSecondErrors);
    SET_DWORD(L"TxServiceBurstThirdAttempts", Adapter->TxServiceBurstThirdAttempts);
    SET_DWORD(L"TxServiceBurstThirdSuccess", Adapter->TxServiceBurstThirdSuccess);
    SET_DWORD(L"TxServiceBurstThirdBusy", Adapter->TxServiceBurstThirdBusy);
    SET_DWORD(L"TxServiceBurstThirdErrors", Adapter->TxServiceBurstThirdErrors);
    SET_DWORD(L"TxServiceBurstFourthAttempts", Adapter->TxServiceBurstFourthAttempts);
    SET_DWORD(L"TxServiceBurstFourthSuccess", Adapter->TxServiceBurstFourthSuccess);
    SET_DWORD(L"TxServiceBurstFourthBusy", Adapter->TxServiceBurstFourthBusy);
    SET_DWORD(L"TxServiceBurstFourthErrors", Adapter->TxServiceBurstFourthErrors);
    SET_DWORD(L"TxServiceBurstSavedStatusChecks", Adapter->TxServiceBurstSavedStatusChecks);
    SET_DWORD(L"TxAdaptiveHybridEnabled", RPI5CYW_TX_ADAPTIVE_HYBRID);
    SET_DWORD(L"TxAdaptiveSmallMax", RPI5CYW_TX_ADAPTIVE_SMALL_MAX);
    SET_DWORD(L"TxAdaptiveSmallFrames", Adapter->TxAdaptiveSmallFrames);
    SET_DWORD(L"TxAdaptiveBulkFrames", Adapter->TxAdaptiveBulkFrames);
    SET_DWORD(L"TxAdaptiveSmallBacklogAccepted", Adapter->TxAdaptiveSmallBacklogAccepted);
    SET_DWORD(L"TxAdaptiveBulkBackpressure", Adapter->TxAdaptiveBulkBackpressure);
    SET_DWORD(L"TxAdaptiveSmallGlomChains", Adapter->TxAdaptiveSmallGlomChains);
    SET_DWORD(L"TxAdaptiveSmallBurstStarts", Adapter->TxAdaptiveSmallBurstStarts);
    SET_DWORD(L"TxAdaptiveBulkFreshF1Attempts", Adapter->TxAdaptiveBulkFreshF1Attempts);
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
    (VOID)CywDiagAppend(Buffer, &ValueName, REG_QWORD,
        &Adapter->TxRetry.ActualFastWait100ns,sizeof(Adapter->TxRetry.ActualFastWait100ns));
    RtlInitUnicodeString(&ValueName,L"TxRetryMaxFastWait100ns");
    (VOID)CywDiagAppend(Buffer, &ValueName, REG_QWORD,
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
        (VOID)CywDiagAppend(Buffer, &ValueName, REG_QWORD,&Blocked,sizeof(Blocked));
        Blocked=CywTransportBlockedTicks(T,0,Now)/10000ULL;
        RtlInitUnicodeString(&ValueName,L"TransportPriorityBlockedMs");
        (VOID)CywDiagAppend(Buffer, &ValueName, REG_QWORD,&Blocked,sizeof(Blocked));
        RtlInitUnicodeString(&ValueName,L"TransportTraceV1");
        (VOID)CywDiagAppend(Buffer, &ValueName, REG_BINARY,
                            (PVOID)&T->Trace,sizeof(T->Trace));
    }
    /* Worker publishes one complete explicit-query report. Unsupported
     * extension fields remain invalid, never inferred from stale registry data. */
    RtlInitUnicodeString(&ValueName,L"RadioReportV2");
    (VOID)CywDiagAppend(Buffer, &ValueName, REG_BINARY,
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
        (VOID)CywDiagAppend(Buffer, &ValueName, REG_QWORD, &Now, sizeof(Now));
    }
    SET_DWORD(L"Stage", Stage);
    SET_DWORD(L"LastStatus", Status);
    SET_DWORD(L"ResourceCount", Adapter->ResourceCount);
    SET_DWORD(L"ResourceTypes", Adapter->ResourceTypes);
    SET_DWORD(L"InterruptResourceCount", Adapter->InterruptResourceCount);
    SET_DWORD(L"InterruptResourceFlags", Adapter->InterruptResourceFlags);
    SET_DWORD(L"InterruptVector", Adapter->InterruptVector);
    SET_DWORD(L"InterruptLevel", Adapter->InterruptLevel);
    SET_DWORD(L"InterruptRegisterStatus", Adapter->InterruptRegisterStatus);
    SET_DWORD(L"InterruptRegistered", Adapter->InterruptRegistered);
    SET_DWORD(L"InterruptType", Adapter->InterruptType);
    SET_DWORD(L"InterruptNeedsRearm", Adapter->InterruptNeedsRearm);
    SET_DWORD(L"InterruptWakePending", Adapter->InterruptWakePending);
    SET_DWORD(L"InterruptIsrCount", Adapter->InterruptIsrCount);
    SET_DWORD(L"InterruptDpcCount", Adapter->InterruptDpcCount);
    SET_DWORD(L"InterruptRearmCount", Adapter->InterruptRearmCount);
    SET_DWORD(L"InterruptSpuriousCount", Adapter->InterruptSpuriousCount);
    SET_DWORD(L"InterruptNdisEnableCalls", Adapter->InterruptNdisEnableCalls);
    SET_DWORD(L"InterruptNdisDisableCalls", Adapter->InterruptNdisDisableCalls);
    SET_DWORD(L"InterruptPendingReads", Adapter->InterruptPendingReads);
    SET_DWORD(L"InterruptPendingReadFailures", Adapter->InterruptPendingReadFailures);
    SET_DWORD(L"InterruptPendingF1", Adapter->InterruptPendingF1);
    SET_DWORD(L"InterruptPendingF2", Adapter->InterruptPendingF2);
    SET_DWORD(L"InterruptPendingEmpty", Adapter->InterruptPendingEmpty);
    SET_DWORD(L"InterruptRearmDeferred", Adapter->InterruptRearmDeferred);
    SET_DWORD(L"InterruptUsefulWakeCount", Adapter->InterruptUsefulWakeCount);
    SET_DWORD(L"InterruptEmptyWakeCount", Adapter->InterruptEmptyWakeCount);
    SET_DWORD(L"InterruptEmptyWakeStreak", Adapter->InterruptEmptyWakeStreak);
    SET_DWORD(L"InterruptStormFallback", Adapter->InterruptStormFallback);
    SET_DWORD(L"InterruptStormFallbackCount", Adapter->InterruptStormFallbackCount);
    SET_DWORD(L"InterruptStatusEnable", Adapter->InterruptStatusEnable);
    SET_DWORD(L"InterruptSignalEnable", Adapter->InterruptSignalEnable);
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
        (VOID)CywDiagAppend(Buffer, &ValueName, REG_BINARY,
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

#undef SET_QWORD
#undef SET_DWORD

}
