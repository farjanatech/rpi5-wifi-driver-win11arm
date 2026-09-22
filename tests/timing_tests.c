/* Production timing helpers, no hardware or driver installation. */
#include <stdio.h>
#define RPI5CYW_HOST_TEST 1
#include "../src/driver/driver.h"
#include "../src/driver/timing_clock.h"
static uint64_t Now,Frequency=10000000;
static unsigned Reads,Failures;
LARGE_INTEGER KeQueryPerformanceCounter(LARGE_INTEGER *F)
{
    LARGE_INTEGER R;R.QuadPart=(LONGLONG)Now;++Reads;
    if(F)F->QuadPart=(LONGLONG)Frequency;
    return R;
}
#define CHECK(X) do{if(!(X)){printf("FAIL %d: %s\n",__LINE__,#X);++Failures;}}while(0)
int main(void)
{
    CYW_TIMING T={0};CYW_TIMING_BUCKET *B=&T.Snapshot.Bucket[CywTimeCmd52];
    uint64_t Start;
    CHECK(sizeof(CYW_TIMING_SNAPSHOT)==560);
    CHECK(CywTimingBegin(&T)==0 && Reads==0);
    CywTimingEnd(&T,CywTimeCmd52,0);CHECK(Reads==0 && B->Count==0);
    CywTimingStart(&T);CHECK(T.Enabled && T.Snapshot.Version==1 && T.Snapshot.BucketCount==13);
    Start=CywTimingBegin(&T);Now=99999;CywTimingEnd(&T,CywTimeCmd52,Start);
    CHECK(B->Count==1 && B->AtLeast10ms==0 && B->TotalTicks==99999);
    CywTimingRecord(&T,CywTimeCmd52,0,100000);
    CywTimingRecord(&T,CywTimeCmd52,0,1000000);
    CHECK(B->Count==3 && B->TotalTicks==1199999 && B->MaxTicks==1000000);
    CHECK(B->AtLeast10ms==2 && B->AtLeast100ms==1);
    CywTimingRecord(&T,CywTimeCount,0,1000000);
    CywTimingRecord(&T,CywTimeCmd52,100,99);CHECK(B->Count==3);
    B->Count=UINT64_MAX;B->TotalTicks=UINT64_MAX-1;B->AtLeast10ms=UINT64_MAX;
    CywTimingRecord(&T,CywTimeCmd52,0,100000);
    CHECK(B->Count==UINT64_MAX && B->TotalTicks==UINT64_MAX && B->AtLeast10ms==UINT64_MAX);
    Frequency=101;Now=17;CywTimingStart(&T);
    CywTimingRecord(&T,CywTimeCmd52,0,1);CHECK(B->AtLeast10ms==0);
    CywTimingRecord(&T,CywTimeCmd52,0,2);CHECK(B->AtLeast10ms==1 && B->AtLeast100ms==0);
    CywTimingRecord(&T,CywTimeCmd52,0,11);CHECK(B->AtLeast100ms==1);
    CHECK(T.Snapshot.SessionQpc==17 && B->Count==3);
    Frequency=0;CywTimingStart(&T);CHECK(!T.Enabled && B->Count==0);
    if(Failures)return 1;
    puts("PASS: timing ABI, disabled startup, zero epoch, thresholds, saturation, invalid clock, reset.");
    return 0;
}
