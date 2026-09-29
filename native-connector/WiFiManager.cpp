// Portable Raspberry Pi 5 WiFi Manager - native Win32 ARM64
// Small standalone GUI for the project's private direct-SDIO control device.
// WPA2-Personal/AES only. Passwords and PMKs are kept in memory only.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <commctrl.h>
#include <bcrypt.h>
#include <shellapi.h>
#include <stdint.h>
#include <string>
#include <vector>
#include <thread>
#include <atomic>
#include <algorithm>

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "bcrypt.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")

static constexpr UINT IOCTL_STATUS     = 0x126004;
static constexpr UINT IOCTL_CONNECT    = 0x12A000;
static constexpr UINT IOCTL_DISCONNECT = 0x12A008;
static constexpr UINT IOCTL_SCAN_START = 0x12A014;
static constexpr UINT IOCTL_SCAN_STAT  = 0x126018;
static constexpr UINT IOCTL_SCAN_CANCEL= 0x12A01C;

enum : int { BAND_AUTO=0, BAND_24=1, BAND_5=2 };
enum : int {
    ID_SCAN=100, ID_LIST, ID_SSID, ID_BAND, ID_PASS, ID_SHOWPASS,
    ID_CONNECT, ID_DISCONNECT, ID_STATUS
};
enum : UINT { WM_APP_DONE = WM_APP + 1 };

struct Network {
    std::wstring ssid;
    std::wstring band;
    std::wstring security;
    int rssi{};
    uint32_t channel{};
    bool supported{};
};
struct WorkerResult {
    std::wstring message;
    std::vector<Network> networks;
    bool replaceNetworks{};
};

static HWND gMain{}, gList{}, gSsid{}, gBand{}, gPass{}, gStatus{};
static HWND gScan{}, gConnect{}, gDisconnect{};
static HFONT gFont{}, gTitleFont{};
static HBRUSH gBg{}, gEditBg{};
static COLORREF gBgColor=RGB(15,23,42), gPanelColor=RGB(17,24,39);
static COLORREF gTextColor=RGB(226,232,240), gMutedColor=RGB(148,163,184);
static std::vector<Network> gNetworks;
static std::atomic<bool> gBusy{false};

