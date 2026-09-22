#include "sdio.h"
/* TIMING-BEGIN */
#include "../driver/timing_clock.h"
/* TIMING-END */

#include "../cyw43455/chip.h"

C_ASSERT(RPI5CYW_CMD5_MAX_ATTEMPTS == SDIO_CMD5_MAX_ATTEMPTS);

static __forceinline UCHAR
SdioRead8(
    _In_ PRPI5CYW_ADAPTER Adapter,
    _In_ ULONG Offset
    )
{
    if (Adapter->IoStopped) return 0xFF;
    return READ_REGISTER_UCHAR((PUCHAR)Adapter->RegisterBase + Offset);
}

static __forceinline USHORT
SdioRead16(
    _In_ PRPI5CYW_ADAPTER Adapter,
    _In_ ULONG Offset
    )
{
    if (Adapter->IoStopped) return 0xFFFF;
    return READ_REGISTER_USHORT((PUSHORT)((PUCHAR)Adapter->RegisterBase + Offset));
}

static __forceinline ULONG
SdioRead32(
    _In_ PRPI5CYW_ADAPTER Adapter,
    _In_ ULONG Offset
    )
{
    if (Adapter->IoStopped) return 0xFFFFFFFF;
    return READ_REGISTER_ULONG((PULONG)((PUCHAR)Adapter->RegisterBase + Offset));
}

static __forceinline VOID
SdioWrite8(
    _In_ PRPI5CYW_ADAPTER Adapter,
    _In_ ULONG Offset,
    _In_ UCHAR Value
    )
{
    if (!Adapter->IoStopped) WRITE_REGISTER_UCHAR((PUCHAR)Adapter->RegisterBase + Offset, Value);
}

static __forceinline VOID
SdioWrite16(
    _In_ PRPI5CYW_ADAPTER Adapter,
    _In_ ULONG Offset,
    _In_ USHORT Value
    )
{
    if (!Adapter->IoStopped) WRITE_REGISTER_USHORT((PUSHORT)((PUCHAR)Adapter->RegisterBase + Offset), Value);
}

static __forceinline VOID
SdioWrite32(
    _In_ PRPI5CYW_ADAPTER Adapter,
    _In_ ULONG Offset,
    _In_ ULONG Value
    )
{
    if (!Adapter->IoStopped) WRITE_REGISTER_ULONG((PULONG)((PUCHAR)Adapter->RegisterBase + Offset), Value);
}

VOID
SdioDelayMilliseconds(
    _In_ ULONG Milliseconds
    )
{
    if (KeGetCurrentIrql() <= APC_LEVEL)
    {
        LARGE_INTEGER Delay;
        Delay.QuadPart = -((LONGLONG)Milliseconds * 10000LL);
        (VOID)KeDelayExecutionThread(KernelMode, FALSE, &Delay);
    }
    else
    {
        while (Milliseconds-- != 0)
        {
            KeStallExecutionProcessor(1000);
        }
    }
}

static NTSTATUS
SdioResetHost(
    _Inout_ PRPI5CYW_ADAPTER Adapter,
    _In_ UCHAR ResetMask
    )
{
    ULONG Timeout;

    Adapter->BpWindowValid=0;
    SdioWrite8(Adapter, SDHCI_SOFTWARE_RESET, ResetMask);
    for (Timeout = 0; Timeout < 1000; Timeout++)
    {
        if (Adapter->IoStopped) return STATUS_INVALID_DEVICE_STATE;
        if ((SdioRead8(Adapter, SDHCI_SOFTWARE_RESET) & ResetMask) == 0)
        {
            return STATUS_SUCCESS;
        }
        KeStallExecutionProcessor(100);
    }

    return STATUS_IO_TIMEOUT;
}

