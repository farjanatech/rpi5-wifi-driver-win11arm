/* Exercise the actual sdio.c CMD52 path with delayed controller events. */
static void Cmd52Operating(PRPI5CYW_ADAPTER A)
{Init(A);A->BusModeStage=6;A->BusWidth=4;A->BusActualKhz=25000;}
static void RunCmd52WaitTests(void)
{
    RPI5CYW_ADAPTER a;UCHAR value;ULONG delay,response,mode;
    Cmd52Operating(&a);
    CHECK(SdioCmd52Read(&a,0,5,&value)==0 && !StallUs && !SleepCount);
    CHECK(a.RuntimeCmd52Commands==1 && !a.RuntimeCmd52FastPolls);
    for(delay=10;delay<=50;delay+=10) {
        Cmd52Operating(&a);CommandDelayUs=delay;Card[0][5]=6;
        CHECK(SdioCmd52Read(&a,0,5,&value)==0 && value==6);
        CHECK(StallUs==delay && !SleepCount && a.RuntimeCmd52FastPolls==delay/10);
        CHECK(!a.RuntimeCmd52WaitSleeps && !a.RuntimeCmd52Timeouts && !ResetCount);
    }
    /* 60 us completion falls back once; the fast spin cannot grow unbounded. */
    Cmd52Operating(&a);CommandDelayUs=60;SleepUs=16000;
    CHECK(SdioCmd52Read(&a,0,5,&value)==0 && StallUs==50 && SleepCount==1);
    CHECK(a.RuntimeCmd52WaitSleeps==1 && !a.RuntimeCmd52Timeouts);
    Cmd52Operating(&a);CommandDelayUs=200000;SleepUs=16000;
    CHECK(SdioCmd52Read(&a,0,5,&value)==0 && StallUs==50 && SleepCount==13);
    /* A real elapsed-time deadline, not a tenfold-shortened polling count. */
    Cmd52Operating(&a);CommandDelayUs=2000000;SleepUs=16000;a.BpWindowValid=1;
    CHECK(SdioCmd52Read(&a,0,5,&value)==STATUS_IO_TIMEOUT);
    CHECK(StallUs==50 && SleepCount==63 && a.RuntimeCmd52Timeouts==1);
    CHECK(SimTime>=10000000ULL && SimTime<10200000ULL && ResetCount==1 && !a.BpWindowValid);
    Cmd52Operating(&a);CommandDelayUs=2000000;SleepUs=1000;
    CHECK(SdioCmd52Read(&a,0,5,&value)==STATUS_IO_TIMEOUT && SleepCount==1000 && StallUs==50);
    /* Cancellation in either wait path; no fake completion or R5 success. */
    Cmd52Operating(&a);CommandDelayUs=20;StopOnStall=1;
    CHECK(SdioCmd52Read(&a,0,5,&value)==STATUS_INVALID_DEVICE_STATE && StallUs==10 && !SleepCount);
    Cmd52Operating(&a);CommandDelayUs=20000;StopOnSleep=1;
    CHECK(SdioCmd52Read(&a,0,5,&value)==STATUS_INVALID_DEVICE_STATE && SleepCount==1);
    Cmd52Operating(&a);CommandDelayUs=20;CommandEvent=SDHCI_INT_CMD_CRC;a.BpWindowValid=1;
    CHECK(SdioCmd52Read(&a,0,5,&value)==STATUS_IO_DEVICE_ERROR && ResetCount==1 && !a.BpWindowValid);
    Cmd52Operating(&a);CommandDelayUs=20;Fail52At=1;
    CHECK(SdioCmd52Read(&a,0,5,&value)==STATUS_IO_DEVICE_ERROR && a.LastResponse==0x200);
    Cmd52Operating(&a);CommandDelayUs=20;
    CHECK(SdioCmd52Write(&a,1,0x1000e,0x55,0xff)==0 && Commands52==2);
    CHECK(a.RuntimeCmd52Commands==2 && StallUs==40 && !SleepCount);
    Cmd52Operating(&a);CommandDelayUs=20;ReadbackMismatch=1;
    CHECK(SdioCmd52Write(&a,1,0x1000e,0x55,0xff)==STATUS_DEVICE_DATA_ERROR);
    /* Startup, unverified mode, width, clock bounds, IRQL and non-CMD52
     * commands must keep the original 100-us polling path. */
    for(mode=0;mode<6;mode++) {
        Cmd52Operating(&a);CommandDelayUs=20;
        if(mode==0)a.BusModeStage=5;
        if(mode==1)a.BusWidth=1;
        if(mode==2)a.BusActualKhz=400;
        if(mode==3)a.BusActualKhz=50000;
        if(mode==4)TestIrql=2;
        if(mode==5)Init(&a);
        CommandDelayUs=20;
        CHECK(SdioCmd52Read(&a,0,5,&value)==0 && StallUs==100 && !SleepCount);
        CHECK(!a.RuntimeCmd52Commands);
    }
    Cmd52Operating(&a);CommandDelayUs=20;
    CHECK(SdioSendCommand(&a,5,0,SDHCI_CMD_RESP_48,&response)==0 && StallUs==100);
    CHECK(!a.RuntimeCmd52Commands);
    Init(&a);CommandDelayUs=2000000;
    CHECK(SdioCmd52Read(&a,0,5,&value)==STATUS_IO_TIMEOUT && StallUs==1000000 && ResetCount==1);
    CHECK(!a.RuntimeCmd52Commands && !SleepCount);
    puts("PASS: runtime CMD52 fast polling, yielding fallback, one-second deadline, cancellation/errors and unchanged startup.");
}
