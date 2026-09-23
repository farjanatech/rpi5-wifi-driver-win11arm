/* SPDX-License-Identifier: GPL-3.0-or-later
 * Explicit, disconnected-only scan on the existing PASSIVE worker.
 * Protocol reference: Linux v6.12 brcmfmac cfg80211.c (ISC, Broadcom):
 * brcmf_run_escan, brcmf_escan_prep, brcmf_notify_escan_complete.
 * No polling of legacy WLC_SCAN_RESULTS (which can be a cached old scan),
 * background scan, new worker, packet scheduling change or retained password.
 */
static BOOLEAN CywScanCancelled(PRPI5CYW_ADAPTER A)
{
    CYW_NETWORK *N=A->Network;
    return A->IoStopped || N->Stop || N->Paused || !N->Powered ||
        InterlockedCompareExchange(&N->ScanCancel,0,0)!=0;
}
/* Called only by the single bus worker's validated event decoder. Firmware
 * partials carry a sync_id; old partials never enter a new generation. Linux
 * accepts terminal events without a payload, so firmware's zero-payload
 * terminal cannot itself identify a generation. DOWN + drain at both ends,
 * one outstanding scan and the accept-events window bound that limitation. */
static VOID CywScanEvent(PRPI5CYW_ADAPTER A,ULONG EventStatus,PUCHAR Payload,ULONG Length)
{
    CYW_NETWORK *N=A->Network;CYW_SCAN_ENTRY entry;KIRQL irql;int result;
    if(!N->ScanBusy || !N->ScanAcceptEvents || N->ScanComplete)return;
    if(EventStatus==8) {
        result=CywScanParsePartial(Payload,Length,N->ScanSyncId,&entry);
        if(result==0)return;
        if(result<0) {N->ScanComplete=TRUE;N->ScanEventStatus=1;return;}
        KeAcquireSpinLock(&N->Lock,&irql);
        CywScanAdd(&N->ScanReport,&entry);
        KeReleaseSpinLock(&N->Lock,irql);
        return;
    }
    if(Length) {
        if(Length<12 || !Payload) {N->ScanComplete=TRUE;N->ScanEventStatus=1;return;}
        if(CywScanU16(Payload+8)!=N->ScanSyncId)return;
        if(CywScanU32(Payload)<12 || CywScanU32(Payload)>Length)EventStatus=1;
    }
    N->ScanEventStatus=EventStatus;N->ScanComplete=TRUE;
}
/* Drain only genuinely notified frames through the existing CywPoll. No
 * speculative FIFO read. A busy/nonquiescent firmware prevents a new scan. */
static NTSTATUS CywScanDrain(PRPI5CYW_ADAPTER A)
{
    ULONGLONG until=KeQueryInterruptTime()+20000000ULL;ULONG channel,off,length,i;
    NTSTATUS status;
    for(i=0;i<256;++i) {
        if(A->IoStopped || A->Network->Stop || !A->Network->Powered)return STATUS_CANCELLED;
        if(KeQueryInterruptTime()>=until)return STATUS_IO_TIMEOUT;
        status=CywPoll(A,&channel,&off,&length);
        if(status==STATUS_NO_MORE_ENTRIES)return STATUS_SUCCESS;
        if(!NT_SUCCESS(status))return status;
    }
    return STATUS_IO_TIMEOUT;
}
static VOID CywScanRequest(PRPI5CYW_ADAPTER A)
{
    CYW_NETWORK *N=A->Network;KIRQL irql;
    UCHAR originalCountry[12]={0},country[12]={0},clm[4]={0};
    UCHAR originalMask[16]={0},mask[16]={0},request[CYW_SCAN_REQUEST_SIZE],abortRequest[CYW_SCAN_ABORT_SIZE];
    ULONG saved[7],error=0,cleanupError=0,channel,off,length;
    NTSTATUS status=STATUS_SUCCESS,cleanup=STATUS_SUCCESS,one;
    BOOLEAN countryTouched=FALSE,maskTouched=FALSE,startAttempted=FALSE,dirty=FALSE,radioDown=FALSE;
    ULONGLONG deadline=KeQueryInterruptTime()+300000000ULL,scanDeadline;
    saved[0]=A->FirmwareCommand;saved[1]=A->FirmwareError;
    saved[2]=A->FirmwareReplyLength;saved[3]=A->FirmwareReplyDeclaredLength;
    saved[4]=A->FirmwareReplyPayloadLength;saved[5]=A->FirmwareRequestCapacity;saved[6]=A->FirmwareValueLength;
    N->ScanAcceptEvents=FALSE;N->ScanComplete=FALSE;
    N->ScanSyncId=(USHORT)(((N->ScanReport.Generation-1u)%65535u)+1u);
    KeAcquireSpinLock(&N->Lock,&irql);
    N->ScanReport.State=CYW_SCAN_RUNNING;
    KeReleaseSpinLock(&N->Lock,irql);
    /* No new setup command starts beyond the aggregate deadline. One in-flight
     * command already has the existing five-second transport timeout. Cleanup
     * has a fixed number of operations, not an unbounded retry loop. */
#define SCAN_CHECK() do {if(CywScanCancelled(A)) {status=STATUS_CANCELLED;goto Cleanup;} \
    if(KeQueryInterruptTime()>=deadline) {status=STATUS_IO_TIMEOUT;goto Cleanup;}} while(0)
