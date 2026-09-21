/* SPDX-License-Identifier: GPL-3.0-or-later
 * Ethernet-interface counters, not PHY speed or over-air ACK statistics.
 * Callers serialize all updates/snapshots. Header bytes count; SDPCM/BCDC do not.
 */
#pragma once
#include "../cyw43455/network_protocol.h"
/* Reuse the kernel-safe wire types; never include user-mode CRT headers in WDK. */
typedef unsigned long long CYW_TRAFFIC_U64;
typedef struct CYW_TRAFFIC_STATS {
    CYW_TRAFFIC_U64 Frames[2][3], Bytes[2][3]; /* RX/TX; unicast/multicast/broadcast */
    CYW_TRAFFIC_U64 Discards[2], Errors[2];
} CYW_TRAFFIC_STATS;
static __inline void CywTrafficFrame(CYW_TRAFFIC_STATS *s,unsigned tx,const uint8_t *p,size_t n)
{
    unsigned group=0,i;
    if(!s || !p || n<14 || tx>1)return;
    if(p[0]&1) {
        group=2;for(i=0;i<6;++i)if(p[i]!=255){group=1;break;}
    }
    s->Frames[tx][group]++;s->Bytes[tx][group]+=n;
}
static __inline CYW_TRAFFIC_U64 CywTrafficTotal(const CYW_TRAFFIC_U64 *v)
{return v[0]+v[1]+v[2];}
