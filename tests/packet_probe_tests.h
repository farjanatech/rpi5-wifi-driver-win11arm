/* Pure byte-array tests; actual parser/timing implementation, no real traffic. */
#include "../src/cyw43455/packet_probe.h"
static void ProbeChecksum(uint8_t *p,size_t n,size_t field,uint32_t initial)
{
    uint32_t sum;p[field]=p[field+1]=0;sum=CywSum(p,n,initial)^65535;
    p[field]=(uint8_t)(sum>>8);p[field+1]=(uint8_t)sum;
}
static void ProbeEcho(uint8_t *p,int reply)
{
    memset(p,0,64);p[12]=8;p[14]=0x45;p[17]=37;p[22]=64;p[23]=1;
    p[26]=192;p[27]=0;p[28]=2;p[29]=(uint8_t)(reply?2:1);
    p[30]=192;p[31]=0;p[32]=2;p[33]=(uint8_t)(reply?1:2);
    p[34]=(uint8_t)(reply?0:8);p[38]=0x12;p[39]=0x34;p[41]=1;
    memset(p+42,0xa5,9);ProbeChecksum(p+14,20,10,0);ProbeChecksum(p+34,17,2,0);
}
static void RunPacketProbeTests(void)
{
    CYW_PACKET_PROBE d={0};uint8_t p[64];unsigned i;
    const uint8_t vector[8]={0,1,0xf2,3,0xf4,0xf5,0xf6,0xf7};
    CHECK(CywSum(vector,8,0)==0xddf2);
    ProbeEcho(p,0);CywProbePacket(&d,p,51,1,10000000ULL);
    CHECK(d.TxEcho==1 && d.TxIpBad==0 && d.TxTransportBad==0);
    ProbeEcho(p,1);CywProbePacket(&d,p,51,0,25000000ULL);
    CHECK(d.RxEchoMatched==1 && d.EchoLate==1 && d.EchoMaxMs==1500);
    CywProbePacket(&d,p,51,0,26000000ULL);CHECK(d.RxEchoUnmatched==1);
    ProbeEcho(p,0);CywProbePacket(&d,p,51,0,0);CHECK(d.RxEchoRequest==1);
    ProbeEcho(p,1);p[50]^=1;CywProbePacket(&d,p,51,0,0);
    CHECK(d.RxTransportBad==1 && d.RxEchoReply==2);
    ProbeEcho(p,1);p[22]^=1;CywProbePacket(&d,p,51,0,0);CHECK(d.RxIpBad==1);
    ProbeEcho(p,1);p[34]=3;p[35]=1;ProbeChecksum(p+34,17,2,0);
    CywProbePacket(&d,p,51,0,0);CHECK(d.RxIcmpUnreachable==1);
    ProbeEcho(p,1);p[20]=0x20;ProbeChecksum(p+14,20,10,0);
    CywProbePacket(&d,p,51,0,0);CHECK(d.RxFragment==1);
    memset(&d,0,sizeof(d));ProbeEcho(p,1);
    for(i=0;i<51;++i)CywProbePacket(&d,p,i,0,0);
    CHECK(d.RxEchoReply==0 && d.RxMalformed==37);
    ProbeEcho(p,1);p[23]=17;p[38]=0;p[39]=17;p[40]=p[41]=0;
    ProbeChecksum(p+14,20,10,0);CywProbePacket(&d,p,51,0,0);
    CHECK(d.RxUdpNoChecksum==1);
    ProbeChecksum(p+34,17,6,CywSum(p+26,8,17+17));
    CywProbePacket(&d,p,51,0,0);CHECK(d.RxTransportBad==0);
    p[50]^=1;CywProbePacket(&d,p,51,0,0);CHECK(d.RxTransportBad==1);
    ProbeEcho(p,1);p[23]=6;ProbeChecksum(p+14,20,10,0);
    CywProbePacket(&d,p,51,0,0);CHECK(d.RxMalformed==38); /* short TCP */
    p[17]=40;p[46]=0x50;ProbeChecksum(p+14,20,10,0);
    ProbeChecksum(p+34,20,16,CywSum(p+26,8,20+6));
    CywProbePacket(&d,p,54,0,0);CHECK(d.RxTransportBad==1);
    p[53]^=1;CywProbePacket(&d,p,54,0,0);CHECK(d.RxTransportBad==2);
    memset(&d,0,sizeof(d));ProbeEcho(p,0);
    for(i=0;i<33;++i) {p[41]=(uint8_t)i;ProbeChecksum(p+34,17,2,0);CywProbePacket(&d,p,51,1,i);}
    CHECK(d.TxEcho==33 && d.EchoEvicted==1);
    memset(&d,0,sizeof(d));ProbeEcho(p,0);
    CywProbePacket(&d,p,51,1,10000);CywProbePacket(&d,p,51,1,20000);
    ProbeEcho(p,1);CywProbePacket(&d,p,51,0,30000);
    CHECK(d.RxEchoMatched==1 && d.EchoMaxMs==1 && !d.EchoLate);
    CHECK(d.Echo[0].Active==0 && d.Echo[1].Active==0);
}
