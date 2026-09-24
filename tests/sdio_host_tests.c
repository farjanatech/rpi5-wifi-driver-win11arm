#include <stdio.h>
#include <stdlib.h>
#define RPI5CYW_HOST_TEST 1
#include "../src/sdio/sdio.c"
#include "../src/cyw43455/chip.c"

static ULONG Registers[64], Card[2][0x20000];
static ULONG Fifo, FifoReads, ResetCount, CommandCount, Ticks, Command53Count;
static ULONG Fault, Fail52At, Commands52, ReadbackMismatch, Command53Events;
static ULONG FifoWrites, WriteWords[16384], DiscoveryMode, Fail53At;
static ULONG BlockModel,BlockRemaining,BlockWords,BlockOrdinal;
static ULONG BlockFailAt,BlockShortAt,BlockHoldAt,BlockStopWord,BlockTotalWords;
static ULONG BlockCoalesced,BlockStaleReady;
static ULONG64 SimTime, ReadyAt;
static ULONG QpcReads;
LARGE_INTEGER KeQueryPerformanceCounter(LARGE_INTEGER *Frequency)
{
    LARGE_INTEGER Now;Now.QuadPart=(LONGLONG)SimTime;++QpcReads;
    if(Frequency)Frequency->QuadPart=10000000;
    return Now;
}
static ULONG SleepUs, SleepCount, StallUs, StopOnSleep;
static ULONG BusClockFault,BusHostFault;
static ULONG HighSpeedClockSeen,HighSpeedHostFault,HighSpeedCardFault,HighSpeedStateFault;
static ULONG PhaseMode, PhaseUs[3], ScheduledEvent, PhaseWords, StopOnStall;
static ULONG64 PhaseDue;
static PRPI5CYW_ADAPTER ActiveAdapter;
static const ULONG Erom[] = {
    0x4BF80001, 0x01080001, 0x18000005, 0x18100085,
    0x4BF82901, 0x01080001, 0x18002005, 0x18101085,
    0x4BF81201, 0x01080001, 0x18003005, 0x18103085,
    0x4BF83E01, 0x01080001, 0x18004005, 0x18102085, 0xF
};
static int Failures;
int TestIrql;
#define CHECK(x) do { if (!(x)) { printf("FAIL line %d: %s\n", __LINE__, #x); Failures++; } } while (0)

