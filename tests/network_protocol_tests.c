#include <stdio.h>
#include "../src/cyw43455/network_protocol.h"
static int failures;
#define CHECK(x) do {if(!(x)){printf("FAIL %d: %s\n",__LINE__,#x);++failures;}}while(0)
int main(void)
{
    uint8_t b[1600]={0},out[128],raw[]="# comment\r\na=1\r\n\n b=2 #tail\n";
    uint32_t len,off,count,listed;size_t used,payload,offset;unsigned i;
    CYW_CONNECT_REQUEST r={0};
    const uint8_t mac[6]={2,1,2,3,4,5},group[6]={1,0,0x5e,0,0,1},bc[6]={255,255,255,255,255,255};
    CHECK(sizeof(r)==76);
    CHECK(CywCountryRequest((const uint8_t*)"BD",out));
    CHECK(memcmp(out,"BD\0\0\0\0\0\0BD\0\0",12)==0);
    CHECK(CywCountryMatches((const uint8_t*)"BD",out,12));
    for(i=0;i<12;++i)CHECK(!CywCountryMatches((const uint8_t*)"BD",out,i));
    CHECK(!CywCountryMatches((const uint8_t*)"BD",out,13));
    out[8]='U';CHECK(!CywCountryMatches((const uint8_t*)"BD",out,12));out[8]='B';
    CywPut32(out+4,0xffffffff);CHECK(!CywCountryMatches((const uint8_t*)"BD",out,12));
    CHECK(!CywCountryRequest((const uint8_t*)"bd",out));
    CHECK(!CywCountryRequest(NULL,out));CHECK(!CywCountryMatches(NULL,out,12));
    memset(b,0,sizeof(b));CywPut32(b,1024);CywPut32(b+12,2);memcpy(b+16,"US\0\0BD\0\0",8);
    CHECK(CywCountryList(b,24,(const uint8_t*)"BD",&count,&listed) && count==2 && listed==1);
    CHECK(CywCountryList(b,1024,(const uint8_t*)"GB",&count,&listed) && count==2 && listed==0);
    for(i=0;i<24;++i)CHECK(!CywCountryList(b,i,(const uint8_t*)"BD",&count,&listed));
    CywPut32(b+12,0xffffffff);CHECK(!CywCountryList(b,1024,(const uint8_t*)"BD",&count,&listed));
    CywPut32(b+12,2);CywPut32(b,20);CHECK(!CywCountryList(b,24,(const uint8_t*)"BD",&count,&listed));
    CywPut32(b,1024);b[23]='X';CHECK(!CywCountryList(b,24,(const uint8_t*)"BD",&count,&listed));b[23]=0;
    CywPut32(b+4,1);CHECK(!CywCountryList(b,24,(const uint8_t*)"BD",&count,&listed));CywPut32(b+4,0);
    CywPut32(b+12,0);CHECK(CywCountryList(b,16,(const uint8_t*)"BD",&count,&listed) && !count && !listed);
    CHECK(!CywCountryList(NULL,24,(const uint8_t*)"BD",&count,&listed));
    CHECK(!CywCountryList(b,24,NULL,&count,&listed));
    CHECK(!CywCountryList(b,24,(const uint8_t*)"BD",NULL,&listed));
    CHECK(!CywCountryList(b,24,(const uint8_t*)"BD",&count,NULL));
    memset(b,0,sizeof(b));
    CHECK(CywPackNvram(raw,sizeof(raw)-1,out,sizeof(out),&used));
    CHECK(used==12 && memcmp(out,"a=1\0b=2\0\0",9)==0);
    CHECK(!CywPackNvram(raw,sizeof(raw),out,sizeof(out),&used));
    CHECK(!CywPackNvram(raw,sizeof(raw)-1,out,8,&used));
    CHECK(!CywPackNvram((const uint8_t*)"badline",7,out,sizeof(out),&used));
    for(i=0;i<12;i++)CHECK(!CywSdpcmHeader(b,i,&len,&off));
    CywPut16(b,64);CywPut16(b+2,(uint16_t)~64);b[7]=12;b[5]=2;
    CHECK(CywSdpcmHeader(b,12,&len,&off) && len==64 && off==12);
    b[2]^=1;CHECK(!CywSdpcmHeader(b,64,&len,&off));b[2]^=1;
    b[7]=65;CHECK(!CywSdpcmHeader(b,64,&len,&off));b[7]=11;
    CHECK(!CywSdpcmHeader(b,64,&len,&off));
    memset(b,0,sizeof(b));b[0]=0x20;
    CHECK(CywEthernetBody(b,1518,&offset,&payload) && offset==4 && payload==1514);
    CHECK(!CywEthernetBody(b,1519,&offset,&payload));
    CHECK(!CywEthernetBody(b,17,&offset,&payload));
    b[3]=255;CHECK(!CywEthernetBody(b,64,&offset,&payload));
    CHECK(CywTxCredit(255,0,0));CHECK(!CywTxCredit(0,255,0));
    CHECK(!CywTxCredit(1,1,0));CHECK(!CywTxCredit(1,2,1));
    CHECK(CywAcceptEthernet(mac,mac,1,NULL,0));
    CHECK(!CywAcceptEthernet(mac,mac,8,NULL,0));
    CHECK(CywAcceptEthernet(bc,mac,8,NULL,0));
    CHECK(!CywAcceptEthernet(bc,mac,4,NULL,0));
    CHECK(CywAcceptEthernet(group,mac,2,group,1));
    CHECK(!CywAcceptEthernet(group,mac,2,NULL,0));
    CHECK(CywAcceptEthernet(group,mac,4,NULL,0));
    CHECK(!CywAcceptEthernet(group,mac,4,group,33));
    CHECK(!CywValidConnect(&r));r.Version=1;r.SsidLength=1;r.Country[0]='B';r.Country[1]='D';
    CHECK(CywValidConnect(&r));r.SsidLength=33;CHECK(!CywValidConnect(&r));
    r.SsidLength=1;r.Reserved[0]=1;CHECK(!CywValidConnect(&r));
    CywPut32(b,0x12345678);CHECK(CywLe32(b)==0x12345678 && CywBe32(b)==0x78563412);
    if(failures)return 1;puts("PASS: bounded network wire formats, NVRAM, credits and credential ABI");return 0;
}
