/* Uses the real sdio.c implementation and the surrounding register model. */
static void InitFifoBlock(PRPI5CYW_ADAPTER a)
{
    Init(a);BlockModel=1;a->BusModeStage=6;a->BusWidth=4;a->BusActualKhz=50000;
    Card[0][CYW_SDIO_CCCR_CAPS]=2;Card[0][0x210]=0;Card[0][0x211]=2;
    CHECK(SdioPrepareRuntimeFifo(a)==STATUS_SUCCESS && a->FifoBlockReady);
}
static void TestFifoBlocks(void)
{
    static UCHAR buffer[65536];RPI5CYW_ADAPTER a;ULONG n,i,write,mode;
    Init(&a);CHECK(SdioPrepareRuntimeFifo(&a)==STATUS_INVALID_DEVICE_STATE && !a.FifoBlockReady);
    InitFifoBlock(&a);Card[0][CYW_SDIO_CCCR_CAPS]=0;
    CHECK(SdioPrepareRuntimeFifo(&a)==0 && !a.FifoBlockReady);
    InitFifoBlock(&a);Card[0][0x211]=1;
    CHECK(SdioPrepareRuntimeFifo(&a)==STATUS_DEVICE_CONFIGURATION_ERROR && !a.FifoBlockReady);
    for(i=1;i<=3;++i) {
        InitFifoBlock(&a);Fail52At=Commands52+i;
        CHECK(SdioPrepareRuntimeFifo(&a)==STATUS_IO_DEVICE_ERROR && !a.FifoBlockReady);
    }
    for(write=0;write<2;++write)for(n=1;n<=32;++n) {
        InitFifoBlock(&a);memset(buffer,0x5a,sizeof(buffer));
        CHECK(SdioFifoTransfer(&a,buffer,n*512,(BOOLEAN)write)==0);
        CHECK(Command53Count==1 && a.FifoBlockCommands==1 && a.FifoBlockBytes==n*512);
        CHECK((a.LastArgument&0x1fffffffUL)==(0x08000000UL|(0x8000UL<<9)|n));
        CHECK(((a.LastArgument>>28)&7)==2 && (a.LastArgument>>31)==write);
        mode=SdioRead16(&a,SDHCI_TRANSFER_MODE);
        CHECK(mode==(ULONG)(SDHCI_TRNS_BLOCK_COUNT_EN|(n>1?SDHCI_TRNS_MULTI:0)|(write?0:SDHCI_TRNS_READ)));
        CHECK(SdioRead16(&a,SDHCI_BLOCK_COUNT)==n && a.Cmd53BytesTransferred==n*512);
        CHECK(!ResetCount && !SleepCount && !a.FifoBlockFailures);
        if(write) {CHECK(FifoWrites==n*128);for(i=0;i<FifoWrites;++i)CHECK(WriteWords[i]==0x5a5a5a5a);}
        else {CHECK(FifoReads==n*128);for(i=0;i<n*512;i+=4)CHECK(SdioLoadLe32(buffer+i)==Fifo);}
    }
    InitFifoBlock(&a);CHECK(SdioFifoTransfer(&a,buffer,65536,FALSE)==0);
    CHECK(Command53Count==4 && a.FifoBlockBytes==65536 && FifoReads==16384);
    for(write=0;write<2;++write) {
        InitFifoBlock(&a);BlockCoalesced=1;
        CHECK(SdioFifoTransfer(&a,buffer,1536,(BOOLEAN)write)==0 && Command53Count==1);
        CHECK(BlockTotalWords==384 && !SleepCount && !ResetCount);
    }
    InitFifoBlock(&a);CHECK(SdioFifoTransfer(&a,buffer,1540,FALSE)==0);
    CHECK(Command53Count==2 && a.FifoBlockCommands==1 && FifoReads==385);
    for(write=0;write<2;++write) {
        InitFifoBlock(&a);Fail53At=2;memset(buffer,0xa5,sizeof(buffer));
        CHECK(SdioFifoTransfer(&a,buffer,65536,(BOOLEAN)write)==STATUS_IO_DEVICE_ERROR);
        CHECK(Command53Count==2 && BlockTotalWords==4096 && a.FifoTransportFailed);
        if(!write)for(i=0;i<sizeof(buffer);++i)CHECK(!buffer[i]);
        CHECK(SdioFifoTransfer(&a,buffer,64,(BOOLEAN)write)==STATUS_INVALID_DEVICE_STATE && Command53Count==2);
    }
    /* Legacy byte path stays available only BEFORE a FIFO failure, never as
     * an automatic replay after some bytes have already left/entered FIFO. */
    Init(&a);CHECK(SdioFifoTransfer(&a,buffer,1536,FALSE)==0);
    CHECK(Command53Count==3 && !a.FifoBlockCommands);
    for(write=0;write<2;++write)for(i=1;i<=3;++i) {
        InitFifoBlock(&a);BlockFailAt=i;memset(buffer,0xa5,1536);
        CHECK(SdioFifoTransfer(&a,buffer,1536,(BOOLEAN)write)==STATUS_IO_DEVICE_ERROR);
        CHECK(Command53Count==1 && ResetCount==1 && !a.FifoBlockReady && a.FifoBlockFailures==1);
        CHECK(BlockTotalWords==i*128);
        if(!write)for(n=0;n<1536;++n)CHECK(!buffer[n]);
        CHECK(SdioFifoTransfer(&a,buffer,64,(BOOLEAN)write)==STATUS_INVALID_DEVICE_STATE && Command53Count==1);
    }
    for(i=1;i<=2;++i) {
        InitFifoBlock(&a);BlockShortAt=i;
        CHECK(SdioFifoTransfer(&a,buffer,1536,FALSE)==STATUS_DEVICE_DATA_ERROR && FifoReads==i*128);
        InitFifoBlock(&a);BlockHoldAt=i;SleepUs=15625;
        CHECK(SdioFifoTransfer(&a,buffer,1536,FALSE)==STATUS_IO_TIMEOUT);
        CHECK(SimTime<2700000ULL && Command53Count==1 && !a.FifoBlockReady);
        CHECK(a.RuntimeF2WaitSleeps==SleepCount && a.RuntimeCmd53SleepPhase[1]==SleepCount);
        CHECK(a.RuntimeCmd53Sleep100ns==(ULONG64)SleepCount*SleepUs*10);
    }
    for(write=0;write<2;++write) {
        InitFifoBlock(&a);BlockHoldAt=1;BlockStaleReady=1;
        CHECK(SdioFifoTransfer(&a,buffer,1536,(BOOLEAN)write)==STATUS_IO_TIMEOUT);
        CHECK(BlockTotalWords==128 && a.FifoTransportFailed); /* no overrun of a stale event */
    }
    for(mode=1;mode<=3;++mode) {
        InitFifoBlock(&a);Fault=mode;
        CHECK(!NT_SUCCESS(SdioFifoTransfer(&a,buffer,1536,FALSE)) && !FifoReads && ResetCount==1);
    }
    InitFifoBlock(&a);Fault=7;
    CHECK(SdioFifoTransfer(&a,buffer,1536,FALSE)==STATUS_IO_TIMEOUT);
    CHECK(a.Cmd53ResetStatus==STATUS_IO_TIMEOUT && !a.FifoBlockReady);
    InitFifoBlock(&a);BlockStopWord=129;
    CHECK(SdioFifoTransfer(&a,buffer,1536,FALSE)==STATUS_INVALID_DEVICE_STATE && FifoReads==129);
    for(i=0;i<1536;++i)CHECK(!buffer[i]);
    InitFifoBlock(&a);TestIrql=2;
    CHECK(SdioFifoTransfer(&a,buffer,512,FALSE)==STATUS_INVALID_PARAMETER && !Command53Count);TestIrql=0;
    CHECK(SdioFifoBlocks(&a,buffer,0,FALSE)==STATUS_INVALID_PARAMETER);
    CHECK(SdioFifoBlocks(&a,buffer,33,FALSE)==STATUS_INVALID_PARAMETER);
    CHECK(SdioFifoTransfer(NULL,buffer,512,FALSE)==STATUS_INVALID_PARAMETER);
    CHECK(SdioPrepareRuntimeFifo(NULL)==STATUS_INVALID_PARAMETER);
    puts("Checked production F2 block counts 1..32, 64KiB split, byte tail, capability gates, partial failure/no replay, deadlines and stop.");
}
