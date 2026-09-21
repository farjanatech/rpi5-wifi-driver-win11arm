/* Shared with the host ownership tests. Send routine owns each detached NBL
 * until either submit returns non-pending or the worker completes it. */
static VOID CywDispatchSendChain(PRPI5CYW_ADAPTER A,PNET_BUFFER_LIST Chain,ULONG Flags)
{
    PNET_BUFFER_LIST nbl,next;NDIS_STATUS status;
    ULONG completeFlags=NDIS_TEST_SEND_AT_DISPATCH_LEVEL(Flags)?NDIS_SEND_COMPLETE_FLAGS_DISPATCH_LEVEL:0;
    for(nbl=Chain;nbl;nbl=next) {
        next=NET_BUFFER_LIST_NEXT_NBL(nbl);NET_BUFFER_LIST_NEXT_NBL(nbl)=NULL;
        status=CywNetworkSend(A,nbl);
        if(status!=NDIS_STATUS_PENDING) {
            PNET_BUFFER nb;ULONG frames=0;
            for(nb=NET_BUFFER_LIST_FIRST_NB(nbl);nb;nb=NET_BUFFER_NEXT_NB(nb))frames++;
            Rpi5CywTrafficDrop(A,TRUE,frames,status==NDIS_STATUS_INVALID_LENGTH || status==NDIS_STATUS_FAILURE);
            NET_BUFFER_LIST_STATUS(nbl)=status;A->TxErrors++;
            NdisMSendNetBufferListsComplete(A->MiniportHandle,nbl,completeFlags);
        }
        /* A pending nbl may already be returned/reused: no access here. */
    }
}
