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
    A->BusHighSpeedActive=0;
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

NTSTATUS SdioRestoreDefaultOperatingBus(PRPI5CYW_ADAPTER A)
{
    NTSTATUS status;
    if(!A || !A->RegisterBase || A->RegisterLength<0x100 ||
        KeGetCurrentIrql()!=PASSIVE_LEVEL)return STATUS_INVALID_PARAMETER;
    A->BusHighSpeedActive=0;
    A->BusWidth=A->BusActualKhz=A->BusTargetKhz=0;
    /* A timing fallback is not permission to repair an unexpected voltage or
     * UHS-mode change. Lower only the clock, invalidate the bus, stop startup. */
    if(A->BusHighSpeedAttempted &&
        (SdioRead16(A,SDHCI_HOST_CONTROL2)!=(USHORT)A->HostControl2 ||
         SdioRead8(A,SDHCI_POWER_CONTROL)!=(UCHAR)A->PowerControl)) {
        (void)SdioSetClock(A,400);
        A->BusRecoveryStatus=STATUS_DEVICE_CONFIGURATION_ERROR;
        A->BusModeStage=99;
        return A->BusRecoveryStatus;
    }
    status=SdioSetDefaultBus(A,TRUE);
    if(NT_SUCCESS(status))status=SdioSetClock(A,CYW_SDIO_OPERATING_CLOCK_KHZ);
    if(NT_SUCCESS(status)) {
        A->BusRecoveryStatus=STATUS_SUCCESS;A->BusModeStage=4;
        return STATUS_SUCCESS; /* Not validated until real CMD53 readback. */
    }
    (void)SdioRestoreIdentificationBus(A);
    A->BusModeStage=NT_SUCCESS(A->BusRecoveryStatus)?90:99;
    return status; /* Recovered400k is not permission to associate at25MHz. */
}

static NTSTATUS SdioSetHighSpeedBus(PRPI5CYW_ADAPTER A)
{
    UCHAR speed,card,host,expected,power;
    USHORT clock,control2;
    NTSTATUS status;
    /* All prior transfers have completed on the sole bus owner. Never change
     * timing under an inhibited transfer or modify voltage/UHS/preset state. */
    if(A->IoStopped || (SdioRead32(A,SDHCI_PRESENT_STATE)&
        (SDHCI_PS_CMD_INHIBIT|SDHCI_PS_DATA_INHIBIT)))return STATUS_INVALID_DEVICE_STATE;
    control2=SdioRead16(A,SDHCI_HOST_CONTROL2);power=SdioRead8(A,SDHCI_POWER_CONTROL);
    status=SdioSetClock(A,400);if(!NT_SUCCESS(status))return status;
    status=SdioCmd52Read(A,0,CYW_SDIO_CCCR_SPEED,&speed);if(!NT_SUCCESS(status))return status;
    if(!SdioCanUseHighSpeed(A->HostVersion,A->Capabilities,control2,(UCHAR)A->CccrRevision,speed))
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    speed=(UCHAR)((speed&~CYW_SDIO_SPEED_BSS_MASK)|CYW_SDIO_SPEED_ENABLE_HS);
    status=SdioCmd52Write(A,0,CYW_SDIO_CCCR_SPEED,speed,CYW_SDIO_SPEED_BSS_MASK);
    if(!NT_SUCCESS(status))return status;
    if(A->IoStopped)return STATUS_INVALID_DEVICE_STATE;
    host=SdioRead8(A,SDHCI_HOST_CONTROL);
    if((host&(SDHCI_HC_DATA_WIDTH_4BIT|SDHCI_HC_DATA_WIDTH_8BIT))!=SDHCI_HC_DATA_WIDTH_4BIT)
        return STATUS_DEVICE_DATA_ERROR;
    expected=(UCHAR)(host|SDHCI_HC_HIGH_SPEED_ENABLE);
    clock=SdioRead16(A,SDHCI_CLOCK_CONTROL);
    SdioWrite16(A,SDHCI_CLOCK_CONTROL,(USHORT)(clock&~SDHCI_CLK_SD_CLK_ENABLE));
    SdioWrite8(A,SDHCI_HOST_CONTROL,expected);
    A->HostControl=SdioRead8(A,SDHCI_HOST_CONTROL);
    if(A->HostControl!=expected)return STATUS_DEVICE_DATA_ERROR;
    status=SdioSetClock(A,CYW_SDIO_HIGH_SPEED_CLOCK_KHZ);if(!NT_SUCCESS(status))return status;
    status=SdioCmd52Read(A,0,CYW_SDIO_CCCR_SPEED,&speed);if(!NT_SUCCESS(status))return status;
    status=SdioCmd52Read(A,0,CYW_SDIO_CCCR_BUS_INTERFACE,&card);if(!NT_SUCCESS(status))return status;
    if((speed&(CYW_SDIO_SPEED_BSS_MASK|CYW_SDIO_SPEED_SUPPORTS_HS))!=
            (CYW_SDIO_SPEED_ENABLE_HS|CYW_SDIO_SPEED_SUPPORTS_HS) ||
        (card&CYW_SDIO_BUS_WIDTH_MASK)!=CYW_SDIO_BUS_WIDTH_4BIT ||
        SdioRead8(A,SDHCI_HOST_CONTROL)!=expected ||
        SdioRead16(A,SDHCI_HOST_CONTROL2)!=control2 || SdioRead8(A,SDHCI_POWER_CONTROL)!=power ||
        A->BusActualKhz<=CYW_SDIO_OPERATING_CLOCK_KHZ || A->BusActualKhz>CYW_SDIO_HIGH_SPEED_CLOCK_KHZ)
        return STATUS_DEVICE_DATA_ERROR;
    A->BusCardSpeed=speed;A->BusCardInterface=card;A->HostControl2=control2;
    A->BusHighSpeedActive=1;
    return STATUS_SUCCESS;
}

