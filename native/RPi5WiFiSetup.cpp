#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <newdev.h>
#include <setupapi.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <wincrypt.h>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <vector>

#include "resource.h"
#include "SetupPayloadIds.h"
#include "RPi5WiFiPlatform.h"

#pragma comment(lib, "newdev.lib")
#pragma comment(lib, "setupapi.lib")
#pragma comment(lib, "crypt32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "ole32.lib")

namespace {

struct Payload { int id; const wchar_t* file; };
constexpr Payload kDriverPayloads[] = {
    {IDR_PAYLOAD_INF,L"rpi5cyw.inf"},{IDR_PAYLOAD_SYS,L"rpi5cyw.sys"},
    {IDR_PAYLOAD_CAT,L"rpi5cyw.cat"},{IDR_PAYLOAD_CERT,L"rpi5cyw-test.cer"},
    {IDR_PAYLOAD_FW,L"cyfmac43455-sdio.bin"},{IDR_PAYLOAD_CLM,L"cyfmac43455-sdio.clm_blob"},
    {IDR_PAYLOAD_NVRAM,L"brcmfmac43455-sdio.txt"}
};
constexpr wchar_t kProductDirName[] = L"RPi5 WiFi";
constexpr wchar_t kGuiName[] = L"RPi5-WiFi.exe";
constexpr wchar_t kTaskName[] = L"RPi5 WiFi AutoConnect";

std::wstring WinError(DWORD code=GetLastError()) {
    wchar_t* raw=nullptr;DWORD n=FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER|FORMAT_MESSAGE_FROM_SYSTEM|
        FORMAT_MESSAGE_IGNORE_INSERTS,nullptr,code,0,reinterpret_cast<wchar_t*>(&raw),0,nullptr);
    std::wstring s=n&&raw?std::wstring(raw,n):L"Windows error "+std::to_wstring(code);
    if(raw)LocalFree(raw);while(!s.empty()&&(s.back()==L'\r'||s.back()==L'\n'||s.back()==L' '))s.pop_back();return s;
}
std::string Utf8(const std::wstring& value) {
    if(value.empty())return {};
    int n=WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,value.data(),static_cast<int>(value.size()),nullptr,0,nullptr,nullptr);
    if(!n)return "Installer error";
    std::string out(static_cast<size_t>(n),'\0');
    if(!WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,value.data(),static_cast<int>(value.size()),out.data(),n,nullptr,nullptr))
        return "Installer error";
    return out;
}
std::wstring Wide(const std::string& value) {
    if(value.empty())return L"Installation failed.";
    int n=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,value.data(),static_cast<int>(value.size()),nullptr,0);
    if(!n)return L"Installation failed.";
    std::wstring out(static_cast<size_t>(n),L'\0');
    if(!MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,value.data(),static_cast<int>(value.size()),out.data(),n))
        return L"Installation failed.";
    return out;
}
[[noreturn]] void Fail(const std::wstring& m){throw std::runtime_error(Utf8(m));}
[[noreturn]] void FailWin(const std::wstring& where,DWORD e=GetLastError()){MessageBoxW(nullptr,(where+L": "+WinError(e)).c_str(),rpiwifi::kSetupTitle,MB_ICONERROR);ExitProcess(2);}
std::wstring Known(REFKNOWNFOLDERID id) {
    PWSTR p=nullptr;if(FAILED(SHGetKnownFolderPath(id,KF_FLAG_DEFAULT,nullptr,&p)))Fail(L"Unable to locate Windows folder.");
    std::wstring s(p);CoTaskMemFree(p);return s;
}
std::vector<BYTE> ResourceBytes(int id) {
    HMODULE m=GetModuleHandleW(nullptr);HRSRC r=FindResourceW(m,MAKEINTRESOURCEW(id),RT_RCDATA);
    if(!r)Fail(L"Installer payload is incomplete.");
    HGLOBAL h=LoadResource(m,r);DWORD n=SizeofResource(m,r);const BYTE* p=reinterpret_cast<const BYTE*>(LockResource(h));
    if(!p||!n)Fail(L"Installer payload is empty.");return std::vector<BYTE>(p,p+n);
}
void WriteResource(int id,const std::wstring& path) {
    auto bytes=ResourceBytes(id);HANDLE h=CreateFileW(path.c_str(),GENERIC_WRITE,0,nullptr,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(h==INVALID_HANDLE_VALUE)FailWin(L"Create "+path);
    DWORD wrote=0;BOOL ok=WriteFile(h,bytes.data(),static_cast<DWORD>(bytes.size()),&wrote,nullptr);
    DWORD e=ok&&wrote==bytes.size()?ERROR_SUCCESS:GetLastError();CloseHandle(h);if(e)FailWin(L"Write "+path,e);
}
void AddCertToStore(const std::vector<BYTE>& bytes,const wchar_t* storeName) {
    PCCERT_CONTEXT cert=CertCreateCertificateContext(X509_ASN_ENCODING|PKCS_7_ASN_ENCODING,bytes.data(),static_cast<DWORD>(bytes.size()));
    if(!cert)FailWin(L"Read bundled driver certificate");
    HCERTSTORE store=CertOpenStore(CERT_STORE_PROV_SYSTEM_W,0,0,CERT_SYSTEM_STORE_LOCAL_MACHINE,storeName);
    if(!store){CertFreeCertificateContext(cert);FailWin(L"Open certificate store");}
    if(!CertAddCertificateContextToStore(store,cert,CERT_STORE_ADD_REPLACE_EXISTING,nullptr)){
        DWORD e=GetLastError();CertCloseStore(store,0);CertFreeCertificateContext(cert);FailWin(L"Trust driver certificate",e);
    }
    CertCloseStore(store,0);CertFreeCertificateContext(cert);
}
void CreateShortcut(const std::wstring& target) {
    HRESULT hr=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);bool uninit=SUCCEEDED(hr);
    IShellLinkW* link=nullptr;hr=CoCreateInstance(CLSID_ShellLink,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&link));
    if(FAILED(hr)){if(uninit)CoUninitialize();Fail(L"Unable to create Desktop shortcut.");}
    link->SetPath(target.c_str());link->SetDescription(L"RPi5 Wi-Fi Manager");link->SetIconLocation(target.c_str(),0);
    IPersistFile* file=nullptr;hr=link->QueryInterface(IID_PPV_ARGS(&file));
    if(SUCCEEDED(hr)){std::wstring shortcut=Known(FOLDERID_Desktop)+L"\\RPi5 Wi-Fi.lnk";hr=file->Save(shortcut.c_str(),TRUE);file->Release();}
    link->Release();if(uninit)CoUninitialize();if(FAILED(hr))Fail(L"Unable to save Desktop shortcut.");
}
DWORD RunHidden(const std::wstring& exe,const std::wstring& arguments) {
    std::wstring line=L"\""+exe+L"\" "+arguments;std::vector<wchar_t> cmd(line.begin(),line.end());cmd.push_back(0);
    STARTUPINFOW si{sizeof(si)};PROCESS_INFORMATION pi{};si.dwFlags=STARTF_USESHOWWINDOW;si.wShowWindow=SW_HIDE;
    if(!CreateProcessW(exe.c_str(),cmd.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,nullptr,&si,&pi))FailWin(L"Start "+exe);
    WaitForSingleObject(pi.hProcess,60000);DWORD code=1;GetExitCodeProcess(pi.hProcess,&code);CloseHandle(pi.hThread);CloseHandle(pi.hProcess);return code;
}
void CreateBootTask(const std::wstring& gui) {
    wchar_t sys[MAX_PATH]{};if(!GetSystemDirectoryW(sys,MAX_PATH))FailWin(L"Locate System32");
    std::wstring schtasks=std::wstring(sys)+L"\\schtasks.exe";
    std::wstring taskRun=L"\\\""+gui+L"\\\" --autoconnect";
    std::wstring args=L"/Create /F /TN \""+std::wstring(kTaskName)+L"\" /SC ONSTART /RU SYSTEM /RL HIGHEST /TR \""+taskRun+L"\"";
    if(RunHidden(schtasks,args)!=0)Fail(L"Unable to create the startup auto-connect task.");
}
void CopyGuiAndLicenses(const std::wstring& installDir) {
    std::error_code ec;std::filesystem::create_directories(installDir,ec);if(ec)Fail(L"Unable to create application directory.");
    WriteResource(IDR_PAYLOAD_GUI,installDir+L"\\"+kGuiName);
    WriteResource(IDR_PAYLOAD_LICENSE,installDir+L"\\LICENSE.txt");
    WriteResource(IDR_PAYLOAD_NOTICES,installDir+L"\\THIRD_PARTY_NOTICES.txt");
}
std::wstring MakeTempDir() {
    wchar_t temp[MAX_PATH]{};if(!GetTempPathW(MAX_PATH,temp))FailWin(L"Locate temporary directory");
    std::wstring dir=std::wstring(temp)+L"RPi5WiFiSetup-"+std::to_wstring(GetCurrentProcessId());
    std::error_code ec;std::filesystem::remove_all(dir,ec);ec.clear();std::filesystem::create_directories(dir,ec);if(ec)Fail(L"Unable to create setup temporary directory.");return dir;
}
bool StageDriver(const std::wstring& inf,bool& reboot) {
    BOOL need=FALSE;
    if(DiInstallDriverW(nullptr,inf.c_str(),0,&need)){reboot=need!=FALSE;return true;}
    // Staging an INF after an install failure is not a successful installation.
    return false;
}

