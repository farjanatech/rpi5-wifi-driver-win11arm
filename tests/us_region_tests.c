/* Actual production admission helper: no hardware, no country rewriting. */
#include <stdio.h>
#include "../src/cyw43455/us_region.h"
static unsigned Failures;
#define CHECK(x) do {if(!(x)){printf("FAIL line %d: %s\n",__LINE__,#x);++Failures;}}while(0)
int main(void)
{
    CYW_CONNECT_REQUEST request,original;unsigned a,b;
    memset(&request,0,sizeof(request));request.Version=1;
    request.SsidLength=1;request.Ssid[0]='x';
    CHECK(!CywUsCountryAllowed(NULL) && !CywUsValidConnect(NULL));
    for(a=0;a<256;++a)for(b=0;b<256;++b) {
        request.Country[0]=(uint8_t)a;request.Country[1]=(uint8_t)b;original=request;
        CHECK(CywUsValidConnect(&request)==(a=='U' && b=='S'));
        CHECK(!memcmp(&request,&original,sizeof(request)));
    }
    request.Country[0]='U';request.Country[1]='S';CHECK(CywUsValidConnect(&request));
    request.Version=2;CHECK(!CywUsValidConnect(&request));request.Version=1;
    request.Reserved[0]=1;CHECK(!CywUsValidConnect(&request));request.Reserved[0]=0;
    request.Reserved[1]=1;CHECK(!CywUsValidConnect(&request));request.Reserved[1]=0;
    request.SsidLength=0;CHECK(!CywUsValidConnect(&request));
    request.SsidLength=33;CHECK(!CywUsValidConnect(&request));
    request.SsidLength=32;CHECK(CywUsValidConnect(&request));
    printf("US region admission: %s (all 65536 country byte pairs)\n",Failures?"FAIL":"PASS");
    return Failures?1:0;
}