static ULONG Offset(const void *Address)
{ return (ULONG)((const UCHAR *)Address - (const UCHAR *)Registers); }
static void BlockWord(void)
{
    ULONG ready;
    if(!BlockModel || !(Registers[SDHCI_ARGUMENT/4]&0x08000000UL))return;
    BlockTotalWords++;
    if(BlockStopWord==BlockTotalWords)ActiveAdapter->IoStopped=1;
    if(--BlockWords)return;
    BlockWords=128;BlockOrdinal++;BlockRemaining--;
    Registers[SDHCI_PRESENT_STATE/4]&=~(SDHCI_PS_DATA_AVAILABLE|SDHCI_PS_SPACE_AVAILABLE);
    ready=(Registers[SDHCI_ARGUMENT/4]&0x80000000UL)?
        SDHCI_INT_BUFFER_WRITE_READY:SDHCI_INT_BUFFER_READ_READY;
    if(BlockOrdinal==BlockFailAt)Registers[SDHCI_INT_STATUS/4]|=SDHCI_INT_DATA_CRC;
    else if(BlockOrdinal==BlockHoldAt) {
        if(BlockStaleReady)Registers[SDHCI_INT_STATUS/4]|=ready;
        return;
    }
    else if(!BlockRemaining || BlockOrdinal==BlockShortAt)
        Registers[SDHCI_INT_STATUS/4]|=SDHCI_INT_XFER_COMPLETE;
    else {
        Registers[SDHCI_PRESENT_STATE/4]|=
            ready==SDHCI_INT_BUFFER_READ_READY?SDHCI_PS_DATA_AVAILABLE:SDHCI_PS_SPACE_AVAILABLE;
        if(!BlockCoalesced)Registers[SDHCI_INT_STATUS/4]|=ready;
    }
}
UCHAR READ_REGISTER_UCHAR(PUCHAR Address) { return *Address; }
USHORT READ_REGISTER_USHORT(PUSHORT Address) { return *Address; }
ULONG READ_REGISTER_ULONG(PULONG Address)
{
    if (Offset(Address) == SDHCI_INT_STATUS && ScheduledEvent && SimTime >= PhaseDue) {
        *Address |= ScheduledEvent;ScheduledEvent=0;
    }
    if (Offset(Address) == SDHCI_INT_STATUS && ReadyAt && SimTime >= ReadyAt)
    {
        *Address |= Command53Events;
        ReadyAt = 0;
    }
    if (Offset(Address) == SDHCI_BUFFER)
    {
        FifoReads++;
        BlockWord();
        if (PhaseMode && --PhaseWords == 0) {
            ScheduledEvent=SDHCI_INT_XFER_COMPLETE;PhaseDue=SimTime+(ULONG64)PhaseUs[2]*10;
        }
        if (Fault == 10) Registers[SDHCI_INT_STATUS / 4] |= SDHCI_INT_XFER_COMPLETE;
        if (Fault == 11) Registers[SDHCI_INT_STATUS / 4] |= SDHCI_INT_DATA_CRC;
        return Fifo;
    }
    return *Address;
}
void WRITE_REGISTER_UCHAR(PUCHAR Address, UCHAR Value)
{
    if (Offset(Address) == SDHCI_SOFTWARE_RESET)
    {
        ResetCount++;
        *Address = Fault == 7 ? Value : 0;
    }
    else if(Offset(Address)==SDHCI_HOST_CONTROL && BusHostFault && (Value&2)) *Address=0;
    else if(Offset(Address)==SDHCI_HOST_CONTROL && HighSpeedHostFault && (Value&4)) *Address=(UCHAR)(Value&~4U);
    else *Address = Value;
}
void WRITE_REGISTER_ULONG(PULONG Address, ULONG Value)
{
    if (Offset(Address) == SDHCI_BUFFER)
    {
        CHECK(FifoWrites < 16384);
        if (FifoWrites < 16384) WriteWords[FifoWrites++] = Value;
        BlockWord();
        if (PhaseMode && --PhaseWords == 0) {
            ScheduledEvent=SDHCI_INT_XFER_COMPLETE;PhaseDue=SimTime+(ULONG64)PhaseUs[2]*10;
        }
        if (Fault == 10) Registers[SDHCI_INT_STATUS / 4] |= SDHCI_INT_XFER_COMPLETE;
        if (Fault == 11) Registers[SDHCI_INT_STATUS / 4] |= SDHCI_INT_DATA_CRC;
        return;
    }
    if (Offset(Address) == SDHCI_INT_STATUS) {
        *Address &= ~Value;
        if (PhaseMode && Value == SDHCI_INT_CMD_COMPLETE) {
            ScheduledEvent=(Registers[SDHCI_ARGUMENT/4]&0x80000000UL)?
                SDHCI_INT_BUFFER_WRITE_READY:SDHCI_INT_BUFFER_READ_READY;
            PhaseDue=SimTime+(ULONG64)PhaseUs[1]*10;
        }
    }
    else *Address = Value;
}
void WRITE_REGISTER_USHORT(PUSHORT Address, USHORT Value)
{
    ULONG Argument, Fn, Reg, Response = 0, Command;
    *Address = Value;
    if(Offset(Address)==SDHCI_CLOCK_CONTROL && (Value&1) && (Value&0xff00)==0x0200) {
        HighSpeedClockSeen=1;
        if(HighSpeedStateFault==1) *((PUCHAR)Registers+SDHCI_POWER_CONTROL)^=2;
        if(HighSpeedStateFault==2) *((PUSHORT)((PUCHAR)Registers+SDHCI_HOST_CONTROL2))|=8;
        HighSpeedStateFault=0;
    }
    if (Offset(Address) == SDHCI_CLOCK_CONTROL && (Value & 1) &&
        !(BusClockFault==2 || (BusClockFault==1 && (Value&0xff00)==0x0400) ||
          (BusClockFault>=3 && (Value&0xff00)==0x0200) ||
          (BusClockFault==4 && HighSpeedClockSeen && (Value&0xff00)==0x0400) ||
          (BusClockFault==5 && HighSpeedClockSeen)))
        *Address |= SDHCI_CLK_INT_CLK_STABLE;
    if (Offset(Address) != SDHCI_COMMAND) return;
    CommandCount++;
    Command = Value >> 8;
    Argument = Registers[SDHCI_ARGUMENT / 4];
    Fn = (Argument >> 28) & 7;
    Reg = (Argument >> 9) & 0x1FFFF;
    Registers[SDHCI_INT_STATUS / 4] = SDHCI_INT_CMD_COMPLETE;
    if (Command == 53)
    {
        if (DiscoveryMode)
        {
            ULONG Backplane = (Card[1][CYW_F1_WINDOW_LOW] << 8) |
                (Card[1][CYW_F1_WINDOW_LOW + 1] << 16) |
                (Card[1][CYW_F1_WINDOW_LOW + 2] << 24) | (Reg & 0x7FFF);
            Fifo = 0xFFFFFFFFUL;
            if (Backplane == 0x180000FC) Fifo = Fault == 12 ? 0 : 0x180FF000;
            if (Backplane >= 0x180FF000 && Backplane < 0x180FF000 + sizeof(Erom))
                Fifo = Erom[(Backplane - 0x180FF000) / 4];
            if (Backplane == 0x18102408) Fifo = 1;
            if (Backplane == 0x18102800) Fifo = Fault == 13 ? 1 : 0;
            if (Backplane == 0x18004004) Fifo = 0x23;
        }
        Command53Count++;
        Registers[SDHCI_INT_STATUS / 4] = Command53Events;
        if (Argument & 0x80000000UL)
        {
            Registers[SDHCI_INT_STATUS / 4] &= ~SDHCI_INT_BUFFER_READ_READY;
            Registers[SDHCI_INT_STATUS / 4] |= SDHCI_INT_BUFFER_WRITE_READY;
        }
        if (Fault == 1 || Fault == 7) Registers[SDHCI_INT_STATUS / 4] = 0;
        if (ReadyAt) Registers[SDHCI_INT_STATUS / 4] = 0;
        if (Fault == 2) Registers[SDHCI_INT_STATUS / 4] |= SDHCI_INT_DATA_CRC;
        if (Fault == 3) Response = 0x200;
        if (Fault == 4) Registers[SDHCI_INT_STATUS / 4] = SDHCI_INT_CMD_COMPLETE;
        if (Fault == 5) Registers[SDHCI_INT_STATUS / 4] &= ~SDHCI_INT_XFER_COMPLETE;
        if (Fault == 10 || Fault == 11)
            Registers[SDHCI_INT_STATUS / 4] &= ~SDHCI_INT_XFER_COMPLETE;
        if (Fault == 6 && Command53Count == 2) Fifo ^= 0x10000;
        if (Command53Count == Fail53At)
            Registers[SDHCI_INT_STATUS / 4] |= SDHCI_INT_DATA_CRC;
        /* Replay the observed exp0.6 failure: F1 rejects the oversized
         * byte-mode request before the host may put data in the FIFO. */
        if (Fault == 14 && Fn == 1 && !(Argument & 0x08000000UL) &&
            ((Argument & 511) == 0 || (Argument & 511) > 64))
        {
            Response = 0x1100;
            Registers[SDHCI_INT_STATUS / 4] = 0x11;
        }
        if (PhaseMode) {
            Registers[SDHCI_INT_STATUS/4]=0;
            ScheduledEvent=SDHCI_INT_CMD_COMPLETE;PhaseDue=SimTime+(ULONG64)PhaseUs[0]*10;
            PhaseWords=(Registers[SDHCI_BLOCK_SIZE/4]&0xfff)+3;
            PhaseWords/=4;
        }
        if(BlockModel && (Argument&0x08000000UL)) {
            BlockRemaining=Argument&511;BlockWords=128;BlockOrdinal=0;
            Registers[SDHCI_PRESENT_STATE/4]|=(Argument&0x80000000UL)?SDHCI_PS_SPACE_AVAILABLE:SDHCI_PS_DATA_AVAILABLE;
            Registers[SDHCI_INT_STATUS/4]=SDHCI_INT_CMD_COMPLETE|
                ((Argument&0x80000000UL)?SDHCI_INT_BUFFER_WRITE_READY:SDHCI_INT_BUFFER_READ_READY);
            if(Fault==1 || Fault==7)Registers[SDHCI_INT_STATUS/4]=0;
            if(Fault==2 || Command53Count==Fail53At)Registers[SDHCI_INT_STATUS/4]|=SDHCI_INT_DATA_CRC;
        }
    }
    else if (Command == 52)
    {
        Commands52++;
        if (Commands52 == Fail52At) { Response = 0x200; goto Complete; }
        CHECK(Fn < 2);
        if (Fn >= 2) { Response = 0x200; goto Complete; }
        if ((Argument & 0x80000000UL) != 0)
        {
            Card[Fn][Reg] = Argument & 0xFF;
            if(HighSpeedCardFault && Fn==0 && Reg==CYW_SDIO_CCCR_SPEED &&
                (Card[0][Reg]&CYW_SDIO_SPEED_BSS_MASK)==CYW_SDIO_SPEED_ENABLE_HS)
                Card[0][Reg]&=~CYW_SDIO_SPEED_ENABLE_HS;
            if (Fn == 0 && Reg == 2 && Fault != 8) Card[0][3] = Card[0][2];
            if (Fn == 1 && Reg == CYW_F1_CLOCK && Fault != 9)
                Card[1][Reg] |= 0x40;
        }
        Response = Card[Fn][Reg];
        if (ReadbackMismatch && !(Argument & 0x80000000UL)) Response ^= 1;
    }
Complete:
    Registers[SDHCI_RESPONSE0 / 4] = Response;
}
ULONG64 KeQueryInterruptTime(void) { return SimTime; }
void KeStallExecutionProcessor(ULONG Microseconds)
{
    SimTime+=(ULONG64)Microseconds*10; StallUs+=Microseconds; Ticks++;
    if(StopOnStall)ActiveAdapter->IoStopped=1;
}
NTSTATUS KeDelayExecutionThread(int Mode, BOOLEAN Alertable, LARGE_INTEGER *Delay)
{
    (void)Mode; (void)Alertable; (void)Delay;
    SimTime+=(ULONG64)SleepUs*10; SleepCount++; Ticks++;
    if(StopOnSleep)ActiveAdapter->IoStopped=1;
    return 0;
}
void Rpi5CywWriteDiagnostics(PRPI5CYW_ADAPTER Adapter, ULONG Stage, NTSTATUS Status)
{
    Adapter->ProbePhase = Stage; Adapter->ProbeStatus = Status;
    if (Stage == 300) DiscoveryMode = 1;
}

