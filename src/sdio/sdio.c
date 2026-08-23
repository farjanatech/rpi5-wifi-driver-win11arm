#include "sdio.h"

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
SdioInitializeHost(
    _Inout_ PRPI5CYW_ADAPTER Adapter
    )
{
    ULONG BaseClockMhz;
    ULONG BaseClockKhz;
    ULONG Timeout;
    USHORT Divider;
    USHORT DividerHigh;
    USHORT ClockControl;
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

    BaseClockMhz = (Adapter->Capabilities & SDHCI_CAP_BASE_CLK_MASK) >> SDHCI_CAP_BASE_CLK_SHIFT;
    if (BaseClockMhz == 0)
    {
        BaseClockMhz = 200;
    }
    BaseClockKhz = BaseClockMhz * 1000UL;

    Divider = SdioCalculateClockDivider(BaseClockKhz, 400);
    if (Divider > 0x3FF)
    {
        Divider = 0x3FF;
    }

    DividerHigh = (USHORT)((Divider & 0x300) >> 2);
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

    SdioDelayMilliseconds(10);

    Adapter->PresentState = SdioRead32(Adapter, SDHCI_PRESENT_STATE);
    Adapter->ClockControl = SdioRead16(Adapter, SDHCI_CLOCK_CONTROL);
    Adapter->PowerControl = SdioRead8(Adapter, SDHCI_POWER_CONTROL);
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

    Status = SdioWaitInhibitClear(Adapter, SDHCI_PS_CMD_INHIBIT);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    Adapter->LastCommand = CommandIndex;
    Adapter->LastArgument = Argument;
    Adapter->LastInterruptStatus = 0;
    Adapter->LastResponse = 0;

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
    if (Timeout == 10000)
    {
        (VOID)SdioResetHost(Adapter, SDHCI_RESET_CMD);
        return STATUS_IO_TIMEOUT;
    }

    if ((InterruptStatus & (SDHCI_INT_ERROR | SDHCI_INT_CMD_ERROR_MASK)) != 0)
    {
        SdioWrite32(Adapter, SDHCI_INT_STATUS, InterruptStatus);
        (VOID)SdioResetHost(Adapter, SDHCI_RESET_CMD);
        return STATUS_IO_DEVICE_ERROR;
    }

    if (Response != NULL)
    {
        *Response = SdioRead32(Adapter, SDHCI_RESPONSE0);
        Adapter->LastResponse = *Response;
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
    ULONG Timeout;
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

    Status = SdioSendCommand(Adapter, SDCMD_GO_IDLE_STATE, 0, SDHCI_CMD_RESP_NONE, NULL);
    Rpi5CywWriteDiagnostics(Adapter, 30, Status);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }
    SdioDelayMilliseconds(1);

    Response = 0;
    Status = SdioSendCommand(Adapter, SDCMD_IO_SEND_OP_COND, 0, SDHCI_CMD_RESP_48, &Response);
    Adapter->Cmd5ProbeResponse = Response;
    Rpi5CywWriteDiagnostics(Adapter, 40, Status);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    for (Timeout = 0; Timeout < 2000; Timeout++)
    {
        Response = 0;
        Status = SdioSendCommand(Adapter,
                                 SDCMD_IO_SEND_OP_COND,
                                 SDIO_OCR_VDD_RANGE,
                                 SDHCI_CMD_RESP_48,
                                 &Response);
        if (!NT_SUCCESS(Status))
        {
            Rpi5CywWriteDiagnostics(Adapter, 41, Status);
            return Status;
        }

        Adapter->SdioOcr = Response;
        if ((Response & SDIO_OCR_READY) != 0)
        {
            break;
        }
        SdioDelayMilliseconds(1);
    }

    if (Timeout == 2000)
    {
        Status = STATUS_IO_TIMEOUT;
        Rpi5CywWriteDiagnostics(Adapter, 42, Status);
        return Status;
    }

    Adapter->SdioFunctions =
        (Adapter->SdioOcr & SDIO_OCR_NUM_FUNCTIONS_MASK) >> SDIO_OCR_NUM_FUNCTIONS_SHIFT;
    if (Adapter->SdioFunctions == 0)
    {
        Status = STATUS_DEVICE_DATA_ERROR;
        Rpi5CywWriteDiagnostics(Adapter, 43, Status);
        return Status;
    }
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
