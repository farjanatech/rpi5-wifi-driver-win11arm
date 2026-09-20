/* SPDX-License-Identifier: GPL-3.0-or-later
 * Internal connection sequence, included by network.c and the host regression
 * harness. Firmware operations are supplied by the includer; no separate worker.
 * Country policy follows Linux v6.12 brcmfmac cfg80211.c (ISC Broadcom).
 */
static NTSTATUS CywConnect(PRPI5CYW_ADAPTER A,CYW_CONNECT_REQUEST *R)
{
    UCHAR country[12]={0},pmk[132]={0},ssid[36]={0};
    UCHAR rsn[22]={0x30,0x14,1,0,0,0x0f,0xac,4,1,0,0,0x0f,0xac,4,1,0,0,0x0f,0xac,2,0,0};
    NTSTATUS Status;
    A->Network->Associated=A->Network->Authorized=FALSE;CywLink(A,FALSE);
    A->NetworkPhase=510;A->NetworkStatus=STATUS_SUCCESS;
    A->CountryRequested=(ULONG)R->Country[0]|((ULONG)R->Country[1]<<8);
    A->CountryApplied=0;A->CountryRevision=0xffffffffUL;
#define STEP(n,op) do { A->ConnectStep=(n); TRY(op); } while(0)
    STEP(1,CywCmdInt(A,3,0));
    A->Network->Associated=A->Network->Authorized=FALSE;
    A->ConnectStep=2;
    if(!CywCountryRequest(R->Country,country)) {Status=STATUS_INVALID_PARAMETER;goto Exit;}
    STEP(2,CywIovar(A,"country",TRUE,country,sizeof(country)));
    RtlZeroMemory(country,sizeof(country));
    STEP(3,CywIovar(A,"country",FALSE,country,sizeof(country)));
    A->CountryApplied=(ULONG)country[8]|((ULONG)country[9]<<8);
    A->CountryRevision=CywLe32(country+4);
    if(!CywCountryMatches(R->Country,country,A->FirmwareReplyLength)) {
        Status=STATUS_DEVICE_DATA_ERROR;goto Exit;
    }
    STEP(4,CywCmdInt(A,20,1));STEP(5,CywCmdInt(A,22,0));STEP(6,CywCmdInt(A,134,4));
    STEP(7,CywCmdInt(A,165,0x80));STEP(8,CywInt(A,"mfp",0));
    STEP(9,CywInt(A,"sup_wpa",1));STEP(10,CywIovar(A,"wpaie",TRUE,rsn,sizeof(rsn)));
    CywPut16(pmk,32);RtlCopyMemory(pmk+4,R->Pmk,32);
    STEP(11,CywFirmwareCommand(A,268,TRUE,pmk,sizeof(pmk)));
    STEP(12,CywCmdInt(A,2,0));
    CywPut32(ssid,R->SsidLength);RtlCopyMemory(ssid+4,R->Ssid,R->SsidLength);
    STEP(13,CywFirmwareCommand(A,26,TRUE,ssid,sizeof(ssid)));
    A->NetworkPhase=520;
Exit:
    RtlSecureZeroMemory(pmk,sizeof(pmk));RtlSecureZeroMemory(ssid,sizeof(ssid));
    return Status;
#undef STEP
}
