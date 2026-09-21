/* Actual single-worker I/O scheduling, also compiled in host simulations.
 * Keep four-frame/2ms RX fairness and four-frame boundary TX bursts. After
 * each complete RX frame (whose header updates credits), offer two pending
 * TX frames immediately. No new SDIO owner, spin or receive-buffer lifetime. */
static NTSTATUS CywServiceIo(PRPI5CYW_ADAPTER A,CYW_NETWORK *N,PULONG Sent,PULONG Received)
{
    ULONG sent,channel,off,len;
    ULONG64 start;
    NTSTATUS status;
    *Sent=*Received=0;
    if(N->Stop || N->Paused)return STATUS_SUCCESS;
    status=CywTxPump(A,&N->Sends,4,&sent);*Sent+=sent;
    if(!NT_SUCCESS(status))return status;
    start=KeQueryInterruptTime();
    while(CywReceiveBudget(*Received,KeQueryInterruptTime()-start) && !N->Stop && !N->Paused) {
        status=CywPoll(A,&channel,&off,&len);
        if(status==STATUS_NO_MORE_ENTRIES)break;
        if(!NT_SUCCESS(status))return status;
        (*Received)++;
        if(N->Stop || N->Paused)break;
        status=CywTxPump(A,&N->Sends,2,&sent);
        *Sent+=sent;A->TxInterleavedPackets+=sent;
        if(!NT_SUCCESS(status))return status;
    }
    if(*Received && !CywReceiveBudget(*Received,KeQueryInterruptTime()-start))A->RxBatchYields++;
    if(N->Stop || N->Paused)return STATUS_SUCCESS;
    status=CywTxPump(A,&N->Sends,4,&sent);*Sent+=sent;
    return status;
}
