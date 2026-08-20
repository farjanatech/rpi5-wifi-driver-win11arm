#pragma once

#include <ntddk.h>
#include <wdf.h>

#define CYW43455_CHIP_ID 0x4345u

NTSTATUS
Cyw43455Probe(
    _In_ WDFDEVICE Device
    );
