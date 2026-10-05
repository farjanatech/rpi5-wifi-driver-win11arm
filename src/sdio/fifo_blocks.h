/* SPDX-License-Identifier: GPL-3.0-or-later
 * Bounded F2 block-mode PIO. Protocol reference: Raspberry Pi Linux
 * 8e8c079 brcmfmac/bcmsdh.c and SDHCI; see THIRD_PARTY_NOTICES.md.
 * The sole PASSIVE_LEVEL bus owner calls this. Never retry a FIFO request:
 * after a partial transfer neither RX nor TX position can be reconstructed.
 * F1 RAM upload, legacy byte transfers, voltage and clocks are unchanged.
 */
#define CYW_FIFO_BLOCK_SIZE 512UL
#define CYW_FIFO_MAX_BLOCKS 32UL
#define CYW_FIFO_BLOCK_WORDS (CYW_FIFO_BLOCK_SIZE/sizeof(ULONG))
#define CYW_FIFO_PIO_BURST_WORDS RPI5CYW_FIFO_BUFFER_PIO_BURST_WORDS
#define CYW_FIFO_PIO_BURSTS_PER_BLOCK (CYW_FIFO_BLOCK_WORDS/CYW_FIFO_PIO_BURST_WORDS)

C_ASSERT(CYW_FIFO_BLOCK_WORDS==128);
C_ASSERT(CYW_FIFO_PIO_BURSTS_PER_BLOCK==4);

static BOOLEAN SdioFifoBufferAligned(PUCHAR Buffer)
{
#ifdef RPI5CYW_HOST_TEST
    return (BOOLEAN)(((uintptr_t)Buffer & (sizeof(ULONG)-1))==0);
#else
    return (BOOLEAN)(((ULONG_PTR)Buffer & (sizeof(ULONG)-1))==0);
#endif
}

/* Use the kernel's register-buffer primitives in four 128-byte bursts per
 * 512-byte SDHCI FIFO block. This cuts mapped-register API/barrier traffic
 * from 128 scalar calls to four bounded calls while preserving an IoStopped
 * check every 128 bytes. Unaligned callers retain the exact scalar path. */
static NTSTATUS SdioFifoMoveBlock(PRPI5CYW_ADAPTER A,PUCHAR Buffer,BOOLEAN Write)
{
    ULONG offset,word,byte;
    if(!A || !Buffer)return STATUS_INVALID_PARAMETER;
#if RPI5CYW_FIFO_BUFFER_PIO
    if(SdioFifoBufferAligned(Buffer)) {
        for(offset=0;offset<CYW_FIFO_BLOCK_SIZE;
            offset+=CYW_FIFO_PIO_BURST_WORDS*sizeof(ULONG)) {
            if(A->IoStopped)return STATUS_INVALID_DEVICE_STATE;
#ifndef RPI5CYW_HOST_TEST
            if(Write) {
                WRITE_REGISTER_BUFFER_ULONG(
                    (volatile ULONG *)((PUCHAR)A->RegisterBase+SDHCI_BUFFER),
                    (PULONG)(Buffer+offset),CYW_FIFO_PIO_BURST_WORDS);
            } else {
                READ_REGISTER_BUFFER_ULONG(
                    (volatile ULONG *)((PUCHAR)A->RegisterBase+SDHCI_BUFFER),
                    (PULONG)(Buffer+offset),CYW_FIFO_PIO_BURST_WORDS);
            }
#else
            /* Host register model has scalar hooks; emulate the same 32-word
             * bounded burst so protocol/fault tests remain deterministic. */
            for(word=0;word<CYW_FIFO_PIO_BURST_WORDS;++word) {
                ULONG pos=offset+word*sizeof(ULONG);
                if(Write)SdioWrite32(A,SDHCI_BUFFER,SdioLoadLe32(Buffer+pos));
                else {
                    ULONG value=SdioRead32(A,SDHCI_BUFFER);
                    Buffer[pos]=(UCHAR)value;Buffer[pos+1]=(UCHAR)(value>>8);
                    Buffer[pos+2]=(UCHAR)(value>>16);Buffer[pos+3]=(UCHAR)(value>>24);
                }
            }
#endif
        }
        if(Write)A->FifoBufferPioWriteBlocks++;
        else A->FifoBufferPioReadBlocks++;
        return STATUS_SUCCESS;
    }
#endif
    A->FifoScalarPioBlocks++;
    for(offset=0;offset<CYW_FIFO_BLOCK_SIZE;offset+=4) {
        if(A->IoStopped)return STATUS_INVALID_DEVICE_STATE;
        if(Write)SdioWrite32(A,SDHCI_BUFFER,SdioLoadLe32(Buffer+offset));
        else {
            word=SdioRead32(A,SDHCI_BUFFER);
            for(byte=0;byte<4;++byte)Buffer[offset+byte]=(UCHAR)(word>>(byte*8));
        }
    }
    return STATUS_SUCCESS;
}

