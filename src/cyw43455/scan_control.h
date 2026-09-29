/* SPDX-License-Identifier: GPL-3.0-or-later
 * Memory-only control dispatch. Caller holds ControlLock to pin the adapter.
 * No SDIO, firmware command, pending IRP, user pointer or packet-path change.
 */
static NTSTATUS CywScanControl(PRPI5CYW_ADAPTER A,ULONG Code,PUCHAR Buffer,
    ULONG InputLength,ULONG OutputLength,PULONG Bytes)
{
    CYW_NETWORK *N=A->Network;KIRQL irql;NTSTATUS status=STATUS_INVALID_PARAMETER;
    ULONG generation;
    *Bytes=0;
    KeAcquireSpinLock(&N->Lock,&irql);
    if(Code==CYW_IOCTL_SCAN_STATUS && !InputLength && Buffer && OutputLength>=sizeof(N->ScanReport)) {
        RtlCopyMemory(Buffer,&N->ScanReport,sizeof(N->ScanReport));
        *Bytes=(ULONG)sizeof(N->ScanReport);status=STATUS_SUCCESS;
    } else if(Code==CYW_IOCTL_SCAN_CANCEL && InputLength==4 && Buffer) {
        generation=CywLe32(Buffer);
        if(!generation || generation!=N->ScanReport.Generation)status=STATUS_INVALID_PARAMETER;
        else {
            if(N->ScanBusy) {
                InterlockedExchange(&N->ScanCancel,1);
                if(N->Request==4 && !N->ControlBusy) {
                    N->Request=0;N->ScanReport.State=5;N->ScanReport.Status=(ULONG)STATUS_CANCELLED;
                    N->ScanReport.Count=0;N->ScanBusy=FALSE;
                }
                KeSetEvent(&N->Wake,0,FALSE);
            }
            status=STATUS_SUCCESS;
        }
    } else if(Code==CYW_IOCTL_SCAN_START && InputLength==8 && Buffer &&
        CywLe32(Buffer)==1 && Buffer[4]>='A' && Buffer[4]<='Z' &&
        Buffer[5]>='A' && Buffer[5]<='Z' && !Buffer[6] && !Buffer[7]) {
        /* Check both queued AND executing control work. An SSID join may not
         * yet have published Associated/Authorized, so link state alone is
         * not a sufficient protection against a disruptive scan. */
        if(!N->Ready || N->Stop || N->Paused || A->IoStopped)status=STATUS_DEVICE_NOT_READY;
        else if(N->Request || N->ControlBusy || N->RadioBusy || N->ScanBusy ||
            N->SelectingBand || N->Associated || N->Authorized ||
            A->NetworkPhase!=500 || A->NetworkStatus!=STATUS_SUCCESS)status=STATUS_DEVICE_BUSY;
        else {
            UCHAR country[2]={Buffer[4],Buffer[5]};
            generation=N->ScanReport.Generation+1;if(!generation)generation=1;
            RtlZeroMemory(&N->ScanReport,sizeof(N->ScanReport));
            N->ScanReport.Version=1;N->ScanReport.Generation=generation;
            N->ScanReport.State=1;N->ScanReport.Status=(ULONG)STATUS_PENDING;
            N->ScanReport.Country=(ULONG)country[0]|((ULONG)country[1]<<8);
            N->ScanCountry[0]=country[0];N->ScanCountry[1]=country[1];
            N->ScanComplete=FALSE;N->ScanAcceptEvents=FALSE;N->ScanEventStatus=0;
            InterlockedExchange(&N->ScanCancel,0);N->ScanBusy=TRUE;
            N->Request=4;KeSetEvent(&N->Wake,0,FALSE);status=STATUS_SUCCESS;
        }
    }
    KeReleaseSpinLock(&N->Lock,irql);
    return status;
}
/* Worker is stopped/starting: invalidate a cached result across D3/restart.
 * Generation is retained so a delayed cancel cannot target a later scan. */
static VOID CywScanReset(PRPI5CYW_ADAPTER A)
{
    CYW_NETWORK *N=A->Network;KIRQL irql;ULONG generation;
    KeAcquireSpinLock(&N->Lock,&irql);
    generation=N->ScanReport.Generation;
    RtlZeroMemory(&N->ScanReport,sizeof(N->ScanReport));
    N->ScanReport.Version=1;N->ScanReport.Generation=generation;
    N->ScanBusy=FALSE;N->ControlBusy=FALSE;N->ScanComplete=FALSE;N->ScanAcceptEvents=FALSE;
    N->ScanCancel=0;N->ScanEventStatus=0;
    KeReleaseSpinLock(&N->Lock,irql);
}
static VOID CywScanQuiesce(PRPI5CYW_ADAPTER A)
{
    CYW_NETWORK *N=A->Network;KIRQL irql;
    KeAcquireSpinLock(&N->Lock,&irql);
    if(N->ScanBusy) {
        InterlockedExchange(&N->ScanCancel,1);
        if(N->Request==4 && !N->ControlBusy) {
            N->Request=0;N->ScanReport.State=5;N->ScanReport.Status=(ULONG)STATUS_CANCELLED;
            N->ScanReport.Count=0;N->ScanBusy=FALSE;N->ScanAcceptEvents=FALSE;
        }
    }
    KeReleaseSpinLock(&N->Lock,irql);
}
