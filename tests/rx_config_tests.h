static unsigned ConfigCall,ConfigFailAt,ConfigFailDisable;
static NTSTATUS ConfigFailure;
static ULONG ConfigFirmwareError;
static NTSTATUS CywInt(PRPI5CYW_ADAPTER A,const char *name,ULONG value)
{
    ++ConfigCall;
    if(ConfigCall==1)CHECK(!strcmp(name,"bus:txglom") && value==0 && !A->RxGlomEnabled);
    if(ConfigCall==2)CHECK(!strcmp(name,"bus:rxglom") && value==0);
    if(ConfigCall==3)CHECK(!strcmp(name,"bus:txglomalign") && value==4);
    if(ConfigCall==4 && ConfigFailAt!=3)CHECK(!strcmp(name,"bus:txglom") && value==1 && A->RxGlomEnabled);
    if(ConfigCall==5 || (ConfigCall==4 && ConfigFailAt==3)) {
        CHECK(!strcmp(name,"bus:txglom") && value==0 && !A->RxGlomEnabled);
        if(ConfigFailDisable)return STATUS_IO_DEVICE_ERROR;
    }
    A->FirmwareError=ConfigCall==ConfigFailAt?ConfigFirmwareError:0;
    return ConfigCall==ConfigFailAt?ConfigFailure:STATUS_SUCCESS;
}
#include "../src/cyw43455/rx_config.h"
static void InitConfig(void)
{
    Init();TestAdapter.FifoBlockReady=1;ConfigCall=ConfigFailAt=ConfigFailDisable=0;
    ConfigFailure=STATUS_IO_DEVICE_ERROR;ConfigFirmwareError=0;
}
static void TestRxConfig(void)
{
    unsigned i;
    InitConfig();CHECK(CywConfigureRxAggregation(&TestAdapter)==0 && TestAdapter.RxGlomEnabled && ConfigCall==4);
    InitConfig();TestAdapter.FifoBlockReady=0;
    CHECK(CywConfigureRxAggregation(&TestAdapter)==0 && !TestAdapter.RxGlomEnabled && ConfigCall==2);
    for(i=1;i<=4;++i) {
        InitConfig();ConfigFailAt=i;
        CHECK(CywConfigureRxAggregation(&TestAdapter)==STATUS_IO_DEVICE_ERROR && !TestAdapter.RxGlomEnabled && ConfigCall==i);
    }
    for(i=3;i<=4;++i) {
        InitConfig();ConfigFailAt=i;ConfigFailure=STATUS_UNSUCCESSFUL;ConfigFirmwareError=0xffffffe9UL;
        CHECK(CywConfigureRxAggregation(&TestAdapter)==0 && !TestAdapter.RxGlomEnabled && ConfigCall==i+1);
        InitConfig();ConfigFailAt=i;ConfigFailure=STATUS_UNSUCCESSFUL;ConfigFirmwareError=0xffffffe9UL;ConfigFailDisable=1;
        CHECK(CywConfigureRxAggregation(&TestAdapter)==STATUS_IO_DEVICE_ERROR && !TestAdapter.RxGlomEnabled);
        InitConfig();ConfigFailAt=i;ConfigFailure=STATUS_UNSUCCESSFUL;ConfigFirmwareError=0xfffffffeUL;
        CHECK(CywConfigureRxAggregation(&TestAdapter)==STATUS_UNSUCCESSFUL && ConfigCall==i && !TestAdapter.RxGlomEnabled);
    }
    puts("Checked production aggregation configuration order, parser readiness, unsupported fallback and terminal setup failures.");
}