static NTSTATUS
SdioSetClock(
    _Inout_ PRPI5CYW_ADAPTER Adapter,
    _In_ ULONG TargetClockKhz
    )
{
    ULONG BaseClockMhz;
    ULONG BaseClockKhz;
    ULONG Timeout;
    USHORT Divider;
    USHORT DividerHigh;
    USHORT ClockControl;
    Adapter->BpWindowValid=0;
    if (TargetClockKhz == 0)
    {
        return STATUS_INVALID_PARAMETER;
    }
    if (Adapter->IoStopped) return STATUS_INVALID_DEVICE_STATE;

    BaseClockMhz = (Adapter->Capabilities & SDHCI_CAP_BASE_CLK_MASK) >> SDHCI_CAP_BASE_CLK_SHIFT;
    if (BaseClockMhz == 0)
    {
        BaseClockMhz = 200;
    }
    BaseClockKhz = BaseClockMhz * 1000UL;

    Divider = SdioCalculateClockDivider(BaseClockKhz, TargetClockKhz);
    if (Divider > 0x3FF)
    {
        Divider = 0x3FF;
    }

    DividerHigh = (USHORT)((Divider & 0x300) >> 2);
    SdioWrite16(Adapter, SDHCI_CLOCK_CONTROL, 0);
    ClockControl = (USHORT)(((Divider & 0xFF) << SDHCI_CLK_FREQ_SEL_SHIFT) |
                            DividerHigh |
                            SDHCI_CLK_INT_CLK_ENABLE);
    SdioWrite16(Adapter, SDHCI_CLOCK_CONTROL, ClockControl);

    for (Timeout = 0; Timeout < 2000; Timeout++)
    {
        if (Adapter->IoStopped) return STATUS_INVALID_DEVICE_STATE;
        ClockControl = SdioRead16(Adapter, SDHCI_CLOCK_CONTROL);
        if ((ClockControl & SDHCI_CLK_INT_CLK_STABLE) != 0)
        {
            break;
        }
        KeStallExecutionProcessor(100);
    }
    if (Timeout == 2000)
    {
        return STATUS_IO_TIMEOUT;
    }

    ClockControl |= SDHCI_CLK_SD_CLK_ENABLE;
    SdioWrite16(Adapter, SDHCI_CLOCK_CONTROL, ClockControl);
    Adapter->ClockControl = SdioRead16(Adapter, SDHCI_CLOCK_CONTROL);
    if ((Adapter->ClockControl & 0xffc7) !=
        (ULONG)(((Divider & 0xff) << 8) | DividerHigh | 7))
        return STATUS_DEVICE_DATA_ERROR;
    Adapter->BusTargetKhz = TargetClockKhz;
    Adapter->BusActualKhz = Divider ? BaseClockKhz / (2UL * Divider) : BaseClockKhz;
    return STATUS_SUCCESS;
}

#include "bus_mode.h"

static NTSTATUS
SdioInitializeHost(
    _Inout_ PRPI5CYW_ADAPTER Adapter
    )
{
    UCHAR PowerControl;
    NTSTATUS Status;

    if (Adapter->RegisterBase == NULL || Adapter->RegisterLength < 0x100)
    {
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    }

    SdioWrite32(Adapter, SDHCI_INT_SIGNAL_ENABLE, 0);

    Status = SdioResetHost(Adapter, SDHCI_RESET_ALL);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    Adapter->HostVersion = SdioRead16(Adapter, SDHCI_HOST_VERSION);
    Adapter->Capabilities = SdioRead32(Adapter, SDHCI_CAPABILITIES);
    Adapter->Capabilities2 = SdioRead32(Adapter, SDHCI_CAPABILITIES2);

    Status = SdioSetClock(Adapter, 400);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    if ((Adapter->Capabilities & SDHCI_CAP_VOLTAGE_330) != 0)
    {
        PowerControl = SDHCI_PC_BUS_VOLTAGE_330 | SDHCI_PC_BUS_POWER_ON;
    }
    else if ((Adapter->Capabilities & SDHCI_CAP_VOLTAGE_300) != 0)
    {
        PowerControl = SDHCI_PC_BUS_VOLTAGE_300 | SDHCI_PC_BUS_POWER_ON;
    }
    else if ((Adapter->Capabilities & SDHCI_CAP_VOLTAGE_180) != 0)
    {
        PowerControl = SDHCI_PC_BUS_VOLTAGE_180 | SDHCI_PC_BUS_POWER_ON;
    }
    else
    {
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    }

    SdioWrite8(Adapter, SDHCI_POWER_CONTROL, PowerControl);
    SdioWrite8(Adapter, SDHCI_TIMEOUT_CONTROL, 0x0E);
    SdioWrite32(Adapter, SDHCI_INT_STATUS, SDHCI_INT_ALL_MASK);
    SdioWrite32(Adapter, SDHCI_INT_STATUS_ENABLE,
                SDHCI_INT_CMD_COMPLETE |
                SDHCI_INT_XFER_COMPLETE |
                SDHCI_INT_BUFFER_READ_READY |
                SDHCI_INT_BUFFER_WRITE_READY |
                SDHCI_INT_DATA_ERROR_MASK |
                SDHCI_INT_ERROR |
                SDHCI_INT_CMD_ERROR_MASK);
    SdioWrite32(Adapter, SDHCI_INT_SIGNAL_ENABLE, 0);

    /*
     * The soldered CYW43455 may already have been enabled by firmware, but a
     * cold boot and a PnP restart do not produce identical power timing.  Keep
     * this bounded settle interval before the first command so both paths give
     * the device time to reach its SDIO command state.
     */
    SdioDelayMilliseconds(200);

    Adapter->PresentState = SdioRead32(Adapter, SDHCI_PRESENT_STATE);
    Adapter->ClockControl = SdioRead16(Adapter, SDHCI_CLOCK_CONTROL);
    Adapter->PowerControl = SdioRead8(Adapter, SDHCI_POWER_CONTROL);
    Adapter->HostControl = SdioRead8(Adapter, SDHCI_HOST_CONTROL);
    Adapter->HostControl2 = SdioRead16(Adapter, SDHCI_HOST_CONTROL2);
    Adapter->TimeoutControl = SdioRead8(Adapter, SDHCI_TIMEOUT_CONTROL);
    Adapter->SoftwareReset = SdioRead8(Adapter, SDHCI_SOFTWARE_RESET);
    return STATUS_SUCCESS;
}

