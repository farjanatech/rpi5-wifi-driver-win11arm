/* Byte-level tests of the production parser. No device or firmware access. */
#include <stdio.h>
#include "../src/cyw43455/scan_protocol.h"
static unsigned Failures;
#define CHECK(x) do {if(!(x)){printf("FAIL line %d: %s\n",__LINE__,#x);++Failures;}} while(0)
static const unsigned char Rsn[22]={48,20,1,0,0,15,172,4,1,0,0,15,172,4,1,0,0,15,172,2,0,0};
static unsigned Fixture(unsigned char *p)
{
    unsigned char *b=p+12;
    memset(p,0,512);CywScanPut32(p,162);CywScanPut32(p+4,109);
    CywScanPut16(p+8,7);CywScanPut16(p+10,1);
    CywScanPut32(b,109);CywScanPut32(b+4,150);b[8]=2;b[13]=1;
    CywScanPut16(b+16,17);b[18]=4;memcpy(b+19,"test",4);
    CywScanPut16(b+72,0xd024);CywScanPut16(b+78,65536u-55u);b[88]=36;
    CywScanPut16(b+116,128);CywScanPut32(b+120,22);memcpy(b+128,Rsn,22);
    return 162;
}
int main(void)
{
    unsigned char bytes[512],request[CYW_SCAN_REQUEST_SIZE],abortRequest[CYW_SCAN_ABORT_SIZE];
    CYW_SCAN_ENTRY entry,unchanged;CYW_SCAN_REPORT report;unsigned i,n;
    CHECK(sizeof(CYW_SCAN_ENTRY)==56 && sizeof(CYW_SCAN_REPORT)==3616);
    CHECK(offsetof(CYW_SCAN_ENTRY,Chanspec)==42 && offsetof(CYW_SCAN_ENTRY,Rssi)==44);
    CHECK(offsetof(CYW_SCAN_REPORT,Entries)==32);
    CywScanBuildRequest(request,0x9234);
    CHECK(CywScanU32(request)==1 && CywScanU16(request+4)==1 && CywScanU16(request+6)==0x9234);
    CHECK(CywScanU32(request+8)==0 && request[44]==255 && request[49]==255);
    CHECK(request[50]==2 && request[51]==1 && CywScanU32(request+68)==0);
    for(i=52;i<68;++i)CHECK(request[i]==255);
    CywScanBuildAbort(abortRequest);
    CHECK(abortRequest[36]==255 && abortRequest[41]==255 && abortRequest[42]==2 && abortRequest[43]==1);
    CHECK(CywScanU32(abortRequest+60)==1 && CywScanU16(abortRequest+64)==65535);
    CHECK(!abortRequest[66] && !abortRequest[67]);
    n=Fixture(bytes);CHECK(CywScanParsePartial(bytes,n,7,&entry)==1);
    CHECK(entry.SsidLength==4 && !memcmp(entry.Ssid,"test",4) && entry.Rssi==-55);
    CHECK(entry.Chanspec==0xd024 && entry.ControlChannel==36 && entry.SecurityFlags==2);
    unchanged=entry;
    CHECK(CywScanParsePartial(bytes,n,8,&entry)==0 && !memcmp(&entry,&unchanged,sizeof(entry)));
    for(i=0;i<n;++i)CHECK(CywScanParsePartial(bytes,i,7,&entry)==-1);
    CHECK(CywScanParsePartial(NULL,n,7,&entry)==-1 && CywScanParsePartial(bytes,n,7,NULL)==-1);
    CywScanPut32(bytes,0xffffffffu);CHECK(CywScanParsePartial(bytes,n,7,&entry)==-1);
    Fixture(bytes);CywScanPut32(bytes+12,108);CHECK(CywScanParsePartial(bytes,n,7,&entry)==-1);
    Fixture(bytes);CywScanPut16(bytes+10,2);CHECK(CywScanParsePartial(bytes,n,7,&entry)==-1);
    Fixture(bytes);CywScanPut32(bytes+16,149);CHECK(CywScanParsePartial(bytes,n,7,&entry)==-1);
    Fixture(bytes);bytes[30]=33;CHECK(CywScanParsePartial(bytes,n,7,&entry)==-1);
    Fixture(bytes);CywScanPut16(bytes+128,127);CHECK(CywScanParsePartial(bytes,n,7,&entry)==-1);
    Fixture(bytes);CywScanPut32(bytes+132,0xffffffffu);CHECK(CywScanParsePartial(bytes,n,7,&entry)==-1);
    Fixture(bytes);bytes[20]=1;CHECK(CywScanParsePartial(bytes,n,7,&entry)==-1);
    Fixture(bytes);memset(bytes+20,0,6);CHECK(CywScanParsePartial(bytes,n,7,&entry)==-1);
    Fixture(bytes);CywScanPut16(bytes+90,0);CHECK(CywScanParsePartial(bytes,n,7,&entry)==1 && entry.Rssi==0);
    Fixture(bytes);bytes[100]=20;CHECK(CywScanParsePartial(bytes,n,7,&entry)==-1);
    Fixture(bytes);bytes[30]=0;CHECK(CywScanParsePartial(bytes,n,7,&entry)==1 && (entry.SecurityFlags&8));
    Fixture(bytes);bytes[31]=0;CHECK(CywScanParsePartial(bytes,n,7,&entry)==1 && entry.SsidLength==4 && entry.Ssid[0]==0);
    CHECK(CywScanSecurity(1,NULL,0)==CYW_SCAN_SECURITY_OPEN);
    CHECK(CywScanSecurity(17,NULL,0)==CYW_SCAN_SECURITY_UNSUPPORTED); /* WEP/unknown */
    CHECK(CywScanSecurity(2,NULL,0)==CYW_SCAN_SECURITY_UNSUPPORTED); /* IBSS */
    CHECK(CywScanSecurity(1,Rsn,22)==CYW_SCAN_SECURITY_UNSUPPORTED); /* privacy mismatch */
    memcpy(bytes,Rsn,22);bytes[21]=0;bytes[20]=64;
    CHECK(CywScanSecurity(17,bytes,22)==(CYW_SCAN_SECURITY_UNSUPPORTED|CYW_SCAN_SECURITY_PMF_REQUIRED));
    memcpy(bytes,Rsn,22);bytes[19]=8;CHECK(CywScanSecurity(17,bytes,22)==4); /* SAE only */
    memcpy(bytes,Rsn,22);bytes[19]=1;CHECK(CywScanSecurity(17,bytes,22)==4); /* enterprise */
    memcpy(bytes,Rsn,22);bytes[7]=2;CHECK(CywScanSecurity(17,bytes,22)==4); /* TKIP group */
    memcpy(bytes,Rsn,22);bytes[13]=2;CHECK(CywScanSecurity(17,bytes,22)==4); /* TKIP pair */
    memcpy(bytes,Rsn,22);bytes[8]=255;bytes[9]=255;
    CHECK(CywScanSecurity(17,bytes,22)==(4|16));
    CHECK(CywScanSecurity(17,Rsn,21)==(4|16));
    memcpy(bytes,Rsn,22);memcpy(bytes+22,Rsn,22);CHECK(CywScanSecurity(17,bytes,44)==(4|16));
    memcpy(bytes,Rsn,22);bytes[1]=19;CHECK(CywScanSecurity(17,bytes,21)==(4|16));
    memcpy(bytes,Rsn,22);bytes[1]=18;CHECK(CywScanSecurity(17,bytes,20)==2); /* absent optional caps */
    memcpy(bytes,Rsn,22);bytes[1]=26;memset(bytes+22,0,6); /* zero PMKIDs + group management suite */
    CHECK(CywScanSecurity(17,bytes,28)==2);
    bytes[22]=1;CHECK(CywScanSecurity(17,bytes,28)==(4|16));
    memset(&report,0,sizeof(report));Fixture(bytes);CHECK(CywScanParsePartial(bytes,n,7,&entry)==1);
    CywScanAdd(&report,&entry);entry.Rssi=-50;CywScanAdd(&report,&entry);
    CHECK(report.Count==1 && report.Entries[0].Rssi==-50);
    for(i=1;i<64;++i) {entry.Bssid[5]=(unsigned char)(i+1);CywScanAdd(&report,&entry);}
    CHECK(report.Count==64 && !report.Flags);
    entry.Bssid[5]=99;CywScanAdd(&report,&entry);CHECK(report.Count==64 && report.Flags==1);
    entry.Bssid[5]=1;entry.Rssi=-40;CywScanAdd(&report,&entry);
    CHECK(report.Count==64 && report.Entries[0].Rssi==-40);
    printf("scan protocol tests: %u failures\n",Failures);return Failures?1:0;
}
