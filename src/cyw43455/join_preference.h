/* SPDX-License-Identifier: GPL-3.0-or-later
 * join_pref wire format/policy: Linux v6.12 brcmfmac common.c,
 * brcmf_c_set_joinpref_default (ISC Broadcom); see THIRD_PARTY_NOTICES.md.
 * RSSI_DELTA +8 dB on 5 GHz, then RSSI ranking. This is NOT SET_BAND and
 * leaves 2.4 GHz eligible. Firmware chooses among the requested SSID's BSSes.
 * Applied once per join by the existing serialized worker, not while loading.
 */
static NTSTATUS CywApplyJoinPreference(PRPI5CYW_ADAPTER A)
{
    UCHAR preference[8]={4,2,8,1, 1,2,0,0};
    NTSTATUS status;
    A->JoinPreferenceAccepted=0;A->JoinPreferenceError=0;
    A->FirmwareError=0;
    status=CywIovar(A,"join_pref",TRUE,preference,sizeof(preference));
    A->JoinPreferenceStatus=status;A->JoinPreferenceError=A->FirmwareError;
    if(NT_SUCCESS(status))A->JoinPreferenceAccepted=1;
    /* Only an explicit unsupported IOVAR permits default firmware selection.
     * Other firmware/transport errors remain failures, not false success. */
    if(status==STATUS_UNSUCCESSFUL && A->FirmwareError==0xffffffe9UL)
        return STATUS_SUCCESS;
    return status;
}
