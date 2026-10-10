#include "RPi5WiFiCommon.h"
#include "RPi5WiFiPlatform.h"

#include <bcrypt.h>
#include <cryptuiapi.h>
#include <sddl.h>
#include <shlobj.h>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <memory>
#include <stdexcept>
#include <thread>

#pragma comment(lib, "bcrypt.lib")
#pragma comment(lib, "crypt32.lib")
#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "ole32.lib")

namespace rpiwifi {
namespace {

constexpr char kProfileMagic[8] = {'R','P','I','5','W','F','2','0'};
constexpr uint32_t kProfileVersion = 1;
constexpr size_t kMaxProfiles = 32;
constexpr size_t kMaxProfileFile = 128 * 1024;
constexpr char kEntropy[] = "RPi5.WiFi.Native.Profile.v1";

uint32_t U32(const uint8_t* p) {
    return static_cast<uint32_t>(p[0]) |
        (static_cast<uint32_t>(p[1]) << 8) |
        (static_cast<uint32_t>(p[2]) << 16) |
        (static_cast<uint32_t>(p[3]) << 24);
}
int32_t I32(const uint8_t* p) {
    return static_cast<int32_t>(U32(p));
}
void Put32(std::vector<uint8_t>& b, size_t o, uint32_t v) {
    b[o] = static_cast<uint8_t>(v);
    b[o+1] = static_cast<uint8_t>(v >> 8);
    b[o+2] = static_cast<uint8_t>(v >> 16);
    b[o+3] = static_cast<uint8_t>(v >> 24);
}
void Append32(std::vector<uint8_t>& b, uint32_t v) {
    size_t o = b.size(); b.resize(o + 4); Put32(b, o, v);
}
void Append64(std::vector<uint8_t>& b, uint64_t v) {
    for (int i = 0; i < 8; ++i) b.push_back(static_cast<uint8_t>(v >> (8*i)));
}
uint64_t Read64(const uint8_t* p) {
    uint64_t v = 0; for (int i = 0; i < 8; ++i) v |= static_cast<uint64_t>(p[i]) << (8*i); return v;
}
[[noreturn]] void ThrowWin32(const char* where, DWORD code = GetLastError()) {
    throw std::runtime_error(std::string(where) + ": " + WideToUtf8(FormatWin32Error(code)));
}
[[noreturn]] void ThrowNt(const char* where, NTSTATUS status) {
    char buffer[64]{};
    sprintf_s(buffer, "%s: NTSTATUS 0x%08X", where, static_cast<unsigned>(status));
    throw std::runtime_error(buffer);
}
std::wstring ProgramDataPath() {
    PWSTR p = nullptr;
    if (FAILED(SHGetKnownFolderPath(FOLDERID_ProgramData, KF_FLAG_DEFAULT, nullptr, &p)))
        throw std::runtime_error("Unable to locate ProgramData.");
    std::wstring result(p); CoTaskMemFree(p); return result;
}
PSECURITY_DESCRIPTOR MakePrivateSd() {
    PSECURITY_DESCRIPTOR sd = nullptr;
    if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(
        L"D:P(A;;FA;;;SY)(A;;FA;;;BA)", SDDL_REVISION_1, &sd, nullptr))
        ThrowWin32("Create security descriptor");
    return sd;
}
void ApplyPrivateAcl(const std::wstring& path) {
    PSECURITY_DESCRIPTOR sd = MakePrivateSd();
    if (!SetFileSecurityW(path.c_str(),
        DACL_SECURITY_INFORMATION | PROTECTED_DACL_SECURITY_INFORMATION, sd)) {
        DWORD e = GetLastError(); LocalFree(sd); ThrowWin32("Protect profile path", e);
    }
    LocalFree(sd);
}
void EnsureProfileDirectory() {
    std::wstring dir = ProfileStore::Directory();
    DWORD a = GetFileAttributesW(dir.c_str());
    if (a == INVALID_FILE_ATTRIBUTES) {
        PSECURITY_DESCRIPTOR sd = MakePrivateSd();
        SECURITY_ATTRIBUTES sa{sizeof(sa), sd, FALSE};
        if (!CreateDirectoryW(dir.c_str(), &sa) && GetLastError() != ERROR_ALREADY_EXISTS) {
            DWORD e = GetLastError(); LocalFree(sd); ThrowWin32("Create profile directory", e);
        }
        LocalFree(sd);
        a = GetFileAttributesW(dir.c_str());
    }
    if (a == INVALID_FILE_ATTRIBUTES || !(a & FILE_ATTRIBUTE_DIRECTORY) || (a & FILE_ATTRIBUTE_REPARSE_POINT))
        throw std::runtime_error("Unsafe profile directory.");
    ApplyPrivateAcl(dir);
}
std::vector<uint8_t> ReadAll(const std::wstring& path) {
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        if (GetLastError() == ERROR_FILE_NOT_FOUND) return {};
        ThrowWin32("Open profile database");
    }
    LARGE_INTEGER size{};
    if (!GetFileSizeEx(h, &size) || size.QuadPart < 0 || size.QuadPart > static_cast<LONGLONG>(kMaxProfileFile)) {
        CloseHandle(h); throw std::runtime_error("Invalid profile database size.");
    }
    std::vector<uint8_t> data(static_cast<size_t>(size.QuadPart));
    DWORD got = 0;
    if (!data.empty() && (!ReadFile(h, data.data(), static_cast<DWORD>(data.size()), &got, nullptr) || got != data.size())) {
        DWORD e = GetLastError(); CloseHandle(h); ThrowWin32("Read profile database", e);
    }
    CloseHandle(h); return data;
}
void WriteAllPrivate(const std::wstring& path, const std::vector<uint8_t>& data) {
    PSECURITY_DESCRIPTOR sd = MakePrivateSd();
    SECURITY_ATTRIBUTES sa{sizeof(sa), sd, FALSE};
    HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, 0, &sa, CREATE_ALWAYS,
        FILE_ATTRIBUTE_NORMAL | FILE_FLAG_WRITE_THROUGH, nullptr);
    LocalFree(sd);
    if (h == INVALID_HANDLE_VALUE) ThrowWin32("Create profile database");
    DWORD wrote = 0;
    bool ok = data.empty() || (WriteFile(h, data.data(), static_cast<DWORD>(data.size()), &wrote, nullptr) && wrote == data.size());
    if (ok) ok = FlushFileBuffers(h) != FALSE;
    DWORD e = ok ? ERROR_SUCCESS : GetLastError();
    CloseHandle(h);
    if (!ok) ThrowWin32("Write profile database", e);
    ApplyPrivateAcl(path);
}
std::wstring ProgressText(const LiveState& s) {
    if (s.status != 0) {
        wchar_t b[96]{}; swprintf_s(b, L"Driver error 0x%08X at phase %u.", s.status, s.phase); return b;
    }
    if (s.authenticated) return L"Wi-Fi authenticated.";
    switch (s.phase) {
    case 400: return L"Preparing firmware...";
    case 410: return L"Preparing Wi-Fi chip...";
    case 420: return L"Loading firmware...";
    case 421: return L"Verifying firmware...";
    case 422: case 430: case 440: case 450: return L"Starting firmware...";
    case 500: return L"Disconnected and ready.";
    case 510: case 520: return L"Connecting and authenticating...";
    default: {
        wchar_t b[64]{}; swprintf_s(b, L"Waiting for driver (phase %u)...", s.phase); return b;
    }}
}
bool SameAsciiNoCase(const std::string& a, const std::string& b) {
    return a == b;
}

} // namespace

