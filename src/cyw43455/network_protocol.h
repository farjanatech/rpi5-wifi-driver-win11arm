#pragma once
/* Wire-format validation, independent of the kernel and radio. Broadcom
 * SDPCM/BCDC definitions: Linux v6.12 brcmfmac (ISC); notices retained. */
#if defined(NDIS_MINIPORT_DRIVER) && !defined(RPI5CYW_HOST_TEST)
/* The WDK already supplies size_t and memory primitives. Pulling user-mode
 * vcruntime headers into the kernel build conflicts with the WDK CRT. */
typedef unsigned char uint8_t;
typedef unsigned short uint16_t;
typedef unsigned int uint32_t;
#else
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#endif

#define CYW_WIRE_CAPACITY 65536u
#define CYW_CONTROL_CAPACITY 8192u
static __inline uint16_t CywLe16(const uint8_t *p)
{ return (uint16_t)(p[0] | (uint16_t)p[1] << 8); }
static __inline uint32_t CywLe32(const uint8_t *p)
{ return p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }
static __inline uint32_t CywBe32(const uint8_t *p)
{ return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3]; }
static __inline void CywPut16(uint8_t *p, uint16_t v)
{ p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static __inline void CywPut32(uint8_t *p, uint32_t v)
{ p[0]=(uint8_t)v; p[1]=(uint8_t)(v>>8); p[2]=(uint8_t)(v>>16); p[3]=(uint8_t)(v>>24); }
static __inline int CywSdpcmHeader(const uint8_t *p, size_t available,
                                  uint32_t *length, uint32_t *offset)
{
    uint32_t n;
    if (!p || !length || !offset || available < 12) return 0;
    n = CywLe16(p);
    if (n < 12 || (uint16_t)(CywLe16(p + 2) ^ n) != 0xffff ||
        p[7] < 12 || p[7] > n || (p[5] & 15) > 3) return 0;
    *length = n; *offset = p[7];
    return 1;
}
static __inline int CywEthernetBody(const uint8_t *p, size_t n,
                                   size_t *offset, size_t *length)
{
    size_t skip;
    if (!p || !offset || !length || n < 4 || (p[0] >> 4) != 2 ||
        (p[2] & 15) != 0) return 0;
    skip = 4 + (size_t)p[3] * 4;
    if (skip > n || n - skip < 14 || n - skip > 1514) return 0;
    *offset = skip; *length = n - skip;
    return 1;
}
static __inline int CywTxCredit(uint8_t seq, uint8_t max, uint8_t flow)
{ uint8_t n=(uint8_t)(max-seq); return n != 0 && n <= 0x40 && flow == 0; }
static __inline int CywAcceptEthernet(const uint8_t *dst,const uint8_t *mac,
                                      uint32_t filter,const uint8_t *multicast,uint32_t count)
{
    uint32_t i;int broadcast=1;
    if(!dst || !mac || count>32 || (count && !multicast))return 0;
    for(i=0;i<6;++i)if(dst[i]!=255)broadcast=0;
    if(broadcast)return (filter&8)!=0;
    if(dst[0]&1) {
        if(filter&4)return 1;
        if(filter&2)for(i=0;i<count;++i)if(memcmp(dst,multicast+i*6,6)==0)return 1;
        return 0;
    }
    return (filter&1)!=0 && memcmp(dst,mac,6)==0;
}
/* Strip comments and blank lines; reject embedded NUL, binary input and
 * unbounded lines. NVRAM is never interpreted as executable content. */
static __inline int CywPackNvram(const uint8_t *raw, size_t size,
                                 uint8_t *out, size_t cap, size_t *written)
{
    size_t i=0, used=0, start, end, j, eq;
    if (!raw || !out || !written || !size || size > 16384 || cap < 8) return 0;
    memset(out, 0, cap);
    while (i < size) {
        start=i;
        while (i<size && raw[i]!='\n') {
            if (raw[i] != '\r' && raw[i] != '\t' && (raw[i]<32 || raw[i]>126)) return 0;
            ++i;
        }
        end=i; if (i<size) ++i;
        while (start<end && (raw[start]==' ' || raw[start]=='\t')) ++start;
        for (j=start;j<end;++j) if(raw[j]=='#') {end=j;break;}
        while (end>start && (raw[end-1]==' ' || raw[end-1]=='\r' || raw[end-1]=='\t')) --end;
        if(end==start) continue;
        eq=start; while(eq<end && raw[eq]!='=') ++eq;
        if(eq==start || eq==end || end-start>1024 || end-start+1>cap-used-4) return 0;
        memcpy(out+used,raw+start,end-start); used+=end-start+1;
    }
    if(!used || used+4>cap) return 0;
    used=(used+1+3)&~(size_t)3;
    if(used>cap || used/4>65535) return 0;
    *written=used; return 1;
}
/* Fixed ABI with no pointers. PMK is derived in user mode; passwords are
 * never stored in the registry, command line, diagnostics or driver logs. */
typedef struct CYW_CONNECT_REQUEST {
    uint32_t Version, SsidLength;
    uint8_t Country[2], Reserved[2], Ssid[32], Pmk[32];
} CYW_CONNECT_REQUEST;
/* brcmfmac cfg80211.c: ISO3166 fallback for 4345 uses revision zero.
 * Never substitute a different country or ignore a firmware rejection. */
static __inline int CywCountryRequest(const uint8_t *alpha2,uint8_t *out)
{
    if(!alpha2 || !out || alpha2[0]<'A' || alpha2[0]>'Z' ||
        alpha2[1]<'A' || alpha2[1]>'Z')return 0;
    memset(out,0,12);
    out[0]=out[8]=alpha2[0];out[1]=out[9]=alpha2[1];
    return 1;
}
static __inline int CywCountryMatches(const uint8_t *alpha2,const uint8_t *value,size_t length)
{
    return alpha2 && value && length>=12 && value[0]==alpha2[0] && value[1]==alpha2[1] &&
        !value[2] && !value[3] && value[8]==alpha2[0] && value[9]==alpha2[1] &&
        !value[10] && !value[11] && !(CywLe32(value+4)&0x80000000u);
}
static __inline int CywValidConnect(const CYW_CONNECT_REQUEST *r)
{
    if(!r || r->Version!=1 || !r->SsidLength || r->SsidLength>32 ||
        r->Country[0]<'A' || r->Country[0]>'Z' ||
        r->Country[1]<'A' || r->Country[1]>'Z' ||
        r->Reserved[0] || r->Reserved[1]) return 0;
    return 1;
}