struct SuccessDialogState { HWND success{}; HFONT titleFont{}; bool done{}; int result{IDNO}; };

LRESULT CALLBACK SuccessWndProc(HWND hwnd,UINT msg,WPARAM wp,LPARAM lp) {
    auto* state=reinterpret_cast<SuccessDialogState*>(GetWindowLongPtrW(hwnd,GWLP_USERDATA));
    switch(msg) {
    case WM_CREATE:{
        auto* create=reinterpret_cast<CREATESTRUCTW*>(lp);
        state=reinterpret_cast<SuccessDialogState*>(create->lpCreateParams);
        SetWindowLongPtrW(hwnd,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(state));
        HFONT font=reinterpret_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
        state->titleFont=CreateFontW(-24,0,0,0,FW_SEMIBOLD,FALSE,FALSE,FALSE,DEFAULT_CHARSET,
            OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe UI");
        state->success=CreateWindowExW(0,L"STATIC",L"Installed Successfully",
            WS_CHILD|WS_VISIBLE|SS_CENTER,24,22,432,42,hwnd,reinterpret_cast<HMENU>(4101),GetModuleHandleW(nullptr),nullptr);
        HWND detail=CreateWindowExW(0,L"STATIC",
            L"Damian Edition Wi-Fi driver and Manager were installed.\r\n"
            L"A Windows restart is required before using the new installation.",
            WS_CHILD|WS_VISIBLE|SS_CENTER,28,76,424,60,hwnd,reinterpret_cast<HMENU>(4102),GetModuleHandleW(nullptr),nullptr);
        HWND now=CreateWindowExW(0,L"BUTTON",L"Restart Now",WS_CHILD|WS_VISIBLE|BS_DEFPUSHBUTTON,
            92,154,135,34,hwnd,reinterpret_cast<HMENU>(IDYES),GetModuleHandleW(nullptr),nullptr);
        HWND later=CreateWindowExW(0,L"BUTTON",L"Restart Later",WS_CHILD|WS_VISIBLE|BS_PUSHBUTTON,
            253,154,135,34,hwnd,reinterpret_cast<HMENU>(IDNO),GetModuleHandleW(nullptr),nullptr);
        SendMessageW(state->success,WM_SETFONT,reinterpret_cast<WPARAM>(state->titleFont?state->titleFont:font),TRUE);
        SendMessageW(detail,WM_SETFONT,reinterpret_cast<WPARAM>(font),TRUE);
        SendMessageW(now,WM_SETFONT,reinterpret_cast<WPARAM>(font),TRUE);
        SendMessageW(later,WM_SETFONT,reinterpret_cast<WPARAM>(font),TRUE);
        return 0;}
    case WM_CTLCOLORSTATIC:
        if(state && reinterpret_cast<HWND>(lp)==state->success) {
            HDC dc=reinterpret_cast<HDC>(wp);
            SetTextColor(dc,RGB(0,145,55));SetBkMode(dc,TRANSPARENT);
            return reinterpret_cast<LRESULT>(GetSysColorBrush(COLOR_WINDOW));
        }
        break;
    case WM_COMMAND:
        if(LOWORD(wp)==IDYES||LOWORD(wp)==IDNO) {
            if(state){state->result=LOWORD(wp);state->done=true;}
            DestroyWindow(hwnd);return 0;
        }
        break;
    case WM_CLOSE:
        if(state){state->result=IDNO;state->done=true;}
        DestroyWindow(hwnd);return 0;
    case WM_DESTROY:
        if(state && state->titleFont){DeleteObject(state->titleFont);state->titleFont=nullptr;}
        return 0;
    }
    return DefWindowProcW(hwnd,msg,wp,lp);
}

int ShowSuccessRestartDialog(HINSTANCE instance) {
    WNDCLASSEXW wc{sizeof(wc)};wc.hInstance=instance;wc.lpfnWndProc=SuccessWndProc;
    wc.lpszClassName=L"RPi5WiFiSetupSuccess";wc.hCursor=LoadCursorW(nullptr,IDC_ARROW);
    wc.hbrBackground=reinterpret_cast<HBRUSH>(COLOR_WINDOW+1);
    RegisterClassExW(&wc);
    SuccessDialogState state{};
    HWND hwnd=CreateWindowExW(WS_EX_DLGMODALFRAME|WS_EX_TOPMOST,wc.lpszClassName,rpiwifi::kSetupTitle,
        WS_CAPTION|WS_SYSMENU,CW_USEDEFAULT,CW_USEDEFAULT,500,235,nullptr,nullptr,instance,&state);
    if(!hwnd)return IDNO;
    ShowWindow(hwnd,SW_SHOW);UpdateWindow(hwnd);
    MSG msg{};
    while(!state.done && GetMessageW(&msg,nullptr,0,0)>0) {
        TranslateMessage(&msg);DispatchMessageW(&msg);
    }
    return state.result;
}

void RestartWindowsNow() {
    wchar_t systemDir[MAX_PATH]{};
    if(!GetSystemDirectoryW(systemDir,MAX_PATH))FailWin(L"Locate System32");
    std::wstring shutdown=std::wstring(systemDir)+L"\\shutdown.exe";
    if(RunHidden(shutdown,L"/r /t 0 /d p:4:1 /c \"RPi5 Wi-Fi installation completed\"")!=0)
        Fail(L"Windows restart request failed. Please restart Windows manually.");
}

bool NativeArm64() {
    SYSTEM_INFO si{};GetNativeSystemInfo(&si);return si.wProcessorArchitecture==PROCESSOR_ARCHITECTURE_ARM64;
}
bool SelfTest() {
    for(int id:{IDR_PAYLOAD_GUI,IDR_PAYLOAD_INF,IDR_PAYLOAD_SYS,IDR_PAYLOAD_CAT,IDR_PAYLOAD_CERT,
        IDR_PAYLOAD_FW,IDR_PAYLOAD_CLM,IDR_PAYLOAD_NVRAM,IDR_PAYLOAD_LICENSE,IDR_PAYLOAD_NOTICES}) {
        auto b=ResourceBytes(id);if(b.empty())return false;
    }
    auto inf=ResourceBytes(IDR_PAYLOAD_INF);
    std::string text(inf.begin(),inf.end());
    return text.find("ACPI\\RPI1060")!=std::string::npos&&
        text.find("RPI0011")==std::string::npos&&
        text.find("0.7.1.24")!=std::string::npos;
}

} // namespace