#define SCAN_CALL(x) do {SCAN_CHECK();A->FirmwareError=0;status=(x); \
    if(!NT_SUCCESS(status)) {error=A->FirmwareError;goto Cleanup;}} while(0)
    SCAN_CHECK();
    /* Dispatch admission and an already-in-flight link event can overlap.
     * Recheck on the event-owning worker before any radio command; a link
     * that appeared meanwhile must be left entirely untouched. */
    if(!N->Ready || N->Associated || N->Authorized || N->SelectingBand ||
        A->NetworkPhase!=500 || A->NetworkStatus!=STATUS_SUCCESS) {
        status=STATUS_DEVICE_BUSY;goto Cleanup;
    }
    /* Even a failed DOWN may have reached firmware; retry it in cleanup. */
    dirty=TRUE;SCAN_CALL(CywCmdInt(A,3,0));
    N->Associated=N->Authorized=FALSE;
    SCAN_CALL(CywScanDrain(A));
    SCAN_CHECK();A->FirmwareError=0;
    status=CywIovar(A,"clmload_status",FALSE,clm,sizeof(clm));
    if(NT_SUCCESS(status)) {
        if(A->FirmwareReplyLength!=4 || CywLe32(clm)) {status=STATUS_DEVICE_DATA_ERROR;goto Cleanup;}
    } else if(status!=STATUS_UNSUCCESSFUL || A->FirmwareError!=0xffffffe9UL) {
        error=A->FirmwareError;goto Cleanup;
    }
    SCAN_CALL(CywIovar(A,"country",FALSE,originalCountry,sizeof(originalCountry)));
    if(A->FirmwareReplyLength!=12) {status=STATUS_DEVICE_DATA_ERROR;goto Cleanup;}
    if(!CywCountryMatches(N->ScanCountry,originalCountry,12)) {
        if(!CywCountryRequest(N->ScanCountry,country)) {status=STATUS_INVALID_PARAMETER;goto Cleanup;}
        SCAN_CHECK();countryTouched=TRUE;A->FirmwareError=0;
        status=CywIovar(A,"country",TRUE,country,sizeof(country));
        if(!NT_SUCCESS(status)) {
            if(status!=STATUS_UNSUCCESSFUL || A->FirmwareError!=0xfffffffeUL) {
                error=A->FirmwareError;goto Cleanup;
            }
            CywPut32(country+4,0xffffffffUL);
            SCAN_CALL(CywIovar(A,"country",TRUE,country,sizeof(country)));
        }
    }
    SCAN_CALL(CywIovar(A,"country",FALSE,country,sizeof(country)));
    if(!CywCountryMatches(N->ScanCountry,country,A->FirmwareReplyLength)) {
        status=STATUS_DEVICE_DATA_ERROR;goto Cleanup;
    }
    SCAN_CALL(CywIovar(A,"event_msgs",FALSE,originalMask,sizeof(originalMask)));
    if(A->FirmwareReplyLength!=sizeof(originalMask)) {status=STATUS_DEVICE_DATA_ERROR;goto Cleanup;}
    RtlCopyMemory(mask,originalMask,sizeof(mask));mask[69u/8u]|=1u<<(69u%8u);
    SCAN_CHECK();maskTouched=TRUE;
    SCAN_CALL(CywIovar(A,"event_msgs",TRUE,mask,sizeof(mask)));
    SCAN_CALL(CywCmdInt(A,2,0));
    SCAN_CALL(CywScanDrain(A));
    CywScanBuildRequest(request,N->ScanSyncId);
    SCAN_CHECK();N->ScanAcceptEvents=TRUE;startAttempted=TRUE;
    scanDeadline=KeQueryInterruptTime()+100000000ULL;
    SCAN_CALL(CywIovar(A,"escan",TRUE,request,sizeof(request)));
    while(!N->ScanComplete) {
        SCAN_CHECK();
        if(KeQueryInterruptTime()>=scanDeadline) {status=STATUS_IO_TIMEOUT;goto Cleanup;}
        status=CywPoll(A,&channel,&off,&length);
        if(status==STATUS_NO_MORE_ENTRIES)SdioDelayMilliseconds(2);
        else if(!NT_SUCCESS(status))goto Cleanup;
    }
    SCAN_CHECK();
    status=N->ScanEventStatus==0?STATUS_SUCCESS:STATUS_UNSUCCESSFUL;
    error=N->ScanEventStatus;
