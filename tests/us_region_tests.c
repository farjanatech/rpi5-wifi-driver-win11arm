/* Portable country/band admission tests. No hardware access. */
#include <stdio.h>
#include <string.h>
#include "../src/cyw43455/network_protocol.h"
static unsigned Failures;
#define CHECK(x) do {if(!(x)){printf("FAIL line %d: %s\n",__LINE__,#x);++Failures;}}while(0)
int main(void)
{
    CYW_CONNECT_REQUEST request,original;unsigned a,b,band;
    unsigned char country[12]={0};
    memset(&request,0,sizeof(request));request.Version=1;
    request.SsidLength=1;request.Ssid[0]='x';
    CHECK(!CywValidConnect(NULL));
    for(a=0;a<256;++a)for(b=0;b<256;++b) {
        request.Country[0]=(uint8_t)a;request.Country[1]=(uint8_t)b;request.Reserved[0]=0;original=request;
        CHECK(CywValidConnect(&request)==(a>='A'&&a<='Z'&&b>='A'&&b<='Z'));
        CHECK(!memcmp(&request,&original,sizeof(request)));
    }
    request.Country[0]='Z';request.Country[1]='Z';
    CHECK(CywCountryAuto(request.Country));
    for(band=CYW_BAND_PREF_AUTO;band<=CYW_BAND_PREF_5;++band) {
        request.Reserved[0]=(uint8_t)band;CHECK(CywValidConnect(&request));
    }
    request.Reserved[0]=3;CHECK(!CywValidConnect(&request));request.Reserved[0]=0;
    request.Reserved[1]=1;CHECK(!CywValidConnect(&request));request.Reserved[1]=0;
    request.Version=2;CHECK(!CywValidConnect(&request));request.Version=1;
    request.SsidLength=0;CHECK(!CywValidConnect(&request));
    request.SsidLength=33;CHECK(!CywValidConnect(&request));request.SsidLength=32;
    memset(country,0,sizeof(country));country[0]=country[8]='X';country[1]=country[9]='2';CywPut32(country+4,7);
    CHECK(CywCountryValueUsable(country,sizeof(country)));
    country[9]='3';CHECK(!CywCountryValueUsable(country,sizeof(country)));country[9]='2';
    CywPut32(country+4,0xffffffffu);CHECK(!CywCountryValueUsable(country,sizeof(country)));
    printf("Portable country/band admission: %s\n",Failures?"FAIL":"PASS");
    return Failures?1:0;
}
