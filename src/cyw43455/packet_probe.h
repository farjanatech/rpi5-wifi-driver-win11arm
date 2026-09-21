#pragma once
#include "network_protocol.h"
/* Diagnostic-only IPv4 integrity and ICMP echo timing. No packet contents or
 * addresses are exported. A bounded worker-owned ring forgets entries on match
 * or reuse. Parsing never modifies or drops a packet or trusts offload flags. */
typedef struct CYW_ECHO_SLOT {
    uint32_t Source, Destination, Key;
    unsigned long long Sent;
    unsigned Active;
} CYW_ECHO_SLOT;
typedef struct CYW_PACKET_PROBE {
    uint32_t TxIpBad, RxIpBad, TxTransportBad, RxTransportBad;
    uint32_t RxMalformed, RxFragment, RxUdpNoChecksum;
    uint32_t TxEcho, RxEchoRequest, RxEchoReply, RxEchoMatched, RxEchoUnmatched;
    uint32_t EchoLate, EchoMaxMs, EchoEvicted, Next;
    uint32_t RxIcmpUnreachable, RxIcmpOther;
    CYW_ECHO_SLOT Echo[32];
} CYW_PACKET_PROBE;
static __inline uint32_t CywSum(const uint8_t *p,size_t n,uint32_t sum)
{
    while(n>1) {sum+=((uint32_t)p[0]<<8)|p[1];p+=2;n-=2;}
    if(n)sum+=(uint32_t)p[0]<<8;
    while(sum>>16)sum=(sum&65535)+(sum>>16);
    return sum;
}
static __inline void CywProbePacket(CYW_PACKET_PROBE *d,const uint8_t *p,
    size_t n,unsigned transmit,unsigned long long now)
{
    const uint8_t *ip,*body;size_t ihl,total,length;uint32_t sum,key,src,dst,i;
    unsigned protocol,valid=1;unsigned long long ms;
    if(!d || !p || n<14 || p[12]!=8 || p[13]!=0)return;
    ip=p+14;
    if(n<34 || (ip[0]>>4)!=4) {if(!transmit)d->RxMalformed++;return;}
    ihl=(size_t)(ip[0]&15)*4;total=((size_t)ip[2]<<8)|ip[3];
    if(ihl<20 || ihl>n-14 || total<ihl || total>n-14) {if(!transmit)d->RxMalformed++;return;}
    if(CywSum(ip,ihl,0)!=65535) {
        if(transmit)d->TxIpBad++;else d->RxIpBad++;valid=0;
    }
    if((ip[6]&0x3f) || ip[7]) {if(!transmit)d->RxFragment++;return;}
    protocol=ip[9];body=ip+ihl;length=total-ihl;
    if((protocol==1 && length<8) || (protocol==6 && (length<20 ||
       (size_t)(body[12]>>4)*4<20 || (size_t)(body[12]>>4)*4>length)) ||
       (protocol==17 && (length<8 || (((size_t)body[4]<<8)|body[5])!=length))) {
        if(!transmit)d->RxMalformed++;return;
    }
    if(protocol!=1 && protocol!=6 && protocol!=17)return;
    if(protocol==17 && !body[6] && !body[7]) {
        if(!transmit)d->RxUdpNoChecksum++;return;
    }
    sum=protocol==1?0:CywSum(ip+12,8,(uint32_t)length+protocol);
    if(CywSum(body,length,sum)!=65535) {
        if(transmit)d->TxTransportBad++;else d->RxTransportBad++;valid=0;
    }
    if(protocol!=1 || !valid)return;
    if(!transmit && body[0]==3) {d->RxIcmpUnreachable++;return;}
    if(body[1]!=0 || (body[0]!=0 && body[0]!=8)) {if(!transmit)d->RxIcmpOther++;return;}
    key=CywBe32(body+4);src=CywBe32(ip+12);dst=CywBe32(ip+16);
    if(transmit && body[0]==8) {
        CYW_ECHO_SLOT *slot=&d->Echo[d->Next++%32];
        for(i=0;i<32;++i)if(d->Echo[i].Active && d->Echo[i].Source==src &&
            d->Echo[i].Destination==dst && d->Echo[i].Key==key)d->Echo[i].Active=0;
        if(slot->Active)d->EchoEvicted++;
        slot->Source=src;slot->Destination=dst;slot->Key=key;slot->Sent=now;slot->Active=1;
        d->TxEcho++;
    } else if(!transmit && body[0]==8)d->RxEchoRequest++;
    else if(!transmit && body[0]==0) {
        d->RxEchoReply++;
        for(i=0;i<32;++i) {
            CYW_ECHO_SLOT *slot=&d->Echo[i];
            if(slot->Active && slot->Source==dst && slot->Destination==src && slot->Key==key) {
                if(now<slot->Sent)break;
                ms=(now-slot->Sent)/10000ULL;slot->Active=0;
                d->RxEchoMatched++;if(ms>=1000)d->EchoLate++;
                if(ms>d->EchoMaxMs)d->EchoMaxMs=ms>0xffffffffULL?0xffffffffu:(uint32_t)ms;
                return;
            }
        }
        d->RxEchoUnmatched++;
    }
}
