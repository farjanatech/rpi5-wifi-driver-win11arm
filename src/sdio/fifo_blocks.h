/* SPDX-License-Identifier: GPL-3.0-or-later
 * Bounded F2 block-mode PIO. Protocol reference: Raspberry Pi Linux
 * 8e8c079 brcmfmac/bcmsdh.c and SDHCI; see THIRD_PARTY_NOTICES.md.
 * The sole PASSIVE_LEVEL bus owner calls this. Never retry a FIFO request:
 * after a partial transfer neither RX nor TX position can be reconstructed.
 * F1 RAM upload, legacy byte transfers, voltage and clocks are unchanged.
 */
#define CYW_FIFO_BLOCK_SIZE 512UL
#define CYW_FIFO_MAX_BLOCKS 32UL

NTSTATUS SdioPrepareRuntimeFifo(PRPI5CYW_ADAPTER A)
{
    UCHAR caps,lo,hi;
    NTSTATUS status;
    A->FifoBlockReady=0;
    if(A->BusModeStage!=6 || A->BusWidth!=4 || A->BusActualKhz<=400)
        return STATUS_INVALID_DEVICE_STATE;
    status=SdioCmd52Read(A,0,CYW_SDIO_CCCR_CAPS,&caps);
    if(!NT_SUCCESS(status))return status;
    if(!(caps&2))return STATUS_SUCCESS; /* Card does not advertise SMB. */
    status=SdioCmd52Read(A,0,0x210,&lo);if(!NT_SUCCESS(status))return status;
    status=SdioCmd52Read(A,0,0x211,&hi);if(!NT_SUCCESS(status))return status;
    if(lo!=0 || hi!=2)return STATUS_DEVICE_CONFIGURATION_ERROR;
    A->FifoBlockReady=1;
    return STATUS_SUCCESS;
}

static NTSTATUS SdioFifoWait(PRPI5CYW_ADAPTER A,ULONG Event,ULONG64 Deadline)
{
    ULONG poll,status;
    for(poll=0;poll<254;++poll) {
        if(A->IoStopped)return STATUS_INVALID_DEVICE_STATE;
        status=SdioRead32(A,SDHCI_INT_STATUS);A->LastInterruptStatus=status;
        if(status&(SDHCI_INT_ERROR|SDHCI_INT_CMD_ERROR_MASK|SDHCI_INT_DATA_ERROR_MASK))
            return STATUS_IO_DEVICE_ERROR;
        if(KeQueryInterruptTime()>=Deadline)break;
        if(status&Event)return STATUS_SUCCESS;
        /* Completing before the requested block is available is a short
         * transfer, not permission to consume uninitialised FIFO words. */
        if(Event!=SDHCI_INT_XFER_COMPLETE && (status&SDHCI_INT_XFER_COMPLETE))
            return STATUS_DEVICE_DATA_ERROR;
        if(poll<5) {KeStallExecutionProcessor(10);A->Cmd53FastPolls++;}
        else {SdioDelayMilliseconds(1);A->Cmd53WaitSleeps++;}
    }
    A->Cmd53Timeouts++;
    return STATUS_IO_TIMEOUT;
}