NTSTATUS SdioNegotiateOperatingSpeed(PRPI5CYW_ADAPTER A)
{
    UCHAR caps;
    NTSTATUS status;
    if (!A || !A->RegisterBase || A->RegisterLength < 0x100 ||
        KeGetCurrentIrql() != PASSIVE_LEVEL) return STATUS_INVALID_PARAMETER;
    A->BusModeStage = 2;
    A->BusHighSpeedEligible=A->BusHighSpeedAttempted=A->BusHighSpeedActive=0;
    A->BusHighSpeedRejectMask=0;
    A->BusHighSpeedStatus=(NTSTATUS)0xc00000bbL; /* STATUS_NOT_SUPPORTED until eligible. */
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
    A->HostControl2=SdioRead16(A,SDHCI_HOST_CONTROL2);
    A->PowerControl=SdioRead8(A,SDHCI_POWER_CONTROL);
    A->BusHighSpeedRejectMask=SdioHighSpeedRejectReason(A->HostVersion,A->Capabilities,
        (USHORT)A->HostControl2,(UCHAR)A->CccrRevision,(UCHAR)A->BusCardSpeed);
    if(A->BusHighSpeedRejectMask)return STATUS_SUCCESS;
    A->BusHighSpeedEligible=A->BusHighSpeedAttempted=1;
    status=SdioSetHighSpeedBus(A);A->BusHighSpeedStatus=status;
    if(!NT_SUCCESS(status)) {
        status=SdioRestoreDefaultOperatingBus(A);
        if(!NT_SUCCESS(status))A->BusUpgradeStatus=status;
        return status;
    }
    return STATUS_SUCCESS; /* Caller MUST verify real CMD53 data next. */
Failed:
    A->BusUpgradeStatus = status;
    (void)SdioRestoreIdentificationBus(A);
    A->BusModeStage = NT_SUCCESS(A->BusRecoveryStatus) ? 90 : 99;
    return status;
}
