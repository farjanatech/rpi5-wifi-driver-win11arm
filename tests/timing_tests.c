/* Production timing helpers, no hardware or driver installation. */
#include <stdio.h>
#include <stddef.h>
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
    CYW_TIMING T={0};CYW_TIMING_BUCKET *B=&T.Snapshot.Bucket[CywTimeWorkerWork];
    uint64_t Start;unsigned i,PriorReads;
    CHECK(sizeof(CYW_TIMING_SNAPSHOT)==568);
    CHECK(offsetof(CYW_TIMING_SNAPSHOT,ActiveMask)==40);
    CHECK(offsetof(CYW_TIMING_SNAPSHOT,Bucket)==48);
    CHECK(CywTimingBegin(&T)==0 && Reads==0);
    CHECK(CywTimingBeginBucket(&T,CywTimeWorkerWork)==0 && Reads==0);
    CywTimingEnd(&T,CywTimeWorkerWork,0);CHECK(Reads==0 && B->Count==0);
    CywTimingStart(&T);CHECK(T.Enabled && T.Snapshot.Version==2 && T.Snapshot.BucketCount==13);
    CHECK(T.Snapshot.ActiveMask==CYW_TIMING_ACTIVE_MASK);
    CHECK(T.TenMsTicks==100000 && T.HundredMsTicks==1000000);
    Start=CywTimingBeginBucket(&T,CywTimeWorkerWork);Now=99999;CywTimingEnd(&T,CywTimeWorkerWork,Start);
    CHECK(B->Count==1 && B->AtLeast10ms==0 && B->TotalTicks==99999);
    CywTimingRecord(&T,CywTimeWorkerWork,0,100000);
    CywTimingRecord(&T,CywTimeWorkerWork,0,1000000);
    CHECK(B->Count==3 && B->TotalTicks==1199999 && B->MaxTicks==1000000);
    CHECK(B->AtLeast10ms==2 && B->AtLeast100ms==1);
    CywTimingRecord(&T,CywTimeCount,0,1000000);
    CywTimingRecord(&T,CywTimeWorkerWork,100,99);CHECK(B->Count==3);
    PriorReads=Reads;
    CHECK(CywTimingBeginBucket(&T,CywTimeCount)==0);
    CHECK(CywTimingBeginBucket(&T,~0u)==0);
    CywTimingEnd(&T,CywTimeCount,0);CHECK(Reads==PriorReads);
    B->Count=UINT64_MAX;B->TotalTicks=UINT64_MAX-1;B->AtLeast10ms=UINT64_MAX;
    CywTimingRecord(&T,CywTimeWorkerWork,0,100000);
    CHECK(B->Count==UINT64_MAX && B->TotalTicks==UINT64_MAX && B->AtLeast10ms==UINT64_MAX);
    Frequency=101;Now=17;CywTimingStart(&T);
    CHECK(T.TenMsTicks==2 && T.HundredMsTicks==11);
    CywTimingRecord(&T,CywTimeWorkerWork,0,1);CHECK(B->AtLeast10ms==0);
    CywTimingRecord(&T,CywTimeWorkerWork,0,2);CHECK(B->AtLeast10ms==1 && B->AtLeast100ms==0);
    CywTimingRecord(&T,CywTimeWorkerWork,0,11);CHECK(B->AtLeast100ms==1);
    CHECK(T.Snapshot.SessionQpc==17 && B->Count==3);
    Frequency=10000000;Now=100;CywTimingStart(&T);
    /* Build this production helper test in both macro modes. Disabled detail
     * must do NO clock reads, not merely omit the recorded measurements. */
    for(i=0;i<(unsigned)CywTimeCount;++i) {
        unsigned Active=(CYW_TIMING_ACTIVE_MASK&(1ULL<<i))!=0;
        PriorReads=Reads;Start=CywTimingBeginBucket(&T,i);Now+=100000;
        CywTimingEnd(&T,i,Start);
        CHECK(Reads==PriorReads+(Active?2u:0u));
        CHECK(T.Snapshot.Bucket[i].Count==(Active?1ULL:0ULL));
        CHECK(T.Snapshot.Bucket[i].TotalTicks==(Active?100000ULL:0ULL));
        CHECK(T.Snapshot.Bucket[i].AtLeast10ms==(Active?1ULL:0ULL));
    }
    T.Snapshot.ActiveMask=0;PriorReads=Reads;
    CHECK(CywTimingBeginBucket(&T,CywTimeWorkerWork)==0);
    CywTimingEnd(&T,CywTimeWorkerWork,0);CHECK(Reads==PriorReads);
    CywTimingRecord(&T,CywTimeWorkerWork,0,999999);CHECK(B->Count==1);
    Frequency=1;CywTimingStart(&T);
    CHECK(T.TenMsTicks==1 && T.HundredMsTicks==1);
    Frequency=0;CywTimingStart(&T);CHECK(!T.Enabled && B->Count==0);
    PriorReads=Reads;CywTimingEnd(&T,CywTimeWorkerWork,0);
    CHECK(CywTimingBeginBucket(&T,CywTimeCmd53Rx)==0 && Reads==PriorReads);
    Frequency=UINT64_MAX;CywTimingStart(&T);CHECK(!T.Enabled);
    if(Failures)return 1;
    printf("PASS: timing v2 ABI, active mask, detail mode %u, disabled clock reads, startup, thresholds, saturation, reset.\n",
        (unsigned)RPI5CYW_DETAILED_TIMING);
    return 0;
}