static uint32_t U32(const uint8_t* p) {
    return (uint32_t)p[0] | ((uint32_t)p[1]<<8) | ((uint32_t)p[2]<<16) | ((uint32_t)p[3]<<24);
}
static void Put32(uint8_t* p,uint32_t v) {
    p[0]=(uint8_t)v; p[1]=(uint8_t)(v>>8); p[2]=(uint8_t)(v>>16); p[3]=(uint8_t)(v>>24);
}
static std::wstring Hex32(uint32_t v) {
    wchar_t b[16]; wsprintfW(b,L"0x%08X",v); return b;
}
static std::wstring WinError(DWORD e) {
    wchar_t* p=nullptr;
    DWORD n=FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER|FORMAT_MESSAGE_FROM_SYSTEM|FORMAT_MESSAGE_IGNORE_INSERTS,
                           nullptr,e,0,(wchar_t*)&p,0,nullptr);
    std::wstring s=n&&p?std::wstring(p,n):L"Windows error "+std::to_wstring(e);
    if(p) LocalFree(p);
    while(!s.empty() && (s.back()==L'\r'||s.back()==L'\n')) s.pop_back();
    return s;
}
static std::string Utf8(const std::wstring& s) {
    if(s.empty()) return {};
    int n=WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,s.data(),(int)s.size(),nullptr,0,nullptr,nullptr);
    if(n<=0) return {};
    std::string o((size_t)n,'\0');
    if(!WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,s.data(),(int)s.size(),o.data(),n,nullptr,nullptr)) return {};
    return o;
}
static std::wstring Wide(const char* p,int n) {
    if(n<=0) return {};
    int m=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,p,n,nullptr,0);
    if(m<=0) return L"<non-displayable>";
    std::wstring o((size_t)m,L'\0');
    MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,p,n,o.data(),m);
    return o;
}
static HANDLE OpenDevice(DWORD access=GENERIC_READ|GENERIC_WRITE) {
    HANDLE h=CreateFileW(L"\\\\.\\Rpi5CywControl",access,FILE_SHARE_READ|FILE_SHARE_WRITE,
                         nullptr,OPEN_EXISTING,0,nullptr);
    if(h==INVALID_HANDLE_VALUE) throw std::runtime_error("open");
    return h;
}
static std::vector<uint8_t> Ioctl(UINT code,const void* in=nullptr,DWORD inLen=0,DWORD outLen=0) {
    HANDLE h=OpenDevice();
    std::vector<uint8_t> out(outLen);
    DWORD got=0;
    BOOL ok=DeviceIoControl(h,code,(LPVOID)in,inLen,outLen?out.data():nullptr,outLen,&got,nullptr);
    DWORD e=ok?0:GetLastError(); CloseHandle(h);
    if(!ok) throw std::runtime_error(("io:"+std::to_string(e)));
    out.resize(got);
    return out;
}
static std::wstring ExceptionText(const std::exception& e) {
    std::string x=e.what();
    if(x.rfind("io:",0)==0) {
        DWORD n=(DWORD)strtoul(x.c_str()+3,nullptr,10);
        return L"Driver request failed: "+WinError(n);
    }
    if(x=="open") return L"Wi-Fi driver is not available. Install/restart the driver first.";
    return L"Wi-Fi operation failed.";
}
struct Live {
    uint32_t phase{}, status{}; bool auth{};
    bool idle() const { return phase==500 && status==0 && !auth; }
};
static Live ReadLive() {
    auto b=Ioctl(IOCTL_STATUS,nullptr,0,96);
    if(b.size()<32 || U32(b.data())<1 || U32(b.data())>3) throw std::runtime_error("status");
    return {U32(b.data()+4),U32(b.data()+8),U32(b.data()+28)==1};
}
static bool Pbkdf2(const std::string& ssid,const std::string& pass,uint8_t out[32]) {
    BCRYPT_ALG_HANDLE alg=nullptr;
    NTSTATUS s=BCryptOpenAlgorithmProvider(&alg,BCRYPT_SHA1_ALGORITHM,nullptr,BCRYPT_ALG_HANDLE_HMAC_FLAG);
    if(s<0) return false;
    s=BCryptDeriveKeyPBKDF2(alg,(PUCHAR)pass.data(),(ULONG)pass.size(),
        (PUCHAR)ssid.data(),(ULONG)ssid.size(),4096,out,32,0);
    BCryptCloseAlgorithmProvider(alg,0);
    return s>=0;
}
static std::wstring SignalText(int rssi) {
    if(rssi>=-50) return L"Excellent";
    if(rssi>=-60) return L"Good";
    if(rssi>=-70) return L"Fair";
    return L"Weak";
}
static WorkerResult ScanNetworks() {
    WorkerResult result; result.replaceNetworks=true;
    auto live=ReadLive();
    if(!live.idle()) { result.message=L"Disconnect first. Scanning is available when the adapter is ready."; return result; }
    uint8_t start[8]{}; Put32(start,1); start[4]='Z'; start[5]='Z';
    auto before=Ioctl(IOCTL_SCAN_STAT,nullptr,0,3616);
    uint32_t previous=before.size()==3616?U32(before.data()+4):0;
    Ioctl(IOCTL_SCAN_START,start,sizeof(start),0);
    uint32_t generation=0;
    ULONGLONG deadline=GetTickCount64()+90000;
    while(GetTickCount64()<deadline) {
        Sleep(250);
        auto b=Ioctl(IOCTL_SCAN_STAT,nullptr,0,3616);
        if(b.size()!=3616 || U32(b.data())!=1) continue;
        uint32_t gen=U32(b.data()+4), state=U32(b.data()+8), status=U32(b.data()+12);
        if(gen==previous) continue;
        if(!generation) generation=gen;
        if(gen!=generation) { result.message=L"Scan ownership changed. Scan again."; return result; }
        if(state==3 && status==0) {
            uint32_t count=std::min<uint32_t>(U32(b.data()+16),64);
            for(uint32_t i=0;i<count;i++) {
                size_t o=32+(size_t)i*56;
                uint32_t len=U32(b.data()+o), sec=U32(b.data()+o+48), ch=U32(b.data()+o+52);
                if(len>32) continue;
                Network n;
                n.ssid=len?Wide((const char*)b.data()+o+4,(int)len):L"<Hidden network>";
                n.rssi=(int32_t)U32(b.data()+o+44);
                n.channel=ch;
                n.band=(ch>=1&&ch<=14)?L"2.4 GHz":((ch>=32&&ch<=196)?L"5 GHz":L"Unknown");
                n.security=sec==2?L"WPA2":L"Unsupported";
                n.supported=sec==2 && len>0 && n.band!=L"Unknown";
                result.networks.push_back(std::move(n));
            }
            std::stable_sort(result.networks.begin(),result.networks.end(),
                [](const Network&a,const Network&b){return a.rssi>b.rssi;});
            result.message=L"Scan complete. Select a WPA2 network.";
            return result;
        }
        if(state==4||state==5||status!=0) {
            result.message=L"Scan failed. Driver status "+Hex32(status)+L".";
            return result;
        }
    }
    if(generation) { uint8_t g[4]; Put32(g,generation); try { Ioctl(IOCTL_SCAN_CANCEL,g,4,0); } catch(...){} }
    result.message=L"Scan timed out.";
    return result;
}
static WorkerResult ConnectNetwork(std::wstring ssidW,std::wstring passW,int band) {
    WorkerResult r;
    auto live=ReadLive();
    if(live.auth) { r.message=L"Already connected. Disconnect before selecting another network."; return r; }
    if(!live.idle()) { r.message=L"Driver is not ready to connect yet."; return r; }
    std::string ssid=Utf8(ssidW), pass=Utf8(passW);
    if(ssid.empty()||ssid.size()>32) { r.message=L"SSID must be 1-32 UTF-8 bytes."; return r; }
    if(pass.size()<8||pass.size()>63) { r.message=L"WPA2 password must be 8-63 characters."; return r; }
    if(band<0||band>2) band=0;
    uint8_t pmk[32]{};
    if(!Pbkdf2(ssid,pass,pmk)) { r.message=L"Could not derive the WPA2 key."; return r; }
    uint8_t req[76]{};
    Put32(req,1); Put32(req+4,(uint32_t)ssid.size());
    req[8]='Z'; req[9]='Z'; req[10]=(uint8_t)band;
    memcpy(req+12,ssid.data(),ssid.size()); memcpy(req+44,pmk,32);
    SecureZeroMemory(pmk,sizeof(pmk));
    try { Ioctl(IOCTL_CONNECT,req,sizeof(req),0); }
    catch(...) { SecureZeroMemory(req,sizeof(req)); throw; }
    SecureZeroMemory(req,sizeof(req));
    ULONGLONG deadline=GetTickCount64()+180000;
    while(GetTickCount64()<deadline) {
        Sleep(250);
        live=ReadLive();
        if(live.status) {
            r.message=L"Connection failed at driver phase "+std::to_wstring(live.phase)+
                      L", status "+Hex32(live.status)+L". Run diagnostics for the exact firmware step.";
            return r;
        }
        if(live.auth) { r.message=L"Connected to "+ssidW+L"."; return r; }
    }
    r.message=L"Connection timed out.";
    return r;
}
static WorkerResult DisconnectNetwork() {
    WorkerResult r; Ioctl(IOCTL_DISCONNECT,nullptr,0,0);
    r.message=L"Disconnect requested."; return r;
}
template<class F>
static void RunWorker(F fn) {
    if(gBusy.exchange(true)) return;
    EnableWindow(gScan,FALSE); EnableWindow(gConnect,FALSE); EnableWindow(gDisconnect,FALSE);
    SetWindowTextW(gStatus,L"Working...");
    std::thread([fn=std::move(fn)]() mutable {
        auto* result=new WorkerResult();
        try { *result=fn(); }
        catch(const std::exception& e) { result->message=ExceptionText(e); }
        catch(...) { result->message=L"Unexpected Wi-Fi operation failure."; }
        PostMessageW(gMain,WM_APP_DONE,0,(LPARAM)result);
    }).detach();
}
static void SetFont(HWND h,HFONT f) { SendMessageW(h,WM_SETFONT,(WPARAM)f,TRUE); }
static HWND Label(const wchar_t* text,int x,int y,int w,int h,HFONT f=nullptr) {
    HWND c=CreateWindowExW(0,L"STATIC",text,WS_CHILD|WS_VISIBLE,x,y,w,h,gMain,nullptr,nullptr,nullptr);
    SetFont(c,f?f:gFont); return c;
}
static HWND Button(const wchar_t* text,int id,int x,int y,int w,int h) {
    HWND c=CreateWindowExW(0,L"BUTTON",text,WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_PUSHBUTTON,x,y,w,h,gMain,(HMENU)(INT_PTR)id,nullptr,nullptr);
    SetFont(c,gFont); return c;
}
static void FillNetworks() {
    ListView_DeleteAllItems(gList);
    for(size_t i=0;i<gNetworks.size();++i) {
        const auto& n=gNetworks[i];
        LVITEMW it{}; it.mask=LVIF_TEXT|LVIF_PARAM; it.iItem=(int)i;
        it.pszText=(LPWSTR)n.ssid.c_str(); it.lParam=(LPARAM)i;
        ListView_InsertItem(gList,&it);
        std::wstring sig=SignalText(n.rssi)+L" ("+std::to_wstring(n.rssi)+L" dBm)";
        ListView_SetItemText(gList,(int)i,1,(LPWSTR)sig.c_str());
        ListView_SetItemText(gList,(int)i,2,(LPWSTR)n.band.c_str());
        ListView_SetItemText(gList,(int)i,3,(LPWSTR)n.security.c_str());
    }
}
static void RefreshButtons() {
    bool busy=gBusy.load();
    EnableWindow(gScan,!busy); EnableWindow(gConnect,!busy); EnableWindow(gDisconnect,!busy);
}
static void UpdateLiveStatus() {
    if(gBusy.load()) return;
    try {
        Live l=ReadLive();
        if(l.auth) SetWindowTextW(gStatus,L"Connected. You can disconnect or run the performance checker.");
        else if(l.idle()) SetWindowTextW(gStatus,L"Ready. Scan for a network or enter an SSID.");
        else if(l.status) {
            std::wstring s=L"Driver error "+Hex32(l.status)+L" at phase "+std::to_wstring(l.phase)+L".";
            SetWindowTextW(gStatus,s.c_str());
        } else {
            std::wstring s=L"Driver starting... phase "+std::to_wstring(l.phase);
            SetWindowTextW(gStatus,s.c_str());
        }
    } catch(...) {
        SetWindowTextW(gStatus,L"Waiting for the Raspberry Pi 5 Wi-Fi driver.");
    }
}
static LRESULT CALLBACK WndProc(HWND h,UINT m,WPARAM w,LPARAM l) {
    switch(m) {
    case WM_CREATE: {
        gMain=h;
        Label(L"WiFi Manager",24,18,360,38,gTitleFont);
        Label(L"Nearby networks",24,68,250,24);
        gScan=Button(L"Scan",274,62,86,32,ID_SCAN);
        gList=CreateWindowExW(WS_EX_CLIENTEDGE,WC_LISTVIEWW,L"",WS_CHILD|WS_VISIBLE|WS_TABSTOP|LVS_REPORT|LVS_SINGLESEL,
            24,102,420,340,h,(HMENU)ID_LIST,nullptr,nullptr);
        SetFont(gList,gFont);
        ListView_SetExtendedListViewStyle(gList,LVS_EX_FULLROWSELECT|LVS_EX_DOUBLEBUFFER);
        ListView_SetBkColor(gList,gPanelColor); ListView_SetTextBkColor(gList,gPanelColor); ListView_SetTextColor(gList,gTextColor);
        struct C{const wchar_t*n;int w;}; C cols[]={{L"SSID",150},{L"Signal",115},{L"Band",75},{L"Security",70}};
        for(int i=0;i<4;i++){LVCOLUMNW c{};c.mask=LVCF_TEXT|LVCF_WIDTH;c.pszText=(LPWSTR)cols[i].n;c.cx=cols[i].w;ListView_InsertColumn(gList,i,&c);}
        Label(L"Connect",480,68,220,30,gTitleFont);
        Label(L"Network name (SSID)",480,118,240,22);
        gSsid=CreateWindowExW(WS_EX_CLIENTEDGE,L"EDIT",L"",WS_CHILD|WS_VISIBLE|WS_TABSTOP|ES_AUTOHSCROLL,
            480,144,276,34,h,(HMENU)ID_SSID,nullptr,nullptr); SetFont(gSsid,gFont);
        Label(L"Band",480,194,120,22);
        gBand=CreateWindowExW(0,L"COMBOBOX",L"",WS_CHILD|WS_VISIBLE|WS_TABSTOP|CBS_DROPDOWNLIST,
            480,220,276,140,h,(HMENU)ID_BAND,nullptr,nullptr); SetFont(gBand,gFont);
        SendMessageW(gBand,CB_ADDSTRING,0,(LPARAM)L"Auto"); SendMessageW(gBand,CB_ADDSTRING,0,(LPARAM)L"2.4 GHz");
        SendMessageW(gBand,CB_ADDSTRING,0,(LPARAM)L"5 GHz"); SendMessageW(gBand,CB_SETCURSEL,0,0);
        Label(L"Password",480,274,180,22);
        gPass=CreateWindowExW(WS_EX_CLIENTEDGE,L"EDIT",L"",WS_CHILD|WS_VISIBLE|WS_TABSTOP|ES_PASSWORD|ES_AUTOHSCROLL,
            480,300,276,34,h,(HMENU)ID_PASS,nullptr,nullptr); SetFont(gPass,gFont);
        gConnect=Button(L"Connect",ID_CONNECT,480,362,132,38);
        gDisconnect=Button(L"Disconnect",ID_DISCONNECT,624,362,132,38);
        gStatus=CreateWindowExW(0,L"STATIC",L"Waiting for driver...",WS_CHILD|WS_VISIBLE|SS_LEFT,
            24,462,732,42,h,(HMENU)ID_STATUS,nullptr,nullptr); SetFont(gStatus,gFont);
        SetTimer(h,1,1000,nullptr); UpdateLiveStatus(); return 0;
    }
    case WM_COMMAND:
        if(HIWORD(w)==BN_CLICKED) {
            if(LOWORD(w)==ID_SCAN) RunWorker([]{return ScanNetworks();});
            else if(LOWORD(w)==ID_CONNECT) {
                wchar_t ssid[128]{},pass[128]{}; GetWindowTextW(gSsid,ssid,128); GetWindowTextW(gPass,pass,128);
                int band=(int)SendMessageW(gBand,CB_GETCURSEL,0,0);
                std::wstring s=ssid,p=pass; SecureZeroMemory(pass,sizeof(pass)); SetWindowTextW(gPass,L"");
                RunWorker([s,p,band]() mutable { auto r=ConnectNetwork(s,p,band); if(!p.empty()) SecureZeroMemory(p.data(),p.size()*sizeof(wchar_t)); return r; });
            } else if(LOWORD(w)==ID_DISCONNECT) RunWorker([]{return DisconnectNetwork();});
        }
        return 0;
    case WM_NOTIFY: {
        auto* n=(NMHDR*)l;
        if(n->idFrom==ID_LIST && n->code==LVN_ITEMCHANGED) {
            auto* v=(NMLISTVIEW*)l;
            if((v->uNewState&LVIS_SELECTED) && v->iItem>=0 && (size_t)v->iItem<gNetworks.size()) {
                const auto& x=gNetworks[(size_t)v->iItem];
                if(x.supported) {
                    SetWindowTextW(gSsid,x.ssid.c_str());
                    SendMessageW(gBand,CB_SETCURSEL,x.band==L"5 GHz"?2:1,0);
                    SetWindowTextW(gStatus,L"Network selected. Enter its WPA2 password.");
                } else SetWindowTextW(gStatus,L"This entry is not a supported WPA2 network.");
            }
        }
        return 0;
    }
    case WM_APP_DONE: {
        auto* r=(WorkerResult*)l;
        if(r->replaceNetworks) { gNetworks=std::move(r->networks); FillNetworks(); }
        SetWindowTextW(gStatus,r->message.c_str()); delete r;
        gBusy=false; RefreshButtons(); return 0;
    }
    case WM_TIMER: UpdateLiveStatus(); return 0;
    case WM_CTLCOLORSTATIC: {
        HDC dc=(HDC)w; SetTextColor(dc,gTextColor); SetBkColor(dc,gBgColor); return (LRESULT)gBg;
    }
    case WM_CTLCOLOREDIT: {
        HDC dc=(HDC)w; SetTextColor(dc,gTextColor); SetBkColor(dc,gPanelColor); return (LRESULT)gEditBg;
    }
    case WM_ERASEBKGND: {
        RECT r; GetClientRect(h,&r); FillRect((HDC)w,&r,gBg); return 1;
    }
    case WM_DESTROY: KillTimer(h,1); PostQuitMessage(0); return 0;
    }
    return DefWindowProcW(h,m,w,l);
}
static bool SelfTest() {
    uint8_t out[32]{};
    const uint8_t expected[32]={0xF4,0x2C,0x6F,0xC5,0x2D,0xF0,0xEB,0xEF,0x9E,0xBB,0x4B,0x90,0xB3,0x8A,0x5F,0x90,
        0x2E,0x83,0xFE,0x1B,0x13,0x5A,0x70,0xE2,0x3A,0xED,0x76,0x2E,0x97,0x10,0xA1,0x2E};
    if(!Pbkdf2("IEEE","password",out) || memcmp(out,expected,32)!=0) return false;
    SecureZeroMemory(out,sizeof(out));
    uint8_t req[76]{}; Put32(req,1); Put32(req+4,4); req[8]='Z';req[9]='Z';req[10]=2; memcpy(req+12,"IEEE",4);
    return U32(req)==1 && U32(req+4)==4 && req[8]=='Z' && req[9]=='Z' && req[10]==2 && req[11]==0;
}
int WINAPI wWinMain(HINSTANCE hi,HINSTANCE,LPWSTR cmd,int show) {
    if(cmd && wcsstr(cmd,L"--self-test")) return SelfTest()?0:1;
    INITCOMMONCONTROLSEX ic{sizeof(ic),ICC_LISTVIEW_CLASSES|ICC_STANDARD_CLASSES}; InitCommonControlsEx(&ic);
    gBg=CreateSolidBrush(gBgColor); gEditBg=CreateSolidBrush(gPanelColor);
    gFont=CreateFontW(-18,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe UI");
    gTitleFont=CreateFontW(-28,0,0,0,FW_SEMIBOLD,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe UI");
    WNDCLASSW wc{}; wc.hInstance=hi; wc.lpszClassName=L"RPi5NativeWiFiManager"; wc.lpfnWndProc=WndProc;
    wc.hCursor=LoadCursorW(nullptr,IDC_ARROW); wc.hbrBackground=gBg; wc.hIcon=LoadIconW(nullptr,IDI_APPLICATION);
    if(!RegisterClassW(&wc)) return 2;
    HWND h=CreateWindowExW(0,wc.lpszClassName,L"Raspberry Pi 5 WiFi Manager",WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU|WS_MINIMIZEBOX,
        CW_USEDEFAULT,CW_USEDEFAULT,800,555,nullptr,nullptr,hi,nullptr);
    if(!h) return 3;
    ShowWindow(h,show); UpdateWindow(h);
    MSG msg{}; while(GetMessageW(&msg,nullptr,0,0)>0){TranslateMessage(&msg);DispatchMessageW(&msg);}
    DeleteObject(gFont); DeleteObject(gTitleFont); DeleteObject(gBg); DeleteObject(gEditBg);
    return (int)msg.wParam;
}