std::string WideToUtf8(const std::wstring& value) {
    if (value.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(),
        static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
    if (!n) ThrowWin32("UTF-8 conversion");
    std::string out(n, '\0');
    if (!WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(),
        static_cast<int>(value.size()), out.data(), n, nullptr, nullptr))
        ThrowWin32("UTF-8 conversion");
    return out;
}
std::wstring Utf8ToWide(const std::string& value) {
    if (value.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
        static_cast<int>(value.size()), nullptr, 0);
    if (!n) ThrowWin32("SSID UTF-8 conversion");
    std::wstring out(n, L'\0');
    if (!MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
        static_cast<int>(value.size()), out.data(), n))
        ThrowWin32("SSID UTF-8 conversion");
    return out;
}
std::wstring FormatWin32Error(DWORD code) {
    wchar_t* raw = nullptr;
    DWORD n = FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
        FORMAT_MESSAGE_IGNORE_INSERTS, nullptr, code, 0, reinterpret_cast<wchar_t*>(&raw), 0, nullptr);
    std::wstring out = n && raw ? std::wstring(raw, n) : L"Windows error " + std::to_wstring(code);
    if (raw) LocalFree(raw);
    while (!out.empty() && (out.back() == L'\r' || out.back() == L'\n' || out.back() == L' ')) out.pop_back();
    return out;
}
bool ValidCountry(const std::wstring& country) {
    return country.size() == 2 && country[0] >= L'A' && country[0] <= L'Z' &&
        country[1] >= L'A' && country[1] <= L'Z';
}
bool ValidSsidUtf8(const std::string& ssid) {
    if (ssid.empty() || ssid.size() > 32) return false;
    try { (void)Utf8ToWide(ssid); return true; } catch (...) { return false; }
}
bool ValidPassword(const std::wstring& password) {
    if (password.size() < 8 || password.size() > 63) return false;
    return std::all_of(password.begin(), password.end(), [](wchar_t c){ return c >= 0x20 && c <= 0x7e; });
}
std::array<uint8_t,32> DerivePmk(const std::string& ssidUtf8, const std::wstring& password) {
    if (!ValidSsidUtf8(ssidUtf8) || !ValidPassword(password))
        throw std::runtime_error("Use an exact SSID (1-32 UTF-8 bytes) and WPA2 password (8-63 printable ASCII characters).");
    std::vector<uint8_t> pass(password.size());
    for (size_t i = 0; i < password.size(); ++i) pass[i] = static_cast<uint8_t>(password[i]);
    BCRYPT_ALG_HANDLE alg = nullptr;
    NTSTATUS s = BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA1_ALGORITHM, nullptr, BCRYPT_ALG_HANDLE_HMAC_FLAG);
    if (s < 0) { SecureZeroMemory(pass.data(), pass.size()); ThrowNt("BCryptOpenAlgorithmProvider", s); }
    std::array<uint8_t,32> pmk{};
    s = BCryptDeriveKeyPBKDF2(alg, pass.data(), static_cast<ULONG>(pass.size()),
        reinterpret_cast<PUCHAR>(const_cast<char*>(ssidUtf8.data())), static_cast<ULONG>(ssidUtf8.size()),
        4096, pmk.data(), static_cast<ULONG>(pmk.size()), 0);
    BCryptCloseAlgorithmProvider(alg, 0);
    SecureZeroMemory(pass.data(), pass.size());
    if (s < 0) { SecureZeroMemory(pmk.data(), pmk.size()); ThrowNt("BCryptDeriveKeyPBKDF2", s); }
    return pmk;
}
std::vector<uint8_t> ProtectPmk(const std::array<uint8_t,32>& pmk) {
    DATA_BLOB in{static_cast<DWORD>(pmk.size()), const_cast<BYTE*>(pmk.data())};
    DATA_BLOB entropy{static_cast<DWORD>(sizeof(kEntropy)-1), reinterpret_cast<BYTE*>(const_cast<char*>(kEntropy))};
    DATA_BLOB out{};
    if (!CryptProtectData(&in, L"RPi5 WiFi PMK", &entropy, nullptr, nullptr,
        CRYPTPROTECT_LOCAL_MACHINE | CRYPTPROTECT_UI_FORBIDDEN, &out))
        ThrowWin32("Protect Wi-Fi profile");
    std::vector<uint8_t> result(out.pbData, out.pbData + out.cbData);
    SecureZeroMemory(out.pbData, out.cbData); LocalFree(out.pbData);
    return result;
}
std::array<uint8_t,32> UnprotectPmk(const std::vector<uint8_t>& blob) {
    if (blob.empty() || blob.size() > 8192) throw std::runtime_error("Invalid saved profile key.");
    DATA_BLOB in{static_cast<DWORD>(blob.size()), const_cast<BYTE*>(blob.data())};
    DATA_BLOB entropy{static_cast<DWORD>(sizeof(kEntropy)-1), reinterpret_cast<BYTE*>(const_cast<char*>(kEntropy))};
    DATA_BLOB out{};
    if (!CryptUnprotectData(&in, nullptr, &entropy, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &out))
        ThrowWin32("Unprotect Wi-Fi profile");
    if (out.cbData != 32) {
        SecureZeroMemory(out.pbData, out.cbData); LocalFree(out.pbData);
        throw std::runtime_error("Invalid saved Wi-Fi key.");
    }
    std::array<uint8_t,32> pmk{}; memcpy(pmk.data(), out.pbData, 32);
    SecureZeroMemory(out.pbData, out.cbData); LocalFree(out.pbData);
    return pmk;
}
uint64_t NowFileTime() {
    FILETIME ft{}; GetSystemTimeAsFileTime(&ft);
    return (static_cast<uint64_t>(ft.dwHighDateTime) << 32) | ft.dwLowDateTime;
}

