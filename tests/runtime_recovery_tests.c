/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <stdio.h>
#include "kernel_shim.h"
#include "../src/cyw43455/runtime_recovery.h"
static int failures;
#define CHECK(x) do{if(!(x)){printf("FAIL %d: %s\n",__LINE__,#x);failures++;}}while(0)
int main(void)
{
    CHECK(CywRuntimeRecoveryEligible(1,0,0,0,STATUS_IO_TIMEOUT,1));
    CHECK(CywRuntimeRecoveryEligible(1,0,0,0,STATUS_IO_DEVICE_ERROR,0));
    CHECK(CywRuntimeRecoveryEligible(1,0,0,0,STATUS_INVALID_DEVICE_STATE,1));
    CHECK(!CywRuntimeRecoveryEligible(1,0,0,0,STATUS_INVALID_DEVICE_STATE,0));
    CHECK(!CywRuntimeRecoveryEligible(0,0,0,0,STATUS_IO_TIMEOUT,1));
    CHECK(!CywRuntimeRecoveryEligible(1,1,0,0,STATUS_IO_TIMEOUT,1));
    CHECK(!CywRuntimeRecoveryEligible(1,0,1,0,STATUS_IO_TIMEOUT,1));
    CHECK(!CywRuntimeRecoveryEligible(1,0,0,1,STATUS_IO_TIMEOUT,1));
    CHECK(!CywRuntimeRecoveryEligible(1,0,0,0,STATUS_DEVICE_DATA_ERROR,1));
    if(failures)return 1;
    puts("PASS: runtime recovery is one-shot and limited to fatal runtime transport failures.");
    return 0;
}
