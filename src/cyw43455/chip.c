/* CYW43455 register definitions / clock sequence reference:
 * Linux v6.12 brcmfmac/sdio.{c,h} (ISC) and Ahmed ARIF's ReactOS chip.c
 * at 9130f67a (GPL-2.0-or-later). See THIRD_PARTY_NOTICES.md.
 * Startup inventory: chip ID, EROM and CR4 state/capabilities.
 * Do not reset cores, select RAM banks, or write RAM.
 */
#include "chip.h"
#include "../sdio/sdio.h"
#include "erom.h"

#define CYW_F1_CLOCK       0x1000EUL
#define CYW_F1_WINDOW_LOW  0x1000AUL
#define CYW_CHIP_ID_OFFSET 0x08000UL

typedef struct _CYW_EROM_READER {
    PRPI5CYW_ADAPTER Adapter;
    ULONG Address, Window, Remaining;
} CYW_EROM_READER;

static NTSTATUS CywReadBackplane32(CYW_EROM_READER *Reader, ULONG Address,
                                   PULONG Value)
{
    ULONG Window = Address & 0xFFFF8000UL, Index;
    UCHAR Bytes[4];
    NTSTATUS Status;
    if (Address < 0x18000000UL || Address > 0x181FFFFCUL || (Address & 3))
        return STATUS_INVALID_PARAMETER;
    if (Reader->Window != Window)
    {
        for (Index = 0; Index < 3; Index++)
        {
            Status = SdioCmd52Write(Reader->Adapter, 1, CYW_F1_WINDOW_LOW + Index,
                (UCHAR)(Window >> (8 + Index * 8)), 0xFF);
            if (!NT_SUCCESS(Status)) return Status;
        }
        Reader->Window = Window;
    }
    Status = SdioCmd53Read(Reader->Adapter, 1, (Address & 0x7FFF) | 0x8000,
                           Bytes, sizeof(Bytes));
    if (NT_SUCCESS(Status)) *Value = SdioLoadLe32(Bytes);
    return Status;
}

static NTSTATUS CywNextEromWord(PVOID Context, PULONG Word)
{
    CYW_EROM_READER *Reader = (CYW_EROM_READER *)Context;
    NTSTATUS Status;
    if (Reader->Remaining == 0) return STATUS_DEVICE_DATA_ERROR;
    Reader->Remaining--;
    Status = CywReadBackplane32(Reader, Reader->Address, Word);
    if (NT_SUCCESS(Status) && Reader->Adapter->EromWords < 512)
        Reader->Adapter->EromTrace[Reader->Adapter->EromWords] = *Word;
    Reader->Address += 4;
    Reader->Adapter->EromWords++;
    return Status;
}

