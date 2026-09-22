/* Reuse the real BCDC/IOVAR transport harness, not a substitute radio parser. */
#define main CywPreviousControlMain
#include "control_reply_tests.c"
#undef main
typedef long LONG;
#define STATUS_DEVICE_NOT_READY ((NTSTATUS)0xc00000a3L)
#include "../src/cyw43455/radio.h"
int main(void)
{
    RPI5CYW_ADAPTER a;CYW_NETWORK n;ULONG report[20];unsigned i;
    CHECK(CywPreviousControlMain()==0);
    for(i=0;i<=9;++i) {
        Init(&a,&n);Mode=2;RadioCase=i;
        a.FirmwareCommand=26;a.FirmwareError=123;a.FirmwareReplyLength=36;
        a.FirmwareReplyDeclaredLength=37;a.FirmwareReplyPayloadLength=38;
        a.FirmwareRequestCapacity=39;a.FirmwareValueLength=40;
        a.NetworkPhase=600;a.NetworkStatus=0;
        CywReadRadio(&a,report);
        CHECK(report[0]==1 && Sent==4 && !Outstanding);
        CHECK(a.FirmwareCommand==26 && a.FirmwareError==123 && a.FirmwareReplyLength==36);
        CHECK(a.FirmwareReplyDeclaredLength==37 && a.FirmwareReplyPayloadLength==38);
        CHECK(a.FirmwareRequestCapacity==39 && a.FirmwareValueLength==40);
        CHECK(a.NetworkPhase==600 && !a.NetworkStatus);
        if(i<=1 || i==8) {
            CHECK(report[2]==15 && !report[3] && (LONG)report[12]==-55);
            CHECK(report[11]==(i==1?2400u:5000u) && report[14]==(i==8?1u:0u));
        } else {
            unsigned failed=i<=3?0:(i<=5?1:(i==6 || i==9?2:3));
            CHECK(report[2]==(15u & ~(1u<<failed)) && report[3] && report[4+failed]);
        }
        if(i==9)CHECK(report[17]==0xffffffe9UL && report[6]==(ULONG)STATUS_UNSUCCESSFUL);
    }
    Init(&a,&n);Mode=2;Fault=3;CywReadRadio(&a,report);
    CHECK(Sent==1 && report[2]==0 && report[3]==(ULONG)STATUS_IO_TIMEOUT);
    CHECK(report[5]==(ULONG)STATUS_DEVICE_NOT_READY);
    Init(&a,&n);Mode=2;FailAlloc=1;CywReadRadio(&a,report);
    CHECK(!Sent && !report[2] && report[3]==(ULONG)STATUS_INSUFFICIENT_RESOURCES && !Outstanding);
    if(Failures)return 1;
    puts("PASS: actual radio GETs, bands/RSSI/PM/MPC, malformed/unsupported/timeout/allocation failures, no SET or connection-diagnostic corruption");return 0;
}
