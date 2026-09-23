/* SPDX-License-Identifier: GPL-3.0-or-later
 * Explicit disconnected scan ABI and byte-oriented firmware decoder.
 * Wire reference: Linux v6.12 brcmfmac/fwil_types.h (ISC, Broadcom),
 * brcmf_scan_params_le, brcmf_escan_result_le and BSS_INFO_VERSION 109.
 * https://github.com/torvalds/linux/blob/v6.12/drivers/net/wireless/broadcom/brcm80211/brcmfmac/fwil_types.h
 * No casts to firmware structs: v109 contains natural-alignment padding.
 */
#ifndef CYW_SCAN_PROTOCOL_H
#define CYW_SCAN_PROTOCOL_H
#if !defined(NDIS_MINIPORT_DRIVER) || defined(RPI5CYW_HOST_TEST)
#include <stddef.h>
#include <string.h>
#endif

#define CYW_SCAN_VERSION 1u
#define CYW_SCAN_MAX_ENTRIES 64u
#define CYW_SCAN_IDLE 0u
#define CYW_SCAN_QUEUED 1u
#define CYW_SCAN_RUNNING 2u
#define CYW_SCAN_COMPLETE 3u
#define CYW_SCAN_FAILED 4u
#define CYW_SCAN_CANCELLED 5u
#define CYW_SCAN_TRUNCATED 1u
#define CYW_SCAN_SECURITY_OPEN 1u
#define CYW_SCAN_SECURITY_WPA2_PSK_CCMP 2u
#define CYW_SCAN_SECURITY_UNSUPPORTED 4u
#define CYW_SCAN_SECURITY_HIDDEN 8u
#define CYW_SCAN_SECURITY_MALFORMED_IE 16u
#define CYW_SCAN_SECURITY_PMF_REQUIRED 32u
#define CYW_SCAN_REQUEST_SIZE 72u
#define CYW_SCAN_ABORT_SIZE 68u

typedef struct _CYW_SCAN_ENTRY {
    unsigned SsidLength;
    unsigned char Ssid[32],Bssid[6];
    unsigned short Chanspec;
    int Rssi;
    unsigned SecurityFlags,ControlChannel;
} CYW_SCAN_ENTRY;
typedef struct _CYW_SCAN_REPORT {
    unsigned Version,Generation,State,Status,Count,Flags,Country,FirmwareError;
    CYW_SCAN_ENTRY Entries[CYW_SCAN_MAX_ENTRIES];
} CYW_SCAN_REPORT;
typedef char CYW_SCAN_ENTRY_SIZE_CHECK[sizeof(CYW_SCAN_ENTRY)==56?1:-1];
typedef char CYW_SCAN_REPORT_SIZE_CHECK[sizeof(CYW_SCAN_REPORT)==3616?1:-1];

static __inline unsigned CywScanU16(const unsigned char *p)
{return (unsigned)p[0]|((unsigned)p[1]<<8);}
static __inline unsigned CywScanU32(const unsigned char *p)
{return CywScanU16(p)|(CywScanU16(p+2)<<16);}
static __inline void CywScanPut16(unsigned char *p,unsigned v)
{p[0]=(unsigned char)(v&255u);p[1]=(unsigned char)((v>>8)&255u);}
static __inline void CywScanPut32(unsigned char *p,unsigned v)
{CywScanPut16(p,v);CywScanPut16(p+2,v>>16);}

/* Passive wildcard scan: no probe requests or guessed foreign channels.
 * The firmware uses its currently verified country/CLM channel list. */
static __inline void CywScanBuildRequest(unsigned char *p,unsigned sync)
{
    memset(p,0,CYW_SCAN_REQUEST_SIZE);
    CywScanPut32(p,1);CywScanPut16(p+4,1);CywScanPut16(p+6,sync);
    memset(p+44,255,6);p[50]=2;p[51]=1;
    memset(p+52,255,16); /* nprobes/active/passive/home = firmware defaults */
}
/* Linux brcmf_notify_escan_complete aborts with WLC_SCAN, one channel -1.
 * sizeof legacy request including channel/padding is 68, not packed 66. */
static __inline void CywScanBuildAbort(unsigned char *p)
{
    memset(p,0,CYW_SCAN_ABORT_SIZE);memset(p+36,255,6);p[42]=2;p[43]=1;
    memset(p+44,255,16);CywScanPut32(p+60,1);CywScanPut16(p+64,65535u);
}

static __inline int CywScanSuite(const unsigned char *p,unsigned type)
{return p[0]==0 && p[1]==15 && p[2]==172 && p[3]==type;}
/* Strict RSN bounds; optional capabilities/PMKIDs/group-management suite are
 * parsed, not read past. Unknown suites remain display-only. The connection
 * path supports WPA2-PSK/CCMP, not WPA3-only, enterprise, WEP or required PMF. */
