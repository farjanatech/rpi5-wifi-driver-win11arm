#pragma once
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <array>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace rpiwifi {

constexpr wchar_t kDevicePath[] = L"\\\\.\\Rpi5CywControl";
constexpr wchar_t kProductDirName[] = L"RPi5 WiFi";
constexpr wchar_t kGuiExeName[] = L"RPi5-WiFi.exe";
constexpr wchar_t kAutoTaskName[] = L"RPi5 WiFi AutoConnect";

constexpr DWORD IOCTL_CONNECT      = 0x12A000;
constexpr DWORD IOCTL_STATUS       = 0x126004;
constexpr DWORD IOCTL_DISCONNECT   = 0x12A008;
constexpr DWORD IOCTL_SCAN_START   = 0x12A014;
constexpr DWORD IOCTL_SCAN_STATUS  = 0x126018;
constexpr DWORD IOCTL_SCAN_CANCEL  = 0x12A01C;

struct LiveState {
    uint32_t version{};
    uint32_t phase{};
    uint32_t status{};
    bool authenticated{};
    uint32_t firmwareTotal{};
    uint32_t uploaded{};
    uint32_t verified{};
    bool Idle() const { return phase == 500 && status == 0 && !authenticated; }
};

struct NetworkEntry {
    std::string ssidUtf8;
    std::wstring display;
    int32_t rssi{};
    uint32_t channel{};
    std::wstring band;
    std::wstring security;
    std::wstring bssid;
    bool supported{};
};

struct ScanReport {
    uint32_t generation{};
    uint32_t state{};
    uint32_t status{};
    int32_t firmwareError{};
    bool truncated{};
    std::vector<NetworkEntry> networks;
};

struct Profile {
    std::string ssidUtf8;
    std::vector<uint8_t> protectedPmk;
    bool autoConnect{};
    uint64_t lastUsed{};
};

struct ProfileDb {
    std::string country;
    std::vector<Profile> profiles;
};

std::string WideToUtf8(const std::wstring& value);
std::wstring Utf8ToWide(const std::string& value);
std::wstring FormatWin32Error(DWORD code);
bool ValidCountry(const std::wstring& country);
bool ValidSsidUtf8(const std::string& ssid);
bool ValidPassword(const std::wstring& password);
std::array<uint8_t,32> DerivePmk(const std::string& ssidUtf8, const std::wstring& password);
std::vector<uint8_t> ProtectPmk(const std::array<uint8_t,32>& pmk);
std::array<uint8_t,32> UnprotectPmk(const std::vector<uint8_t>& blob);
uint64_t NowFileTime();

LiveState ParseLiveState(const std::vector<uint8_t>& data);
ScanReport ParseScanReport(const std::vector<uint8_t>& data);
std::vector<uint8_t> BuildConnectRequest(const std::wstring& country,
    const std::string& ssidUtf8, const std::array<uint8_t,32>& pmk);
std::vector<uint8_t> BuildScanRequest(const std::wstring& country);

class Driver {
public:
    Driver();
    ~Driver();
    Driver(const Driver&) = delete;
    Driver& operator=(const Driver&) = delete;
    std::vector<uint8_t> Call(DWORD code, const std::vector<uint8_t>& input = {});
    LiveState Status();
private:
    HANDLE handle_ = INVALID_HANDLE_VALUE;
};

class OperationMutex {
public:
    explicit OperationMutex(DWORD waitMs = 10000);
    ~OperationMutex();
    OperationMutex(const OperationMutex&) = delete;
    OperationMutex& operator=(const OperationMutex&) = delete;
private:
    HANDLE handle_ = nullptr;
    bool owned_ = false;
};

class ProfileStore {
public:
    static std::wstring Directory();
    static std::wstring FilePath();
    static ProfileDb Load();
    static void Save(const ProfileDb& db);
    static Profile* Find(ProfileDb& db, const std::string& ssidUtf8);
    static const Profile* Find(const ProfileDb& db, const std::string& ssidUtf8);
    static void Upsert(ProfileDb& db, const std::string& ssidUtf8,
        const std::array<uint8_t,32>& pmk, bool autoConnect);
    static bool Remove(ProfileDb& db, const std::string& ssidUtf8);
};

LiveState WaitForIdleOrConnected(Driver& driver, DWORD timeoutMs,
    const std::function<void(const std::wstring&)>& progress = {});
ScanReport ScanNetworks(Driver& driver, const std::wstring& country,
    const std::function<void(const std::wstring&)>& progress = {});
void ConnectNetwork(Driver& driver, const std::wstring& country,
    const std::string& ssidUtf8, const std::array<uint8_t,32>& pmk,
    const std::function<void(const std::wstring&)>& progress = {});
void DisconnectNetwork(Driver& driver);
int RunAutoConnect();

int RunCommonSelfTests();

} // namespace rpiwifi
