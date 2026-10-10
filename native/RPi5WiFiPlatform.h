#pragma once
#include <string>
#include <vector>

namespace rpiwifi {

inline constexpr wchar_t kHardwareId[] = L"ACPI\\RPI1060";
inline constexpr wchar_t kWindowTitle[] = L"RPi5 Wi-Fi - Damian Edition 0.7.1.24";
inline constexpr wchar_t kSetupTitle[] = L"RPi5 Wi-Fi Setup - Damian Edition 0.7.1.24";

// Keep policy independent of SetupAPI so negative hardware cases can be tested
// without installing a driver or requiring a Raspberry Pi.
struct PlatformDevice {
    std::vector<std::wstring> hardwareIds;
    std::wstring service;
    bool present{true};
    bool started{};
    unsigned long problemCode{};
};
struct PlatformStatus {
    unsigned targetCount{};
    bool driverBound{};
    bool driverStarted{};
    unsigned long problemCode{};
    unsigned long enumerationError{};
    bool legacyCollision{};
};
inline bool SameId(const std::wstring& a, const std::wstring& b) {
    if(a.size()!=b.size())return false;
    for(size_t i=0;i<a.size();++i) {
        auto upper=[](wchar_t c){return c>=L'a'&&c<=L'z'?static_cast<wchar_t>(c-32):c;};
        if(upper(a[i])!=upper(b[i]))return false;
    }
    return true;
}
inline bool HasHardwareId(const PlatformDevice& device, const wchar_t* id) {
    for(const auto& value:device.hardwareIds)if(SameId(value,id))return true;
    return false;
}
inline PlatformStatus AssessPlatform(const std::vector<PlatformDevice>& devices) {
    PlatformStatus result;
    for(const auto& device:devices) {
        if(!device.present)continue;
        bool wifiService=SameId(device.service,L"rpi5cyw");
        if(HasHardwareId(device,L"ACPI\\RPI0011")&&wifiService)result.legacyCollision=true;
        if(!HasHardwareId(device,kHardwareId))continue;
        ++result.targetCount;
        result.driverBound=wifiService;
        result.driverStarted=wifiService&&device.started&&device.problemCode==0;
        result.problemCode=device.problemCode;
    }
    return result;
}
inline std::wstring InstallBlockReason(const PlatformStatus& status) {
    if(status.enumerationError)return L"Unable to inspect ACPI devices (error "+std::to_wstring(status.enumerationError)+L").";
    if(status.legacyCollision)return L"Legacy Wi-Fi driver is bound to RPI0011. Remove the old Wi-Fi package and restore Damian's RP1 IRQ driver before installing.";
    if(status.targetCount==0)return L"Wi-Fi device RPI1060 is missing. In Damian Edition UEFI, select Wi-Fi SDIO Mode: Windows Direct NDIS, then reboot.";
    if(status.targetCount!=1)return L"Multiple RPI1060 devices were found. Check the firmware ACPI configuration.";
    return {};
}
inline std::wstring DriverBlockReason(const PlatformStatus& status) {
    auto reason=InstallBlockReason(status);
    if(!reason.empty())return reason;
    if(!status.driverBound)return L"RPI1060 is present. Install the Damian Edition Wi-Fi setup and restart Windows.";
    if(!status.driverStarted)return L"Wi-Fi driver has not started (PnP code "+std::to_wstring(status.problemCode)+L"). Check Test Signing and restart Windows.";
    return {};
}

PlatformStatus QueryPlatform();

} // namespace rpiwifi
