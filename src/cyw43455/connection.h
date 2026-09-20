/* SPDX-License-Identifier: GPL-3.0-or-later
 * Internal connection sequence, included by network.c and the host regression
 * harness. Firmware operations are supplied by the includer; no separate worker.
 * Country policy follows Linux v6.12 brcmfmac cfg80211.c (ISC Broadcom).
 */
static NTSTATUS CywConnect(PRPI5CYW_ADAPTER A,CYW_CONNECT_REQUEST *R)
{
    UCHAR country[12]={0},clm[4]={0},pmk[132]={0},ssid[36]={0},countries[1024]={0};
    uint32_t countryCount=0,countryListed=0;
    UCHAR rsn[22]={0x30,0x14,1,0,0,0x0f,0xac,4,1,0,0,0x0f,0xac,4,1,0,0,0x0f,0xac,2,0,0};
    NTSTATUS Status;
    A->Network->Associated=A->Network->Authorized=FALSE;CywLink(A,FALSE);
    A->NetworkPhase=510;A->NetworkStatus=STATUS_SUCCESS;
    A->CountryRequested=(ULONG)R->Country[0]|((ULONG)R->Country[1]<<8);
    A->CountryApplied=0;A->CountryRevision=0xffffffffUL;
    A->CountryBefore=0;A->CountryBeforeRevision=0xffffffffUL;
    A->CountrySetMode=0;A->CountryExplicitError=0;
    A->ClmLoadStatus=0xffffffffUL;A->ClmQueryStatus=(NTSTATUS)0x103;
    A->CountryListStatus=(NTSTATUS)0x103;A->CountryListError=0;
    A->CountryListCount=0;A->CountryListMembership=0;A->CountryListReplyLength=0;
#define STEP(n,op) do { A->ConnectStep=(n); TRY(op); } while(0)
    STEP(1,CywCmdInt(A,3,0));
    A->Network->Associated=A->Network->Authorized=FALSE;
    /* Observe the loaded CLM before applying any country. Older firmware may
     * lack this diagnostic IOVAR (-23); only that specific case is optional. */
    A->ConnectStep=15;
    Status=CywIovar(A,"clmload_status",FALSE,clm,sizeof(clm));
    A->ClmQueryStatus=Status;
    if(NT_SUCCESS(Status)) {
        if(A->FirmwareReplyLength!=sizeof(clm)) {Status=STATUS_DEVICE_DATA_ERROR;goto Exit;}
        A->ClmLoadStatus=CywLe32(clm);
        if(A->ClmLoadStatus) {Status=STATUS_DEVICE_DATA_ERROR;goto Exit;}
    } else if(Status!=STATUS_UNSUCCESSFUL || A->FirmwareError!=0xffffffe9UL) goto Exit;
    /* Optional bounded read-only query. Unsupported/malformed results remain
     * unknown; the actual country SET/readback remains the authority. */
    A->ConnectStep=17;CywPut32(countries,sizeof(countries));
    A->FirmwareError=0;A->FirmwareReplyLength=0;
    A->CountryListStatus=CywFirmwareCommand(A,261,FALSE,countries,sizeof(countries));
    A->CountryListError=A->FirmwareError;
    A->CountryListReplyLength=A->FirmwareReplyLength;
    if(NT_SUCCESS(A->CountryListStatus)) {
        if(!CywCountryList(countries,A->CountryListReplyLength,R->Country,&countryCount,&countryListed))
            A->CountryListStatus=STATUS_DEVICE_DATA_ERROR;
        else {
            A->CountryListCount=countryCount;
            /* Empty list is inconclusive, not evidence of no supported country. */
            if(countryCount)A->CountryListMembership=countryListed?1:2;
        }
    }
    STEP(14,CywIovar(A,"country",FALSE,country,sizeof(country)));
    if(A->FirmwareReplyLength!=sizeof(country)) {Status=STATUS_DEVICE_DATA_ERROR;goto Exit;}
    A->CountryBefore=(ULONG)country[0]|((ULONG)country[1]<<8);
    A->CountryBeforeRevision=CywLe32(country+4);
    if(CywCountryMatches(R->Country,country,A->FirmwareReplyLength)) {
        A->CountrySetMode=1; /* Already matches the user's physical location. */
        goto CountryVerified;
    }
    A->ConnectStep=2;A->CountrySetMode=2;
    if(!CywCountryRequest(R->Country,country)) {Status=STATUS_INVALID_PARAMETER;goto Exit;}
    Status=CywIovar(A,"country",TRUE,country,sizeof(country));
    if(!NT_SUCCESS(Status)) {
        A->CountryExplicitError=A->FirmwareError;
        if(Status!=STATUS_UNSUCCESSFUL || A->FirmwareError!=0xfffffffeUL) goto Exit;
        /* Full wl_country_t, revision -1, as used by ReactOS CywSetCountry.
         * Both country fields stay the requested ISO code. Never substitute
         * another country or proceed without matching nonnegative readback. */
        if(!CywCountryRequest(R->Country,country)) {Status=STATUS_INVALID_PARAMETER;goto Exit;}
        CywPut32(country+4,0xffffffffUL);
        A->CountrySetMode=4;
        STEP(16,CywIovar(A,"country",TRUE,country,sizeof(country)));
    }
    RtlZeroMemory(country,sizeof(country));
    STEP(3,CywIovar(A,"country",FALSE,country,sizeof(country)));
    if(!CywCountryMatches(R->Country,country,A->FirmwareReplyLength)) {
        Status=STATUS_DEVICE_DATA_ERROR;goto Exit;
    }
CountryVerified:
    A->CountryApplied=(ULONG)country[8]|((ULONG)country[9]<<8);
    A->CountryRevision=CywLe32(country+4);
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
