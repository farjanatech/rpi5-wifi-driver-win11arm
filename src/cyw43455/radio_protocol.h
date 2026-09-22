#pragma once
/* SPDX-License-Identifier: GPL-3.0-or-later
 * Radio report v2 keeps the first 20 ULONGs of v1. Its extension never changes
 * connection policy. A clear valid bit means unknown, not a zero measurement.
 * Station wire offsets below are the naturally aligned, common prefix of
 * Linux v6.12 brcmfmac/fwil_types.h brcmf_sta_info_le (ISC Broadcom).
 * Versions 3, 4, 5 and 7 ONLY; unknown future layouts are not guessed.
 */
#define CYW_RADIO_REPORT_WORDS 64u
#define CYW_RADIO_REPORT_VERSION 2u
#define CYW_RADIO_RATE_VALID 1u
#define CYW_RADIO_BSSID_VALID 2u
#define CYW_RADIO_CHANSPEC_VALID 4u
#define CYW_RADIO_PKTCNT_VALID 8u
#define CYW_RADIO_STA_VALID 16u
#define CYW_RADIO_STA_STATS_VALID 32u
#define CYW_RADIO_STA_RETRIES_VALID 64u
#define CYW_RADIO_STA_TXRATE_VALID 128u
#define CYW_RADIO_STA_RXRATE_VALID 256u
/* Extension words: 20 mask, 21 first error, 22..26 per-query NTSTATUS,
 * 27..31 firmware errors (rate/BSSID/chanspec/pktcnt/sta_info), 32 rate in
 * 500kbps units, 33..34 BSSID little-endian bytes, 35 raw chanspec,
 * 36..40 firmware rx_good/rx_bad/tx_good/tx_bad/rx_other packet counts.
 * 41..43 station version/length/flags, 44..45 last TX/RX rate (kbps),
 * 46..50 station TX packets/failures/RX unicast/RX multicast/decrypt failures,
 * 51..59 retry_count_raw/retry_exhausted/user_TX_success/user_TX_retries/
 * FW_TX_success/FW_TX_retries/FW_retry_exhausted/RX_retried/fallback_kbps.
 * 60..63 reserved zero. Counters are cumulative firmware observations, not
 * proof that a particular host packet was acknowledged. No payloads or SSID.
 */
static __inline unsigned int CywRadioLe32(const unsigned char *p)
{return p[0]|(unsigned int)p[1]<<8|(unsigned int)p[2]<<16|(unsigned int)p[3]<<24;}
static __inline unsigned int CywRadioLe16(const unsigned char *p)
{return p[0]|(unsigned int)p[1]<<8;}
static __inline int CywRadioMacValid(const unsigned char *p)
{
    unsigned int i,any=0;
    if(!p || (p[0]&1))return 0;
    for(i=0;i<6;++i)any|=p[i];
    return any!=0;
}
static __inline int CywRadioParseStation(const unsigned char *p,unsigned int available,
                                        const unsigned char *bssid,unsigned long *r)
{
    unsigned int version,length,flags,i,minimum;
    if(!p || !bssid || !r || available<4)return 0;
    version=CywRadioLe16(p);length=CywRadioLe16(p+2);
    if(version!=3 && version!=4 && version!=5 && version!=7)return 0;
    minimum=version==3?84u:(version==4?200u:(version==5?252u:296u));
    if(length<minimum || length>available || available>521u || !CywRadioMacValid(bssid))return 0;
    for(i=0;i<6;++i)if(p[16+i]!=bssid[i])return 0;
    if(CywRadioLe32(p+24)>16u)return 0;
    flags=CywRadioLe32(p+8);
    r[41]=version;r[42]=length;r[43]=flags;r[20]|=CYW_RADIO_STA_VALID;
    /* Firmware marks these fields valid with BRCMF_STA_SCBSTATS. */
    if(!(flags&0x4000u))return 1;
    r[46]=CywRadioLe32(p+52);r[47]=CywRadioLe32(p+56);
    r[48]=CywRadioLe32(p+60);r[49]=CywRadioLe32(p+64);r[50]=CywRadioLe32(p+80);
    r[20]|=CYW_RADIO_STA_STATS_VALID;
    for(i=0;i<2;++i) {
        unsigned int rate=CywRadioLe32(p+68+4*i);
        unsigned int havePackets=i?(r[48]!=0 || r[49]!=0):
            (r[46]!=0 || (version>=4 && CywRadioLe32(p+92)!=0));
        /* Zero is unavailable; no maximum rate is substituted. */
        if(havePackets && rate && rate<=10000000u) {r[44+i]=rate;r[20]|=i?CYW_RADIO_STA_RXRATE_VALID:CYW_RADIO_STA_TXRATE_VALID;}
    }
    if(version>=4) {
        r[51]=CywRadioLe32(p+160);r[52]=CywRadioLe32(p+164);
        for(i=0;i<7;++i)r[53+i]=CywRadioLe32(p+172+4*i);
        r[20]|=CYW_RADIO_STA_RETRIES_VALID;
    }
    return 1;
}
