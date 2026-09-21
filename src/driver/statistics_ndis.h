/* Included by driver.c, after Rpi5CywCopyQuery. Never accesses hardware. */
VOID Rpi5CywTrafficFrame(PRPI5CYW_ADAPTER A,BOOLEAN Tx,PUCHAR Data,ULONG Length)
{
    KIRQL irql;KeAcquireSpinLock(&A->TrafficLock,&irql);
    CywTrafficFrame(&A->Traffic,Tx,Data,Length);
    KeReleaseSpinLock(&A->TrafficLock,irql);
}
VOID Rpi5CywTrafficDrop(PRPI5CYW_ADAPTER A,BOOLEAN Tx,ULONG Frames,BOOLEAN Error)
{
    KIRQL irql;KeAcquireSpinLock(&A->TrafficLock,&irql);
    if(Error)A->Traffic.Errors[Tx]+=Frames;else A->Traffic.Discards[Tx]+=Frames;
    KeReleaseSpinLock(&A->TrafficLock,irql);
}
static VOID CywTrafficSnapshot(PRPI5CYW_ADAPTER A,CYW_TRAFFIC_STATS *Out)
{
    KIRQL irql;KeAcquireSpinLock(&A->TrafficLock,&irql);
    *Out=A->Traffic;KeReleaseSpinLock(&A->TrafficLock,irql);
}
static VOID CywNdisStatistics(const CYW_TRAFFIC_STATS *s,NDIS_STATISTICS_INFO *v)
{
    RtlZeroMemory(v,sizeof(*v));
    v->Header.Type=NDIS_OBJECT_TYPE_DEFAULT;v->Header.Revision=NDIS_STATISTICS_INFO_REVISION_1;
    v->Header.Size=NDIS_SIZEOF_STATISTICS_INFO_REVISION_1;
    v->SupportedStatistics=CYW_STATISTICS_VALID;
    v->ifHCInOctets=CywTrafficTotal(s->Bytes[0]);v->ifHCOutOctets=CywTrafficTotal(s->Bytes[1]);
    v->ifHCInUcastPkts=s->Frames[0][0];v->ifHCInMulticastPkts=s->Frames[0][1];v->ifHCInBroadcastPkts=s->Frames[0][2];
    v->ifHCOutUcastPkts=s->Frames[1][0];v->ifHCOutMulticastPkts=s->Frames[1][1];v->ifHCOutBroadcastPkts=s->Frames[1][2];
    v->ifHCInUcastOctets=s->Bytes[0][0];v->ifHCInMulticastOctets=s->Bytes[0][1];v->ifHCInBroadcastOctets=s->Bytes[0][2];
    v->ifHCOutUcastOctets=s->Bytes[1][0];v->ifHCOutMulticastOctets=s->Bytes[1][1];v->ifHCOutBroadcastOctets=s->Bytes[1][2];
    v->ifInDiscards=s->Discards[0];v->ifOutDiscards=s->Discards[1];
    v->ifInErrors=s->Errors[0];v->ifOutErrors=s->Errors[1];
}