static NTSTATUS CywDiscoverCores(PRPI5CYW_ADAPTER Adapter)
{
    CYW_EROM_READER Reader;
    CYW_CORE_MAP Map;
    ULONG Cap;
    NTSTATUS Status;
    Reader.Adapter = Adapter;
    Reader.Window = 0x18000000UL; /* caller selected ChipCommon */
    Rpi5CywWriteDiagnostics(Adapter, 300, STATUS_SUCCESS);
    if ((Adapter->ChipIdRaw >> 28) != 1 || Adapter->ChipRevision != 6)
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    Status = CywReadBackplane32(&Reader, 0x180000FCUL, &Adapter->EromAddress);
    if (!NT_SUCCESS(Status)) return Status;
    if (Adapter->EromAddress < 0x18000000UL ||
        Adapter->EromAddress >= 0x18200000UL || (Adapter->EromAddress & 3))
        return STATUS_DEVICE_DATA_ERROR;
    Reader.Address = Adapter->EromAddress;
    /* Never scan past the EROM page or more than 512 total words. */
    Reader.Remaining = (0x1000 - (Reader.Address & 0xFFF)) / 4;
    if (Reader.Remaining > 512) Reader.Remaining = 512;
    Status = CywParseErom(CywNextEromWord, &Reader, &Map);
    Adapter->CoreCount = Map.Count;
    Adapter->ChipCommonBase = Map.ChipCommon;
    Adapter->SdioCoreBase = Map.Sdio;
    Adapter->D11CoreBase = Map.D11;
    Adapter->Cr4CoreBase = Map.Cr4;
    Adapter->Cr4WrapperBase = Map.Cr4Wrapper;
    if (!NT_SUCCESS(Status)) return Status;
    Rpi5CywWriteDiagnostics(Adapter, 310, STATUS_SUCCESS);
    Status = CywReadBackplane32(&Reader, Map.Cr4Wrapper + 0x408,
                                &Adapter->Cr4IoControl);
    if (!NT_SUCCESS(Status)) return Status;
    Status = CywReadBackplane32(&Reader, Map.Cr4Wrapper + 0x800,
                                &Adapter->Cr4ResetControl);
    if (!NT_SUCCESS(Status)) return Status;
    /* Do not reset/wake/stop the CPU just to inventory RAM capabilities. */
    if ((Adapter->Cr4IoControl & 3) == 1 &&
        (Adapter->Cr4ResetControl & 1) == 0)
    {
        Status = CywReadBackplane32(&Reader, Map.Cr4 + 4, &Cap);
        if (!NT_SUCCESS(Status)) return Status;
        if (Cap == 0xFFFFFFFFUL || ((Cap & 15) + ((Cap >> 4) & 15)) == 0)
            return STATUS_DEVICE_DATA_ERROR;
        Adapter->Cr4Capabilities = Cap;
        Adapter->RamBankCount = (Cap & 15) + ((Cap >> 4) & 15);
    }
    /* Reference-defined base, NOT a verified RAM size or upload permission. */
    Adapter->RamBase = 0x198000;
    Adapter->CoreInventoryComplete = 1;
    return STATUS_SUCCESS;
}

