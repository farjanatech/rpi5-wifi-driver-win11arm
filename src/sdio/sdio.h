#pragma once

#include <ntddk.h>
#include <wdf.h>
#include <ntddsd.h>
#include <sddef.h>

#define CYW_SDIO_MAX_FUNCTION       7U
#define CYW_SDIO_MAX_ADDRESS        0x1FFFFUL
#define CYW_SDIO_MAX_BYTE_TRANSFER  512UL
#define CYW_SDIO_MAX_BLOCK_COUNT    511UL
#define CYW_SDIO_F1_BLOCK_SIZE      64U
#define CYW_SDIO_F2_BLOCK_SIZE      512U
#define CYW_SDIO_POOL_TAG           'oiSC'

typedef struct _CYW_SDIO_CONTEXT
{
    SDBUS_INTERFACE_STANDARD BusInterface;
    ULONG FunctionNumber;
    USHORT FunctionBlockSize;
    BOOLEAN BusOpen;
    BOOLEAN InterfaceInitialized;
    WDFWAITLOCK TransferLock;
} CYW_SDIO_CONTEXT, *PCYW_SDIO_CONTEXT;

WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(CYW_SDIO_CONTEXT, CywGetSdioContext);

EVT_WDF_DEVICE_PREPARE_HARDWARE CywSdioEvtPrepareHardware;
EVT_WDF_DEVICE_RELEASE_HARDWARE CywSdioEvtReleaseHardware;

NTSTATUS
CywSdioInitialize(
    _In_ WDFDEVICE Device
    );

VOID
CywSdioShutdown(
    _In_ WDFDEVICE Device
    );

NTSTATUS
CywSdioReadByte(
    _In_ WDFDEVICE Device,
    _In_ UCHAR Function,
    _In_ ULONG Address,
    _Out_ PUCHAR Value
    );

NTSTATUS
CywSdioWriteByte(
    _In_ WDFDEVICE Device,
    _In_ UCHAR Function,
    _In_ ULONG Address,
    _In_ UCHAR Value
    );

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
    );