static NTSTATUS
SdioWaitInhibitClear(
    _Inout_ PRPI5CYW_ADAPTER Adapter,
    _In_ ULONG Mask
    )
{
    ULONG Timeout;

    for (Timeout = 0; Timeout < 10000; Timeout++)
    {
        if (Adapter->IoStopped) return STATUS_INVALID_DEVICE_STATE;
        if ((SdioRead32(Adapter, SDHCI_PRESENT_STATE) & Mask) == 0)
        {
            return STATUS_SUCCESS;
        }
        KeStallExecutionProcessor(100);
    }

    return STATUS_IO_TIMEOUT;
}

static NTSTATUS
SdioSendCommandRaw(
    _Inout_ PRPI5CYW_ADAPTER Adapter,
    _In_ UCHAR CommandIndex,
    _In_ ULONG Argument,
    _In_ USHORT CommandFlags,
    _Out_opt_ PULONG Response
    )
{
    ULONG InterruptStatus = 0;
    ULONG Timeout;
    NTSTATUS Status;

    Adapter->LastCommand = CommandIndex;
    Adapter->LastArgument = Argument;
    Adapter->LastInterruptStatus = 0;
    Adapter->LastResponse = 0;
    Adapter->LastCommandResetStatus = STATUS_SUCCESS;

    Status = SdioWaitInhibitClear(Adapter, SDHCI_PS_CMD_INHIBIT);
    if (!NT_SUCCESS(Status))
    {
        Adapter->LastCommandResetStatus = SdioResetHost(Adapter, SDHCI_RESET_CMD);
        return Status;
    }

    SdioWrite32(Adapter, SDHCI_INT_STATUS, SDHCI_INT_ALL_MASK);
    SdioWrite32(Adapter, SDHCI_ARGUMENT, Argument);
    KeMemoryBarrier();
    SdioWrite16(Adapter, SDHCI_COMMAND, SDHCI_MAKE_CMD(CommandIndex, CommandFlags));

    for (Timeout = 0; Timeout < 10000; Timeout++)
    {
        InterruptStatus = SdioRead32(Adapter, SDHCI_INT_STATUS);
        if ((InterruptStatus & (SDHCI_INT_CMD_COMPLETE | SDHCI_INT_ERROR | SDHCI_INT_CMD_ERROR_MASK)) != 0)
        {
            break;
        }
        KeStallExecutionProcessor(100);
    }

    Adapter->LastInterruptStatus = InterruptStatus;
    if (Response != NULL)
    {
        *Response = SdioRead32(Adapter, SDHCI_RESPONSE0);
        Adapter->LastResponse = *Response;
    }
    if (Timeout == 10000)
    {
        Adapter->LastCommandResetStatus = SdioResetHost(Adapter, SDHCI_RESET_CMD);
        return STATUS_IO_TIMEOUT;
    }

    if ((InterruptStatus & (SDHCI_INT_ERROR | SDHCI_INT_CMD_ERROR_MASK)) != 0)
    {
        SdioWrite32(Adapter, SDHCI_INT_STATUS, InterruptStatus);
        Adapter->LastCommandResetStatus = SdioResetHost(Adapter, SDHCI_RESET_CMD);
        return STATUS_IO_DEVICE_ERROR;
    }

    SdioWrite32(Adapter, SDHCI_INT_STATUS, InterruptStatus);

    if ((CommandFlags & SDHCI_CMD_RESP_MASK) == SDHCI_CMD_RESP_48_BUSY)
    {
        Status = SdioWaitInhibitClear(Adapter, SDHCI_PS_DATA_INHIBIT);
        if (!NT_SUCCESS(Status))
        {
            (VOID)SdioResetHost(Adapter, SDHCI_RESET_DATA);
            return Status;
        }
    }

    return STATUS_SUCCESS;
}

