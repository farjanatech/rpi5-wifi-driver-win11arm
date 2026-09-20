/* SPDX-License-Identifier: GPL-3.0-or-later
 * Internal BCDC/IOVAR transport, shared with the host regression harness.
 * Called by the single PASSIVE_LEVEL worker; no new worker or public ABI.
 * Protocol reference: Linux v6.12 brcmfmac/bcdc.c (ISC, Broadcom).
 */
NTSTATUS CywFirmwareCommand(PRPI5CYW_ADAPTER A, ULONG Command, BOOLEAN Set, PUCHAR Data, ULONG Length)
{
    CYW_NETWORK *N=A->Network;
    PUCHAR message;
    ULONG channel,off,len,flags,id,copy;
    ULONGLONG until;
    NTSTATUS Status=STATUS_IO_TIMEOUT;
    A->FirmwareReplyDeclaredLength=0;A->FirmwareReplyPayloadLength=0;
    A->FirmwareRequestCapacity=Length;A->FirmwareValueLength=0;
    if(Length>CYW_CONTROL_CAPACITY-28 || (Length && !Data))return STATUS_INVALID_PARAMETER;
    message=ExAllocatePool2(POOL_FLAG_NON_PAGED,Length+16,RPI5CYW_TAG);
    if(!message)return STATUS_INSUFFICIENT_RESOURCES;
    id=++N->RequestId;CywPut32(message,Command);CywPut32(message+4,Length);
    /* BCDC control flags have NO protocol-version bits: bit 1 means SET. */
    CywPut32(message+8,(id<<16)|(Set?2:0));
    if(Length)RtlCopyMemory(message+16,Data,Length);
    A->FirmwareCommand=Command;A->FirmwareError=0;A->FirmwareReplyLength=0;
    until=KeQueryInterruptTime()+50000000ULL;
    while(!CywTxCredit(N->TxSeq,N->TxMax,0) && KeQueryInterruptTime()<until && !N->Stop) {
        Status=CywPoll(A,&channel,&off,&len);
        if(!NT_SUCCESS(Status) && Status!=STATUS_NO_MORE_ENTRIES)goto Exit;
        SdioDelayMilliseconds(1);
    }
    TRY(CywSendFrame(A,0,message,Length+16));
    while(KeQueryInterruptTime()<until && !N->Stop) {
        Status=CywPoll(A,&channel,&off,&len);
        if(Status==STATUS_NO_MORE_ENTRIES) {SdioDelayMilliseconds(2);continue;}
        if(!NT_SUCCESS(Status))goto Exit;
        if(channel!=0)continue;
        if(off>len || len>CYW_WIRE_CAPACITY || len-off<16 || len-off>CYW_CONTROL_CAPACITY) {Status=STATUS_DEVICE_DATA_ERROR;goto Exit;}
        flags=CywLe32(N->Rx+off+8);
        if((flags>>16)!=id || CywLe32(N->Rx+off)!=Command)continue;
        A->FirmwareReplyDeclaredLength=CywLe32(N->Rx+off+4);
        A->FirmwareReplyPayloadLength=len-off-16;
        if(flags&1) {A->FirmwareError=CywLe32(N->Rx+off+12);Status=STATUS_UNSUCCESSFUL;goto Exit;}
        /* BCDC len is buffer metadata, not an exact value length. Only the
         * validated SDPCM frame establishes how many reply bytes arrived.
         * Linux brcmfmac likewise bounds copies by received bytes/capacity. */
        copy=A->FirmwareReplyPayloadLength;if(copy>Length)copy=Length;
        if(!Set) {
            RtlZeroMemory(Data,Length);RtlCopyMemory(Data,N->Rx+off+16,copy);
            A->FirmwareReplyLength=copy;
        }
        Status=STATUS_SUCCESS;goto Exit;
    }
    Status=N->Stop?STATUS_CANCELLED:STATUS_IO_TIMEOUT;
Exit:
    RtlSecureZeroMemory(message,Length+16);ExFreePoolWithTag(message,RPI5CYW_TAG);
    return Status;
}
NTSTATUS CywIovar(PRPI5CYW_ADAPTER A,const char *Name,BOOLEAN Set,PUCHAR Data,ULONG Length)
{
    ULONG n=0,total; PUCHAR b; NTSTATUS Status;
    if(!Name)return STATUS_INVALID_PARAMETER;
    while(n<64 && Name[n])++n;
    if(!n || n==64 || (Length && !Data) || Length>CYW_CONTROL_CAPACITY-128)return STATUS_INVALID_PARAMETER;
    ++n;total=n+Length;b=ExAllocatePool2(POOL_FLAG_NON_PAGED,total,RPI5CYW_TAG);
    if(!b)return STATUS_INSUFFICIENT_RESOURCES;
    RtlCopyMemory(b,Name,n);if(Set && Length)RtlCopyMemory(b+n,Data,Length);
    Status=CywFirmwareCommand(A,Set?263:262,Set,b,total);
    A->FirmwareValueLength=Length;
    if(!Set && NT_SUCCESS(Status)) {
        /* Do not manufacture a zero status or valid country from a short reply.
         * Copy just the requested value; extra transport buffer space is not
         * part of that value. Retain raw lengths in separate diagnostics. */
        if(A->FirmwareReplyLength<Length)Status=STATUS_DEVICE_DATA_ERROR;
        else {RtlCopyMemory(Data,b,Length);A->FirmwareReplyLength=Length;}
    }
    RtlSecureZeroMemory(b,total);ExFreePoolWithTag(b,RPI5CYW_TAG);return Status;
}