int APIENTRY wWinMain(HINSTANCE,HINSTANCE,LPWSTR cmd,int) {
    std::wstring args=cmd?cmd:L"";
    try{
        if(args.find(L"--self-test")!=std::wstring::npos)return SelfTest()?0:10;
        if(!NativeArm64()){MessageBoxW(nullptr,L"This installer is for Windows ARM64 on Raspberry Pi 5.",rpiwifi::kSetupTitle,MB_ICONERROR);return 3;}

        if(!SelfTest())Fail(L"The embedded driver package is not the Damian Edition RPI1060 package.");
        auto blocked=rpiwifi::InstallBlockReason(rpiwifi::QueryPlatform());
        if(!blocked.empty())Fail(blocked);

        std::wstring temp=MakeTempDir();
        for(const auto&p:kDriverPayloads)WriteResource(p.id,temp+L"\\"+p.file);

        auto cert=ResourceBytes(IDR_PAYLOAD_CERT);AddCertToStore(cert,L"ROOT");AddCertToStore(cert,L"TrustedPublisher");

        bool reboot=false;std::wstring inf=temp+L"\\rpi5cyw.inf";
        if(!StageDriver(inf,reboot))FailWin(L"Stage/install CYW43455 driver");
        (void)reboot;

        std::wstring installDir=Known(FOLDERID_ProgramFiles)+L"\\"+kProductDirName;
        CopyGuiAndLicenses(installDir);
        std::wstring gui=installDir+L"\\"+kGuiName;
        CreateShortcut(gui);CreateBootTask(gui);

        std::error_code ec;std::filesystem::remove_all(temp,ec);

        int restart=ShowSuccessRestartDialog(GetModuleHandleW(nullptr));
        if(restart==IDYES)RestartWindowsNow();
        return 0;
    }catch(const std::exception&e){
        std::wstring m=Wide(e.what());MessageBoxW(nullptr,m.c_str(),rpiwifi::kSetupTitle,MB_ICONERROR);return 2;
    }catch(...){MessageBoxW(nullptr,L"Installation failed.",rpiwifi::kSetupTitle,MB_ICONERROR);return 2;}
}
