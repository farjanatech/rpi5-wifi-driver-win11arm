/* Included by sdio.c after SdioSetClock. Actual code is host-tested.
 * CCCR definitions follow Linux include/linux/mmc/sdio.h / SDIO spec.
 * Inspired by the user-supplied operating-speed patch; rewritten for bounded
 * recovery, explicit default timing, capability checks and diagnostics.
 * All calls are serialized by the sole bus owner at PASSIVE_LEVEL.
 */
static NTSTATUS SdioSetDefaultBus(PRPI5CYW_ADAPTER A, BOOLEAN Wide)
{
    UCHAR card, speed = 0, host, expected;
    USHORT clock;
    NTSTATUS status;
    if (A->IoStopped) return STATUS_INVALID_DEVICE_STATE;
    /* Always lower the clock BEFORE modifying width/timing. */
    status = SdioSetClock(A, 400);
    if (!NT_SUCCESS(status)) return status;
    if ((A->CccrRevision & 15) >= 2) {
        status = SdioCmd52Read(A, 0, CYW_SDIO_CCCR_SPEED, &speed);
        if (!NT_SUCCESS(status)) return status;
        speed = (UCHAR)(speed & ~CYW_SDIO_SPEED_BSS_MASK);
        status = SdioCmd52Write(A, 0, CYW_SDIO_CCCR_SPEED, speed,
                                CYW_SDIO_SPEED_BSS_MASK);
        if (!NT_SUCCESS(status)) return status;
    }
    status = SdioCmd52Read(A, 0, CYW_SDIO_CCCR_BUS_INTERFACE, &card);
    if (!NT_SUCCESS(status)) return status;
    card = (UCHAR)((card & ~CYW_SDIO_BUS_WIDTH_MASK) |
                  (Wide ? CYW_SDIO_BUS_WIDTH_4BIT : 0));
    status = SdioCmd52Write(A, 0, CYW_SDIO_CCCR_BUS_INTERFACE, card,
                            CYW_SDIO_BUS_WIDTH_MASK);
    if (!NT_SUCCESS(status)) return status;
    if (A->IoStopped) return STATUS_INVALID_DEVICE_STATE;
    host = SdioRead8(A, SDHCI_HOST_CONTROL);
    expected = (UCHAR)((host & ~(SDHCI_HC_DATA_WIDTH_4BIT |
        SDHCI_HC_DATA_WIDTH_8BIT | SDHCI_HC_HIGH_SPEED_ENABLE)) |
        (Wide ? SDHCI_HC_DATA_WIDTH_4BIT : 0));
    /* SDHCI timing/width changes occur with the external clock gated. */
    clock = SdioRead16(A, SDHCI_CLOCK_CONTROL);
    SdioWrite16(A, SDHCI_CLOCK_CONTROL, (USHORT)(clock & ~SDHCI_CLK_SD_CLK_ENABLE));
    SdioWrite8(A, SDHCI_HOST_CONTROL, expected);
    host = SdioRead8(A, SDHCI_HOST_CONTROL);
    A->HostControl = host;
    if (host != expected) return STATUS_DEVICE_DATA_ERROR;
    status = SdioSetClock(A, 400);
    if (!NT_SUCCESS(status)) return status;
    A->BusWidth = Wide ? 4 : 1;
    A->BusCardInterface = card;
    A->BusCardSpeed = speed;
    return STATUS_SUCCESS;
}

NTSTATUS SdioRestoreIdentificationBus(PRPI5CYW_ADAPTER A)
{
    NTSTATUS status;
    if (!A || !A->RegisterBase || A->RegisterLength < 0x100 ||
        KeGetCurrentIrql() != PASSIVE_LEVEL) return STATUS_INVALID_PARAMETER;
    /* Zero means unknown until both sides and the clock have been verified. */
    A->BusWidth = A->BusActualKhz = A->BusTargetKhz = 0;
    status = SdioSetDefaultBus(A, FALSE);
    A->BusRecoveryStatus = status;
    return status;
}

NTSTATUS SdioNegotiateOperatingSpeed(PRPI5CYW_ADAPTER A)
{
    UCHAR caps;
    NTSTATUS status;
    if (!A || !A->RegisterBase || A->RegisterLength < 0x100 ||
        KeGetCurrentIrql() != PASSIVE_LEVEL) return STATUS_INVALID_PARAMETER;
    A->BusModeStage = 2;
    if (A->IoStopped) { status = STATUS_INVALID_DEVICE_STATE; goto Failed; }
    /* Do not use the legacy 200 MHz guess for a performance upgrade. */
    if (!(A->Capabilities & SDHCI_CAP_BASE_CLK_MASK)) {
        status = STATUS_DEVICE_CONFIGURATION_ERROR; goto Failed;
    }
    status = SdioCmd52Read(A, 0, CYW_SDIO_CCCR_CAPS, &caps);
    if (!NT_SUCCESS(status)) goto Failed;
    if (caps & CYW_SDIO_CAP_LOW_SPEED) {
        status = STATUS_DEVICE_CONFIGURATION_ERROR; goto Failed;
    }
    A->BusModeStage = 3;
    status = SdioSetDefaultBus(A, TRUE);
    if (!NT_SUCCESS(status)) goto Failed;
    A->BusModeStage = 4;
    status = SdioSetClock(A, CYW_SDIO_OPERATING_CLOCK_KHZ);
    if (!NT_SUCCESS(status)) goto Failed;
    A->BusUpgradeStatus = STATUS_SUCCESS;
    return STATUS_SUCCESS; /* Caller MUST verify real CMD53 data next. */
Failed:
    A->BusUpgradeStatus = status;
    (void)SdioRestoreIdentificationBus(A);
    A->BusModeStage = NT_SUCCESS(A->BusRecoveryStatus) ? 90 : 99;
    return status;
}