/* TIMING-BEGIN */
static NTSTATUS
SdioSendCommand(PRPI5CYW_ADAPTER Adapter,UCHAR CommandIndex,ULONG Argument,
                USHORT CommandFlags,PULONG Response)
{
    uint64_t Start=CywTimingBegin(&Adapter->Timing);
    NTSTATUS Status=SdioSendCommandRaw(Adapter,CommandIndex,Argument,CommandFlags,Response);
    if(CommandIndex==52)CywTimingEnd(&Adapter->Timing,CywTimeCmd52,Start);
    return Status;
}
/* TIMING-END */
static NTSTATUS
SdioNegotiateOperatingConditionWithRetries(
    _Inout_ PRPI5CYW_ADAPTER Adapter,
    _Out_ PULONG ReadyResponse
    )
{
    static const ULONG Arguments[SDIO_CMD5_COMMANDS_PER_CYCLE] =
    {
        0,
        SDIO_OCR_VDD_RANGE
    };
    ULONG AttemptIndex = 0;
    ULONG CommandIndex;
    ULONG CycleIndex;
    ULONG Response;
    ULONG TargetClockKhz = 0;
    NTSTATUS LastStatus = STATUS_DEVICE_DATA_ERROR;
    NTSTATUS Status;
    BOOLEAN SawValidResponse = FALSE;

    if (ReadyResponse == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }

    Adapter->Cmd5AttemptCount = 0;
    Adapter->Cmd5ValidAttempt = 0;
    Adapter->Cmd5SuccessAttempt = 0;
    Adapter->Cmd5ProbeResponse = 0;
    Adapter->SdioOcr = 0;
    Adapter->SdioFunctions = 0;
    RtlZeroMemory(Adapter->Cmd5Attempts, sizeof(Adapter->Cmd5Attempts));
    *ReadyResponse = 0;

    for (CycleIndex = 0; CycleIndex < SDIO_CMD5_MAX_CYCLES; CycleIndex++)
    {
        ULONG RequestedClockKhz = SdioGetCmd5CycleClockKhz(CycleIndex);

        if (RequestedClockKhz != TargetClockKhz)
        {
            Status = SdioSetClock(Adapter, RequestedClockKhz);
            if (!NT_SUCCESS(Status))
            {
                return Status;
            }
            TargetClockKhz = RequestedClockKhz;

            Status = SdioSendCommand(Adapter,
                                     SDCMD_GO_IDLE_STATE,
                                     0,
                                     SDHCI_CMD_RESP_NONE,
                                     NULL);
            if (!NT_SUCCESS(Status))
            {
                return Status;
            }
            SdioDelayMilliseconds(2);
        }

        for (CommandIndex = 0;
             CommandIndex < SDIO_CMD5_COMMANDS_PER_CYCLE;
             CommandIndex++)
        {
            PRPI5CYW_CMD5_ATTEMPT_DIAG Attempt;
            ULONG Argument = Arguments[CommandIndex];

            if (AttemptIndex >= SDIO_CMD5_MAX_ATTEMPTS)
            {
                return STATUS_INTERNAL_ERROR;
            }
            Attempt = &Adapter->Cmd5Attempts[AttemptIndex];
            Attempt->TargetClockKhz = TargetClockKhz;
            Attempt->Argument = Argument;
            Attempt->PresentStateBefore = SdioRead32(Adapter, SDHCI_PRESENT_STATE);
            Attempt->ClockControlBefore = SdioRead16(Adapter, SDHCI_CLOCK_CONTROL);
            Attempt->PowerControlBefore = SdioRead8(Adapter, SDHCI_POWER_CONTROL);
            Attempt->HostControlBefore = SdioRead8(Adapter, SDHCI_HOST_CONTROL);
            Attempt->HostControl2Before = SdioRead16(Adapter, SDHCI_HOST_CONTROL2);
            Attempt->TimeoutControlBefore = SdioRead8(Adapter, SDHCI_TIMEOUT_CONTROL);

            Response = 0;
            Status = SdioSendCommand(Adapter,
                                     SDCMD_IO_SEND_OP_COND,
                                     Argument,
                                     SDHCI_CMD_RESP_48,
                                     &Response);

            Attempt->Status = Status;
            Attempt->ResetStatus = Adapter->LastCommandResetStatus;
            Attempt->InterruptStatus = Adapter->LastInterruptStatus;
            Attempt->Response = Adapter->LastResponse;
            Attempt->PresentStateAfter = SdioRead32(Adapter, SDHCI_PRESENT_STATE);
            Attempt->ClockControlAfter = SdioRead16(Adapter, SDHCI_CLOCK_CONTROL);
            Attempt->PowerControlAfter = SdioRead8(Adapter, SDHCI_POWER_CONTROL);
            Attempt->HostControlAfter = SdioRead8(Adapter, SDHCI_HOST_CONTROL);
            Attempt->HostControl2After = SdioRead16(Adapter, SDHCI_HOST_CONTROL2);
            Attempt->TimeoutControlAfter = SdioRead8(Adapter, SDHCI_TIMEOUT_CONTROL);

            Adapter->PresentState = Attempt->PresentStateAfter;
            Adapter->ClockControl = Attempt->ClockControlAfter;
            Adapter->PowerControl = Attempt->PowerControlAfter;
            Adapter->HostControl = Attempt->HostControlAfter;
            Adapter->HostControl2 = Attempt->HostControl2After;
            Adapter->TimeoutControl = Attempt->TimeoutControlAfter;
            Adapter->SoftwareReset = SdioRead8(Adapter, SDHCI_SOFTWARE_RESET);
            Adapter->Cmd5AttemptCount = ++AttemptIndex;

            if (Argument == 0)
            {
                Adapter->Cmd5ProbeResponse = Response;
            }
            else
            {
                Adapter->SdioOcr = Response;
            }

            if (NT_SUCCESS(Status) && SdioR4HasBasicInfo(Response))
            {
                Attempt->ResponseValid = 1;
                SawValidResponse = TRUE;
                if (Adapter->Cmd5ValidAttempt == 0)
                {
                    Adapter->Cmd5ValidAttempt = AttemptIndex;
                }
                Adapter->SdioFunctions =
                    (Response & SDIO_OCR_NUM_FUNCTIONS_MASK) >> SDIO_OCR_NUM_FUNCTIONS_SHIFT;

                if (Argument != 0 && SdioR4IsReady(Response))
                {
                    Adapter->Cmd5SuccessAttempt = AttemptIndex;
                    *ReadyResponse = Response;
                    return STATUS_SUCCESS;
                }

                LastStatus = STATUS_IO_TIMEOUT;
            }
            else if (NT_SUCCESS(Status))
            {
                /* Transport completion with an empty/malformed R4 is not success. */
                LastStatus = STATUS_DEVICE_DATA_ERROR;
            }
            else
            {
                LastStatus = Status;
            }

            SdioDelayMilliseconds(5);
        }
    }

    return SawValidResponse ? STATUS_IO_TIMEOUT : LastStatus;
}

