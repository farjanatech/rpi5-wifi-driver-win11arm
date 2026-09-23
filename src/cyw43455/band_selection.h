/* SPDX-License-Identifier: GPL-3.0-or-later
 * Included after join_preference.h by connection.h. Only the existing
 * PASSIVE worker calls this while holding connected publication. It keeps
 * no credentials and never performs radio queries during data traffic.
 */
static BOOLEAN CywBandCancelled(PRPI5CYW_ADAPTER A)
{
    return A->IoStopped || A->Network->Stop || A->Network->Paused || !A->Network->Powered ||
        CywBandRequestPending(A);
}
static NTSTATUS CywBandVerify(PRPI5CYW_ADAPTER A,ULONG Out[4],ULONGLONG Deadline)
{
    UCHAR channel[12]={0},rssi[12]={0},before[6]={0},after[6]={0},station[521]={0};
    ULONG i,command,length;PUCHAR data;NTSTATUS status;
    for(i=0;i<5;i++) {
        if(CywBandCancelled(A))return STATUS_CANCELLED;
        if(KeQueryInterruptTime()>=Deadline)return STATUS_IO_TIMEOUT;
        command=i==1?29u:(i==2?127u:(i==3?262u:23u));
        data=i==0?before:(i==1?channel:(i==2?rssi:(i==3?station:after)));
        length=i==1 || i==2?12u:(i==3?(ULONG)sizeof(station):6u);
        if(i==3) {RtlCopyMemory(station,"sta_info",9);RtlCopyMemory(station+9,before,6);}
        status=CywFirmwareCommand(A,command,FALSE,data,length);
        if(!NT_SUCCESS(status))return status;
        if(i==3) {
            if(!CywBandStationAuthorized(station,A->FirmwareReplyLength,before))return STATUS_DEVICE_DATA_ERROR;
        } else if(A->FirmwareReplyLength!=length)return STATUS_DEVICE_DATA_ERROR;
        if(!A->Network->Associated || !A->Network->Authorized)return STATUS_DEVICE_DATA_ERROR;
    }
    if(CywBandCancelled(A))return STATUS_CANCELLED;
    return CywBandReadback(channel,12,rssi,12,before,6,after,6,Out)?
        STATUS_SUCCESS:STATUS_DEVICE_DATA_ERROR;
}
static NTSTATUS CywBandWait(PRPI5CYW_ADAPTER A,ULONG Out[4],ULONGLONG Deadline)
{
    ULONG channel,offset,length;NTSTATUS status;
    /* FirmwareCommand has an existing five-second in-flight bound. Check the
     * overall deadline before each GET, so it can overrun by at most that one
     * operation (scheduler/hardware preemption aside), not five full waits. */
    while(KeQueryInterruptTime()<Deadline) {
        if(CywBandCancelled(A))return STATUS_CANCELLED;
        if(A->Network->Associated && A->Network->Authorized)
            return CywBandVerify(A,Out,Deadline);
        status=CywPoll(A,&channel,&offset,&length); /* dispatches auth events */
        if(status==STATUS_NO_MORE_ENTRIES)SdioDelayMilliseconds(2);
        else if(!NT_SUCCESS(status))return status;
    }
    return CywBandCancelled(A)?STATUS_CANCELLED:STATUS_IO_TIMEOUT;
}
static NTSTATUS CywJoinSelectedBand(PRPI5CYW_ADAPTER A,UCHAR Ssid[36])
{
    ULONG *r=A->BandSelection;UCHAR disassoc[12]={0};
    ULONGLONG started=KeQueryInterruptTime();NTSTATUS status=STATUS_SUCCESS,initial=STATUS_SUCCESS,restore;
    ULONGLONG deadline=started+CYW_BAND_JOIN_WAIT_100NS;
    BOOLEAN fallback=FALSE;
    A->Network->SelectingBand=TRUE;r[1]=1;r[2]=1;
    if(CywBandCancelled(A)) {status=initial=STATUS_CANCELLED;goto Exit;}
    r[13]=1;A->Network->Associated=A->Network->Authorized=FALSE;
    A->ConnectStep=13;status=CywFirmwareCommand(A,26,TRUE,Ssid,36);
    if(NT_SUCCESS(status)) {A->ConnectStep=19;status=CywBandWait(A,r+5,deadline);}
    initial=status;r[3]=(ULONG)status;
    if(CywBandCancelled(A) || status==STATUS_CANCELLED) {status=STATUS_CANCELLED;goto Exit;}
    if(NT_SUCCESS(status)) {
        r[2]|=2;
        if(!CywBandCandidateUsable(r[5],r[6])) {fallback=TRUE;r[15]=2;}
    } else if(status==STATUS_IO_TIMEOUT || status==STATUS_DEVICE_DATA_ERROR || status==STATUS_UNSUCCESSFUL) {
        fallback=TRUE;r[15]=A->ConnectStep==13?4u:(status==STATUS_IO_TIMEOUT?1u:3u);
    } else goto Exit; /* Do not hide a transport/allocation failure by retrying. */

    A->ConnectStep=20;restore=CywSetJoinPreference(A,FALSE);
    /* Once accepted, the initial preference must be removed successfully.
     * An unsupported restore is not success, even though unsupported initial
     * setup is allowed to take the legacy asynchronous path. */
    if(!NT_SUCCESS(restore) || !A->JoinPreferenceAccepted) {
        status=NT_SUCCESS(restore)?A->JoinPreferenceStatus:restore;goto Exit;
    }
    r[2]|=8;
    if(CywBandCancelled(A)) {status=STATUS_CANCELLED;goto Exit;}
    if(!fallback) {
        /* Restoring preference itself processes asynchronous events. Observe
         * the final current association again after that command; never copy
         * an obsolete pre-restore BSSID/radio/auth snapshot as final evidence.
         * This uses the SAME initial deadline, not a fresh unbounded window. */
        A->ConnectStep=24;status=CywBandVerify(A,r+9,deadline);
        if(NT_SUCCESS(status)) {
            if(!CywBandCandidateUsable(r[9],r[10])) {fallback=TRUE;r[15]=2;}
        } else if(status==STATUS_IO_TIMEOUT || status==STATUS_DEVICE_DATA_ERROR || status==STATUS_UNSUCCESSFUL) {
            fallback=TRUE;r[15]=status==STATUS_IO_TIMEOUT?1u:3u;
        } else goto Exit;
    }
    if(fallback) {
        r[1]=4;r[2]|=4;
        /* wl_scb_val: reason + BSSID + alignment padding, as used by Linux
         * brcmf_cfg80211_disconnect. Explicit disassociation replaces an old
         * in-progress join without DOWN (which could discard key state). */
        CywPut32(disassoc,3);CywPut32(disassoc+4,r[7]);CywPut16(disassoc+8,(USHORT)r[8]);
        A->ConnectStep=21;status=CywFirmwareCommand(A,52,TRUE,disassoc,sizeof(disassoc));
        /* BCME_NOTASSOCIATED (-17), Linux brcmfmac fwil.c error table: an
         * unassociated initial timeout is already in the required state. */
        if(status==STATUS_UNSUCCESSFUL && A->FirmwareError==0xffffffefUL)status=STATUS_SUCCESS;
        if(!NT_SUCCESS(status))goto Exit;
        A->Network->Associated=A->Network->Authorized=FALSE;
        if(CywBandCancelled(A)) {status=STATUS_CANCELLED;goto Exit;}
        deadline=KeQueryInterruptTime()+CYW_BAND_JOIN_WAIT_100NS;
        A->ConnectStep=22;r[13]=2;status=CywFirmwareCommand(A,26,TRUE,Ssid,36);
        if(!NT_SUCCESS(status))goto Exit;
        A->ConnectStep=23;status=CywBandWait(A,r+9,deadline);
        if(!NT_SUCCESS(status))goto Exit;
        r[1]=5;
    } else {
        /* No firmware radio query occurs after releasing the startup hold. */
        r[1]=r[9]>14?2u:3u;status=STATUS_SUCCESS;
    }
    if(CywBandCancelled(A)) {status=STATUS_CANCELLED;goto Exit;}
    if(!A->Network->Associated || !A->Network->Authorized) {status=STATUS_DEVICE_DATA_ERROR;goto Exit;}
    r[2]|=16;
Exit:
    r[3]=(ULONG)initial;r[4]=(ULONG)status;
    r[14]=(ULONG)((KeQueryInterruptTime()-started)/10000ULL);
    if(!NT_SUCCESS(status)) {
        r[1]=status==STATUS_CANCELLED?8u:6u;
        A->Network->Associated=A->Network->Authorized=FALSE;
    }
    /* Keep failed selection unpublished even if an old firmware auth event
     * arrives late. A fresh connect/disconnect or power reset owns clearing
     * this hold; successful verification is the only local release. */
    A->Network->SelectingBand=!NT_SUCCESS(status);
    CywLink(A,NT_SUCCESS(status) && A->Network->Associated && A->Network->Authorized);
    A->NetworkPhase=NT_SUCCESS(status)?600:510;
    RtlSecureZeroMemory(disassoc,sizeof(disassoc));return status;
}
