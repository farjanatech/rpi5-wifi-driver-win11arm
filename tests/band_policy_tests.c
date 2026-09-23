/* Pure startup policy and exact firmware wire validation. No hardware access. */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "../src/cyw43455/network_protocol.h"
#include "../src/cyw43455/band_policy.h"
static unsigned Failures;
#define CHECK(x) do {if(!(x)){printf("FAIL line %d: %s\n",__LINE__,#x);++Failures;}}while(0)
int main(void)
{
    unsigned char pref[8],c[12]={0},r[12]={0},m[6]={2,0x10,0x20,0x30,0x40,0x50},other[6];
    unsigned char sta[521];unsigned long out[4];unsigned i,j;
    const unsigned char band[8]={3,2,0,1,1,2,0,0},rssi[8]={1,2,0,0,0,0,0,0};
    const unsigned channels[]={1,6,14,32,36,100,149,196};
    const unsigned invalid[]={0,15,31,197,0xffffffffu};
    const unsigned versions[]={3,4,5,7},lengths[]={84,200,252,296};
    CHECK(CywBuildJoinPreference(1,pref)==8 && !memcmp(pref,band,8));
    CHECK(CywBuildJoinPreference(0,pref)==4 && !memcmp(pref,rssi,8));
    CHECK(!CywBandMacValid(NULL));memset(other,0,6);CHECK(!CywBandMacValid(other));
    memcpy(other,m,6);other[0]|=1;CHECK(!CywBandMacValid(other));
    CywPut32(r,(uint32_t)-55);
    for(i=0;i<sizeof(channels)/sizeof(channels[0]);i++) {
        CywPut32(c,channels[i]);CywPut32(c+4,channels[i]);
        CHECK(CywBandReadback(c,12,r,12,m,6,m,6,out));
        CHECK(out[0]==channels[i] && (int32_t)out[1]==-55 && out[2]==0x30201002u && out[3]==0x5040u);
    }
    for(i=0;i<sizeof(invalid)/sizeof(invalid[0]);i++) {
        CywPut32(c,invalid[i]);CywPut32(c+4,invalid[i]);
        CHECK(!CywBandReadback(c,12,r,12,m,6,m,6,out));
    }
    CywPut32(c,36);CywPut32(c+4,36);
    for(i=0;i<13;i++)if(i!=12) {
        CHECK(!CywBandReadback(c,i,r,12,m,6,m,6,out));
        CHECK(!CywBandReadback(c,12,r,i,m,6,m,6,out));
    }
    CHECK(!CywBandReadback(c,13,r,12,m,6,m,6,out));
    CHECK(!CywBandReadback(c,12,r,13,m,6,m,6,out));
    for(i=0;i<8;i++)if(i!=6) {
        CHECK(!CywBandReadback(c,12,r,12,m,i,m,6,out));
        CHECK(!CywBandReadback(c,12,r,12,m,6,m,i,out));
    }
    CHECK(!CywBandReadback(NULL,12,r,12,m,6,m,6,out));
    CHECK(!CywBandReadback(c,12,NULL,12,m,6,m,6,out));
    CHECK(!CywBandReadback(c,12,r,12,NULL,6,m,6,out));
    CHECK(!CywBandReadback(c,12,r,12,m,6,NULL,6,out));
    CHECK(!CywBandReadback(c,12,r,12,m,6,m,6,NULL));
    CywPut32(c+4,40);CHECK(!CywBandReadback(c,12,r,12,m,6,m,6,out));CywPut32(c+4,36);
    CywPut32(c+8,36);CHECK(!CywBandReadback(c,12,r,12,m,6,m,6,out));CywPut32(c+8,0);
    memcpy(other,m,6);other[5]++;CHECK(!CywBandReadback(c,12,r,12,m,6,other,6,out));
    for(i=0;i<4;i++) {
        const uint32_t values[]={0,1,(uint32_t)-128,0x80000000u};
        CywPut32(r,values[i]);CHECK(!CywBandReadback(c,12,r,12,m,6,m,6,out));
    }
    CywPut32(r,(uint32_t)-127);CHECK(CywBandReadback(c,12,r,12,m,6,m,6,out));
    CHECK(CywBandCandidateUsable(36,(unsigned long)-70));
    CHECK(!CywBandCandidateUsable(36,(unsigned long)-71));
    CHECK(CywBandCandidateUsable(6,(unsigned long)-100)); /* never forbid 2.4 */
    for(i=0;i<4;i++) {
        memset(sta,0,sizeof(sta));CywPut16(sta,(uint16_t)versions[i]);CywPut16(sta+2,(uint16_t)lengths[i]);
        memcpy(sta+16,m,6);CywPut32(sta+8,0x30);
        CHECK(CywBandStationAuthorized(sta,lengths[i],m));
        for(j=0;j<lengths[i];j++)CHECK(!CywBandStationAuthorized(sta,j,m));
        for(j=0;j<3;j++) {
            CywPut32(sta+8,j==0?0u:(j==1?0x10u:0x20u));CHECK(!CywBandStationAuthorized(sta,lengths[i],m));
        }
        CywPut32(sta+8,0x30);CHECK(!CywBandStationAuthorized(sta,lengths[i],other));
        CywPut16(sta,6);CHECK(!CywBandStationAuthorized(sta,lengths[i],m));
    }
    if(Failures)return 1;
    puts("PASS: bounded band policy wire builders, exact readback bounds, BSSID/RSSI/channel validation and fresh firmware authorization");
    return 0;
}