NTSTATUS
SdioCmd52Read(
    _Inout_ PRPI5CYW_ADAPTER Adapter,
    _In_ UCHAR Function,
    _In_ ULONG Address,
    _Out_ PUCHAR Value
    )
{
    ULONG Argument;
    ULONG Response;
    NTSTATUS Status;

    if (Adapter == NULL || Adapter->RegisterBase == NULL ||
        Value == NULL || Function > 7 || Address > 0x1FFFF)
    {
        return STATUS_INVALID_PARAMETER;
    }

    Argument = SdioBuildCmd52Argument(FALSE,
                                      Function,
                                      FALSE,
                                      Address,
                                      0);

    Status = SdioSendCommand(Adapter,
                             SDCMD_IO_RW_DIRECT,
                             Argument,
                             SDHCI_CMD_RESP_48 | SDHCI_CMD_CRC_CHECK | SDHCI_CMD_INDEX_CHECK,
                             &Response);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    if (SdioR5HasError(Response))
    {
        return STATUS_IO_DEVICE_ERROR;
    }

    *Value = (UCHAR)(Response & 0xFF);
    return STATUS_SUCCESS;
}

NTSTATUS
SdioCmd52Write(PRPI5CYW_ADAPTER Adapter, UCHAR Function, ULONG Address,
               UCHAR Value, UCHAR VerifyMask)
{
    ULONG Response;
    UCHAR ReadBack;
    NTSTATUS Status;
    if (Adapter == NULL || Adapter->RegisterBase == NULL ||
        Function > 7 || Address > SDIO_CMD52_ADDRESS_MASK)
        return STATUS_INVALID_PARAMETER;

    if ((Function == 1 && Address >= 0x1000a && Address <= 0x1000c) ||
        (Function == 0 && (Address == 2 || Address == 6))) Adapter->BpWindowValid=0;
    Status = SdioSendCommand(Adapter, SDCMD_IO_RW_DIRECT,
        SdioBuildCmd52Argument(TRUE, Function, FALSE, Address, Value),
        SDHCI_CMD_RESP_48 | SDHCI_CMD_CRC_CHECK | SDHCI_CMD_INDEX_CHECK,
        &Response);
    if (!NT_SUCCESS(Status)) return Status;
    if (SdioR5HasError(Response)) return STATUS_IO_DEVICE_ERROR;
    if (VerifyMask == 0) return STATUS_SUCCESS;
    Status = SdioCmd52Read(Adapter, Function, Address, &ReadBack);
    if (!NT_SUCCESS(Status)) return Status;
    return ((ReadBack ^ Value) & VerifyMask) == 0 ?
        STATUS_SUCCESS : STATUS_DEVICE_DATA_ERROR;
}

/* PASSIVE_LEVEL, serialized access only. No DMA or IRQ callbacks.
 * Keep data-ready and transfer-complete latched until their phase consumes
 * them: the command-only path clears all status and cannot be reused here.
 */
