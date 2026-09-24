/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
/* Allocation-free SDPCM validation. Wire reference: brcmfmac sdio.c at
 * Raspberry Pi Linux 8e8c079 (ISC); see THIRD_PARTY_NOTICES.md. Validate a
 * COMPLETE superframe before exposing even its first child to NDIS. */
#include "network_protocol.h"
#define CYW_RX_GLOM_MAX 32u
#define CYW_RX_FRAME_MAX 2048u
typedef struct {
    uint32_t Count, Index, Bytes, DescriptorBytes, NextLength, Pending;
    uint32_t Slot[CYW_RX_GLOM_MAX], Start[CYW_RX_GLOM_MAX];
    uint32_t End[CYW_RX_GLOM_MAX], Payload[CYW_RX_GLOM_MAX];
} CYW_RX_GLOM;

static __inline uint32_t CywRxNextLength(const uint8_t *header)
{
    uint32_t n=(uint32_t)header[6]<<4;
    return n>=64 && n<=CYW_RX_FRAME_MAX?n:0;
}
static __inline int CywRxReadAheadValid(const uint8_t *header,uint32_t hint,uint32_t length)
{
    return hint>=64 && hint<=CYW_RX_FRAME_MAX && !(hint&15) &&
        length>=12 && ((length+15)&~15u)==hint && (header[5]&15)!=0;
}
static __inline int CywRxGlomDescriptor(CYW_RX_GLOM *g,const uint8_t *data,uint32_t bytes)
{
    uint32_t i,n,total=0;
    g->Count=g->Index=g->Pending=0;
    if(!data || !bytes || (bytes&1) || bytes>CYW_RX_GLOM_MAX*2)return 0;
    for(i=0;i<bytes/2;++i) {
        n=CywLe16(data+2*i);
        if(n<(i?12u:24u) || (n&3) || n>CYW_WIRE_CAPACITY-total)return 0;
        g->Slot[i]=n;total+=n;
    }
    g->Bytes=(total+511)&~511u;
    if(g->Bytes>CYW_WIRE_CAPACITY)return 0;
    g->DescriptorBytes=total;g->Count=bytes/2;g->Pending=1;
    return 1;
}
static __inline int CywRxGlomValidate(CYW_RX_GLOM *g,const uint8_t *data,uint32_t bytes)
{
    uint32_t i,len,off,pos=0,slot,base,available,n,o;
    if(!g->Pending || !g->Count || g->Count>CYW_RX_GLOM_MAX ||
       bytes!=g->Bytes || bytes>CYW_WIRE_CAPACITY ||
       !CywSdpcmHeader(data,bytes,&len,&off) ||
       (data[5]&0x8f)!=3 || ((len+511)&~511u)!=bytes ||
       off>g->Slot[0]-12)return 0;
    for(i=0;i<g->Count;++i) {
        slot=g->Slot[i];
        if(i+1==g->Count)slot+=bytes-g->DescriptorBytes;
        if(slot>bytes-pos)return 0;
        base=pos+(i?0:off);available=slot-(i?0:off);
        if(!CywSdpcmHeader(data+base,available,&n,&o) ||
           n>available || n>CYW_RX_FRAME_MAX || (data[base+5]&0x80) ||
           ((data[base+5]&15)!=1 && (data[base+5]&15)!=2))return 0;
        g->Start[i]=base;g->End[i]=base+n;g->Payload[i]=base+o;
        pos+=slot;
    }
    if(pos!=bytes)return 0;
    g->NextLength=CywRxNextLength(data);g->Index=0;g->Pending=0;
    return 1;
}
