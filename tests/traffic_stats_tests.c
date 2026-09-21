/* Actual counter and NDIS mapping helpers against host-only lock/type stubs.
 * WDK compilation separately checks real NDIS types/flags and the OID switch. */
#include <stdio.h>
#include <string.h>
#include "../src/driver/traffic_stats.h"
typedef void VOID;typedef unsigned long ULONG;typedef unsigned char BOOLEAN,*PUCHAR;
typedef int KIRQL;
typedef struct { CYW_TRAFFIC_STATS Traffic;int TrafficLock; } ADAPTER,*PRPI5CYW_ADAPTER;
typedef struct {struct {unsigned char Type,Revision;unsigned short Size;} Header;ULONG SupportedStatistics;
uint64_t ifInDiscards;
uint64_t ifInErrors;
uint64_t ifHCInOctets;
uint64_t ifHCInUcastPkts;
uint64_t ifHCInMulticastPkts;
uint64_t ifHCInBroadcastPkts;
uint64_t ifHCOutOctets;
uint64_t ifHCOutUcastPkts;
uint64_t ifHCOutMulticastPkts;
uint64_t ifHCOutBroadcastPkts;
uint64_t ifOutErrors;
uint64_t ifOutDiscards;
uint64_t ifHCInUcastOctets;
uint64_t ifHCInMulticastOctets;
uint64_t ifHCInBroadcastOctets;
uint64_t ifHCOutUcastOctets;
uint64_t ifHCOutMulticastOctets;
uint64_t ifHCOutBroadcastOctets;
} NDIS_STATISTICS_INFO;
#define RtlZeroMemory(p,n) memset(p,0,n)
#define NDIS_OBJECT_TYPE_DEFAULT 0x80
#define NDIS_STATISTICS_INFO_REVISION_1 1
#define NDIS_SIZEOF_STATISTICS_INFO_REVISION_1 sizeof(NDIS_STATISTICS_INFO)
#define CYW_STATISTICS_VALID 0x3ffff
static int failures;
#define CHECK(x) do {if(!(x)){printf("FAIL %d: %s\n",__LINE__,#x);failures++;}}while(0)
static void KeAcquireSpinLock(int *lock,KIRQL *irql){CHECK(!*lock);*lock=1;*irql=0;}
static void KeReleaseSpinLock(int *lock,KIRQL irql){(void)irql;CHECK(*lock);*lock=0;}
#include "../src/driver/statistics_ndis.h"
int main(void)
{
    ADAPTER a={0};CYW_TRAFFIC_STATS s;NDIS_STATISTICS_INFO v;
    unsigned tx,g;unsigned char p[64]={0};
    for(tx=0;tx<2;++tx)for(g=0;g<3;++g) {
        memset(p,0,sizeof(p));p[0]=g?1:2;if(g==2)memset(p,255,6);
        Rpi5CywTrafficFrame(&a,(BOOLEAN)tx,p,14+tx*10+g);
    }
    Rpi5CywTrafficFrame(&a,0,p,13); /* malformed length not counted */
    Rpi5CywTrafficDrop(&a,0,7,0);Rpi5CywTrafficDrop(&a,1,9,0);
    Rpi5CywTrafficDrop(&a,0,11,1);Rpi5CywTrafficDrop(&a,1,13,1);
    a.Traffic.Bytes[0][0]+=0x100000000ULL;
    CywTrafficSnapshot(&a,&s);CywNdisStatistics(&s,&v);
    CHECK(v.Header.Type==0x80 && v.Header.Revision==1 && v.Header.Size==sizeof(v));
    CHECK(v.SupportedStatistics==CYW_STATISTICS_VALID);
    CHECK(v.ifHCInOctets==0x10000002dULL && v.ifHCOutOctets==75);
    CHECK(v.ifHCInUcastOctets==0x10000000eULL && v.ifHCInMulticastOctets==15 && v.ifHCInBroadcastOctets==16);
    CHECK(v.ifHCOutUcastOctets==24 && v.ifHCOutMulticastOctets==25 && v.ifHCOutBroadcastOctets==26);
    CHECK(v.ifHCInUcastPkts==1 && v.ifHCInMulticastPkts==1 && v.ifHCInBroadcastPkts==1);
    CHECK(v.ifHCOutUcastPkts==1 && v.ifHCOutMulticastPkts==1 && v.ifHCOutBroadcastPkts==1);
    CHECK(v.ifInDiscards==7 && v.ifOutDiscards==9 && v.ifInErrors==11 && v.ifOutErrors==13);
    CHECK(!a.TrafficLock);
    if(!failures)puts("PASS: actual traffic classification, 64-bit byte totals, locked snapshot and NDIS field mapping.");
    return failures?1:0;
}