static void Init(PRPI5CYW_ADAPTER Adapter)
{
    memset(Adapter, 0, sizeof(*Adapter));
    memset(Registers, 0, sizeof(Registers)); memset(Card, 0, sizeof(Card));
    Adapter->RegisterBase = Registers; Adapter->RegisterLength = sizeof(Registers);
    Adapter->SdioFunctions = 3;
    Fifo = 0x15264345; FifoReads = ResetCount = CommandCount = Ticks = 0;
    FifoWrites = DiscoveryMode = Fail53At = 0; memset(WriteWords, 0, sizeof(WriteWords));
    Fault = Fail52At = Commands52 = ReadbackMismatch = Command53Count = 0;
    Command53Events = SDHCI_INT_CMD_COMPLETE | SDHCI_INT_BUFFER_READ_READY |
                      SDHCI_INT_XFER_COMPLETE;
    TestIrql = 0;
    SimTime=ReadyAt=0;SleepUs=1000;SleepCount=StallUs=StopOnSleep=0;
    ActiveAdapter=Adapter;
    BusClockFault=BusHostFault=0;
    HighSpeedClockSeen=HighSpeedHostFault=HighSpeedCardFault=HighSpeedStateFault=0;
    PhaseMode=ScheduledEvent=PhaseWords=StopOnStall=0;PhaseDue=0;
    BlockModel=BlockRemaining=BlockWords=BlockOrdinal=0;
    BlockFailAt=BlockShortAt=BlockHoldAt=BlockStopWord=BlockTotalWords=0;
    BlockCoalesced=BlockStaleReady=0;
    memset(PhaseUs,0,sizeof(PhaseUs));
    Card[1][CYW_F1_WINDOW_LOW] = 0x80;
    Card[1][CYW_F1_WINDOW_LOW + 1] = 0x12;
    Card[1][CYW_F1_WINDOW_LOW + 2] = 0x18;
}

static void CheckRestored(void)
{
    CHECK(Card[0][2] == 0);
    CHECK((Card[1][CYW_F1_CLOCK] & 0x3F) == 0);
    CHECK(Card[1][CYW_F1_WINDOW_LOW] == 0x80);
    CHECK(Card[1][CYW_F1_WINDOW_LOW + 1] == 0x12);
    CHECK(Card[1][CYW_F1_WINDOW_LOW + 2] == 0x18);
}

#include "erom_tests.h"
#include "bus_mode_tests.h"
#include "fifo_block_tests.h"

