/* CYW43455 register definitions / clock sequence reference:
 * Linux v6.12 brcmfmac/sdio.{c,h} (ISC) and Ahmed ARIF's ReactOS chip.c
 * at 9130f67a (GPL-2.0-or-later). See THIRD_PARTY_NOTICES.md.
 * Startup test only: select/read ChipCommon ID; do not reset cores or write RAM.
 */
#include "chip.h"
#include "../sdio/sdio.h"

#define CYW_F1_CLOCK       0x1000EUL
#define CYW_F1_WINDOW_LOW  0x1000AUL
#define CYW_CHIP_ID_OFFSET 0x08000UL

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
    Phase = 250;
    Status = STATUS_SUCCESS;

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
