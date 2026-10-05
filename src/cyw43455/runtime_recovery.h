/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
/* A fatal runtime transport fault may receive one adapter lifecycle restart.
 * Startup failures, pause/power transitions, cancellation and repeated faults
 * are never looped automatically. The failed FIFO transaction itself is never
 * replayed; recovery starts a new firmware/SDIO session. */
#define CYW_RUNTIME_RECOVERY_MAX 1u
static __inline int CywRuntimeRecoveryEligible(
    unsigned RuntimeReady,unsigned Stop,unsigned Paused,unsigned Attempts,
    long Status,unsigned FifoTransportFailed)
{
    if(!RuntimeReady || Stop || Paused || Attempts>=CYW_RUNTIME_RECOVERY_MAX)return 0;
    if(Status==(long)STATUS_IO_TIMEOUT || Status==(long)STATUS_IO_DEVICE_ERROR)return 1;
    return FifoTransportFailed && Status==(long)STATUS_INVALID_DEVICE_STATE;
}
