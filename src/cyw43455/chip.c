#include "chip.h"

NTSTATUS
Cyw43455Probe(
    _In_ WDFDEVICE Device
    )
{
    UNREFERENCED_PARAMETER(Device);

    /*
     * TODO: use the SDIO transport to select the Broadcom backplane
     * window, read the chip/core identity and validate CYW43455 RAM.
     */
    return STATUS_NOT_IMPLEMENTED;
}
