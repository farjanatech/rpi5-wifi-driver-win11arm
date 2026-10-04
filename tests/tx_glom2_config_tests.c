/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <stdio.h>
#include <string.h>
#define RPI5CYW_HOST_TEST 1
#define RPI5CYW_TX_GLOM2 1
#include "../src/driver/driver.h"
#define STATUS_UNSUCCESSFUL ((NTSTATUS)0xc0000001L)

static unsigned Failures,Calls;
static NTSTATUS Result;
static ULONG FirmwareError;
#define CHECK(x) do {if(!(x)){printf("FAIL line %d: %s\n",__LINE__,#x);++Failures;}}while(0)

static NTSTATUS CywInt(PRPI5CYW_ADAPTER A,const char *Name,ULONG Value)
{
    ++Calls;CHECK(!strcmp(Name,"bus:rxglom") && Value==1);
    A->FirmwareError=FirmwareError;return Result;
}
#include "../src/cyw43455/tx_glom2_config.h"

static void Init(PRPI5CYW_ADAPTER A)
{
    memset(A,0,sizeof(*A));Calls=0;Result=STATUS_SUCCESS;FirmwareError=0;
}
int main(void)
{
    RPI5CYW_ADAPTER a;
    Init(&a);a.TxGlomEnabled=1;a.TxGlomConfigStatus=STATUS_IO_DEVICE_ERROR;
    CywTxGlom2ResetProtocol(&a);
    CHECK(!a.TxGlomEnabled && a.TxGlomConfigStatus==STATUS_SUCCESS && !Calls);

    Init(&a);CHECK(CywConfigureTxGlom2(&a)==STATUS_SUCCESS);
    CHECK(Calls==1 && a.TxGlomRequested==1 && a.TxGlomEnabled==1 &&
          a.TxGlomConfigStatus==STATUS_SUCCESS);

    Init(&a);Result=STATUS_UNSUCCESSFUL;FirmwareError=0xffffffe9UL;
    CHECK(CywConfigureTxGlom2(&a)==STATUS_SUCCESS);
    CHECK(Calls==1 && a.TxGlomRequested==1 && !a.TxGlomEnabled &&
          a.TxGlomConfigStatus==STATUS_UNSUCCESSFUL);

    Init(&a);Result=STATUS_IO_DEVICE_ERROR;
    CHECK(CywConfigureTxGlom2(&a)==STATUS_IO_DEVICE_ERROR);
    CHECK(Calls==1 && a.TxGlomRequested==1 && !a.TxGlomEnabled &&
          a.TxGlomConfigStatus==STATUS_IO_DEVICE_ERROR);

    Init(&a);Result=STATUS_UNSUCCESSFUL;FirmwareError=0xfffffffeUL;
    CHECK(CywConfigureTxGlom2(&a)==STATUS_UNSUCCESSFUL && !a.TxGlomEnabled);

    if(Failures)return 1;
    puts("PASS: two-frame host TX glom negotiation accepts success, falls back only on explicit UNSUPPORTED, and fails closed on ambiguous errors");
    return 0;
}
