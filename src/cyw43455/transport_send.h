/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Actual SDPCM sender, shared with the mocked transport integration tests. */
static NTSTATUS CywSendFrame(PRPI5CYW_ADAPTER A, UCHAR Channel, PUCHAR Data, ULONG Length)
{
    CYW_NETWORK *N=A->Network;
    ULONG total=Length+12, padded=(total+3)&~3UL;
    NTSTATUS Status;
    if(Length>CYW_CONTROL_CAPACITY-12)return STATUS_INVALID_BUFFER_SIZE;
    if(A->FifoBlockReady && padded>512)padded=(padded+511)&~511UL;
    if(Channel==2) {
        /* Fresh global state before EVERY data frame, including frames in the
         * same four-frame TX pump. RX batching never makes this gate stale. */
        A->Transport.TxStatusChecks++;
        Status=CywTransportService(A,FALSE);
        if(!NT_SUCCESS(Status))return Status;
        if(A->Transport.GlobalFlow || !CywTransportPriorityAllowed(&A->Transport,N->TxFlow))
            return STATUS_DEVICE_BUSY;
    }
    if(A->Transport.Halted)return STATUS_DEVICE_NOT_READY;
    if(!CywTxCredit(N->TxSeq,N->TxMax,0))return STATUS_DEVICE_BUSY;
    RtlZeroMemory(N->Tx,padded);
    CywPut16(N->Tx,(USHORT)total);CywPut16(N->Tx+2,(USHORT)~total);
    N->Tx[4]=N->TxSeq;N->Tx[5]=Channel;N->Tx[7]=12;
    RtlCopyMemory(N->Tx+12,Data,Length);
    Status=SdioFifoTransfer(A,N->Tx,padded,TRUE);
    RtlSecureZeroMemory(N->Tx,padded);
    if(NT_SUCCESS(Status))N->TxSeq++;
    return Status;
}
