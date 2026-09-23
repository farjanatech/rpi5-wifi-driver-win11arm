/* Deterministic production-policy tests. CI only; no driver or hardware use. */
#include <stdio.h>
#include "../src/cyw43455/tx_retry.h"

static unsigned int Failures;
#define CHECK(x) do {if(!(x)){printf("FAIL line %d: %s\n",__LINE__,#x);++Failures;}}while(0)

static void TestAttemptBound(void)
{
    CYW_TX_RETRY retry={0};unsigned int attempt;
    for(attempt=0;attempt<CYW_TX_RETRY_FAST_LIMIT;++attempt) {
        CHECK(CywTxRetrySelect(&retry,1000,0,0,1)==1);
        CywTxRetryRecordWait(&retry,1000,1000,0); /* Already-signaled event. */
    }
    CHECK(CywTxRetrySelect(&retry,1000,0,0,1)==10);
    CHECK(CywTxRetrySelect(&retry,101000,0,0,1)==10);
    CHECK(CywTxRetrySelect(&retry,99999999,0,0,1)==10);
    CHECK(retry.FastRequests==4 && retry.FastAttempts==4 && retry.FastWakes==4);
    CHECK(retry.BackoffRequests==3 && retry.ActualFastWait100ns==0);
    CHECK(retry.FastTimeouts==0 && retry.MaxFastWait100ns==0);
}

static void TestElapsedBound(void)
{
    CYW_TX_RETRY retry={0};
    CHECK(CywTxRetrySelect(&retry,1000,0,0,1)==1);
    CywTxRetryRecordWait(&retry,1000,11000,1);
    CHECK(CywTxRetrySelect(&retry,200999,0,0,1)==1);
    CywTxRetryRecordWait(&retry,200999,201000,1);
    CHECK(CywTxRetrySelect(&retry,201000,0,0,1)==10); /* Exactly 20 ms. */
    CHECK(retry.FastAttempts==2 && retry.BackoffLatched==1);
    CHECK(retry.FastTimeouts==2 && retry.ActualFastWait100ns==10001);
    CHECK(retry.MaxFastWait100ns==10000);
    CHECK(CywTxRetrySelect(&retry,1201000,0,0,1)==10); /* No automatic restart. */
}

static void TestProgressAndReset(void)
{
    CYW_TX_RETRY retry={0};unsigned int attempt;
    for(attempt=0;attempt<4;++attempt) {
        CHECK(CywTxRetrySelect(&retry,1000+attempt,0,0,1)==1);
        CywTxRetryRecordWait(&retry,1000+attempt,1000+attempt,0);
        CHECK(CywTxRetrySelect(&retry,1000+attempt,0,1,1)==0);
        CHECK(retry.FastAttempts==attempt+1);
    }
    CHECK(CywTxRetrySelect(&retry,2000,0,0,1)==10);
    CHECK(CywTxRetrySelect(&retry,3000,0,1,1)==0); /* Exhausted RX does not reset. */
    CHECK(retry.FastAttempts==4 && retry.BackoffLatched==1);
    CHECK(CywTxRetrySelect(&retry,4000,0,0,1)==10);
    CHECK(CywTxRetrySelect(&retry,5000,1,0,0)==0); /* TX starts a new episode. */
    CHECK(retry.FastAttempts==0 && retry.BackoffLatched==0);
    CHECK(CywTxRetrySelect(&retry,6000,0,0,1)==1);
    CywTxRetryRecordWait(&retry,6000,16000,1);
    CHECK(CywTxRetrySelect(&retry,16000,1,0,1)==0);
    CHECK(retry.FastResumes==1 && retry.FastAttempts==0);
    CHECK(CywTxRetrySelect(&retry,16001,1,1,0)==0);
    CHECK(retry.FastResumes==1); /* No double count on subsequent progress. */
    CHECK(CywTxRetrySelect(&retry,17000,0,0,0)==10); /* Queue inactive/gate closed. */
    CHECK(retry.IdleRequests==1 && retry.EpisodeActive==0);
    CHECK(CywTxRetrySelect(&retry,18000,0,0,1)==1);
    CywTxRetryReset(&retry); /* Explicit lifecycle/empty reset during RX. */
    CHECK(retry.EpisodeActive==0 && retry.PendingFastWait==0 && retry.FollowUp==0);
    CHECK(retry.FastRequests==6 && retry.FastResumes==1 && retry.FastTimeouts==1);
    CHECK(retry.ActualFastWait100ns==10000);
}

