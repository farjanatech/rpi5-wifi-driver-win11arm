/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Production setup, shared with fault-injection tests. Device TX = host RX;
 * device RX/host TX aggregation remains disabled. */
static NTSTATUS CywConfigureRxAggregation(PRPI5CYW_ADAPTER A)
{
    NTSTATUS status;
    A->RxGlomEnabled=0;
    status=CywInt(A,"bus:txglom",0);if(!NT_SUCCESS(status))return status;
    status=CywInt(A,"bus:rxglom",0);if(!NT_SUCCESS(status))return status;
    if(!A->FifoBlockReady)return STATUS_SUCCESS;
    status=CywInt(A,"bus:txglomalign",4);
    if(NT_SUCCESS(status)) {
        /* Parser must be ready before the SET is accepted by firmware. */
        A->RxGlomEnabled=1;
        status=CywInt(A,"bus:txglom",1);
    }
    if(NT_SUCCESS(status))return status;
    A->RxGlomEnabled=0;
    if(status==STATUS_UNSUCCESSFUL && A->FirmwareError==0xffffffe9UL)
        return CywInt(A,"bus:txglom",0); /* explicit UNSUPPORTED only */
    return status; /* Never hide a timeout, corrupted reply or BADARG. */
}