NTSTATUS
Cyw43455Probe(PRPI5CYW_ADAPTER Adapter)
{
    UCHAR OriginalEnable = 0, OriginalClock = 0, OriginalWindow[3] = {0};
    UCHAR Value, Bytes[4];
    ULONG Index, Poll, RawId = 0;
    ULONG Phase = 200;
    NTSTATUS Status, RestoreStatus = STATUS_SUCCESS, StepStatus;
    BOOLEAN EnableTouched = FALSE, ClockTouched = FALSE, WindowTouched = FALSE;
    const UCHAR Window[3] = { 0, 0, 0x18 }; /* ChipCommon @ 0x18000000 */

    if (Adapter == NULL || Adapter->RegisterBase == NULL)
        return STATUS_INVALID_PARAMETER;
    if (KeGetCurrentIrql() != PASSIVE_LEVEL) return STATUS_INVALID_DEVICE_STATE;
    if (Adapter->SdioFunctions < 1) return STATUS_DEVICE_CONFIGURATION_ERROR;

    Rpi5CywWriteDiagnostics(Adapter, Phase, STATUS_SUCCESS);
    Status = SdioCmd52Read(Adapter, 0, CYW_SDIO_CCCR_IO_ENABLE, &OriginalEnable);
    if (!NT_SUCCESS(Status)) goto Done;
    EnableTouched = TRUE;
    Status = SdioCmd52Write(Adapter, 0, CYW_SDIO_CCCR_IO_ENABLE,
                           (UCHAR)(OriginalEnable | 2), 0xFE);
    if (!NT_SUCCESS(Status)) goto Done;
    Phase = 210;
    for (Poll = 0; Poll < 100; Poll++)
    {
        Status = SdioCmd52Read(Adapter, 0, CYW_SDIO_CCCR_IO_READY, &Value);
        if (!NT_SUCCESS(Status)) goto Done;
        Adapter->IoReady = Value;
        if ((Value & 2) != 0) break;
        SdioDelayMilliseconds(1);
    }
    if (Poll == 100) { Status = STATUS_IO_TIMEOUT; goto Done; }
    Adapter->Function1Ready = 1;
    Adapter->IoEnable = OriginalEnable | 2;

    Phase = 220;
    Status = SdioCmd52Read(Adapter, 1, CYW_F1_CLOCK, &OriginalClock);
    if (!NT_SUCCESS(Status)) goto Done;
    ClockTouched = TRUE;
    Status = SdioCmd52Write(Adapter, 1, CYW_F1_CLOCK, 0x28, 0x3F);
    if (!NT_SUCCESS(Status)) goto Done;
    for (Poll = 0; Poll < 100; Poll++)
    {
        Status = SdioCmd52Read(Adapter, 1, CYW_F1_CLOCK, &Value);
        if (!NT_SUCCESS(Status)) goto Done;
        Adapter->ChipClockCsr = Value;
        if ((Value & 0xC0) != 0) break;
        SdioDelayMilliseconds(1);
    }
    if (Poll == 100) { Status = STATUS_IO_TIMEOUT; goto Done; }
    Status = SdioCmd52Write(Adapter, 1, CYW_F1_CLOCK, 0x21, 0x3F);
    if (!NT_SUCCESS(Status)) goto Done;
    KeStallExecutionProcessor(65);

    Phase = 230;
    for (Index = 0; Index < 3; Index++)
    {
        Status = SdioCmd52Read(Adapter, 1, CYW_F1_WINDOW_LOW + Index,
                              &OriginalWindow[Index]);
        if (!NT_SUCCESS(Status)) goto Done;
    }
    WindowTouched = TRUE;
    for (Index = 0; Index < 3; Index++)
    {
        Status = SdioCmd52Write(Adapter, 1, CYW_F1_WINDOW_LOW + Index,
                               Window[Index], 0xFF);
        if (!NT_SUCCESS(Status)) goto Done;
    }

    Phase = 240;
    for (Index = 0; Index < 16; Index++)
    {
        Status = SdioCmd53Read(Adapter, 1, CYW_CHIP_ID_OFFSET, Bytes, sizeof(Bytes));
        if (!NT_SUCCESS(Status)) goto Done;
        RawId = SdioLoadLe32(Bytes);
        if (Index == 0)
        {
            Adapter->ChipIdRaw = RawId;
            Adapter->ChipId = RawId & 0xFFFFUL;
            Adapter->ChipRevision = (RawId >> 16) & 0xFUL;
        }
        if (Adapter->ChipId != CYW43455_CHIP_ID || RawId != Adapter->ChipIdRaw)
        {
            Status = STATUS_DEVICE_DATA_ERROR;
            goto Done;
        }
    }
    Status = CywDiscoverCores(Adapter);
    Phase = NT_SUCCESS(Status) ? 350 : Adapter->ProbePhase;

Done:
    /* Preserve the command snapshot: cleanup issues its own CMD52 commands. */
    {
        ULONG LastCommand = Adapter->LastCommand;
        ULONG LastArgument = Adapter->LastArgument;
        ULONG LastResponse = Adapter->LastResponse;
        ULONG LastInterrupt = Adapter->LastInterruptStatus;
        NTSTATUS LastReset = Adapter->LastCommandResetStatus;
#define RESTORE(_expr) do { StepStatus = (_expr); \
    if (!NT_SUCCESS(StepStatus) && NT_SUCCESS(RestoreStatus)) \
        RestoreStatus = StepStatus; } while (0)
        if (WindowTouched)
            for (Index = 0; Index < 3; Index++)
                RESTORE(SdioCmd52Write(Adapter, 1, CYW_F1_WINDOW_LOW + Index,
                                      OriginalWindow[Index], 0xFF));
        if (ClockTouched)
            RESTORE(SdioCmd52Write(Adapter, 1, CYW_F1_CLOCK,
                                  (UCHAR)(OriginalClock & 0x3F), 0x3F));
        if (EnableTouched)
            RESTORE(SdioCmd52Write(Adapter, 0, CYW_SDIO_CCCR_IO_ENABLE,
                                  OriginalEnable, 0xFE));
#undef RESTORE
        Adapter->LastCommand = LastCommand;
        Adapter->LastArgument = LastArgument;
        Adapter->LastResponse = LastResponse;
        Adapter->LastInterruptStatus = LastInterrupt;
        Adapter->LastCommandResetStatus = LastReset;
    }
    Adapter->ProbeRestoreStatus = RestoreStatus;
    if (NT_SUCCESS(Status) && !NT_SUCCESS(RestoreStatus))
    {
        Phase = 260;
        Status = RestoreStatus;
    }
    Rpi5CywWriteDiagnostics(Adapter, Phase, Status);
    return Status;
}