Cleanup:
    N->ScanAcceptEvents=FALSE;
    if(dirty)N->Associated=N->Authorized=FALSE;
    /* Cancelled/failed results are never advertised as a complete network list.
     * Keep ScanBusy until the exact mask/country restore has finished. */
    if(dirty && (A->IoStopped || N->Stop || !N->Powered))cleanup=STATUS_CANCELLED;
    else if(dirty) {
        if(startAttempted && (!N->ScanComplete || N->ScanEventStatus!=0)) {
            CywScanBuildAbort(abortRequest);
            /* DOWN below is the final cancellation authority even if the
             * optional scan-abort command itself is unsupported. */
            (void)CywFirmwareCommand(A,50,TRUE,abortRequest,sizeof(abortRequest));
        }
        one=CywCmdInt(A,3,0);radioDown=NT_SUCCESS(one);
        if(!NT_SUCCESS(one)) {cleanup=one;cleanupError=A->FirmwareError;}
        if(maskTouched) {
            one=CywIovar(A,"event_msgs",TRUE,originalMask,sizeof(originalMask));
            if(NT_SUCCESS(one)) {
                RtlZeroMemory(mask,sizeof(mask));
                one=CywIovar(A,"event_msgs",FALSE,mask,sizeof(mask));
                if(NT_SUCCESS(one) && (A->FirmwareReplyLength!=sizeof(mask) ||
                    memcmp(mask,originalMask,sizeof(mask))))one=STATUS_DEVICE_DATA_ERROR;
            }
            if(!NT_SUCCESS(one) && NT_SUCCESS(cleanup)) {cleanup=one;cleanupError=A->FirmwareError;}
        }
        if(countryTouched && radioDown) {
            /* Restore while DOWN only; never re-enable a prior country. */
            one=CywIovar(A,"country",TRUE,originalCountry,sizeof(originalCountry));
            if(NT_SUCCESS(one)) {
                RtlZeroMemory(country,sizeof(country));
                one=CywIovar(A,"country",FALSE,country,sizeof(country));
                if(NT_SUCCESS(one) && (A->FirmwareReplyLength!=sizeof(country) ||
                    memcmp(country,originalCountry,sizeof(country))))one=STATUS_DEVICE_DATA_ERROR;
            }
            if(!NT_SUCCESS(one) && NT_SUCCESS(cleanup)) {cleanup=one;cleanupError=A->FirmwareError;}
        }
        if(NT_SUCCESS(cleanup)) {cleanup=CywScanDrain(A);if(!NT_SUCCESS(cleanup))cleanupError=A->FirmwareError;}
    }
    if(!NT_SUCCESS(cleanup)) {
        status=cleanup;error=cleanupError;
        N->Ready=FALSE;A->NetworkStatus=cleanup;CywRefreshTxGate(A);
    }
    if(CywScanCancelled(A) && NT_SUCCESS(status))status=STATUS_CANCELLED;
    KeAcquireSpinLock(&N->Lock,&irql);
    N->ScanReport.Status=(unsigned)status;N->ScanReport.FirmwareError=error;
    N->ScanReport.State=NT_SUCCESS(status)?CYW_SCAN_COMPLETE:
        (status==STATUS_CANCELLED?CYW_SCAN_CANCELLED:CYW_SCAN_FAILED);
    if(!NT_SUCCESS(status)) {N->ScanReport.Count=0;RtlZeroMemory(N->ScanReport.Entries,sizeof(N->ScanReport.Entries));}
    N->ScanBusy=FALSE;
    KeReleaseSpinLock(&N->Lock,irql);
    A->FirmwareCommand=saved[0];A->FirmwareError=saved[1];
    A->FirmwareReplyLength=saved[2];A->FirmwareReplyDeclaredLength=saved[3];
    A->FirmwareReplyPayloadLength=saved[4];A->FirmwareRequestCapacity=saved[5];A->FirmwareValueLength=saved[6];
#undef SCAN_CHECK
#undef SCAN_CALL
}
