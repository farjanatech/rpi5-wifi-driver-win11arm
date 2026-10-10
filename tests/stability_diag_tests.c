#include <stdio.h>
#include "../src/cyw43455/stability_diag.h"
#define CHECK(x) do {if(!(x)){printf("FAIL %d: %s\n",__LINE__,#x);return 1;}}while(0)
int main(void)
{
    unsigned int i;
    const unsigned int progress[]={4,5,8,9,10,11};
    CHECK(CywFirmwareDisconnectClass(5,0,0,0)==CYW_FW_DISCONNECT_DEAUTH);
    CHECK(CywFirmwareDisconnectClass(6,0,0,0)==CYW_FW_DISCONNECT_DEAUTH);
    CHECK(CywFirmwareDisconnectClass(11,0,0,0)==CYW_FW_DISCONNECT_DISASSOC);
    CHECK(CywFirmwareDisconnectClass(12,0,0,0)==CYW_FW_DISCONNECT_DISASSOC);
    CHECK(CywFirmwareDisconnectClass(16,0,1,0)==CYW_FW_DISCONNECT_NONE);
    CHECK(CywFirmwareDisconnectClass(16,0,0,0)==CYW_FW_DISCONNECT_LINK_DOWN);
    CHECK(CywFirmwareDisconnectClass(16,1,1,0)==CYW_FW_DISCONNECT_LINK_DOWN);
    CHECK(CywFirmwareDisconnectClass(46,6,0,0)==CYW_FW_DISCONNECT_NONE);
    CHECK(CywFirmwareDisconnectClass(46,6,0,15)==CYW_FW_DISCONNECT_AUTH_LOSS);
    CHECK(CywFirmwareDisconnectClass(46,0,0,0)==CYW_FW_DISCONNECT_AUTH_LOSS);
    CHECK(CywFirmwareDisconnectClass(46,7,0,0)==CYW_FW_DISCONNECT_AUTH_LOSS);
    for(i=0;i<sizeof(progress)/sizeof(progress[0]);i++) {
        CHECK(CywFirmwareDisconnectClass(46,progress[i],0,0)==CYW_FW_DISCONNECT_NONE);
        CHECK(CywFirmwareDisconnectClass(46,progress[i],0,15)==CYW_FW_DISCONNECT_AUTH_LOSS);
    }
    CHECK(CywFirmwareDisconnectClass(0,1,0,0)==CYW_FW_DISCONNECT_OTHER);
    CHECK(CywFirmwareDisconnectClass(69,0,0,0)==CYW_FW_DISCONNECT_NONE);
    puts("PASS: persistent disconnect diagnostic event classification");
    return 0;
}
