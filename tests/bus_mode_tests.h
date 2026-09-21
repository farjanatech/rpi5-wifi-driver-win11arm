/* Actual sdio.c mode code, not a second implementation. */
static void InitBus(PRPI5CYW_ADAPTER A)
{
    Init(A);A->Capabilities=200UL<<8;A->CccrRevision=3;
    Card[0][CYW_SDIO_CCCR_BUS_INTERFACE]=0x80;
    Card[0][CYW_SDIO_CCCR_SPEED]=1;
}
static void CheckSlow(PRPI5CYW_ADAPTER A)
{
    CHECK(A->BusRecoveryStatus==0 && A->BusWidth==1 && A->BusActualKhz==400);
    CHECK((Card[0][7]&3)==0 && (Card[0][0x13]&14)==0);
    CHECK((SdioRead8(A,SDHCI_HOST_CONTROL)&0x26)==0);
    CHECK(SdioRead16(A,SDHCI_CLOCK_CONTROL)==0xfa07);
}
static void RunBusModeTests(void)
{
    RPI5CYW_ADAPTER busAdapter;
    ULONG modeCalls,failedCall;
    InitBus(&busAdapter);
    CHECK(SdioNegotiateOperatingSpeed(&busAdapter)==0);
    CHECK(busAdapter.BusWidth==4 && busAdapter.BusActualKhz==25000);
    CHECK(Card[0][7]==0x82 && Card[0][0x13]==1);
    CHECK(SdioRead8(&busAdapter,SDHCI_HOST_CONTROL)==2);
    CHECK(SdioRead16(&busAdapter,SDHCI_CLOCK_CONTROL)==0x0407);
    modeCalls=Commands52;
    /* Every CMD52, including a failure AFTER the card write but before its
     * readback succeeds. Recovery must not assume the failed write did nothing. */
    for(failedCall=1;failedCall<=modeCalls;++failedCall) {
        InitBus(&busAdapter);Fail52At=failedCall;
        CHECK(!NT_SUCCESS(SdioNegotiateOperatingSpeed(&busAdapter)));
        CHECK(busAdapter.BusModeStage==90);CheckSlow(&busAdapter);
    }
    InitBus(&busAdapter);BusClockFault=1;
    CHECK(SdioNegotiateOperatingSpeed(&busAdapter)==STATUS_IO_TIMEOUT);
    CheckSlow(&busAdapter);
    InitBus(&busAdapter);BusClockFault=2;
    CHECK(SdioNegotiateOperatingSpeed(&busAdapter)==STATUS_IO_TIMEOUT);
    CHECK(busAdapter.BusModeStage==99 && !NT_SUCCESS(busAdapter.BusRecoveryStatus));
    InitBus(&busAdapter);BusHostFault=1;
    CHECK(SdioNegotiateOperatingSpeed(&busAdapter)==STATUS_DEVICE_DATA_ERROR);
    CheckSlow(&busAdapter);
    /* Warm card state after host-only reset, repeated resume and re-upgrade. */
    InitBus(&busAdapter);Card[0][7]=0x82;Card[0][0x13]=3;
    SdioWrite8(&busAdapter,SDHCI_HOST_CONTROL,0x26);
    CHECK(SdioRestoreIdentificationBus(&busAdapter)==0);CheckSlow(&busAdapter);
    CHECK(SdioNegotiateOperatingSpeed(&busAdapter)==0);
    CHECK(SdioRestoreIdentificationBus(&busAdapter)==0);CheckSlow(&busAdapter);
    CHECK(SdioNegotiateOperatingSpeed(&busAdapter)==0);
    InitBus(&busAdapter);busAdapter.Capabilities=0;
    CHECK(!NT_SUCCESS(SdioNegotiateOperatingSpeed(&busAdapter)));CheckSlow(&busAdapter);
    InitBus(&busAdapter);Card[0][8]=0x40;
    CHECK(!NT_SUCCESS(SdioNegotiateOperatingSpeed(&busAdapter)));CheckSlow(&busAdapter);
    InitBus(&busAdapter);busAdapter.Capabilities=250UL<<8;
    CHECK(SdioNegotiateOperatingSpeed(&busAdapter)==0 && busAdapter.BusActualKhz==25000);
    InitBus(&busAdapter);busAdapter.Capabilities=201UL<<8;
    CHECK(SdioNegotiateOperatingSpeed(&busAdapter)==0 && busAdapter.BusActualKhz<=25000);
    InitBus(&busAdapter);busAdapter.IoStopped=1;
    CHECK(!NT_SUCCESS(SdioNegotiateOperatingSpeed(&busAdapter)) && CommandCount==0);
    InitBus(&busAdapter);TestIrql=2;
    CHECK(!NT_SUCCESS(SdioNegotiateOperatingSpeed(&busAdapter)) && CommandCount==0);
    TestIrql=0;
    puts("PASS: actual 4-bit/25 MHz mode, CMD52 fault matrix, clock/host failure, warm/resume recovery, capability bounds.");
}