static void InitHighSpeedBus(PRPI5CYW_ADAPTER A)
{
    InitBus(A);A->HostVersion=2;A->Capabilities|=SDHCI_CAP_HIGH_SPEED;
    SdioWrite8(A,SDHCI_POWER_CONTROL,SDHCI_PC_BUS_VOLTAGE_330|SDHCI_PC_BUS_POWER_ON);
}
static void CheckDefaultOperatingBus(PRPI5CYW_ADAPTER A)
{
    CHECK(A->BusModeStage==4 && A->BusWidth==4 && A->BusActualKhz==25000);
    CHECK(!A->BusHighSpeedActive && A->BusRecoveryStatus==0);
    CHECK(Card[0][CYW_SDIO_CCCR_SPEED]==1 && Card[0][CYW_SDIO_CCCR_BUS_INTERFACE]==0x82);
    CHECK(SdioRead8(A,SDHCI_HOST_CONTROL)==2 && SdioRead16(A,SDHCI_CLOCK_CONTROL)==0x0407);
}
static void RunHighSpeedBusTests(void)
{
    RPI5CYW_ADAPTER hsAdapter;
    ULONG baselineCalls,highSpeedCalls,failedCall,modeFault;
    InitBus(&hsAdapter);CHECK(SdioNegotiateOperatingSpeed(&hsAdapter)==0);
    baselineCalls=Commands52;
    InitHighSpeedBus(&hsAdapter);CHECK(SdioNegotiateOperatingSpeed(&hsAdapter)==0);
    highSpeedCalls=Commands52;
    CHECK(highSpeedCalls>baselineCalls);
    CHECK(hsAdapter.BusHighSpeedEligible==1 && hsAdapter.BusHighSpeedAttempted==1);
    CHECK(hsAdapter.BusHighSpeedActive==1 && hsAdapter.BusHighSpeedStatus==0);
    CHECK(hsAdapter.BusHighSpeedRejectMask==0 && hsAdapter.BusModeStage==4);
    CHECK(hsAdapter.BusWidth==4 && hsAdapter.BusActualKhz==50000);
    CHECK(hsAdapter.BusTargetKhz==50000 && Card[0][CYW_SDIO_CCCR_SPEED]==3);
    CHECK(SdioRead8(&hsAdapter,SDHCI_HOST_CONTROL)==6);
    CHECK(SdioRead16(&hsAdapter,SDHCI_CLOCK_CONTROL)==0x0207);
    CHECK(SdioRead16(&hsAdapter,SDHCI_HOST_CONTROL2)==0);
    CHECK(SdioRead8(&hsAdapter,SDHCI_POWER_CONTROL)==15);
    /* Successful selection is deliberately NOT stage6: caller must perform
     * real chip-ID CMD53 readback before any firmware association. */
    CHECK(SdioRestoreDefaultOperatingBus(&hsAdapter)==0);CheckDefaultOperatingBus(&hsAdapter);
    CHECK(SdioNegotiateOperatingSpeed(&hsAdapter)==0 && hsAdapter.BusHighSpeedActive==1);
    CHECK(SdioRestoreIdentificationBus(&hsAdapter)==0);CheckSlow(&hsAdapter);
    CHECK(hsAdapter.BusHighSpeedActive==0);
    /* Odd base clocks must use the divider, never round above 50 MHz. */
    InitHighSpeedBus(&hsAdapter);hsAdapter.Capabilities=SDHCI_CAP_HIGH_SPEED|(201UL<<8);
    CHECK(SdioNegotiateOperatingSpeed(&hsAdapter)==0);
    CHECK(hsAdapter.BusActualKhz==33500 && hsAdapter.BusHighSpeedActive==1);
    /* Missing or unknown evidence preserves the old default-rate path. */
    InitHighSpeedBus(&hsAdapter);hsAdapter.HostVersion=0xffff;
    CHECK(SdioNegotiateOperatingSpeed(&hsAdapter)==0);CheckDefaultOperatingBus(&hsAdapter);
    CHECK(hsAdapter.BusHighSpeedRejectMask==SDIO_HS_REJECT_HOST_VERSION);
    CHECK(!hsAdapter.BusHighSpeedAttempted && !HighSpeedClockSeen);
    InitHighSpeedBus(&hsAdapter);hsAdapter.Capabilities&=~SDHCI_CAP_HIGH_SPEED;
    CHECK(SdioNegotiateOperatingSpeed(&hsAdapter)==0);CheckDefaultOperatingBus(&hsAdapter);
    CHECK(hsAdapter.BusHighSpeedRejectMask==SDIO_HS_REJECT_HOST_CAP);
    InitHighSpeedBus(&hsAdapter);Card[0][CYW_SDIO_CCCR_SPEED]=0;
    CHECK(SdioNegotiateOperatingSpeed(&hsAdapter)==0);
    CHECK(hsAdapter.BusActualKhz==25000 && !hsAdapter.BusHighSpeedAttempted);
    CHECK(hsAdapter.BusHighSpeedRejectMask==SDIO_HS_REJECT_CARD_CAP);
    InitHighSpeedBus(&hsAdapter);SdioWrite16(&hsAdapter,SDHCI_HOST_CONTROL2,8);
    CHECK(SdioNegotiateOperatingSpeed(&hsAdapter)==0);CheckDefaultOperatingBus(&hsAdapter);
    CHECK(hsAdapter.BusHighSpeedRejectMask==SDIO_HS_REJECT_HOST_MODE);
    CHECK(SdioRead16(&hsAdapter,SDHCI_HOST_CONTROL2)==8 && !hsAdapter.BusHighSpeedAttempted);
    /* Every high-speed CMD52 can fail, including readback after the card has
     * already changed timing. A successful 25 MHz recovery is the ONLY success. */
    for(failedCall=baselineCalls+1;failedCall<=highSpeedCalls;++failedCall) {
        InitHighSpeedBus(&hsAdapter);Fail52At=failedCall;
        CHECK(SdioNegotiateOperatingSpeed(&hsAdapter)==0);CheckDefaultOperatingBus(&hsAdapter);
        CHECK(hsAdapter.BusHighSpeedAttempted==1 && !NT_SUCCESS(hsAdapter.BusHighSpeedStatus));
    }
    InitHighSpeedBus(&hsAdapter);HighSpeedHostFault=1;
    CHECK(SdioNegotiateOperatingSpeed(&hsAdapter)==0);CheckDefaultOperatingBus(&hsAdapter);
    CHECK(hsAdapter.BusHighSpeedStatus==STATUS_DEVICE_DATA_ERROR);
    InitHighSpeedBus(&hsAdapter);HighSpeedCardFault=1;
    CHECK(SdioNegotiateOperatingSpeed(&hsAdapter)==0);CheckDefaultOperatingBus(&hsAdapter);
    CHECK(!NT_SUCCESS(hsAdapter.BusHighSpeedStatus));
    InitHighSpeedBus(&hsAdapter);BusClockFault=3;
    CHECK(SdioNegotiateOperatingSpeed(&hsAdapter)==0);CheckDefaultOperatingBus(&hsAdapter);
    CHECK(hsAdapter.BusHighSpeedStatus==STATUS_IO_TIMEOUT);
    /* If default-rate recovery itself fails, even successful 400 kHz restore
     * must fail startup; inconsistent or unrecoverable buses never associate. */
    InitHighSpeedBus(&hsAdapter);BusClockFault=4;
    CHECK(SdioNegotiateOperatingSpeed(&hsAdapter)==STATUS_IO_TIMEOUT);
    CHECK(hsAdapter.BusModeStage==90 && !hsAdapter.BusHighSpeedActive);CheckSlow(&hsAdapter);
    InitHighSpeedBus(&hsAdapter);BusClockFault=5;
    CHECK(SdioNegotiateOperatingSpeed(&hsAdapter)==STATUS_IO_TIMEOUT);
    CHECK(hsAdapter.BusModeStage==99 && !NT_SUCCESS(hsAdapter.BusRecoveryStatus));
    for(modeFault=1;modeFault<=2;++modeFault) {
        InitHighSpeedBus(&hsAdapter);HighSpeedStateFault=modeFault;
        CHECK(SdioNegotiateOperatingSpeed(&hsAdapter)==STATUS_DEVICE_CONFIGURATION_ERROR);
        CHECK(hsAdapter.BusModeStage==99 && !hsAdapter.BusHighSpeedActive);
        CHECK(hsAdapter.BusActualKhz==400 && !NT_SUCCESS(hsAdapter.BusRecoveryStatus));
    }
    InitHighSpeedBus(&hsAdapter);CHECK(SdioNegotiateOperatingSpeed(&hsAdapter)==0);
    hsAdapter.IoStopped=1;baselineCalls=CommandCount;
    CHECK(!NT_SUCCESS(SdioRestoreDefaultOperatingBus(&hsAdapter)) && CommandCount==baselineCalls);
    CHECK(hsAdapter.BusModeStage==99 && !hsAdapter.BusHighSpeedActive);
    puts("PASS: capability-checked standard SDR high speed, unchanged voltage, CMD52/clock/host fault matrix, verified rollback gating.");
}