static __inline unsigned CywScanRsn(const unsigned char *p,unsigned n)
{
    unsigned off=0,count,i,caps=0;int group=0,pair=0,psk=0;
    if(n<8 || CywScanU16(p)!=1)return CYW_SCAN_SECURITY_MALFORMED_IE;
    group=CywScanSuite(p+2,4);off=6;count=CywScanU16(p+off);off+=2;
    if(!count || count>(n-off)/4)return CYW_SCAN_SECURITY_MALFORMED_IE;
    for(i=0;i<count;++i) {if(CywScanSuite(p+off,4))pair=1;off+=4;}
    if(n-off<2)return CYW_SCAN_SECURITY_MALFORMED_IE;
    count=CywScanU16(p+off);off+=2;
    if(!count || count>(n-off)/4)return CYW_SCAN_SECURITY_MALFORMED_IE;
    for(i=0;i<count;++i) {if(CywScanSuite(p+off,2))psk=1;off+=4;}
    if(off<n) {
        if(n-off<2)return CYW_SCAN_SECURITY_MALFORMED_IE;
        caps=CywScanU16(p+off);off+=2;
    }
    if(off<n) {
        if(n-off<2)return CYW_SCAN_SECURITY_MALFORMED_IE;
        count=CywScanU16(p+off);off+=2;
        if(count>(n-off)/16)return CYW_SCAN_SECURITY_MALFORMED_IE;
        off+=count*16;
        if(off<n) {if(n-off!=4)return CYW_SCAN_SECURITY_MALFORMED_IE;off+=4;}
    }
    if(off!=n)return CYW_SCAN_SECURITY_MALFORMED_IE;
    if(caps&64u)return CYW_SCAN_SECURITY_PMF_REQUIRED;
    return group && pair && psk?CYW_SCAN_SECURITY_WPA2_PSK_CCMP:0;
}
static __inline unsigned CywScanSecurity(unsigned capability,const unsigned char *p,unsigned n)
{
    unsigned off=0,rsn=0,result=0;int wpa=0,seen=0;
    while(off<n) {
        unsigned id,len;
        if(n-off<2)return CYW_SCAN_SECURITY_UNSUPPORTED|CYW_SCAN_SECURITY_MALFORMED_IE;
        id=p[off];len=p[off+1];off+=2;
        if(len>n-off)return CYW_SCAN_SECURITY_UNSUPPORTED|CYW_SCAN_SECURITY_MALFORMED_IE;
        if(id==48) {
            if(seen)return CYW_SCAN_SECURITY_UNSUPPORTED|CYW_SCAN_SECURITY_MALFORMED_IE;
            seen=1;rsn=CywScanRsn(p+off,len);
        }
        if(id==221 && len>=4 && p[off]==0 && p[off+1]==80 && p[off+2]==242 && p[off+3]==1)wpa=1;
        off+=len;
    }
    if(!(capability&1u) || (capability&2u))result=CYW_SCAN_SECURITY_UNSUPPORTED;
    else if(!(capability&16u) && !seen && !wpa)result=CYW_SCAN_SECURITY_OPEN;
    else if((capability&16u) && seen && rsn==CYW_SCAN_SECURITY_WPA2_PSK_CCMP)
        result=CYW_SCAN_SECURITY_WPA2_PSK_CCMP;
    else result=CYW_SCAN_SECURITY_UNSUPPORTED;
    return result|(rsn&(CYW_SCAN_SECURITY_MALFORMED_IE|CYW_SCAN_SECURITY_PMF_REQUIRED));
}
/* Return 1 for an accepted record, 0 for a stale/nonmatching sync, -1 for a
 * malformed/unsupported record. No partial structure is ever published. */
static __inline int CywScanParsePartial(const unsigned char *p,unsigned n,unsigned sync,CYW_SCAN_ENTRY *out)
{
    const unsigned char *b;unsigned total,length,ieoff,ielen,rssi,i;int allzero=1;
    CYW_SCAN_ENTRY entry;
    if(!p || !out || n<12)return -1;
    if(CywScanU16(p+8)!=(sync&65535u))return 0;
    total=CywScanU32(p);
    if(total<140 || total>n || CywScanU16(p+10)!=1)return -1;
    b=p+12;length=CywScanU32(b+4);
    if(CywScanU32(b)!=109 || length!=total-12 || b[18]>32)return -1;
    ieoff=CywScanU16(b+116);ielen=CywScanU32(b+120);
    if(ieoff<128 || ieoff>length || ielen>length-ieoff)return -1;
    if(b[8]&1u)return -1;
    for(i=0;i<6;++i)if(b[8+i])allzero=0;
    if(allzero)return -1;
    rssi=CywScanU16(b+78);
    memset(&entry,0,sizeof(entry));entry.Rssi=rssi&32768u?(int)rssi-65536:(int)rssi;
    /* Preserve sentinel/unknown RSSI too. Only -127..-1 is a measured RSSI;
     * the app must show other values as unknown, not invent signal quality. */
    entry.SsidLength=b[18];memcpy(entry.Ssid,b+19,entry.SsidLength);
    memcpy(entry.Bssid,b+8,6);entry.Chanspec=(unsigned short)CywScanU16(b+72);
    entry.ControlChannel=b[88];
    if(!entry.ControlChannel || entry.ControlChannel>196 ||
       (entry.ControlChannel>14 && entry.ControlChannel<32))return -1;
    entry.SecurityFlags=CywScanSecurity(CywScanU16(b+16),b+ieoff,ielen);
    if(!entry.SsidLength)entry.SecurityFlags|=CYW_SCAN_SECURITY_HIDDEN;
    *out=entry;return 1;
}
static __inline void CywScanAdd(CYW_SCAN_REPORT *report,const CYW_SCAN_ENTRY *entry)
{
    unsigned i;
    if(report->Count>CYW_SCAN_MAX_ENTRIES) {report->Flags|=CYW_SCAN_TRUNCATED;return;}
    for(i=0;i<report->Count;++i) {
        if(!memcmp(report->Entries[i].Bssid,entry->Bssid,6) &&
            report->Entries[i].SsidLength==entry->SsidLength &&
            !memcmp(report->Entries[i].Ssid,entry->Ssid,entry->SsidLength)) {
            report->Entries[i]=*entry;return;
        }
    }
    if(report->Count==CYW_SCAN_MAX_ENTRIES) {report->Flags|=CYW_SCAN_TRUNCATED;return;}
    report->Entries[report->Count++]=*entry;
}
#endif