NTSTATUS SdioPrepareRuntimeFifo(PRPI5CYW_ADAPTER A)
{
    UCHAR caps,lo,hi;
    NTSTATUS status;
    if(!A)return STATUS_INVALID_PARAMETER;
    A->FifoBlockReady=0;
    if(A->BusModeStage!=6 || A->BusWidth!=4 || A->BusActualKhz<=400)
        return STATUS_INVALID_DEVICE_STATE;
    status=SdioCmd52Read(A,0,CYW_SDIO_CCCR_CAPS,&caps);
    if(!NT_SUCCESS(status))return status;
    if(!(caps&2)) {A->FifoTransportFailed=0;return STATUS_SUCCESS;} /* no SMB */
    status=SdioCmd52Read(A,0,0x210,&lo);if(!NT_SUCCESS(status))return status;
    status=SdioCmd52Read(A,0,0x211,&hi);if(!NT_SUCCESS(status))return status;
    if(lo!=0 || hi!=2)return STATUS_DEVICE_CONFIGURATION_ERROR;
    A->FifoBlockReady=1;A->FifoTransportFailed=0;
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
        /* Buffer-ready interrupts may coalesce across blocks. PRESENT_STATE
         * is level state, as used by Linux sdhci_transfer_pio; consume exactly
         * one block per readiness observation, with errors checked first.
         * A stale latched event alone must not authorize another FIFO block. */
        if(Event==SDHCI_INT_BUFFER_READ_READY) {
            if(SdioRead32(A,SDHCI_PRESENT_STATE)&SDHCI_PS_DATA_AVAILABLE)return STATUS_SUCCESS;
        } else if(Event==SDHCI_INT_BUFFER_WRITE_READY) {
            if(SdioRead32(A,SDHCI_PRESENT_STATE)&SDHCI_PS_SPACE_AVAILABLE)return STATUS_SUCCESS;
        } else if(status&Event)return STATUS_SUCCESS;
        /* Completing before the requested block is available is a short
         * transfer, not permission to consume uninitialised FIFO words. */
        if(Event!=SDHCI_INT_XFER_COMPLETE && (status&SDHCI_INT_XFER_COMPLETE))
            return STATUS_DEVICE_DATA_ERROR;
        if(poll<5) {KeStallExecutionProcessor(10);A->Cmd53FastPolls++;}
        else {
            ULONG phase=Event==SDHCI_INT_CMD_COMPLETE?0:(Event==SDHCI_INT_XFER_COMPLETE?2:1);
            ULONG64 start=KeQueryInterruptTime();
            SdioDelayMilliseconds(1);A->Cmd53WaitSleeps++;
            A->RuntimeF2WaitSleeps++;A->RuntimeCmd53SleepPhase[phase]++;
            A->RuntimeCmd53Sleep100ns+=KeQueryInterruptTime()-start;
        }
    }
    A->Cmd53Timeouts++;
    return STATUS_IO_TIMEOUT;
}

static NTSTATUS SdioFifoBlocksRaw(PRPI5CYW_ADAPTER A,PUCHAR Buffer,ULONG Blocks,BOOLEAN Write)
{
    ULONG block,ready,length;
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
        status=SdioFifoMoveBlock(A,Buffer+block*CYW_FIFO_BLOCK_SIZE,Write);
        if(!NT_SUCCESS(status))goto Failed;
        A->Cmd53BytesTransferred+=CYW_FIFO_BLOCK_SIZE;
    }
    status=SdioFifoWait(A,SDHCI_INT_XFER_COMPLETE,deadline);
    if(!NT_SUCCESS(status))goto Failed;
    SdioWrite32(A,SDHCI_INT_STATUS,SDHCI_INT_XFER_COMPLETE);
    if(Write)A->Cmd53WriteCount++;else A->Cmd53ReadCount++;
    A->FifoBlockCommands++;A->FifoBlockBytes+=length;
    return STATUS_SUCCESS;
Failed:
    A->FifoBlockFailures++;A->FifoBlockReady=0;A->FifoTransportFailed=1;
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
