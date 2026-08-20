#include "sdio.h"

NTSTATUS
CywSdioInitialize(
    _In_ WDFDEVICE Device
    )
{
    UNREFERENCED_PARAMETER(Device);

    /*
     * TODO: Open/query the Windows SD bus interface and prove basic
     * direct/extended transfers before adding CYW43455 firmware logic.
     */
    return STATUS_NOT_IMPLEMENTED;
}

VOID
CywSdioShutdown(
    _In_ WDFDEVICE Device
    )
{
    UNREFERENCED_PARAMETER(Device);
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