static NTSTATUS SdioFifoBlocksRaw(PRPI5CYW_ADAPTER A,PUCHAR Buffer,ULONG Blocks,BOOLEAN Write)
{
    ULONG block,offset,word,ready,length;
    ULONG64 deadline;
    NTSTATUS status;
    if(!A || !A->RegisterBase || !Buffer || !A->FifoBlockReady ||
       !Blocks || Blocks>CYW_FIFO_MAX_BLOCKS || KeGetCurrentIrql()!=PASSIVE_LEVEL)
        return STATUS_INVALID_PARAMETER;
    if(A->IoStopped)return STATUS_INVALID_DEVICE_STATE;
    length=Blocks*CYW_FIFO_BLOCK_SIZE;
    if(!Write)RtlZeroMemory(Buffer,length);
    A->Cmd53BytesTransferred=0;A->Cmd53ResetStatus=STATUS_SUCCESS;
    status=SdioWaitInhibitClear(A,SDHCI_PS_CMD_INHIBIT|SDHCI_PS_DATA_INHIBIT);
    if(!NT_SUCCESS(status))goto Failed;
    A->LastCommand=SDCMD_IO_RW_EXTENDED;
    /* F2 is a FIFO: a block-mode command uses a fixed address, like Linux's
     * sglist path. Legacy byte-mode TX addressing is left untouched. */
    A->LastArgument=SdioBuildCmd53Argument(Write,2,TRUE,FALSE,0x8000,Blocks);
    A->LastResponse=0;
    SdioWrite32(A,SDHCI_INT_STATUS,SDHCI_INT_ALL_MASK);
    SdioWrite16(A,SDHCI_BLOCK_SIZE,CYW_FIFO_BLOCK_SIZE);
    SdioWrite16(A,SDHCI_BLOCK_COUNT,(USHORT)Blocks);
    SdioWrite16(A,SDHCI_TRANSFER_MODE,(USHORT)(SDHCI_TRNS_BLOCK_COUNT_EN|
        (Blocks>1?SDHCI_TRNS_MULTI:0)|(Write?0:SDHCI_TRNS_READ)));
    SdioWrite32(A,SDHCI_ARGUMENT,A->LastArgument);
    KeMemoryBarrier();
    SdioWrite16(A,SDHCI_COMMAND,SDHCI_MAKE_CMD(53,SDHCI_CMD_RESP_48|
        SDHCI_CMD_CRC_CHECK|SDHCI_CMD_INDEX_CHECK|SDHCI_CMD_DATA_PRESENT));
    /* One absolute deadline for the entire command; per-block progress
     * cannot turn a failed request into an unbounded wait. */
    deadline=KeQueryInterruptTime()+2500000ULL;
    status=SdioFifoWait(A,SDHCI_INT_CMD_COMPLETE,deadline);
    if(!NT_SUCCESS(status))goto Failed;
    A->LastResponse=SdioRead32(A,SDHCI_RESPONSE0);
    if(SdioR5HasError(A->LastResponse)) {status=STATUS_IO_DEVICE_ERROR;goto Failed;}
    SdioWrite32(A,SDHCI_INT_STATUS,SDHCI_INT_CMD_COMPLETE);
    ready=Write?SDHCI_INT_BUFFER_WRITE_READY:SDHCI_INT_BUFFER_READ_READY;
    for(block=0;block<Blocks;++block) {
        status=SdioFifoWait(A,ready,deadline);if(!NT_SUCCESS(status))goto Failed;
        SdioWrite32(A,SDHCI_INT_STATUS,ready);
        for(offset=0;offset<CYW_FIFO_BLOCK_SIZE;offset+=4) {
            ULONG pos=block*CYW_FIFO_BLOCK_SIZE+offset;
            if(A->IoStopped) {status=STATUS_INVALID_DEVICE_STATE;goto Failed;}
            if(Write)SdioWrite32(A,SDHCI_BUFFER,SdioLoadLe32(Buffer+pos));
            else {
                word=SdioRead32(A,SDHCI_BUFFER);
                Buffer[pos]=(UCHAR)word;Buffer[pos+1]=(UCHAR)(word>>8);
                Buffer[pos+2]=(UCHAR)(word>>16);Buffer[pos+3]=(UCHAR)(word>>24);
            }
        }
        A->Cmd53BytesTransferred+=CYW_FIFO_BLOCK_SIZE;
    }
    status=SdioFifoWait(A,SDHCI_INT_XFER_COMPLETE,deadline);
    if(!NT_SUCCESS(status))goto Failed;
    SdioWrite32(A,SDHCI_INT_STATUS,SDHCI_INT_XFER_COMPLETE);
    if(Write)A->Cmd53WriteCount++;else A->Cmd53ReadCount++;
    A->FifoBlockCommands++;A->FifoBlockBytes+=length;
    return STATUS_SUCCESS;
Failed:
    A->FifoBlockFailures++;A->FifoBlockReady=0;
    A->Cmd53ResetStatus=SdioResetHost(A,SDHCI_RESET_CMD|SDHCI_RESET_DATA);
    SdioWrite32(A,SDHCI_INT_STATUS,SDHCI_INT_ALL_MASK);
    if(!Write)RtlZeroMemory(Buffer,length);
    return status;
}

static NTSTATUS SdioFifoBlocks(PRPI5CYW_ADAPTER A,PUCHAR Buffer,ULONG Blocks,BOOLEAN Write)
{
    unsigned bucket=Write?CywTimeCmd53Tx:CywTimeCmd53Rx;
    CYW_TIMING_U64 start=A?CywTimingBeginBucket(&A->Timing,bucket):0;
    NTSTATUS status=SdioFifoBlocksRaw(A,Buffer,Blocks,Write);
    if(A)CywTimingEnd(&A->Timing,bucket,start);
    return status;
}
