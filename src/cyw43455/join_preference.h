/* SPDX-License-Identifier: GPL-3.0-or-later
 * join_pref wire format/policy: Linux v6.12 brcmfmac cfg80211.c,
 * brcmf_set_join_pref (ISC Broadcom); see THIRD_PARTY_NOTICES.md.
 * BAND preference is used only for the bounded initial join. RSSI-only is
 * restored before publication/fallback. This is NOT SET_BAND; both bands
 * remain eligible and the firmware uses the requested SSID/country/security.
 */
#include "band_policy.h"
static NTSTATUS CywSetJoinPreference(PRPI5CYW_ADAPTER A,BOOLEAN Prefer5)
{
    UCHAR preference[8];ULONG length=CywBuildJoinPreference(Prefer5,preference);
    NTSTATUS status;
    A->JoinPreferenceAccepted=0;A->JoinPreferenceError=0;
    A->FirmwareError=0;
    status=CywIovar(A,"join_pref",TRUE,preference,length);
    A->JoinPreferenceStatus=status;A->JoinPreferenceError=A->FirmwareError;
    if(NT_SUCCESS(status))A->JoinPreferenceAccepted=1;
    /* Only an explicit unsupported IOVAR permits default firmware selection.
     * Other firmware/transport errors remain failures, not false success. */
    if(status==STATUS_UNSUCCESSFUL && A->FirmwareError==0xffffffe9UL)
        return STATUS_SUCCESS;
    return status;
}
static NTSTATUS CywApplyJoinPreference(PRPI5CYW_ADAPTER A)
{return CywSetJoinPreference(A,TRUE);}