static void TestIneligibleDuringRx(void)
{
    CYW_TX_RETRY retry={0};unsigned int attempt;
    for(attempt=0;attempt<4;++attempt) {
        CHECK(CywTxRetrySelect(&retry,1000+attempt,0,0,1)==1);
        CywTxRetryRecordWait(&retry,1000+attempt,1000+attempt,0);
    }
    CHECK(CywTxRetrySelect(&retry,2000,0,0,1)==10);
    /* Credits returned, queue empty, or lifecycle/flow gate closed: no longer
     * an eligible exhausted-credit episode, even if this loop received data. */
    CHECK(CywTxRetrySelect(&retry,3000,0,1,0)==0);
    CHECK(retry.EpisodeActive==0 && retry.BackoffLatched==0 && retry.FastAttempts==0);
    CHECK(CywTxRetrySelect(&retry,4000,0,0,1)==1);
    CHECK(retry.FastRequests==5 && retry.FastAttempts==1);
}

static void TestOverrunAndFollowUp(void)
{
    CYW_TX_RETRY retry={0};
    CHECK(CywTxRetrySelect(&retry,1000,0,0,1)==1);
    CywTxRetryRecordWait(&retry,1000,301000,1); /* Requested 1 ms, actually 30 ms. */
    CHECK(retry.ActualFastWait100ns==300000 && retry.MaxFastWait100ns==300000);
    CHECK(retry.BackoffLatched==1 && retry.FastAttempts==1);
    CHECK(CywTxRetrySelect(&retry,301000,0,1,1)==0);
    CHECK(retry.FastResumes==0 && retry.BackoffLatched==1);
    CHECK(CywTxRetrySelect(&retry,302000,0,0,1)==10);
    CywTxRetryRecordWait(&retry,302000,402000,1); /* Normal wait ignored. */
    CHECK(retry.FastTimeouts==1 && retry.ActualFastWait100ns==300000);
    CHECK(CywTxRetrySelect(&retry,402000,1,0,1)==0);
    CHECK(retry.FastResumes==0); /* RX-only intervening loop consumed follow-up. */
    CHECK(CywTxRetrySelect(&retry,403000,0,0,1)==1);
    CywTxRetryRecordWait(&retry,403000,403000,0);
    CHECK(CywTxRetrySelect(&retry,404000,1,1,0)==0);
    CHECK(retry.FastResumes==1 && retry.FastWakes==1);
}

static void TestClockRollback(void)
{
    CYW_TX_RETRY retry={0};
    CHECK(CywTxRetrySelect(&retry,10000,0,0,1)==1);
    CywTxRetryRecordWait(&retry,10000,11000,1);
    CHECK(CywTxRetrySelect(&retry,10999,0,0,1)==10);
    CHECK(retry.BackoffLatched==1 && retry.FastRequests==1);
    CHECK(CywTxRetrySelect(&retry,12000,0,0,1)==10);
    CywTxRetryReset(&retry);
    CHECK(CywTxRetrySelect(&retry,20000,0,0,1)==1);
    CywTxRetryRecordWait(&retry,20000,19999,0);
    CHECK(retry.BackoffLatched==1 && retry.ActualFastWait100ns==1000);
    CHECK(CywTxRetrySelect(&retry,21000,0,0,1)==10);
    CywTxRetryReset(&retry);
    CHECK(CywTxRetrySelect(&retry,30000,0,0,1)==1);
    CywTxRetryRecordWait(&retry,29999,30001,0); /* Start predates Select. */
    CHECK(retry.BackoffLatched==1 && retry.ActualFastWait100ns==1000);
}

static void TestDiagnosticsSaturate(void)
{
    CYW_TX_RETRY retry={0};
    retry.FastRequests=~0u;retry.FastTimeouts=~0u;
    retry.ActualFastWait100ns=~0ULL-5ULL;
    CHECK(CywTxRetrySelect(&retry,1000,0,0,1)==1);
    CywTxRetryRecordWait(&retry,1000,1010,1);
    CHECK(retry.FastRequests==~0u && retry.FastTimeouts==~0u);
    CHECK(retry.ActualFastWait100ns==~0ULL && retry.MaxFastWait100ns==10);
    CywTxRetryReset(&retry);
    CHECK(retry.ActualFastWait100ns==~0ULL && retry.FastRequests==~0u);
}

int main(void)
{
    TestAttemptBound();TestElapsedBound();TestProgressAndReset();TestIneligibleDuringRx();
    TestOverrunAndFollowUp();TestClockRollback();TestDiagnosticsSaturate();
    if(Failures)printf("tx_retry_tests: %u failures\n",Failures);
    else puts("tx_retry_tests: passed");
    return Failures?1:0;
}
