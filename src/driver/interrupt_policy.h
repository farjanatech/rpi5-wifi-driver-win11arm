/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once

/* SDIO CCCR INTx uses bit 1 for Function 1 and bit 2 for Function 2. */
#define CYW_INTERRUPT_PENDING_F1 0x02u
#define CYW_INTERRUPT_PENDING_F2 0x04u
#define CYW_INTERRUPT_PENDING_MASK (CYW_INTERRUPT_PENDING_F1 | CYW_INTERRUPT_PENDING_F2)

/*
 * A real CARD_INT may be fully serviced before rearm, so one empty INTx read
 * is normal. Fall back only after many consecutive interrupt-driven worker
 * passes that observed no firmware/RX work at all.
 */
#define CYW_INTERRUPT_EMPTY_WAKE_LIMIT 32u

static __inline unsigned int
CywInterruptPendingFunctions(unsigned int Pending)
{
    return Pending & CYW_INTERRUPT_PENDING_MASK;
}

static __inline unsigned int
CywInterruptNextEmptyWakeStreak(
    unsigned int InterruptWake,
    unsigned int UsefulWork,
    unsigned int Pending,
    unsigned int Current
    )
{
    /* Timer/host wakes neither prove nor disprove an IRQ storm episode. */
    if (!InterruptWake)
        return Current;
    if (UsefulWork || CywInterruptPendingFunctions(Pending))
        return 0;
    if (Current < CYW_INTERRUPT_EMPTY_WAKE_LIMIT)
        ++Current;
    return Current;
}

static __inline int
CywInterruptUsePollingFallback(unsigned int EmptyWakeStreak)
{
    return EmptyWakeStreak >= CYW_INTERRUPT_EMPTY_WAKE_LIMIT;
}
