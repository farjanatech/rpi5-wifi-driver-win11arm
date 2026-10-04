/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Host TX glom negotiation. Firmware names bus directions from the device
 * perspective: bus:rxglom enables host-to-device aggregated SDPCM chains.
 * v0.7.1.10 uses only two-frame chains and falls back to the v0.7.1.9 path
 * when firmware explicitly reports UNSUPPORTED. Ambiguous transport/firmware
 * failures remain fatal so we never continue with an unknown packet format.
 */
static VOID CywTxGlom2ResetProtocol(PRPI5CYW_ADAPTER A)
{
    /* Fresh firmware always starts in the legacy 12-byte host TX framing.
     * Clear negotiated state before CLM/WLC/IOVAR traffic on every worker life. */
    A->TxGlomEnabled=0;
    A->TxGlomConfigStatus=STATUS_SUCCESS;
}
static NTSTATUS CywConfigureTxGlom2(PRPI5CYW_ADAPTER A)
{
    A->TxGlomRequested=RPI5CYW_TX_GLOM2?1u:0u;
    A->TxGlomConfigStatus=STATUS_SUCCESS;
#if RPI5CYW_TX_GLOM2
    NTSTATUS status=CywInt(A,"bus:rxglom",1);
    A->TxGlomConfigStatus=status;
    if(NT_SUCCESS(status)) {
        A->TxGlomEnabled=1;
        return STATUS_SUCCESS;
    }
    /* Same explicit firmware UNSUPPORTED evidence already accepted by the RX
     * aggregation setup. Anything else could mean the SET reached firmware but
     * its reply was lost/corrupted, so fail closed and reload on next start. */
    if(status==STATUS_UNSUCCESSFUL && A->FirmwareError==0xffffffe9UL) {
        A->TxGlomEnabled=0;
        return STATUS_SUCCESS;
    }
    return status;
#else
    return STATUS_SUCCESS;
#endif
}