static NTSTATUS
SdioCmd53TransferRaw(PRPI5CYW_ADAPTER Adapter, UCHAR Function, ULONG Address,
                  PUCHAR Buffer, ULONG Length, BOOLEAN Write, BOOLEAN Increment)
{
    ULONG InterruptStatus = 0, Response, Offset, Word, Byte, Poll;
    NTSTATUS Status;
    const ULONG Errors = SDHCI_INT_ERROR | SDHCI_INT_CMD_ERROR_MASK |
                         SDHCI_INT_DATA_ERROR_MASK;
    const ULONG Events[3] = { SDHCI_INT_CMD_COMPLETE,
        Write ? SDHCI_INT_BUFFER_WRITE_READY : SDHCI_INT_BUFFER_READ_READY,
        SDHCI_INT_XFER_COMPLETE };
    ULONG Phase, FastPolls = 0, FastLimit;
    ULONG64 Deadline, SleepStart;
    BOOLEAN OperatingBus;

    if (Adapter == NULL || Adapter->RegisterBase == NULL || Buffer == NULL ||
        Function > 7 || Address > 0x1FFFF || Length == 0 || Length > 512 ||
        (Increment && !SdioIsValidByteRead(Function, Address, Length)))
        return STATUS_INVALID_PARAMETER;
    if (KeGetCurrentIrql() != PASSIVE_LEVEL) return STATUS_INVALID_DEVICE_STATE;
    OperatingBus = (Function == 1 || Function == 2) &&
        Adapter->BusModeStage == 6 && Adapter->BusWidth == 4 &&
        Adapter->BusActualKhz > 400 && Adapter->BusActualKhz <= 25000;
    FastLimit = OperatingBus ? 5 : (Function == 2 ? 4 : 0);
    if (!Write) RtlZeroMemory(Buffer, Length);
    Adapter->Cmd53BytesTransferred = 0;
    Adapter->Cmd53ResetStatus = STATUS_SUCCESS;
    Adapter->LastCommand = SDCMD_IO_RW_EXTENDED;
    Adapter->LastArgument = SdioBuildCmd53Argument(Write, Function, FALSE,
                                                  Increment, Address, Length);
    Adapter->LastResponse = 0;
    Adapter->LastInterruptStatus = 0;
    Status = SdioWaitInhibitClear(Adapter,
        SDHCI_PS_CMD_INHIBIT | SDHCI_PS_DATA_INHIBIT);
    if (!NT_SUCCESS(Status)) goto Failed;

    SdioWrite32(Adapter, SDHCI_INT_STATUS, SDHCI_INT_ALL_MASK);
    SdioWrite16(Adapter, SDHCI_BLOCK_SIZE, (USHORT)Length);
    SdioWrite16(Adapter, SDHCI_BLOCK_COUNT, 1);
    SdioWrite16(Adapter, SDHCI_TRANSFER_MODE, (USHORT)(Write ? 0 : SDHCI_TRNS_READ));
    SdioWrite32(Adapter, SDHCI_ARGUMENT, Adapter->LastArgument);
    KeMemoryBarrier();
    SdioWrite16(Adapter, SDHCI_COMMAND, SDHCI_MAKE_CMD(SDCMD_IO_RW_EXTENDED,
        SDHCI_CMD_RESP_48 | SDHCI_CMD_CRC_CHECK | SDHCI_CMD_INDEX_CHECK |
        SDHCI_CMD_DATA_PRESENT));

    for (Phase = 0; Phase < 3; Phase++)
    {
        /* Command, buffer-ready and transfer-complete are separate hardware
         * waits. Replenish the short-poll allowance only on phase progress,
         * never after a sleep. At 4-bit/25 MHz a 512-byte payload alone takes
         * ~41 us: a 40 us allowance shared by all phases can force a scheduler
         * sleep on every normal packet. Verified operating mode allows up to
         * 50 us per phase (150 us per transaction, ten-us individual stalls).
         * Startup/recovery retain F1 sleep / F2 40-us-total behavior. These
         * PASSIVE_LEVEL waits remain cancellable, with 250 ms phase deadlines. */
        if (OperatingBus) FastPolls = 0;
        Deadline = KeQueryInterruptTime() + 2500000ULL;
        for (Poll = 0; Poll < 254; Poll++)
        {
            if (Adapter->IoStopped) { Status = STATUS_INVALID_DEVICE_STATE; goto Failed; }
            InterruptStatus = SdioRead32(Adapter, SDHCI_INT_STATUS);
            Adapter->LastInterruptStatus = InterruptStatus;
            if ((InterruptStatus & Errors) != 0)
            {
                Status = STATUS_IO_DEVICE_ERROR;
                goto Failed;
            }
            if ((InterruptStatus & Events[Phase]) != 0) break;
            if (KeQueryInterruptTime() >= Deadline) break;
            if (FastPolls < FastLimit)
            {
                KeStallExecutionProcessor(10);
                FastPolls++;
                Adapter->Cmd53FastPolls++;
                if (Function == 1 && OperatingBus) Adapter->RuntimeF1FastPolls++;
            }
            else
            {
                Adapter->Cmd53WaitSleeps++;
                SleepStart = KeQueryInterruptTime();
                SdioDelayMilliseconds(1);
                if (OperatingBus) {
                    Adapter->RuntimeCmd53SleepPhase[Phase]++;
                    if (Function == 1) Adapter->RuntimeF1WaitSleeps++;
                    else Adapter->RuntimeF2WaitSleeps++;
                    Adapter->RuntimeCmd53Sleep100ns += KeQueryInterruptTime() - SleepStart;
                }
            }
        }
        if ((InterruptStatus & Events[Phase]) == 0)
        {
            Adapter->Cmd53Timeouts++;
            Status = STATUS_IO_TIMEOUT;
            goto Failed;
        }
        if (Phase == 0)
        {
            Response = SdioRead32(Adapter, SDHCI_RESPONSE0);
            Adapter->LastResponse = Response;
            if (SdioR5HasError(Response))
            {
                Status = STATUS_IO_DEVICE_ERROR;
                goto Failed;
            }
        }
        SdioWrite32(Adapter, SDHCI_INT_STATUS, Events[Phase]);
        if (Phase == 1)
        {
            for (Offset = 0; Offset < Length; Offset += 4)
            {
                if (Write)
                {
                    Word = 0;
                    for (Byte = 0; Byte < 4 && Offset + Byte < Length; Byte++)
                        Word |= (ULONG)Buffer[Offset + Byte] << (Byte * 8);
                    SdioWrite32(Adapter, SDHCI_BUFFER, Word);
                }
                else
                {
                    Word = SdioRead32(Adapter, SDHCI_BUFFER);
                    for (Byte = 0; Byte < 4 && Offset + Byte < Length; Byte++)
                        Buffer[Offset + Byte] = (UCHAR)(Word >> (Byte * 8));
                }
            }
            Adapter->Cmd53BytesTransferred = Length;
        }
    }
    if (Write) Adapter->Cmd53WriteCount++;
    else Adapter->Cmd53ReadCount++;
    return STATUS_SUCCESS;

Failed:
    Adapter->Cmd53ResetStatus = SdioResetHost(Adapter,
                                             SDHCI_RESET_CMD | SDHCI_RESET_DATA);
    SdioWrite32(Adapter, SDHCI_INT_STATUS, SDHCI_INT_ALL_MASK);
    if (!Write) RtlZeroMemory(Buffer, Length);
    return Status;
}

