/* Pure observation/ABI tests: no hardware or fabricated transport credits. */
#include <stdio.h>
#include <string.h>
#include <stddef.h>
#include "../src/cyw43455/tx_credit_diag.h"
static unsigned failures;
#define CHECK(x) do {if(!(x)){printf("FAIL %d: %s\n",__LINE__,#x);failures++;}}while(0)
int main(void)
{
    CYW_TX_CREDIT_DIAG d;unsigned seq,win;
    CHECK(sizeof(CYW_TXD_U64)==8 && sizeof(d)==8*CYW_TX_DIAG_WORDS);
    CHECK(offsetof(CYW_TX_CREDIT_DIAG,RxEndToPostPumpMaxTicks)==8*(CYW_TX_DIAG_WORDS-1));
    memset(&d,0,sizeof(d));
    for(seq=0;seq<256;seq++)for(win=0;win<256;win++) {
        unsigned observed=CywTxDiagWindow((unsigned char)seq,(unsigned char)(seq+win));
        CHECK(observed==win);CywTxDiagCredit(&d,observed);
    }
    CHECK(d.CreditSamples==65536 && d.CreditZero==256 && d.CreditInvalid==191*256);
    CHECK(d.Credit1To4==4*256 && d.Credit5To16==12*256 && d.Credit17To64==48*256);
    CywTxDiagRx(&d,0,8);CywTxDiagRx(&d,8,8);CywTxDiagRx(&d,8,12);
    CywTxDiagRx(&d,0,255);CywTxDiagRx(&d,255,8);
    CHECK(d.RxBatches==5 && d.RxCreditReopens==1 && d.RxWindowGrows==2 && d.RxWindowChanges==4);
    CywTxDiagDuration(&d,&d.F1Ticks,&d.F1MaxTicks,10,20);
    CywTxDiagDuration(&d,&d.F1Ticks,&d.F1MaxTicks,20,20);
    CywTxDiagDuration(&d,&d.F1Ticks,&d.F1MaxTicks,20,19);
    CHECK(d.F1Ticks==10 && d.F1MaxTicks==10 && d.ClockRegressions==1);
    d.F1Ticks=~0ULL-2;CywTxDiagDuration(&d,&d.F1Ticks,&d.F1MaxTicks,0,20);
    CHECK(d.F1Ticks==~0ULL && d.F1MaxTicks==20);
    d.CreditSamples=~0ULL;CywTxDiagCredit(&d,0);CHECK(d.CreditSamples==~0ULL);
    CHECK(CywTxDiagAdd(~0ULL,1)==~0ULL && CywTxDiagAdd(1,2)==3);
    if(failures)return 1;
    puts("PASS: TX diagnostic ABI, all 65536 wrapped credit windows, batch observations, saturation and clock rollback.");
    return 0;
}
