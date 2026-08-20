#include "sdio.h"

#define CYW_SDIO_CCCR_REVISION       0x00UL
#define CYW_SDIO_CCCR_IO_ENABLE      0x02UL
#define CYW_SDIO_CCCR_IO_READY       0x03UL
#define CYW_SDIO_FBR_STRIDE          0x100UL
#define CYW_SDIO_FBR_INTERFACE_CODE  0x00UL

static
PSDBUS_REQUEST_PACKET
CywSdioAllocateRequest(
    VOID
    )
{
    PSDBUS_REQUEST_PACKET packet;

    packet = (PSDBUS_REQUEST_PACKET)ExAllocatePool2(
        POOL_FLAG_NON_PAGED,
        sizeof(SDBUS_REQUEST_PACKET),
        CYW_SDIO_POOL_TAG);

    if (packet != NULL)
    {
        RtlZeroMemory(packet, sizeof(*packet));
    }

    return packet;
}

static
VOID
CywSdioFreeRequest(
    _In_opt_ PSDBUS_REQUEST_PACKET Packet
    )
{
    if (Packet != NULL)
    {
        ExFreePoolWithTag(Packet, CYW_SDIO_POOL_TAG);
    }
}

static
NTSTATUS
CywSdioGetProperty(
    _In_ PCYW_SDIO_CONTEXT Context,
    _In_ SDBUS_PROPERTY Property,
    _Out_writes_bytes_(Length) PVOID Buffer,
    _In_ ULONG Length
    )
{
    PSDBUS_REQUEST_PACKET packet;
    NTSTATUS status;

    if (!Context->BusOpen || Context->BusInterface.Context == NULL)
    {
        return STATUS_DEVICE_NOT_READY;
    }

    packet = CywSdioAllocateRequest();
    if (packet == NULL)
    {
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    packet->RequestFunction = SDRF_GET_PROPERTY;
    packet->Parameters.GetSetProperty.Property = Property;
    packet->Parameters.GetSetProperty.Buffer = Buffer;
    packet->Parameters.GetSetProperty.Length = Length;

    status = SdBusSubmitRequest(Context->BusInterface.Context, packet);
    CywSdioFreeRequest(packet);
    return status;
}

static
NTSTATUS
CywSdioSetProperty(
    _In_ PCYW_SDIO_CONTEXT Context,
    _In_ SDBUS_PROPERTY Property,
    _In_reads_bytes_(Length) PVOID Buffer,
    _In_ ULONG Length
    )
{
    PSDBUS_REQUEST_PACKET packet;
    NTSTATUS status;

    if (!Context->BusOpen || Context->BusInterface.Context == NULL)
    {
        return STATUS_DEVICE_NOT_READY;
    }

    packet = CywSdioAllocateRequest();
    if (packet == NULL)
    {
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    packet->RequestFunction = SDRF_SET_PROPERTY;
    packet->Parameters.GetSetProperty.Property = Property;
    packet->Parameters.GetSetProperty.Buffer = Buffer;
    packet->Parameters.GetSetProperty.Length = Length;

    status = SdBusSubmitRequest(Context->BusInterface.Context, packet);
    CywSdioFreeRequest(packet);
    return status;
}

static
NTSTATUS
CywSdioDirectTransfer(
    _In_ WDFDEVICE Device,
    _In_ UCHAR Function,
    _In_ BOOLEAN WriteToDevice,
    _In_ ULONG Address,
    _Inout_ PUCHAR Value
    )
{
    PCYW_SDIO_CONTEXT context;
    PSDBUS_REQUEST_PACKET packet;
    SD_RW_DIRECT_ARGUMENT argument;
    SDCMD_DESCRIPTOR descriptor;
    NTSTATUS status;

    if (Function > CYW_SDIO_MAX_FUNCTION ||
        Address > CYW_SDIO_MAX_ADDRESS ||
        Value == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }

    if (KeGetCurrentIrql() != PASSIVE_LEVEL)
    {
        return STATUS_INVALID_DEVICE_STATE;
    }

    context = CywGetSdioContext(Device);
    if (context == NULL || context->TransferLock == NULL)
    {
        return STATUS_INVALID_DEVICE_STATE;
    }

    status = WdfWaitLockAcquire(context->TransferLock, NULL);
    if (!NT_SUCCESS(status))
    {
        return status;
    }

    if (!context->BusOpen || context->BusInterface.Context == NULL)
    {
        status = STATUS_DEVICE_NOT_READY;
        goto Exit;
    }

    packet = CywSdioAllocateRequest();
    if (packet == NULL)
    {
        status = STATUS_INSUFFICIENT_RESOURCES;
        goto Exit;
    }

    descriptor.Cmd = SDCMD_IO_RW_DIRECT;
    descriptor.CmdClass = SDCC_STANDARD;
    descriptor.TransferDirection = WriteToDevice ? SDTD_WRITE : SDTD_READ;
    descriptor.TransferType = SDTT_CMD_ONLY;
    descriptor.ResponseType = SDRT_5;

    argument.u.AsULONG = 0;
    argument.u.bits.Address = Address;
    argument.u.bits.Function = Function;
    argument.u.bits.WriteToDevice = WriteToDevice ? 1U : 0U;
    argument.u.bits.ReadAfterWrite = 0;
    if (WriteToDevice)
    {
        argument.u.bits.Data = *Value;
    }

    packet->RequestFunction = SDRF_DEVICE_COMMAND;
    packet->Parameters.DeviceCommand.CmdDesc = descriptor;
    packet->Parameters.DeviceCommand.Argument = argument.u.AsULONG;
    packet->Parameters.DeviceCommand.Mdl = NULL;
    packet->Parameters.DeviceCommand.Length = 0;

    status = SdBusSubmitRequest(context->BusInterface.Context, packet);
    if (NT_SUCCESS(status) && !WriteToDevice)
    {
        *Value = packet->ResponseData.AsUCHAR[0];
    }

    CywSdioFreeRequest(packet);

Exit:
    WdfWaitLockRelease(context->TransferLock);
    return status;
}

NTSTATUS
CywSdioReadByte(
    _In_ WDFDEVICE Device,
    _In_ UCHAR Function,
    _In_ ULONG Address,
    _Out_ PUCHAR Value
    )
{
    if (Value == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }

    *Value = 0;
    return CywSdioDirectTransfer(Device, Function, FALSE, Address, Value);
}

NTSTATUS
CywSdioWriteByte(
    _In_ WDFDEVICE Device,
    _In_ UCHAR Function,
    _In_ ULONG Address,
    _In_ UCHAR Value
    )
{
    UCHAR data = Value;

    return CywSdioDirectTransfer(Device, Function, TRUE, Address, &data);
}

NTSTATUS
CywSdioReadWriteExtended(
    _In_ WDFDEVICE Device,
    _In_ UCHAR Function,
    _In_ BOOLEAN WriteToDevice,
    _In_ BOOLEAN IncrementAddress,
    _In_ BOOLEAN BlockMode,
    _In_ ULONG Address,
    _Inout_updates_bytes_(Length) PUCHAR Buffer,
    _In_ ULONG Length
    )
{
    PCYW_SDIO_CONTEXT context;
    PSDBUS_REQUEST_PACKET packet = NULL;
    SD_RW_EXTENDED_ARGUMENT argument;
    SDCMD_DESCRIPTOR descriptor;
    PUCHAR bounceBuffer = NULL;
    PMDL mdl = NULL;
    ULONG blockCount = 0;
    ULONG encodedCount;
    NTSTATUS status;

    if (Function > CYW_SDIO_MAX_FUNCTION ||
        Address > CYW_SDIO_MAX_ADDRESS ||
        Buffer == NULL ||
        Length == 0)
    {
        return STATUS_INVALID_PARAMETER;
    }

    if (IncrementAddress &&
        (Length - 1 > CYW_SDIO_MAX_ADDRESS - Address))
    {
        return STATUS_INVALID_PARAMETER;
    }

    if (KeGetCurrentIrql() != PASSIVE_LEVEL)
    {
        return STATUS_INVALID_DEVICE_STATE;
    }

    context = CywGetSdioContext(Device);
    if (context == NULL || context->TransferLock == NULL)
    {
        return STATUS_INVALID_DEVICE_STATE;
    }

    status = WdfWaitLockAcquire(context->TransferLock, NULL);
    if (!NT_SUCCESS(status))
    {
        return status;
    }

    if (!context->BusOpen || context->BusInterface.Context == NULL)
    {
        status = STATUS_DEVICE_NOT_READY;
        goto Exit;
    }

    if (BlockMode)
    {
        if (Function != context->FunctionNumber ||
            context->FunctionBlockSize == 0 ||
            (Length % context->FunctionBlockSize) != 0)
        {
            status = STATUS_INVALID_PARAMETER;
            goto Exit;
        }

        blockCount = Length / context->FunctionBlockSize;
        if (blockCount == 0 || blockCount > CYW_SDIO_MAX_BLOCK_COUNT)
        {
            status = STATUS_INVALID_PARAMETER;
            goto Exit;
        }

        encodedCount = blockCount;
    }
    else
    {
        if (Length > CYW_SDIO_MAX_BYTE_TRANSFER)
        {
            status = STATUS_INVALID_PARAMETER;
            goto Exit;
        }

        encodedCount = (Length == CYW_SDIO_MAX_BYTE_TRANSFER) ? 0 : Length;
    }

    bounceBuffer = (PUCHAR)ExAllocatePool2(
        POOL_FLAG_NON_PAGED,
        Length,
        CYW_SDIO_POOL_TAG);
    if (bounceBuffer == NULL)
    {
        status = STATUS_INSUFFICIENT_RESOURCES;
        goto Exit;
    }

    if (WriteToDevice)
    {
        RtlCopyMemory(bounceBuffer, Buffer, Length);
    }
    else
    {
        RtlZeroMemory(bounceBuffer, Length);
    }

    mdl = IoAllocateMdl(
        bounceBuffer,
        Length,
        FALSE,
        FALSE,
        NULL);
    if (mdl == NULL)
    {
        status = STATUS_INSUFFICIENT_RESOURCES;
        goto Exit;
    }

    MmBuildMdlForNonPagedPool(mdl);

    packet = CywSdioAllocateRequest();
    if (packet == NULL)
    {
        status = STATUS_INSUFFICIENT_RESOURCES;
        goto Exit;
    }

    descriptor.Cmd = SDCMD_IO_RW_EXTENDED;
    descriptor.CmdClass = SDCC_STANDARD;
    descriptor.TransferDirection = WriteToDevice ? SDTD_WRITE : SDTD_READ;
    descriptor.TransferType =
        (BlockMode && blockCount > 1) ?
            SDTT_MULTI_BLOCK_NO_CMD12 :
            SDTT_SINGLE_BLOCK;
    descriptor.ResponseType = SDRT_5;

    argument.u.AsULONG = 0;
    argument.u.bits.Count = encodedCount;
    argument.u.bits.Address = Address;
    argument.u.bits.OpCode = IncrementAddress ? 1U : 0U;
    argument.u.bits.BlockMode = BlockMode ? 1U : 0U;
    argument.u.bits.Function = Function;
    argument.u.bits.WriteToDevice = WriteToDevice ? 1U : 0U;

    packet->RequestFunction = SDRF_DEVICE_COMMAND;
    packet->Parameters.DeviceCommand.CmdDesc = descriptor;
    packet->Parameters.DeviceCommand.Argument = argument.u.AsULONG;
    packet->Parameters.DeviceCommand.Mdl = mdl;
    packet->Parameters.DeviceCommand.Length = Length;

    status = SdBusSubmitRequest(context->BusInterface.Context, packet);
    if (NT_SUCCESS(status) && !WriteToDevice)
    {
        RtlCopyMemory(Buffer, bounceBuffer, Length);
    }

Exit:
    CywSdioFreeRequest(packet);

    if (mdl != NULL)
    {
        IoFreeMdl(mdl);
    }

    if (bounceBuffer != NULL)
    {
        ExFreePoolWithTag(bounceBuffer, CYW_SDIO_POOL_TAG);
    }

    WdfWaitLockRelease(context->TransferLock);
    return status;
}

VOID
CywSdioShutdown(
    _In_ WDFDEVICE Device
    )
{
    PCYW_SDIO_CONTEXT context;

    context = CywGetSdioContext(Device);
    if (context == NULL)
    {
        return;
    }

    if (context->BusOpen &&
        context->BusInterface.InterfaceDereference != NULL)
    {
        context->BusInterface.InterfaceDereference(
            context->BusInterface.Context);
    }

    RtlZeroMemory(
        &context->BusInterface,
        sizeof(context->BusInterface));

    context->FunctionNumber = 0;
    context->FunctionBlockSize = 0;
    context->InterfaceInitialized = FALSE;
    context->BusOpen = FALSE;
}

NTSTATUS
CywSdioInitialize(
    _In_ WDFDEVICE Device
    )
{
    PCYW_SDIO_CONTEXT context;
    PDEVICE_OBJECT pdo;
    PDEVICE_OBJECT lowerDevice;
    SDBUS_INTERFACE_PARAMETERS parameters;
    USHORT blockSize;
    UCHAR cccrRevision = 0;
    UCHAR ioEnable = 0;
    UCHAR ioReady = 0;
    UCHAR interfaceCode = 0;
    ULONG fbrAddress;
    NTSTATUS status;

    if (KeGetCurrentIrql() != PASSIVE_LEVEL)
    {
        return STATUS_INVALID_DEVICE_STATE;
    }

    context = CywGetSdioContext(Device);
    if (context == NULL || context->TransferLock == NULL)
    {
        return STATUS_INVALID_DEVICE_STATE;
    }

    CywSdioShutdown(Device);

    pdo = WdfDeviceWdmGetPhysicalDevice(Device);
    lowerDevice = WdfDeviceWdmGetAttachedDevice(Device);
    if (pdo == NULL || lowerDevice == NULL)
    {
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    }

    status = SdBusOpenInterface(
        pdo,
        &context->BusInterface,
        sizeof(context->BusInterface),
        SDBUS_INTERFACE_VERSION);
    if (!NT_SUCCESS(status))
    {
        DbgPrintEx(
            DPFLTR_IHVDRIVER_ID,
            DPFLTR_ERROR_LEVEL,
            "RPI5CYW: SdBusOpenInterface failed 0x%08X\n",
            (ULONG)status);
        goto Failure;
    }

    context->BusOpen = TRUE;

    if (context->BusInterface.InitializeInterface == NULL)
    {
        status = STATUS_DEVICE_CONFIGURATION_ERROR;
        goto Failure;
    }

    RtlZeroMemory(&parameters, sizeof(parameters));
    parameters.Size = sizeof(parameters);
    parameters.SdioFlags = 0;
    parameters.TargetObject = lowerDevice;
    parameters.DeviceGeneratesInterrupts = FALSE;
    parameters.CallbackAtDpcLevel = FALSE;
    parameters.CallbackRoutine = NULL;
    parameters.CallbackRoutineContext = NULL;

    status = context->BusInterface.InitializeInterface(
        context->BusInterface.Context,
        &parameters);
    if (!NT_SUCCESS(status))
    {
        DbgPrintEx(
            DPFLTR_IHVDRIVER_ID,
            DPFLTR_ERROR_LEVEL,
            "RPI5CYW: InitializeInterface failed 0x%08X\n",
            (ULONG)status);
        goto Failure;
    }

    context->InterfaceInitialized = TRUE;

    status = CywSdioGetProperty(
        context,
        SDP_FUNCTION_NUMBER,
        &context->FunctionNumber,
        sizeof(context->FunctionNumber));
    if (!NT_SUCCESS(status))
    {
        DbgPrintEx(
            DPFLTR_IHVDRIVER_ID,
            DPFLTR_ERROR_LEVEL,
            "RPI5CYW: SDP_FUNCTION_NUMBER failed 0x%08X\n",
            (ULONG)status);
        goto Failure;
    }

    if (context->FunctionNumber == 0 ||
        context->FunctionNumber > CYW_SDIO_MAX_FUNCTION)
    {
        status = STATUS_DEVICE_CONFIGURATION_ERROR;
        DbgPrintEx(
            DPFLTR_IHVDRIVER_ID,
            DPFLTR_ERROR_LEVEL,
            "RPI5CYW: invalid SDIO function %lu\n",
            context->FunctionNumber);
        goto Failure;
    }

    blockSize = 0;
    if (context->FunctionNumber == 1)
    {
        blockSize = CYW_SDIO_F1_BLOCK_SIZE;
    }
    else if (context->FunctionNumber == 2)
    {
        blockSize = CYW_SDIO_F2_BLOCK_SIZE;
    }

    if (blockSize != 0)
    {
        status = CywSdioSetProperty(
            context,
            SDP_FUNCTION_BLOCK_LENGTH,
            &blockSize,
            sizeof(blockSize));
        if (!NT_SUCCESS(status))
        {
            DbgPrintEx(
                DPFLTR_IHVDRIVER_ID,
                DPFLTR_ERROR_LEVEL,
                "RPI5CYW: set block size %u failed 0x%08X\n",
                blockSize,
                (ULONG)status);
            goto Failure;
        }

        context->FunctionBlockSize = blockSize;
    }

    status = CywSdioReadByte(
        Device,
        0,
        CYW_SDIO_CCCR_REVISION,
        &cccrRevision);
    if (!NT_SUCCESS(status))
    {
        goto SmokeTestFailure;
    }

    status = CywSdioReadByte(
        Device,
        0,
        CYW_SDIO_CCCR_IO_ENABLE,
        &ioEnable);
    if (!NT_SUCCESS(status))
    {
        goto SmokeTestFailure;
    }

    status = CywSdioReadByte(
        Device,
        0,
        CYW_SDIO_CCCR_IO_READY,
        &ioReady);
    if (!NT_SUCCESS(status))
    {
        goto SmokeTestFailure;
    }

    fbrAddress =
        (context->FunctionNumber * CYW_SDIO_FBR_STRIDE) +
        CYW_SDIO_FBR_INTERFACE_CODE;

    status = CywSdioReadByte(
        Device,
        0,
        fbrAddress,
        &interfaceCode);
    if (!NT_SUCCESS(status))
    {
        goto SmokeTestFailure;
    }

    DbgPrintEx(
        DPFLTR_IHVDRIVER_ID,
        DPFLTR_INFO_LEVEL,
        "RPI5CYW: SDIO ready fn=%lu block=%u CCCR=0x%02X IOEx=0x%02X IORx=0x%02X FBR=0x%02X\n",
        context->FunctionNumber,
        context->FunctionBlockSize,
        cccrRevision,
        ioEnable,
        ioReady,
        interfaceCode);

    return STATUS_SUCCESS;

SmokeTestFailure:
    DbgPrintEx(
        DPFLTR_IHVDRIVER_ID,
        DPFLTR_ERROR_LEVEL,
        "RPI5CYW: CMD52 smoke test failed fn=%lu status=0x%08X\n",
        context->FunctionNumber,
        (ULONG)status);

Failure:
    CywSdioShutdown(Device);
    return status;
}

NTSTATUS
CywSdioEvtPrepareHardware(
    _In_ WDFDEVICE Device,
    _In_ WDFCMRESLIST ResourcesRaw,
    _In_ WDFCMRESLIST ResourcesTranslated
    )
{
    UNREFERENCED_PARAMETER(ResourcesRaw);
    UNREFERENCED_PARAMETER(ResourcesTranslated);

    return CywSdioInitialize(Device);
}

NTSTATUS
CywSdioEvtReleaseHardware(
    _In_ WDFDEVICE Device,
    _In_ WDFCMRESLIST ResourcesTranslated
    )
{
    UNREFERENCED_PARAMETER(ResourcesTranslated);

    CywSdioShutdown(Device);
    return STATUS_SUCCESS;
}
