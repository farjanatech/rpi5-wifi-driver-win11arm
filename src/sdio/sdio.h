#pragma once

#include <ntddk.h>
#include <wdf.h>

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
