#include <stdio.h>
#include "../src/driver/interrupt_policy.h"

#define CHECK(x) do { if(!(x)) {     fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x); return 1; } } while(0)

int main(void)
{
    unsigned int streak=0,i;

    CHECK(CywInterruptPendingFunctions(0)==0);
    CHECK(CywInterruptPendingFunctions(CYW_INTERRUPT_PENDING_F1)==CYW_INTERRUPT_PENDING_F1);
    CHECK(CywInterruptPendingFunctions(CYW_INTERRUPT_PENDING_F2)==CYW_INTERRUPT_PENDING_F2);
    CHECK(CywInterruptPendingFunctions(0xff)==CYW_INTERRUPT_PENDING_MASK);

    /* Timer/host work must not erase evidence from consecutive empty IRQs. */
    streak=CywInterruptNextEmptyWakeStreak(0,0,0,17);
    CHECK(streak==17);

    /* Any useful interrupt service starts a fresh episode. */
    streak=CywInterruptNextEmptyWakeStreak(1,1,0,17);
    CHECK(streak==0);

    /* Function-pending state is meaningful even before the current pass
     * consumes it, so it resets the empty-wake episode. */
    streak=CywInterruptNextEmptyWakeStreak(1,0,CYW_INTERRUPT_PENDING_F1,17);
    CHECK(streak==0);
    streak=CywInterruptNextEmptyWakeStreak(1,0,CYW_INTERRUPT_PENDING_F2,17);
    CHECK(streak==0);

    for(i=0;i<CYW_INTERRUPT_EMPTY_WAKE_LIMIT-1;++i)
        streak=CywInterruptNextEmptyWakeStreak(1,0,0,streak);
    CHECK(!CywInterruptUsePollingFallback(streak));
    streak=CywInterruptNextEmptyWakeStreak(1,0,0,streak);
    CHECK(CywInterruptUsePollingFallback(streak));

    /* Saturation avoids wraparound re-enabling a proven bad IRQ path. */
    for(i=0;i<100;++i)
        streak=CywInterruptNextEmptyWakeStreak(1,0,0,streak);
    CHECK(streak==CYW_INTERRUPT_EMPTY_WAKE_LIMIT);

    puts("Interrupt policy tests passed.");
    return 0;
}