LiveState ParseLiveState(const std::vector<uint8_t>& data) {
    if (data.size() < 32) throw std::runtime_error("Incomplete driver status.");
    LiveState s{}; s.version=U32(data.data()); s.phase=U32(data.data()+4); s.status=U32(data.data()+8);
    s.authenticated=U32(data.data()+28)==1;
    if (s.version < 1 || s.version > 3 || (s.version == 2 && data.size() < 48) || (s.version == 3 && data.size() < 96))
        throw std::runtime_error("Unsupported driver status.");
    if (s.version == 3) { s.firmwareTotal=U32(data.data()+48); s.uploaded=U32(data.data()+52); s.verified=U32(data.data()+56); }
    return s;
}
ScanReport ParseScanReport(const std::vector<uint8_t>& data) {
    if (data.size() != 3616 || U32(data.data()) != 1) throw std::runtime_error("Unsupported scan report.");
    ScanReport r{}; r.generation=U32(data.data()+4); r.state=U32(data.data()+8); r.status=U32(data.data()+12);
    uint32_t count=U32(data.data()+16), flags=U32(data.data()+20), country=U32(data.data()+24);
    r.firmwareError=I32(data.data()+28); r.truncated=flags!=0;
    if (r.state>5 || count>64 || flags>1 || (r.state!=0 && r.generation==0) || country>0xffff)
        throw std::runtime_error("Invalid scan report.");
    for (uint32_t i=0;i<count;++i) {
        const uint8_t* e=data.data()+32+i*56; uint32_t len=U32(e), security=U32(e+48), channel=U32(e+52);
        if (len>32) throw std::runtime_error("Invalid SSID length in scan result.");
        NetworkEntry n{}; n.rssi=I32(e+44); n.channel=channel; n.ssidUtf8.assign(reinterpret_cast<const char*>(e+4), len);
        if (len && ValidSsidUtf8(n.ssidUtf8)) n.display=Utf8ToWide(n.ssidUtf8); else n.display=L"<Hidden / invalid SSID>";
        wchar_t mac[32]{}; swprintf_s(mac,L"%02X:%02X:%02X:%02X:%02X:%02X",e[36],e[37],e[38],e[39],e[40],e[41]); n.bssid=mac;
        if (channel>=1 && channel<=14) n.band=L"2.4 GHz";
        else if (channel>=32 && channel<=196) n.band=L"5 GHz";
        else n.band=L"Unknown";
        if (security==2) n.security=L"WPA2-Personal / AES";
        else if (security&1) n.security=L"Open (unsupported)";
        else n.security=L"Unsupported";
        n.supported=security==2 && n.band!=L"Unknown" && ValidSsidUtf8(n.ssidUtf8);
        r.networks.push_back(std::move(n));
    }
    std::stable_sort(r.networks.begin(),r.networks.end(),[](const NetworkEntry&a,const NetworkEntry&b){return a.rssi>b.rssi;});
    return r;
}
std::vector<uint8_t> BuildConnectRequest(const std::wstring& country,
    const std::string& ssidUtf8, const std::array<uint8_t,32>& pmk) {
    if (!ValidCountry(country) || !ValidSsidUtf8(ssidUtf8)) throw std::runtime_error("Invalid connection profile.");
    std::vector<uint8_t> b(76); Put32(b,0,1); Put32(b,4,static_cast<uint32_t>(ssidUtf8.size()));
    b[8]=static_cast<uint8_t>(country[0]); b[9]=static_cast<uint8_t>(country[1]);
    memcpy(b.data()+12,ssidUtf8.data(),ssidUtf8.size()); memcpy(b.data()+44,pmk.data(),pmk.size()); return b;
}
std::vector<uint8_t> BuildScanRequest(const std::wstring& country) {
    if (!ValidCountry(country)) throw std::runtime_error("Country must be two uppercase letters.");
    std::vector<uint8_t> b(8); Put32(b,0,1); b[4]=static_cast<uint8_t>(country[0]); b[5]=static_cast<uint8_t>(country[1]); return b;
}