/* TIMING-BEGIN */
static NTSTATUS
SdioCmd53Transfer(PRPI5CYW_ADAPTER Adapter,UCHAR Function,ULONG Address,
                  PUCHAR Buffer,ULONG Length,BOOLEAN Write,BOOLEAN Increment)
{
    uint64_t Start=Adapter?CywTimingBegin(&Adapter->Timing):0;
    NTSTATUS Status=SdioCmd53TransferRaw(Adapter,Function,Address,Buffer,Length,Write,Increment);
    if(Adapter && (Function==1 || Function==2))
        CywTimingEnd(&Adapter->Timing,Function==1?CywTimeCmd53F1:
            (Write?CywTimeCmd53Tx:CywTimeCmd53Rx),Start);
    return Status;
}
/* TIMING-END */
NTSTATUS
SdioCmd53Read(PRPI5CYW_ADAPTER Adapter, UCHAR Function, ULONG Address,
              PUCHAR Buffer, ULONG Length)
{
    return SdioCmd53Transfer(Adapter, Function, Address, Buffer, Length, FALSE, TRUE);
}

NTSTATUS
SdioCmd53Write(PRPI5CYW_ADAPTER Adapter, UCHAR Function, ULONG Address,
               PUCHAR Buffer, ULONG Length)
{
    return SdioCmd53Transfer(Adapter, Function, Address, Buffer, Length, TRUE, TRUE);
}

/* Broadcom F2 RX uses a fixed address; TX uses incrementing writes, matching
 * Linux brcmfmac/bcmsdh.c and the reference driver. Keep bounded byte-mode
 * chunks and the proven PIO engine, rather than adding an untested DMA path. */
NTSTATUS SdioFifoTransfer(PRPI5CYW_ADAPTER Adapter, PUCHAR Buffer,
                          ULONG Length, BOOLEAN Write)
{
    ULONG Done = 0, Chunk;
    NTSTATUS Status;
    if (Buffer == NULL || Length == 0 || Length > 65536 || (Length & 3))
        return STATUS_INVALID_PARAMETER;
    while (Done < Length)
    {
        Chunk = Length - Done;
        if (Chunk > 512) Chunk = 512;
        Status = SdioCmd53Transfer(Adapter, 2, 0x8000, Buffer + Done,
                                   Chunk, Write, Write);
        if (!NT_SUCCESS(Status)) return Status;
        Done += Chunk;
    }
    return STATUS_SUCCESS;
}

