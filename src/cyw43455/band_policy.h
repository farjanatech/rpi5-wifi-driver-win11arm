#pragma once
/* SPDX-License-Identifier: GPL-3.0-or-later
 * Startup policy, not a throughput measurement or persistent band lock.
 * join_pref and GET_CHANNEL/GET_RSSI/GET_BSSID formats: Linux v6.12
 * brcmfmac cfg80211.c and fwil_types.h (ISC Broadcom).
 */
#include "radio_protocol.h"
#define CYW_BAND_SELECTION_WORDS 16u
#define CYW_BAND_SELECTION_VERSION 1u
#define CYW_BAND_JOIN_WAIT_100NS 150000000ULL
#define CYW_BAND_MIN_5GHZ_RSSI (-70)

static __inline unsigned CywBuildJoinPreference(int prefer5,unsigned char out[8])
{
    static const unsigned char band[8]={3,2,0,1,1,2,0,0};
    static const unsigned char rssi[8]={1,2,0,0,0,0,0,0};
    unsigned i;for(i=0;i<8;i++)out[i]=prefer5?band[i]:rssi[i];
    return prefer5?8u:4u;
}
static __inline int CywBandMacValid(const unsigned char *p)
{
    unsigned i,any=0;if(!p || (p[0]&1))return 0;
    for(i=0;i<6;i++)any|=p[i];return any!=0;
}
static __inline int CywBandReadback(const unsigned char *channel,unsigned channelLength,
    const unsigned char *rssi,unsigned rssiLength,const unsigned char *before,unsigned beforeLength,
    const unsigned char *after,unsigned afterLength,unsigned long out[4])
{
    unsigned c,i;int dbm;
    if(!channel || !rssi || !before || !after || !out || channelLength!=12 ||
       rssiLength!=12 || beforeLength!=6 || afterLength!=6)return 0;
    if(!CywBandMacValid(before) || !CywBandMacValid(after))return 0;
    for(i=0;i<6;i++)if(before[i]!=after[i])return 0;
    c=CywLe32(channel);dbm=(int)CywLe32(rssi);
    if(!c || c>196 || (c>14 && c<32) || CywLe32(channel+4)!=c ||
       CywLe32(channel+8) || dbm>=0 || dbm< -127)return 0;
    out[0]=c;out[1]=(unsigned long)dbm;
    out[2]=CywLe32(before);out[3]=CywLe16(before+4);return 1;
}
static __inline int CywBandCandidateUsable(unsigned long channel,unsigned long rssi)
{
    /* Prefer a reasonably strong 5 GHz association, not an arbitrarily weak
     * one. RSSI is only a conservative eligibility floor, not a speed score.
     * A valid 2.4 GHz result is permitted if firmware found no preferred AP. */
    return channel<=14 || (int)rssi>=CYW_BAND_MIN_5GHZ_RSSI;
}
static __inline int CywBandStationAuthorized(const unsigned char *data,unsigned length,
                                            const unsigned char bssid[6])
{
    unsigned long report[CYW_RADIO_REPORT_WORDS]={0};
    /* Fresh firmware station identity and authorization, not delayed LINK/
     * SUP_PSK events from an earlier join. Reuse the already validated,
     * versioned station parser; unsupported layouts are not guessed. */
    return CywRadioParseStation(data,length,bssid,report) && (report[43]&0x30u)==0x30u;
}