Driver::Driver() {
    auto reason=DriverBlockReason(QueryPlatform());
    if(!reason.empty())throw std::runtime_error(WideToUtf8(reason));
    handle_=CreateFileW(kDevicePath,GENERIC_READ|GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE,
        nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(handle_==INVALID_HANDLE_VALUE) ThrowWin32("Open RPi5 Wi-Fi driver");
}
Driver::~Driver(){if(handle_!=INVALID_HANDLE_VALUE)CloseHandle(handle_);}
std::vector<uint8_t> Driver::Call(DWORD code,const std::vector<uint8_t>& input) {
    DWORD cap=0;
    if(code==IOCTL_STATUS)cap=96; else if(code==IOCTL_SCAN_STATUS)cap=3616;
    else if(code==IOCTL_CONNECT||code==IOCTL_DISCONNECT||code==IOCTL_SCAN_START||code==IOCTL_SCAN_CANCEL)cap=0;
    else throw std::runtime_error("Unsupported driver control operation.");
    std::vector<uint8_t> out(cap);DWORD got=0;
    void* in=input.empty()?nullptr:const_cast<uint8_t*>(input.data());
    void* outp=out.empty()?nullptr:out.data();
    if(!DeviceIoControl(handle_,code,in,static_cast<DWORD>(input.size()),outp,cap,&got,nullptr))
        ThrowWin32("Driver control request");
    if(got>cap || (code==IOCTL_SCAN_STATUS && got!=cap))throw std::runtime_error("Incomplete driver response.");
    out.resize(got);return out;
}
LiveState Driver::Status(){return ParseLiveState(Call(IOCTL_STATUS));}

OperationMutex::OperationMutex(DWORD waitMs) {
    PSECURITY_DESCRIPTOR sd=MakePrivateSd();SECURITY_ATTRIBUTES sa{sizeof(sa),sd,FALSE};
    handle_=CreateMutexW(&sa,FALSE,L"Global\\RPi5WiFi.Operation.v1");DWORD createError=GetLastError();LocalFree(sd);
    if(!handle_)ThrowWin32("Create Wi-Fi operation lock",createError);
    DWORD w=WaitForSingleObject(handle_,waitMs);
    if(w==WAIT_OBJECT_0||w==WAIT_ABANDONED)owned_=true;
    else {CloseHandle(handle_);handle_=nullptr;if(w==WAIT_TIMEOUT)throw std::runtime_error("Another Wi-Fi operation is running.");ThrowWin32("Wait for Wi-Fi operation lock");}
}
OperationMutex::~OperationMutex(){if(handle_){if(owned_)ReleaseMutex(handle_);CloseHandle(handle_);}}

std::wstring ProfileStore::Directory(){return ProgramDataPath()+L"\\"+kProductDirName;}
std::wstring ProfileStore::FilePath(){return Directory()+L"\\profiles.bin";}
ProfileDb ProfileStore::Load() {
    EnsureProfileDirectory();auto data=ReadAll(FilePath());ProfileDb db;
    if(data.empty())return db;
    if(data.size()<20 || memcmp(data.data(),kProfileMagic,8)!=0 || U32(data.data()+8)!=kProfileVersion)
        throw std::runtime_error("Invalid profile database.");
    db.country.assign(reinterpret_cast<const char*>(data.data()+12),2);
    if(db.country.size()!=2 || db.country[0]<'A'||db.country[0]>'Z'||db.country[1]<'A'||db.country[1]>'Z')
        throw std::runtime_error("Invalid saved country.");
    uint32_t count=U32(data.data()+16);if(count>kMaxProfiles)throw std::runtime_error("Too many saved profiles.");
    size_t pos=20;
    for(uint32_t i=0;i<count;++i){
        if(pos+20>data.size())throw std::runtime_error("Truncated profile database.");
        uint32_t flags=U32(data.data()+pos),ssidLen=U32(data.data()+pos+4),blobLen=U32(data.data()+pos+8);
        uint64_t last=Read64(data.data()+pos+12);pos+=20;
        if(ssidLen<1||ssidLen>32||blobLen<1||blobLen>8192||pos+ssidLen+blobLen>data.size())
            throw std::runtime_error("Invalid saved profile.");
        Profile p;p.autoConnect=(flags&1)!=0;p.lastUsed=last;
        p.ssidUtf8.assign(reinterpret_cast<const char*>(data.data()+pos),ssidLen);pos+=ssidLen;
        p.protectedPmk.assign(data.begin()+pos,data.begin()+pos+blobLen);pos+=blobLen;
        if(!ValidSsidUtf8(p.ssidUtf8))throw std::runtime_error("Invalid saved SSID.");
        db.profiles.push_back(std::move(p));
    }
    if(pos!=data.size())throw std::runtime_error("Unexpected trailing profile data.");
    return db;
}
void ProfileStore::Save(const ProfileDb& db) {
    EnsureProfileDirectory();
    if(db.country.size()!=2||db.country[0]<'A'||db.country[0]>'Z'||db.country[1]<'A'||db.country[1]>'Z')
        throw std::runtime_error("Invalid country.");
    if(db.profiles.size()>kMaxProfiles)throw std::runtime_error("Too many saved profiles.");
    std::vector<uint8_t> b; b.insert(b.end(),kProfileMagic,kProfileMagic+8);Append32(b,kProfileVersion);
    b.push_back(static_cast<uint8_t>(db.country[0]));b.push_back(static_cast<uint8_t>(db.country[1]));b.push_back(0);b.push_back(0);
    Append32(b,static_cast<uint32_t>(db.profiles.size()));
    for(const auto&p:db.profiles){
        if(!ValidSsidUtf8(p.ssidUtf8)||p.protectedPmk.empty()||p.protectedPmk.size()>8192)throw std::runtime_error("Invalid profile.");
        Append32(b,p.autoConnect?1u:0u);Append32(b,static_cast<uint32_t>(p.ssidUtf8.size()));
        Append32(b,static_cast<uint32_t>(p.protectedPmk.size()));Append64(b,p.lastUsed);
        b.insert(b.end(),p.ssidUtf8.begin(),p.ssidUtf8.end());b.insert(b.end(),p.protectedPmk.begin(),p.protectedPmk.end());
    }
    std::wstring tmp=FilePath()+L".tmp";WriteAllPrivate(tmp,b);
    if(!MoveFileExW(tmp.c_str(),FilePath().c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)){
        DWORD e=GetLastError();DeleteFileW(tmp.c_str());ThrowWin32("Commit profile database",e);
    }
    ApplyPrivateAcl(FilePath());
}
Profile* ProfileStore::Find(ProfileDb& db,const std::string& ssidUtf8){
    for(auto& p:db.profiles)if(SameAsciiNoCase(p.ssidUtf8,ssidUtf8))return &p;return nullptr;
}
const Profile* ProfileStore::Find(const ProfileDb& db,const std::string& ssidUtf8){
    for(const auto& p:db.profiles)if(SameAsciiNoCase(p.ssidUtf8,ssidUtf8))return &p;return nullptr;
}
void ProfileStore::Upsert(ProfileDb& db,const std::string& ssidUtf8,const std::array<uint8_t,32>& pmk,bool autoConnect){
    if(!ValidSsidUtf8(ssidUtf8))throw std::runtime_error("Invalid SSID.");
    Profile* p=Find(db,ssidUtf8);if(!p){if(db.profiles.size()>=kMaxProfiles)throw std::runtime_error("Maximum saved profiles reached.");db.profiles.push_back({});p=&db.profiles.back();p->ssidUtf8=ssidUtf8;}
    p->protectedPmk=ProtectPmk(pmk);p->autoConnect=autoConnect;p->lastUsed=NowFileTime();
}
bool ProfileStore::Remove(ProfileDb& db,const std::string& ssidUtf8){
    auto it=std::find_if(db.profiles.begin(),db.profiles.end(),[&](const Profile&p){return p.ssidUtf8==ssidUtf8;});
    if(it==db.profiles.end())return false;db.profiles.erase(it);return true;
}

LiveState WaitForIdleOrConnected(Driver& driver,DWORD timeoutMs,const std::function<void(const std::wstring&)>& progress){
    ULONGLONG start=GetTickCount64();
    for(;;){
        LiveState s=driver.Status();if(progress)progress(ProgressText(s));
        if(s.status!=0)throw std::runtime_error("Driver reports an error. Collect diagnostics.");
        if(s.authenticated||s.Idle())return s;
        if(GetTickCount64()-start>=timeoutMs)throw std::runtime_error("Timed out waiting for Wi-Fi driver readiness.");
        Sleep(500);
    }
}
ScanReport ScanNetworks(Driver& driver,const std::wstring& country,const std::function<void(const std::wstring&)>& progress){
    OperationMutex lease;LiveState live=driver.Status();
    if(!live.Idle())throw std::runtime_error("Scanning is available only when disconnected and ready.");
    ScanReport previous=ParseScanReport(driver.Call(IOCTL_SCAN_STATUS));
    if(previous.state==1||previous.state==2)throw std::runtime_error("A scan is already running.");
    driver.Call(IOCTL_SCAN_START,BuildScanRequest(country));
    uint32_t generation=0;ULONGLONG start=GetTickCount64();
    try{
        for(;;){
            ScanReport r=ParseScanReport(driver.Call(IOCTL_SCAN_STATUS));
            if(r.generation==previous.generation)throw std::runtime_error("Fresh scan was not observed.");
            if(!generation)generation=r.generation; else if(generation!=r.generation)throw std::runtime_error("Scan ownership changed.");
            if(progress)progress(L"Scanning nearby networks...");
            if(r.state==3&&r.status==0)return r;
            if(r.state==4||r.state==5||(r.status!=0&&r.status!=259)){
                wchar_t b[128]{};swprintf_s(b,L"Scan failed: 0x%08X, firmware %d.",r.status,r.firmwareError);
                throw std::runtime_error(WideToUtf8(b));
            }
            if(GetTickCount64()-start>=90000)throw std::runtime_error("Scan timed out.");
            Sleep(250);
        }
    }catch(...){
        if(generation){
            std::vector<uint8_t> cancel(4);Put32(cancel,0,generation);
            try{driver.Call(IOCTL_SCAN_CANCEL,cancel);}catch(...){}
        }
        throw;
    }
}
void ConnectNetwork(Driver& driver,const std::wstring& country,const std::string& ssidUtf8,
    const std::array<uint8_t,32>& pmk,const std::function<void(const std::wstring&)>& progress){
    OperationMutex lease;LiveState live=driver.Status();
    if(live.authenticated&&live.status==0){if(progress)progress(L"Already connected.");return;}
    if(!live.Idle())throw std::runtime_error("Driver must be disconnected and ready before connecting.");
    auto req=BuildConnectRequest(country,ssidUtf8,pmk);
    try{driver.Call(IOCTL_CONNECT,req);}catch(...){SecureZeroMemory(req.data(),req.size());throw;}
    SecureZeroMemory(req.data(),req.size());
    ULONGLONG start=GetTickCount64();
    for(;;){
        LiveState s=driver.Status();if(progress)progress(ProgressText(s));
        if(s.status!=0)throw std::runtime_error("Connection failed in the driver. Collect diagnostics.");
        if(s.authenticated)return;
        if(GetTickCount64()-start>=180000)throw std::runtime_error("Connection timed out.");
        Sleep(250);
    }
}
void DisconnectNetwork(Driver& driver){
    OperationMutex lease;driver.Call(IOCTL_DISCONNECT);
    ULONGLONG start=GetTickCount64();
    while(GetTickCount64()-start<15000){LiveState s=driver.Status();if(s.Idle())return;if(s.status!=0)throw std::runtime_error("Driver reported an error during disconnect.");Sleep(250);}
    throw std::runtime_error("Disconnect timed out.");
}

int RunAutoConnect(){
    try{
        ProfileDb db=ProfileStore::Load();
        bool any=false;for(const auto&p:db.profiles)if(p.autoConnect){any=true;break;}if(!any)return 0;
        std::unique_ptr<Driver> driver;
        ULONGLONG openStart=GetTickCount64();
        while(!driver && GetTickCount64()-openStart<300000) {
            try { driver=std::make_unique<Driver>(); }
            catch(...) { Sleep(1000); }
        }
        if(!driver)return 2;
        LiveState live=WaitForIdleOrConnected(*driver,300000);
        if(live.authenticated)return 0;
        std::wstring country=Utf8ToWide(db.country);
        ScanReport report=ScanNetworks(*driver,country);
        const Profile* best=nullptr;int bestRssi=-1000;
        for(const auto&n:report.networks){
            if(!n.supported)continue;
            const Profile*p=ProfileStore::Find(db,n.ssidUtf8);
            if(p&&p->autoConnect&&n.rssi>bestRssi){best=p;bestRssi=n.rssi;}
        }
        if(!best)return 0;
        auto pmk=UnprotectPmk(best->protectedPmk);
        ConnectNetwork(*driver,country,best->ssidUtf8,pmk);
        SecureZeroMemory(pmk.data(),pmk.size());
        if(Profile* p=ProfileStore::Find(db,best->ssidUtf8)){p->lastUsed=NowFileTime();ProfileStore::Save(db);}
        return 0;
    }catch(...){return 2;}
}

int RunCommonSelfTests(){
    try{
        auto pmk=DerivePmk("IEEE",L"password");
        static const uint8_t expected[32]={0xF4,0x2C,0x6F,0xC5,0x2D,0xF0,0xEB,0xEF,0x9E,0xBB,0x4B,0x90,0xB3,0x8A,0x5F,0x90,0x2E,0x83,0xFE,0x1B,0x13,0x5A,0x70,0xE2,0x3A,0xED,0x76,0x2E,0x97,0x10,0xA1,0x2E};
        if(memcmp(pmk.data(),expected,32)!=0)return 10;
        auto protectedBlob=ProtectPmk(pmk);auto unprotected=UnprotectPmk(protectedBlob);
        if(memcmp(pmk.data(),unprotected.data(),32)!=0)return 11;
        SecureZeroMemory(pmk.data(),pmk.size());SecureZeroMemory(unprotected.data(),unprotected.size());
        std::vector<uint8_t> scan(3616);Put32(scan,0,1);Put32(scan,4,7);Put32(scan,8,3);Put32(scan,16,1);
        uint8_t* e=scan.data()+32;Put32(scan,32,7);memcpy(e+4,"TestNet",7);
        e[36]=0x02;e[37]=0x11;e[38]=0x22;e[39]=0x33;e[40]=0x44;e[41]=0x55;
        Put32(scan,32+44,static_cast<uint32_t>(-42));Put32(scan,32+48,2);Put32(scan,32+52,36);
        auto r=ParseScanReport(scan);
        if(r.networks.size()!=1||r.networks[0].ssidUtf8!="TestNet"||r.networks[0].band!=L"5 GHz"||!r.networks[0].supported)return 12;
        return 0;
    }catch(...){return 99;}
}

} // namespace rpiwifi