NTSTATUS
Rpi5CywDirectSdioProbe(
    _Inout_ PRPI5CYW_ADAPTER Adapter
    )
{
    NTSTATUS Status;
    ULONG Response;
    UCHAR Value;

    if (Adapter == NULL || Adapter->RegisterBase == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }

    Status = SdioInitializeHost(Adapter);
    Adapter->EromWords = 0;
    Adapter->CoreInventoryComplete = 0;
    Rpi5CywWriteDiagnostics(Adapter, 20, Status);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    Response = 0;
    Status = SdioNegotiateOperatingConditionWithRetries(Adapter, &Response);
    Rpi5CywWriteDiagnostics(Adapter, 40, Status);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }
    Adapter->SdioOcr = Response;
    Rpi5CywWriteDiagnostics(Adapter, 50, STATUS_SUCCESS);

    Response = 0;
    Status = SdioSendCommand(Adapter,
                             SDCMD_SEND_RELATIVE_ADDR,
                             0,
                             SDHCI_CMD_RESP_48 | SDHCI_CMD_CRC_CHECK | SDHCI_CMD_INDEX_CHECK,
                             &Response);
    if (!NT_SUCCESS(Status))
    {
        Rpi5CywWriteDiagnostics(Adapter, 60, Status);
        return Status;
    }
    Adapter->RelativeAddress = (Response >> 16) & 0xFFFFUL;
    if (Adapter->RelativeAddress == 0)
    {
        Status = STATUS_DEVICE_DATA_ERROR;
        Rpi5CywWriteDiagnostics(Adapter, 61, Status);
        return Status;
    }

    Status = SdioSendCommand(Adapter,
                             SDCMD_SELECT_CARD,
                             Adapter->RelativeAddress << 16,
                             SDHCI_CMD_RESP_48_BUSY | SDHCI_CMD_CRC_CHECK | SDHCI_CMD_INDEX_CHECK,
                             &Response);
    Rpi5CywWriteDiagnostics(Adapter, 70, Status);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    Value = 0;
    Status = SdioCmd52Read(Adapter, 0, CYW_SDIO_CCCR_REVISION, &Value);
    if (!NT_SUCCESS(Status))
    {
        Rpi5CywWriteDiagnostics(Adapter, 80, Status);
        return Status;
    }
    Adapter->CccrRevision = Value;

    /* A host reset does not establish the card's width/timing. Explicitly
     * synchronize both ends before the first F1 data transfer, including D0
     * resume and warm restart. CMD52 uses CMD, not the DAT bus width. */
    Adapter->BusModeStage = 1;
    Adapter->BusUpgradeStatus = STATUS_SUCCESS;
    Adapter->BusVerifyStatus = STATUS_SUCCESS;
    Adapter->BusVerifyReads = 0;
    Status = SdioRestoreIdentificationBus(Adapter);
    Rpi5CywWriteDiagnostics(Adapter, 80, Status);
    if (!NT_SUCCESS(Status)) return Status;

    Status = SdioCmd52Read(Adapter, 0, CYW_SDIO_CCCR_IO_ENABLE, &Value);
    if (!NT_SUCCESS(Status))
    {
        Rpi5CywWriteDiagnostics(Adapter, 81, Status);
        return Status;
    }
    Adapter->IoEnable = Value;

    Status = SdioCmd52Read(Adapter, 0, CYW_SDIO_CCCR_IO_READY, &Value);
    if (!NT_SUCCESS(Status))
    {
        Rpi5CywWriteDiagnostics(Adapter, 82, Status);
        return Status;
    }
    Adapter->IoReady = Value;

    Status = SdioCmd52Read(Adapter, 0, CYW_SDIO_F1_INTERFACE, &Value);
    if (!NT_SUCCESS(Status))
    {
        Rpi5CywWriteDiagnostics(Adapter, 83, Status);
        return Status;
    }
    Adapter->F1InterfaceCode = Value;

    Status = SdioCmd52Read(Adapter, 0, CYW_SDIO_F2_INTERFACE, &Value);
    if (!NT_SUCCESS(Status))
    {
        Rpi5CywWriteDiagnostics(Adapter, 84, Status);
        return Status;
    }
    Adapter->F2InterfaceCode = Value;

    Adapter->PresentState = SdioRead32(Adapter, SDHCI_PRESENT_STATE);
    Adapter->ClockControl = SdioRead16(Adapter, SDHCI_CLOCK_CONTROL);
    Adapter->PowerControl = SdioRead8(Adapter, SDHCI_POWER_CONTROL);
    Rpi5CywWriteDiagnostics(Adapter, 90, STATUS_SUCCESS);
    return Cyw43455Probe(Adapter);
}
