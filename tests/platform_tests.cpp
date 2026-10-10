#include "../native/RPi5WiFiPlatform.h"
#include <cstdio>
#include <cstdlib>

#define CHECK(x) do { if(!(x)){std::fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x);return EXIT_FAILURE;} } while(0)
using namespace rpiwifi;

int main() {
    PlatformDevice wifi{{L"ACPI\\RPI1060"},L"rpi5cyw",true,true,0};
    PlatformDevice irq{{L"ACPI\\RPI0011"},L"Pi5Rp1Irq",true,true,0};
    auto good=AssessPlatform({irq,wifi});
    CHECK(good.targetCount==1&&good.driverStarted&&!good.legacyCollision);
    CHECK(InstallBlockReason(good).empty()&&DriverBlockReason(good).empty());
    CHECK(!InstallBlockReason(AssessPlatform({irq})).empty());
    CHECK(!InstallBlockReason(AssessPlatform({})).empty());
    wifi.present=false;
    CHECK(AssessPlatform({irq,wifi}).targetCount==0); // phantom is not a target
    wifi.present=true;wifi.hardwareIds={L"ACPI\\RPI10600",L"ACPI\\RPI1060\\0"};
    CHECK(AssessPlatform({wifi}).targetCount==0); // no prefix or instance-ID match
    wifi.hardwareIds={L"ACPI\\OTHER",L"acpi\\rpi1060"};
    CHECK(AssessPlatform({wifi}).targetCount==1); // full MULTI_SZ, case insensitive
    wifi.service=L"";wifi.started=false;wifi.problemCode=28;
    CHECK(InstallBlockReason(AssessPlatform({irq,wifi})).empty()); // unbound can install
    CHECK(!DriverBlockReason(AssessPlatform({irq,wifi})).empty());
    wifi.service=L"rpi5cyw";wifi.problemCode=52;
    CHECK(DriverBlockReason(AssessPlatform({wifi})).find(L"52")!=std::wstring::npos);
    irq.service=L"RPI5CYW";
    CHECK(AssessPlatform({irq,wifi}).legacyCollision);
    CHECK(!InstallBlockReason(AssessPlatform({irq,wifi})).empty());
    irq.present=false;
    CHECK(!AssessPlatform({irq,wifi}).legacyCollision);
    CHECK(!InstallBlockReason(AssessPlatform({wifi,wifi})).empty());
    good.enumerationError=5;
    CHECK(!InstallBlockReason(good).empty()&&!DriverBlockReason(good).empty());
    std::puts("PASS: Damian target, IRQ collision, phantom, exact ID, PnP and enumeration policies");
    return EXIT_SUCCESS;
}
