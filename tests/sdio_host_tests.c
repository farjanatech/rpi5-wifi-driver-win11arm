#include <stdio.h>
#include <stdlib.h>
#define RPI5CYW_HOST_TEST 1
#include "../src/sdio/sdio.c"
#include "../src/cyw43455/chip.c"

static ULONG Registers[64], Card[2][0x20000];
static ULONG Fifo, FifoReads, ResetCount, CommandCount, Ticks, Command53Count;
static ULONG Fault, Fail52At, Commands52, ReadbackMismatch, Command53Events;
static ULONG FifoWrites, WriteWords[128], DiscoveryMode, Fail53At;
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
UCHAR READ_REGISTER_UCHAR(PUCHAR Address) { return *Address; }
USHORT READ_REGISTER_USHORT(PUSHORT Address) { return *Address; }
ULONG READ_REGISTER_ULONG(PULONG Address)
{
    if (Offset(Address) == SDHCI_BUFFER)
    {
        FifoReads++;
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
    else *Address = Value;
}
void WRITE_REGISTER_ULONG(PULONG Address, ULONG Value)
{
    if (Offset(Address) == SDHCI_BUFFER)
    {
        CHECK(FifoWrites < 128);
        if (FifoWrites < 128) WriteWords[FifoWrites++] = Value;
        if (Fault == 10) Registers[SDHCI_INT_STATUS / 4] |= SDHCI_INT_XFER_COMPLETE;
        if (Fault == 11) Registers[SDHCI_INT_STATUS / 4] |= SDHCI_INT_DATA_CRC;
        return;
    }
    if (Offset(Address) == SDHCI_INT_STATUS) *Address &= ~Value;
    else *Address = Value;
}
void WRITE_REGISTER_USHORT(PUSHORT Address, USHORT Value)
{
    ULONG Argument, Fn, Reg, Response = 0, Command;
    *Address = Value;
    if (Offset(Address) == SDHCI_CLOCK_CONTROL && (Value & 1))
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
        if (Fault == 2) Registers[SDHCI_INT_STATUS / 4] |= SDHCI_INT_DATA_CRC;
        if (Fault == 3) Response = 0x200;
        if (Fault == 4) Registers[SDHCI_INT_STATUS / 4] = SDHCI_INT_CMD_COMPLETE;
        if (Fault == 5) Registers[SDHCI_INT_STATUS / 4] &= ~SDHCI_INT_XFER_COMPLETE;
        if (Fault == 10 || Fault == 11)
            Registers[SDHCI_INT_STATUS / 4] &= ~SDHCI_INT_XFER_COMPLETE;
        if (Fault == 6 && Command53Count == 2) Fifo ^= 0x10000;
        if (Command53Count == Fail53At)
            Registers[SDHCI_INT_STATUS / 4] |= SDHCI_INT_DATA_CRC;
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
void KeStallExecutionProcessor(ULONG Microseconds) { (void)Microseconds; Ticks++; }
NTSTATUS KeDelayExecutionThread(int Mode, BOOLEAN Alertable, LARGE_INTEGER *Delay)
{ (void)Mode; (void)Alertable; (void)Delay; Ticks++; return 0; }
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

int main(void)
{
    RPI5CYW_ADAPTER Adapter;
    UCHAR Buffer[514];
    ULONG Length, Mode, FailAt, Success52Count, Success53Count, Byte;
    NTSTATUS Status;
    C_ASSERT(sizeof(ULONG) == 4);
    C_ASSERT(sizeof(NTSTATUS) == 4);
    RunEromTests();
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
    if (Failures) { printf("%d failures\n", Failures); return 1; }
    puts("PASS: actual CMD52/CMD53 read/write + core probe, 512 lengths each, bounds, errors, timeouts, cleanup.");
    return 0;
}
