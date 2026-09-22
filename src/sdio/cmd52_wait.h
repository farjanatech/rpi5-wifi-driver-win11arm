/* Runtime CMD52 only. The verified operating bus should finish a short
 * command within microseconds. Bound fast polling to 50 us, then yield the
 * PASSIVE worker with the original one-second command timeout. No command
 * retry, response fabrication, clock change or relaxed error handling. */
static NTSTATUS SdioWaitRuntimeCmd52(PRPI5CYW_ADAPTER A,PULONG InterruptStatus)
{
    ULONG fast=0,waits=0;
    ULONG64 deadline=KeQueryInterruptTime()+10000000ULL;
    A->RuntimeCmd52Commands++;
    for (;;) {
        if(A->IoStopped)return STATUS_INVALID_DEVICE_STATE;
        if(KeQueryInterruptTime()>=deadline || waits>=1000) {
            A->RuntimeCmd52Timeouts++;return STATUS_IO_TIMEOUT;
        }
        *InterruptStatus=SdioRead32(A,SDHCI_INT_STATUS);
        if(*InterruptStatus & (SDHCI_INT_CMD_COMPLETE | SDHCI_INT_ERROR | SDHCI_INT_CMD_ERROR_MASK))
            return STATUS_SUCCESS; /* Caller validates errors/R5 normally. */
        if(fast<5) {
            KeStallExecutionProcessor(10);fast++;A->RuntimeCmd52FastPolls++;
        } else {
            SdioDelayMilliseconds(1);waits++;A->RuntimeCmd52WaitSleeps++;
        }
    }
}