static void RunPhasePollingTests(void)
{
    RPI5CYW_ADAPTER a;UCHAR b[512]={0};ULONG write,phase;
    /* Independent event timings reproduce the shared-budget defect. Original
     * tests made all three events ready together and could not detect it. */
    for(write=0;write<2;++write) {
        Init(&a);PhaseMode=1;PhaseUs[0]=20;PhaseUs[1]=PhaseUs[2]=40;SleepUs=16000;
        CHECK(SdioFifoTransfer(&a,b,sizeof(b),(BOOLEAN)write)==0);
        CHECK(StallUs==40 && SleepCount==2); /* unchanged startup F2 */
        Init(&a);PhaseMode=1;PhaseUs[0]=20;PhaseUs[1]=PhaseUs[2]=40;SleepUs=16000;
        a.BusModeStage=6;a.BusWidth=4;a.BusActualKhz=25000;
        CHECK(SdioFifoTransfer(&a,b,sizeof(b),(BOOLEAN)write)==0);
        CHECK(StallUs==100 && SleepCount==0 && a.Cmd53BytesTransferred==512);
        /* Full phase budgets, including the ~41us wire-time boundary. */
        Init(&a);PhaseMode=1;PhaseUs[0]=PhaseUs[1]=PhaseUs[2]=50;
        a.BusModeStage=6;a.BusWidth=4;a.BusActualKhz=25000;
        CHECK(SdioFifoTransfer(&a,b,sizeof(b),(BOOLEAN)write)==0);
        CHECK(StallUs==150 && SleepCount==0);
        Init(&a);PhaseMode=1;PhaseUs[0]=20;PhaseUs[1]=PhaseUs[2]=40;SleepUs=16000;
        a.BusModeStage=6;a.BusWidth=4;a.BusActualKhz=50000;
        a.BusHighSpeedActive=1;a.BusCardSpeed=3;a.HostControl=6;
        CHECK(SdioFifoTransfer(&a,b,sizeof(b),(BOOLEAN)write)==0);
        CHECK(StallUs==100 && SleepCount==0);
        Init(&a);PhaseMode=1;PhaseUs[0]=20;PhaseUs[1]=PhaseUs[2]=40;SleepUs=16000;
        a.BusModeStage=6;a.BusWidth=4;a.BusActualKhz=50000;
        a.BusHighSpeedActive=1;a.BusCardSpeed=1;a.HostControl=6;
        CHECK(SdioFifoTransfer(&a,b,sizeof(b),(BOOLEAN)write)==0);
        CHECK(StallUs==40 && SleepCount==2); /* inconsistent timing is not validated HS */
    }
    for(phase=0;phase<3;++phase) {
        Init(&a);PhaseMode=1;PhaseUs[phase]=1000;SleepUs=16000;
        a.BusModeStage=6;a.BusWidth=4;a.BusActualKhz=25000;
        CHECK(SdioFifoTransfer(&a,b,sizeof(b),FALSE)==0);
        CHECK(StallUs==50 && SleepCount==1 && a.RuntimeCmd53SleepPhase[phase]==1);
        CHECK(a.RuntimeF2WaitSleeps==1 && a.RuntimeF1WaitSleeps==0);
        CHECK(a.RuntimeCmd53Sleep100ns==160000ULL);
        Init(&a);PhaseMode=1;PhaseUs[phase]=1000000;SleepUs=16000;
        a.BusModeStage=6;a.BusWidth=4;a.BusActualKhz=25000;
        CHECK(SdioFifoTransfer(&a,b,sizeof(b),FALSE)==STATUS_IO_TIMEOUT);
        CHECK(StallUs==50 && SleepCount==16 && ResetCount==1);
        CHECK(a.RuntimeCmd53SleepPhase[phase]==16 && a.Cmd53Timeouts==1);
        CHECK(SimTime<2700000);
    }
    Init(&a);PhaseMode=1;PhaseUs[0]=10;StopOnStall=1;
    a.BusModeStage=6;a.BusWidth=4;a.BusActualKhz=25000;
    CHECK(SdioFifoTransfer(&a,b,sizeof(b),FALSE)==STATUS_INVALID_DEVICE_STATE);
    CHECK(StallUs==10 && SleepCount==0 && FifoReads==0);
    /* Restored slow mode must not inherit runtime fast polling. */
    Init(&a);PhaseMode=1;PhaseUs[0]=20;a.BusModeStage=90;a.BusWidth=1;a.BusActualKhz=400;
    CHECK(SdioCmd53Read(&a,1,0x8000,b,4)==0);
    CHECK(StallUs==0 && SleepCount==1 && a.RuntimeF1WaitSleeps==0);
}

