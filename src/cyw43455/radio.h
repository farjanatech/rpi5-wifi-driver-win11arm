/* SPDX-License-Identifier: GPL-3.0-or-later
 * Read-only, explicit-request radio snapshot. Single PASSIVE bus worker only.
 * Wire formats: Linux v6.12 brcmfmac fwil.h/fwil_types.h (ISC Broadcom).
 * No SET, channel selection, scan, PM change or periodic hot-path polling.
 */
static VOID CywReadRadio(PRPI5CYW_ADAPTER A,ULONG Report[20])
{
    ULONG saved[7],i,capacity,channel;UCHAR data[12];NTSTATUS status;
    saved[0]=A->FirmwareCommand;saved[1]=A->FirmwareError;
    saved[2]=A->FirmwareReplyLength;saved[3]=A->FirmwareReplyDeclaredLength;
    saved[4]=A->FirmwareReplyPayloadLength;saved[5]=A->FirmwareRequestCapacity;
    saved[6]=A->FirmwareValueLength;
    RtlZeroMemory(Report,20*sizeof(ULONG));Report[0]=1;
    for(i=0;i<4;++i)Report[4+i]=(ULONG)STATUS_DEVICE_NOT_READY;
    for(i=0;i<4;++i) {
        RtlZeroMemory(data,sizeof(data));capacity=i<2?12:4;
        A->FirmwareError=0;
        status=i==3?CywIovar(A,"mpc",FALSE,data,4):
            CywFirmwareCommand(A,i==0?29:(i==1?127:85),FALSE,data,capacity);
        Report[15+i]=A->FirmwareError;
        if(NT_SUCCESS(status) && A->FirmwareReplyLength!=capacity)status=STATUS_DEVICE_DATA_ERROR;
        if(NT_SUCCESS(status)) {
            if(i==0) {
                Report[8]=CywLe32(data);Report[9]=CywLe32(data+4);Report[10]=CywLe32(data+8);
                channel=Report[8];
                /* Do not label a scan/different hardware channel as the joined band. */
                if(channel!=Report[9] || Report[10] || !channel || channel>196 ||
                   (channel>14 && channel<32))status=STATUS_DEVICE_DATA_ERROR;
                else Report[11]=channel<=14?2400:5000;
            } else if(i==1) {
                Report[12]=CywLe32(data);
                if((LONG)Report[12]>=0 || (LONG)Report[12]<-127)status=STATUS_DEVICE_DATA_ERROR;
            } else if(i==2) {
                Report[13]=CywLe32(data);if(Report[13]>2)status=STATUS_DEVICE_DATA_ERROR;
            } else {
                Report[14]=CywLe32(data);if(Report[14]>1)status=STATUS_DEVICE_DATA_ERROR;
            }
        }
        Report[4+i]=(ULONG)status;
        if(NT_SUCCESS(status))Report[2]|=1u<<i;
        else if(!Report[3])Report[3]=(ULONG)status;
        /* Stop on a transport/allocation failure; unsupported/malformed values
         * remain unknown but do not suppress independent readbacks. */
        if(!NT_SUCCESS(status) && status!=STATUS_UNSUCCESSFUL && status!=STATUS_DEVICE_DATA_ERROR)break;
    }
    /* Optional observations must not overwrite connection failure diagnostics. */
    A->FirmwareCommand=saved[0];A->FirmwareError=saved[1];
    A->FirmwareReplyLength=saved[2];A->FirmwareReplyDeclaredLength=saved[3];
    A->FirmwareReplyPayloadLength=saved[4];A->FirmwareRequestCapacity=saved[5];
    A->FirmwareValueLength=saved[6];
}
