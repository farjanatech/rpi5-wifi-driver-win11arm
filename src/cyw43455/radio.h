/* SPDX-License-Identifier: GPL-3.0-or-later
 * Read-only, explicit-request radio snapshot. Single PASSIVE bus worker only.
 * Wire formats: Linux v6.12 brcmfmac fwil.h/fwil_types.h (ISC Broadcom).
 * No SET, channel selection, scan, PM change or periodic hot-path polling.
 */
#include "radio_protocol.h"
static NTSTATUS CywReadStationInfo(PRPI5CYW_ADAPTER A,const UCHAR Bssid[6],ULONG *Report)
{
    /* sta_info GET has an input MAC after its NUL-terminated IOVAR name.
     * Ordinary CywIovar GET intentionally has no input payload, so construct
     * this read-only request explicitly. FirmwareCommand bounds the reply. */
    UCHAR data[521];NTSTATUS status;
    RtlZeroMemory(data,sizeof(data));RtlCopyMemory(data,"sta_info",9);
    RtlCopyMemory(data+9,Bssid,6);
    status=CywFirmwareCommand(A,262,FALSE,data,sizeof(data));
    if(NT_SUCCESS(status) && !CywRadioParseStation(data,A->FirmwareReplyLength,Bssid,Report))
        status=STATUS_DEVICE_DATA_ERROR;
    RtlSecureZeroMemory(data,sizeof(data));return status;
}
static VOID CywReadRadio(PRPI5CYW_ADAPTER A,ULONG Report[CYW_RADIO_REPORT_WORDS])
{
    ULONG saved[7],i,capacity,channel;UCHAR data[20],bssid[6]={0};NTSTATUS status;
    ULONGLONG deadline=KeQueryInterruptTime()+150000000ULL;
    saved[0]=A->FirmwareCommand;saved[1]=A->FirmwareError;
    saved[2]=A->FirmwareReplyLength;saved[3]=A->FirmwareReplyDeclaredLength;
    saved[4]=A->FirmwareReplyPayloadLength;saved[5]=A->FirmwareRequestCapacity;
    saved[6]=A->FirmwareValueLength;
    RtlZeroMemory(Report,CYW_RADIO_REPORT_WORDS*sizeof(ULONG));Report[0]=CYW_RADIO_REPORT_VERSION;
    for(i=0;i<5;++i)Report[22+i]=(ULONG)STATUS_DEVICE_NOT_READY;
    for(i=0;i<4;++i)Report[4+i]=(ULONG)STATUS_DEVICE_NOT_READY;
    for(i=0;i<4;++i) {
        /* One command can already take five seconds. Stop starting GETs
         * after 15 seconds so the aggregate query remains below the utility's
         * 25-second wait absent unbounded scheduler/hardware preemption. */
        if(KeQueryInterruptTime()>=deadline) {
            Report[4+i]=(ULONG)STATUS_IO_TIMEOUT;
            if(!Report[3])Report[3]=(ULONG)STATUS_IO_TIMEOUT;
            goto Exit;
        }
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
        if(!NT_SUCCESS(status) && status!=STATUS_UNSUCCESSFUL && status!=STATUS_DEVICE_DATA_ERROR)goto Exit;
    }
    /* Optional, explicit-query-only evidence. Never run from the packet loop.
     * fwil.h: GET_RATE=12 (500kbps), GET_BSSID=23, GET_GET_PKTCNTS=137.
     * fwil_types.h: brcmf_pktcnt_le is exactly five little-endian ULONGs.
     * No guessed decoding of firmware-version-specific global counters blobs. */
    for(i=0;i<5;++i) {
        if(i==4 && !(Report[20]&CYW_RADIO_BSSID_VALID))continue;
        if(KeQueryInterruptTime()>=deadline) {
            Report[22+i]=(ULONG)STATUS_IO_TIMEOUT;
            if(!Report[21])Report[21]=(ULONG)STATUS_IO_TIMEOUT;
            goto Exit;
        }
        RtlZeroMemory(data,sizeof(data));A->FirmwareError=0;
        capacity=i==1?6u:(i==3?20u:4u);
        if(i==4) {
            status=CywReadStationInfo(A,bssid,Report);
        } else {
            status=i==2?CywIovar(A,"chanspec",FALSE,data,4):
                CywFirmwareCommand(A,i==0?12u:(i==1?23u:137u),FALSE,data,capacity);
            if(NT_SUCCESS(status) && A->FirmwareReplyLength!=capacity)status=STATUS_DEVICE_DATA_ERROR;
            if(NT_SUCCESS(status)) {
                if(i==0) {
                    channel=CywLe32(data);
                    if(!channel || channel>20000u)status=STATUS_DEVICE_DATA_ERROR;
                    else {Report[32]=channel;Report[20]|=CYW_RADIO_RATE_VALID;}
                } else if(i==1) {
                    if(!CywRadioMacValid(data))status=STATUS_DEVICE_DATA_ERROR;
                    else {RtlCopyMemory(bssid,data,6);Report[33]=CywLe32(data);
                        Report[34]=CywLe16(data+4);Report[20]|=CYW_RADIO_BSSID_VALID;}
                } else if(i==2) {
                    channel=CywLe32(data);
                    if(!channel || channel>0xffffu)status=STATUS_DEVICE_DATA_ERROR;
                    else {Report[35]=channel;Report[20]|=CYW_RADIO_CHANSPEC_VALID;}
                } else {
                    ULONG j;for(j=0;j<5;++j)Report[36+j]=CywLe32(data+4*j);
                    Report[20]|=CYW_RADIO_PKTCNT_VALID;
                }
            }
        }
        Report[22+i]=(ULONG)status;Report[27+i]=A->FirmwareError;
        if(!NT_SUCCESS(status) && !Report[21])Report[21]=(ULONG)status;
        if(!NT_SUCCESS(status) && status!=STATUS_UNSUCCESSFUL && status!=STATUS_DEVICE_DATA_ERROR)break;
    }
Exit:
    /* Optional observations must not overwrite connection failure diagnostics. */
    A->FirmwareCommand=saved[0];A->FirmwareError=saved[1];
    A->FirmwareReplyLength=saved[2];A->FirmwareReplyDeclaredLength=saved[3];
    A->FirmwareReplyPayloadLength=saved[4];A->FirmwareRequestCapacity=saved[5];
    A->FirmwareValueLength=saved[6];
}
