#include "sdio.h"

C_ASSERT(RPI5CYW_CMD5_MAX_ATTEMPTS == SDIO_CMD5_MAX_ATTEMPTS);

static __forceinline UCHAR
SdioRead8(
    _In_ PRPI5CYW_ADAPTER Adapter,
    _In_ ULONG Offset
    )
{
    return READ_REGISTER_UCHAR((PUCHAR)Adapter->RegisterBase + Offset);
}

static __forceinline USHORT
SdioRead16(
    _In_ PRPI5CYW_ADAPTER Adapter,
    _In_ ULONG Offset
    )
{
    return READ_REGISTER_USHORT((PUSHORT)((PUCHAR)Adapter->RegisterBase + Offset));
}

static __forceinline ULONG
SdioRead32(
    _In_ PRPI5CYW_ADAPTER Adapter,
    _In_ ULONG Offset
    )
{
    return READ_REGISTER_ULONG((PULONG)((PUCHAR)Adapter->RegisterBase + Offset));
}

static __forceinline VOID
SdioWrite8(
    _In_ PRPI5CYW_ADAPTER Adapter,
    _In_ ULONG Offset,
    _In_ UCHAR Value
    )
{
    WRITE_REGISTER_UCHAR((PUCHAR)Adapter->RegisterBase + Offset, Value);
}

static __forceinline VOID
SdioWrite16(
    _In_ PRPI5CYW_ADAPTER Adapter,
    _In_ ULONG Offset,
    _In_ USHORT Value
    )
{
    WRITE_REGISTER_USHORT((PUSHORT)((PUCHAR)Adapter->RegisterBase + Offset), Value);
}

static __forceinline VOID
SdioWrite32(
    _In_ PRPI5CYW_ADAPTER Adapter,
    _In_ ULONG Offset,
    _In_ ULONG Value
    )
{
    WRITE_REGISTER_ULONG((PULONG)((PUCHAR)Adapter->RegisterBase + Offset), Value);
}

static VOID
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

    SdioWrite8(Adapter, SDHCI_SOFTWARE_RESET, ResetMask);
    for (Timeout = 0; Timeout < 1000; Timeout++)
    {
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
    if (TargetClockKhz == 0)
    {
        return STATUS_INVALID_PARAMETER;
    }

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
    return STATUS_SUCCESS;
}

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
        if ((SdioRead32(Adapter, SDHCI_PRESENT_STATE) & Mask) == 0)
        {
            return STATUS_SUCCESS;
        }
        KeStallExecutionProcessor(100);
    }

    return STATUS_IO_TIMEOUT;
}

static NTSTATUS
SdioSendCommand(
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

static NTSTATUS
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

    if (Value == NULL || Function > 7 || Address > 0x1FFFF)
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
    return STATUS_SUCCESS;
}
