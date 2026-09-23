/* SPDX-License-Identifier: GPL-3.0-or-later
 * Firmware-start-only acceleration. Never called by the connected worker.
 * Reuses the tested default 4-bit/25MHz bus setup and existing CMD53 engine.
 * No larger F1 transfers, DMA, voltage/UHS changes or skipped RAM verification.
 */
static NTSTATUS CywStartupProbeBus(PRPI5CYW_ADAPTER A)
{
    ULONG i,value;NTSTATUS status;
    A->BpWindowValid=0;A->BusModeStage=5;
    for(i=0;i<16;++i) {
        if(CywNetworkCancelled(A))return STATUS_CANCELLED;
        status=CywBpRead(A,A->ChipCommonBase,&value);
        if(!NT_SUCCESS(status))return status;
        if(value!=A->ChipIdRaw)return STATUS_DEVICE_DATA_ERROR;
    }
    return STATUS_SUCCESS;
}
static NTSTATUS CywPrepareFirmwareBus(PRPI5CYW_ADAPTER A)
{
    UCHAR caps=0;NTSTATUS status;
    A->FirmwareStartupBusKhz=0;A->FirmwareStartupFallback=0;
    A->FirmwareStartupStatus=STATUS_SUCCESS;A->FirmwareStartupElapsedMs=0;
    if(CywNetworkCancelled(A))return STATUS_CANCELLED;
    /* Full-speed capability and an explicit host base clock are required;
     * never use the old 200MHz guessed divider to accelerate startup. */
    status=SdioCmd52Read(A,0,CYW_SDIO_CCCR_CAPS,&caps);
    if(NT_SUCCESS(status) && (!(A->Capabilities&SDHCI_CAP_BASE_CLK_MASK) ||
        (caps&CYW_SDIO_CAP_LOW_SPEED)))status=STATUS_DEVICE_CONFIGURATION_ERROR;
    if(NT_SUCCESS(status))status=SdioRestoreDefaultOperatingBus(A);
    if(NT_SUCCESS(status) && (A->BusWidth!=4 || A->BusActualKhz<=400 || A->BusActualKhz>25000 || A->BusHighSpeedActive))
        status=STATUS_DEVICE_DATA_ERROR;
    if(NT_SUCCESS(status))status=CywStartupProbeBus(A);
    A->FirmwareStartupStatus=status;
    if(NT_SUCCESS(status)) {
        /* Only verified default mode uses the existing bounded fast polls.
         * Post-firmware high-speed negotiation is still performed separately. */
        A->BusModeStage=6;A->FirmwareStartupBusKhz=A->BusActualKhz;
        return STATUS_SUCCESS;
    }
    if(CywNetworkCancelled(A) || status==STATUS_CANCELLED)return STATUS_CANCELLED;
    if(A->BusModeStage==99)return status; /* Unsafe voltage/recovery state is terminal. */
    A->FirmwareStartupFallback=1;
    status=SdioRestoreIdentificationBus(A);
    if(NT_SUCCESS(status))status=CywStartupProbeBus(A);
    if(!NT_SUCCESS(status)) {
        A->BusRecoveryStatus=status;A->BusModeStage=99;return status;
    }
    A->BusModeStage=1;A->BpWindowValid=0;A->FirmwareStartupBusKhz=A->BusActualKhz;
    return STATUS_SUCCESS;
}
static NTSTATUS CywFinishFirmwareBus(PRPI5CYW_ADAPTER A)
{
    NTSTATUS status;
    if(CywNetworkCancelled(A))return STATUS_CANCELLED;
    /* Restore the original mode before NVRAM/vector/CPU-start. The complete
     * running-firmware bus negotiation and traffic path stay unchanged. */
    status=SdioRestoreIdentificationBus(A);A->BpWindowValid=0;
    if(NT_SUCCESS(status))status=CywStartupProbeBus(A);
    if(!NT_SUCCESS(status)) {A->BusRecoveryStatus=status;A->BusModeStage=99;return status;}
    A->BusModeStage=1;return STATUS_SUCCESS;
}