int main(void)
{
    RPI5CYW_ADAPTER Adapter;
    UCHAR Buffer[514], FifoBuffer[1024];
    ULONG Length, Mode, FailAt, Success52Count, Success53Count, Byte;
    NTSTATUS Status;
    C_ASSERT(sizeof(ULONG) == 4);
    C_ASSERT(sizeof(NTSTATUS) == 4);
    RunEromTests();
    RunBusModeTests();
    RunHighSpeedBusTests();
    RunPhasePollingTests();
    Init(&Adapter);Adapter.BpWindowValid=1;
    CHECK(SdioCmd52Write(&Adapter,1,0x1000a,0,0xff)==0 && !Adapter.BpWindowValid);
    Adapter.BpWindowValid=1;CHECK(SdioCmd52Write(&Adapter,1,0x1000e,0,0)==0 && Adapter.BpWindowValid);
    CHECK(SdioCmd52Write(&Adapter,0,6,2,0)==0 && !Adapter.BpWindowValid);
    Adapter.BpWindowValid=1;CHECK(SdioResetHost(&Adapter,SDHCI_RESET_CMD)==0 && !Adapter.BpWindowValid);
    /* Actual F2 completion polling: immediate, short-ready, slow scheduler,
     * timeout and cancellation. No MMIO or driver loaded on this host. */
    Init(&Adapter);
    CHECK(SdioFifoTransfer(&Adapter,FifoBuffer,64,FALSE)==0);
    CHECK(SleepCount==0 && StallUs==0);
    Init(&Adapter);ReadyAt=200;
    CHECK(SdioFifoTransfer(&Adapter,FifoBuffer,64,FALSE)==0);
    CHECK(SleepCount==0 && StallUs==20 && Adapter.Cmd53FastPolls==2);
    Init(&Adapter);ReadyAt=200;SleepUs=16000;
    CHECK(SdioCmd53Read(&Adapter,1,0x8000,Buffer,4)==0);
    CHECK(SleepCount==1 && StallUs==0 && Adapter.RuntimeF1FastPolls==0);
    Init(&Adapter);ReadyAt=200;SleepUs=16000;
    Adapter.BusModeStage=6;Adapter.BusWidth=4;Adapter.BusActualKhz=25000;
    CHECK(SdioCmd53Read(&Adapter,1,0x8000,Buffer,4)==0);
    CHECK(SleepCount==0 && StallUs==20 && Adapter.RuntimeF1FastPolls==2);
    Init(&Adapter);ReadyAt=10000;SleepUs=16000;
    Adapter.BusModeStage=6;Adapter.BusWidth=4;Adapter.BusActualKhz=25000;
    CHECK(SdioCmd53Read(&Adapter,1,0x8000,Buffer,4)==0);
    CHECK(SleepCount==1 && StallUs==50); /* Never spin until ready. */
    CHECK(Adapter.RuntimeF1WaitSleeps==1 && Adapter.RuntimeCmd53SleepPhase[0]==1);
    Init(&Adapter);Fault=1;SleepUs=16000;
    Adapter.BusModeStage=6;Adapter.BusWidth=4;Adapter.BusActualKhz=25000;
    CHECK(SdioCmd53Read(&Adapter,1,0x8000,Buffer,4)==STATUS_IO_TIMEOUT);
    CHECK(SleepCount==16 && StallUs==50 && Adapter.Cmd53Timeouts==1);
    Init(&Adapter);ReadyAt=10000;
    CHECK(SdioFifoTransfer(&Adapter,FifoBuffer,64,FALSE)==0);
    CHECK(SleepCount==1 && StallUs==40 && Adapter.Cmd53WaitSleeps==1);
    Init(&Adapter);Fault=1;SleepUs=16000;
    CHECK(SdioFifoTransfer(&Adapter,FifoBuffer,64,FALSE)==STATUS_IO_TIMEOUT);
    CHECK(SleepCount==16 && StallUs==40 && Adapter.Cmd53Timeouts==1);
    CHECK(SimTime<2700000 && ResetCount==1);
    Init(&Adapter);Fault=1;StopOnSleep=1;
    CHECK(SdioFifoTransfer(&Adapter,FifoBuffer,64,FALSE)==STATUS_INVALID_DEVICE_STATE);
    CHECK(SleepCount==1 && FifoReads==0);
    Init(&Adapter); Fault=14; memset(Buffer,0xA5,sizeof(Buffer));
    CHECK(SdioCmd53Write(&Adapter,1,0x8000,Buffer,512)==STATUS_IO_DEVICE_ERROR);
    CHECK(Adapter.LastArgument==0x95000000UL && Adapter.LastResponse==0x1100);
    CHECK(Adapter.LastInterruptStatus==0x11 && Adapter.Cmd53BytesTransferred==0);
    CHECK(FifoWrites==0 && ResetCount==1);
    Init(&Adapter); Fault=14;
    CHECK(SdioCmd53Write(&Adapter,1,0x8000,Buffer,64)==STATUS_SUCCESS);
    CHECK(Adapter.LastArgument==0x95000040UL && FifoWrites==16);
    Init(&Adapter); Fault=14;
    CHECK(SdioCmd53Read(&Adapter,1,0x8000,Buffer,64)==STATUS_SUCCESS);
    CHECK(FifoReads==16);
    Init(&Adapter); memset(FifoBuffer,0,sizeof(FifoBuffer));
    CHECK(SdioFifoTransfer(&Adapter,FifoBuffer,sizeof(FifoBuffer),FALSE)==0);
    CHECK(Command53Count==2 && FifoReads==256);
    CHECK(((Adapter.LastArgument>>28)&7)==2);
    CHECK((Adapter.LastArgument&0x04000000)==0); /* fixed FIFO address */
    CHECK(((Adapter.LastArgument>>9)&0x1ffff)==0x8000);
    Init(&Adapter);Fail53At=2;
    CHECK(!NT_SUCCESS(SdioFifoTransfer(&Adapter,FifoBuffer,sizeof(FifoBuffer),FALSE)));
    CHECK(Command53Count==2);
    Init(&Adapter);
    CHECK(SdioFifoTransfer(&Adapter,FifoBuffer,512,TRUE)==0);
    CHECK(FifoWrites==128 && ((Adapter.LastArgument>>28)&7)==2);
    CHECK((Adapter.LastArgument&0x04000000)!=0); /* F2 TX increments */
    Init(&Adapter);
    CHECK(SdioFifoTransfer(&Adapter,FifoBuffer,3,FALSE)==STATUS_INVALID_PARAMETER);
    CHECK(SdioFifoTransfer(&Adapter,FifoBuffer,65540,FALSE)==STATUS_INVALID_PARAMETER);
    CHECK(CommandCount==0);
    Init(&Adapter);Adapter.IoStopped=1;
    CHECK(!NT_SUCCESS(SdioCmd53Read(&Adapter,1,0x8000,Buffer,4)));
    CHECK(CommandCount==0 && FifoReads==0 && FifoWrites==0);
    for (Length = 1; Length <= 512; Length++)
    {
        Init(&Adapter); memset(Buffer, 0xAA, sizeof(Buffer));
        CHECK(SdioCmd53Read(&Adapter, 1, 0x8000, Buffer + 1, Length) == 0);
        CHECK(Buffer[0] == 0xAA && Buffer[Length + 1] == 0xAA);
        CHECK(Buffer[1] == 0x45);
        CHECK(FifoReads == (Length + 3) / 4);
        CHECK(Adapter.Cmd53ReadCount == 1 && Adapter.Cmd53BytesTransferred == Length);
        CHECK(Registers[SDHCI_INT_STATUS / 4] == 0);
        CHECK((Adapter.LastArgument & 511) == (Length & 511));
    }
    for (Length = 1; Length <= 512; Length++)
    {
        Init(&Adapter); memset(Buffer, 0xA5, sizeof(Buffer));
        CHECK(SdioCmd53Write(&Adapter, 1, 0x8000, Buffer + 1, Length) == 0);
        CHECK(Buffer[0] == 0xA5 && Buffer[Length + 1] == 0xA5);
        CHECK(FifoWrites == (Length + 3) / 4 && FifoReads == 0);
        CHECK(Adapter.Cmd53WriteCount == 1 && Adapter.Cmd53ReadCount == 0);
        CHECK(Adapter.LastArgument & 0x80000000UL);
        CHECK((Adapter.LastArgument & 511) == (Length & 511));
        CHECK(Registers[SDHCI_INT_STATUS / 4] == 0);
        for (Byte = 0; Byte < Length; Byte++)
            CHECK(((WriteWords[Byte / 4] >> ((Byte % 4) * 8)) & 0xFF) == 0xA5);
        if ((Length & 3) != 0)
            CHECK((WriteWords[FifoWrites - 1] >> ((Length & 3) * 8)) == 0);
    }
    for (Mode = 1; Mode <= 11; Mode++)
    {
        if (Mode == 6 || Mode == 8 || Mode == 9) continue;
        Init(&Adapter); Fault = Mode; memset(Buffer, 0xA5, sizeof(Buffer));
        Status = SdioCmd53Write(&Adapter, 1, 0x8000, Buffer + 1, 5);
        CHECK(Mode == 10 ? NT_SUCCESS(Status) : !NT_SUCCESS(Status));
        CHECK(Buffer[1] == 0xA5 && Buffer[5] == 0xA5 && Buffer[6] == 0xA5);
        if (Mode != 10) CHECK(Adapter.Cmd53WriteCount == 0 && ResetCount == 1);
    }
    Init(&Adapter);
    CHECK(SdioCmd53Read(&Adapter, 0, 0, Buffer, 4) == STATUS_INVALID_PARAMETER);
    CHECK(SdioCmd53Read(&Adapter, 1, 0, Buffer, 0) == STATUS_INVALID_PARAMETER);
    CHECK(SdioCmd53Read(&Adapter, 1, 0, Buffer, 513) == STATUS_INVALID_PARAMETER);
    CHECK(SdioCmd53Read(&Adapter, 1, 0x1FFFF, Buffer, 2) == STATUS_INVALID_PARAMETER);
    CHECK(CommandCount == 0);
    TestIrql = 2;
    CHECK(SdioCmd53Read(&Adapter, 1, 0, Buffer, 4) == STATUS_INVALID_DEVICE_STATE);
    CHECK(SdioCmd53Write(&Adapter, 1, 0, Buffer, 4) == STATUS_INVALID_DEVICE_STATE);
    CHECK(CommandCount == 0);
    Init(&Adapter);
    CHECK(SdioCmd53Write(&Adapter, 0, 0, Buffer, 4) == STATUS_INVALID_PARAMETER);
    CHECK(SdioCmd53Write(&Adapter, 1, 0, Buffer, 0) == STATUS_INVALID_PARAMETER);
    CHECK(SdioCmd53Write(&Adapter, 1, 0, Buffer, 513) == STATUS_INVALID_PARAMETER);
    CHECK(SdioCmd53Write(&Adapter, 1, 0x1FFFF, Buffer, 2) == STATUS_INVALID_PARAMETER);
    CHECK(SdioCmd53Write(&Adapter, 1, 0, NULL, 4) == STATUS_INVALID_PARAMETER);
    CHECK(CommandCount == 0 && FifoWrites == 0);
    Init(&Adapter); Fault = 10;
    CHECK(SdioCmd53Read(&Adapter, 1, 0x8000, Buffer, 4) == 0);
    Init(&Adapter); Fault = 11;
    CHECK(SdioCmd53Read(&Adapter, 1, 0x8000, Buffer, 4) == STATUS_IO_DEVICE_ERROR);
    CHECK(SdioLoadLe32(Buffer) == 0);
    Init(&Adapter); Registers[SDHCI_PRESENT_STATE / 4] = SDHCI_PS_DATA_INHIBIT;
    CHECK(SdioCmd53Read(&Adapter, 1, 0x8000, Buffer, 4) == STATUS_IO_TIMEOUT);
    CHECK(CommandCount == 0 && ResetCount == 1);

    for (Mode = 1; Mode <= 5; Mode++)
    {
        Init(&Adapter); Fault = Mode;
        CHECK(!NT_SUCCESS(SdioCmd53Read(&Adapter, 1, 0x8000, Buffer, 4)));
        CHECK(ResetCount == 1 && Adapter.Cmd53ReadCount == 0);
        CHECK(SdioLoadLe32(Buffer) == 0 && Ticks <= 250);
    }
    Init(&Adapter); Fault = 7;
    CHECK(!NT_SUCCESS(SdioCmd53Read(&Adapter, 1, 0x8000, Buffer, 4)));
    CHECK(Adapter.Cmd53ResetStatus == STATUS_IO_TIMEOUT);
    Init(&Adapter); ReadbackMismatch = 1;
    CHECK(SdioCmd52Write(&Adapter, 0, 2, 2, 0xFF) == STATUS_DEVICE_DATA_ERROR);
    Init(&Adapter);
    CHECK(SdioCmd52Write(&Adapter, 8, 0, 0, 0) == STATUS_INVALID_PARAMETER);
    CHECK(CommandCount == 0);

    Init(&Adapter);
    CHECK(Cyw43455Probe(&Adapter) == 0);
    CHECK(Adapter.ChipId == 0x4345 && Adapter.ChipRevision == 6);
    CHECK(Adapter.Cmd53ReadCount > 16 && Adapter.ProbePhase == 350);
    CHECK(Adapter.CoreInventoryComplete && Adapter.CoreCount == 4);
    CHECK(Adapter.EromWords == sizeof(Erom) / sizeof(Erom[0]));
    CHECK(memcmp(Adapter.EromTrace, Erom, sizeof(Erom)) == 0);
    CHECK(Adapter.Cr4CoreBase == 0x18004000 && Adapter.RamBankCount == 5);
    CHECK(Adapter.RamBase == 0x198000 && Adapter.Cmd53WriteCount == 0);
    CHECK(Adapter.Function1Ready && Adapter.ProbeRestoreStatus == 0);
    CheckRestored();
    Success52Count = Commands52;
    Success53Count = Command53Count;
    for (FailAt = 1; FailAt <= Success53Count; FailAt++)
    {
        Init(&Adapter); Fail53At = FailAt;
        CHECK(Cyw43455Probe(&Adapter) == STATUS_IO_DEVICE_ERROR);
        CHECK(!Adapter.CoreInventoryComplete && Adapter.Cmd53WriteCount == 0);
        CheckRestored();
    }
    /* Fault each individual CMD52, including every restoration command. */
    for (FailAt = 1; FailAt <= Success52Count; FailAt++)
    {
        Init(&Adapter); Fail52At = FailAt;
        Status = Cyw43455Probe(&Adapter);
        CHECK(!NT_SUCCESS(Status));
        if (NT_SUCCESS(Adapter.ProbeRestoreStatus)) CheckRestored();
        else CHECK(Adapter.ProbePhase == 260);
    }
    for (Mode = 1; Mode <= 9; Mode++)
    {
        Init(&Adapter); Fault = Mode;
        CHECK(!NT_SUCCESS(Cyw43455Probe(&Adapter)));
        CheckRestored();
    }
    Init(&Adapter); Fifo = 0xFFFFFFFF;
    CHECK(Cyw43455Probe(&Adapter) == STATUS_DEVICE_DATA_ERROR);
    CHECK(Adapter.Cmd53ReadCount == 1); CheckRestored();
    Init(&Adapter); Fault = 12;
    CHECK(Cyw43455Probe(&Adapter) == STATUS_DEVICE_DATA_ERROR);
    CHECK(!Adapter.CoreInventoryComplete); CheckRestored();
    Init(&Adapter); Fault = 13;
    CHECK(Cyw43455Probe(&Adapter) == 0);
    CHECK(Adapter.CoreInventoryComplete && Adapter.RamBankCount == 0);
    CHECK(Adapter.Cmd53WriteCount == 0); CheckRestored();
    Init(&Adapter); Fifo = 0x15334345; /* untested chip revision */
    CHECK(Cyw43455Probe(&Adapter) == STATUS_DEVICE_CONFIGURATION_ERROR);
    CHECK(Adapter.Cmd53ReadCount == 16 && !Adapter.CoreInventoryComplete);
    CheckRestored();
    /* Real wrappers must retain return codes and resets even on failure. */
    for(Mode=0;Mode<=5;++Mode) {
        ULONG RawResets;NTSTATUS RawStatus;
        Init(&Adapter);Fault=Mode;
        RawStatus=SdioCmd53TransferRaw(&Adapter,1,0x8000,Buffer,64,FALSE,TRUE);
        RawResets=ResetCount;
        Init(&Adapter);Fault=Mode;CywTimingStart(&Adapter.Timing);
        Status=SdioCmd53Read(&Adapter,1,0x8000,Buffer,64);
        CHECK(Status==RawStatus && ResetCount==RawResets);
        CHECK(Adapter.Timing.Snapshot.Bucket[CywTimeCmd53F1].Count==(RPI5CYW_DETAILED_TIMING?1ULL:0ULL));
        CHECK(Adapter.Timing.Snapshot.Bucket[CywTimeCmd53Rx].Count==0);
        CHECK(Adapter.Timing.Snapshot.Bucket[CywTimeCmd53F1].TotalTicks==(RPI5CYW_DETAILED_TIMING?SimTime:0ULL));
        Init(&Adapter);Fault=Mode;CywTimingStart(&Adapter.Timing);
        Status=SdioFifoTransfer(&Adapter,Buffer,64,FALSE);
        CHECK(Adapter.Timing.Snapshot.Bucket[CywTimeCmd53Rx].Count==(RPI5CYW_DETAILED_TIMING?1ULL:0ULL));
        CHECK(Adapter.Timing.Snapshot.Bucket[CywTimeCmd53Tx].Count==0);
        Init(&Adapter);Fault=Mode;CywTimingStart(&Adapter.Timing);
        Status=SdioFifoTransfer(&Adapter,Buffer,64,TRUE);
        CHECK(Adapter.Timing.Snapshot.Bucket[CywTimeCmd53Tx].Count==(RPI5CYW_DETAILED_TIMING?1ULL:0ULL));
        Init(&Adapter);Fault=Mode;
        RawStatus=SdioSendCommandRaw(&Adapter,52,0,SDHCI_CMD_RESP_48,NULL);
        RawResets=ResetCount;
        Init(&Adapter);Fault=Mode;CywTimingStart(&Adapter.Timing);
        Status=SdioSendCommand(&Adapter,52,0,SDHCI_CMD_RESP_48,NULL);
        CHECK(Status==RawStatus && ResetCount==RawResets);
        CHECK(Adapter.Timing.Snapshot.Bucket[CywTimeCmd52].Count==(RPI5CYW_DETAILED_TIMING?1ULL:0ULL));
    }
    Init(&Adapter);QpcReads=0;
    CHECK(SdioCmd53Read(&Adapter,1,0x8000,Buffer,64)==STATUS_SUCCESS);
    CHECK(QpcReads==0 && Adapter.Timing.Snapshot.Bucket[CywTimeCmd53F1].Count==0);
    CHECK(SdioCmd53Transfer(NULL,1,0x8000,Buffer,64,FALSE,TRUE)==STATUS_INVALID_PARAMETER);
    TestFifoBlocks();
    if (Failures) { printf("%d failures\n", Failures); return 1; }
    puts("PASS: actual CMD52/CMD53 read/write + core probe, 512 lengths each, bounds, errors, timeouts, cleanup.");
    return 0;
}
