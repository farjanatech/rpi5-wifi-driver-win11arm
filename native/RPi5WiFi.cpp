#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <commctrl.h>
#include <shellapi.h>
#include <algorithm>
#include <atomic>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include "RPi5WiFiCommon.h"
#include "resource.h"

#pragma comment(lib, "comctl32.lib")

using namespace rpiwifi;

namespace {

enum : int {
    IDC_STATUS=1001, IDC_COUNTRY, IDC_SCAN, IDC_NETWORKS, IDC_PASSWORD,
    IDC_CONNECT, IDC_DISCONNECT, IDC_SAVE_PROFILE, IDC_AUTO, IDC_PROFILES,
    IDC_DELETE_PROFILE, IDC_HINT, IDC_SELECTED
};
constexpr UINT WM_APP_DONE = WM_APP + 10;

enum class OpKind { Scan, Connect, Disconnect, SaveProfile, DeleteProfile };

struct AsyncResult {
    OpKind kind{};
    bool ok{};
    std::wstring message;
    std::vector<NetworkEntry> networks;
};

struct App {
    HWND hwnd{}, status{}, country{}, scan{}, networks{}, password{}, connect{}, disconnect{},
        saveProfile{}, autoBox{}, profiles{}, deleteProfile{}, hint{}, selected{};
    HFONT font{};
    UINT dpi{96};
    std::atomic_bool busy{false};
    std::vector<NetworkEntry> scanEntries;
    ProfileDb db;
};

App g;

int S(int value) { return MulDiv(value, static_cast<int>(g.dpi ? g.dpi : 96), 96); }

std::wstring WidenError(const std::exception& e) {
    try { return Utf8ToWide(e.what()); } catch (...) { return L"Unexpected error."; }
}
std::wstring GetText(HWND h) {
    int n=GetWindowTextLengthW(h); std::wstring s(static_cast<size_t>(n)+1,L'\0');
    if(n) GetWindowTextW(h,s.data(),n+1);
    s.resize(static_cast<size_t>(n)); return s;
}
void SetText(HWND h,const std::wstring&s){SetWindowTextW(h,s.c_str());}
std::wstring UpperCountry() {
    std::wstring c=GetText(g.country);
    for(auto& ch:c) if(ch>=L'a'&&ch<=L'z')ch-=32;
    SetText(g.country,c); return c;
}
int SelectedIndex(HWND list) {
    return ListView_GetNextItem(list,-1,LVNI_SELECTED);
}
std::string SelectedSsid(bool requireSupported=true) {
    int i=SelectedIndex(g.networks);
    if(i>=0 && static_cast<size_t>(i)<g.scanEntries.size()) {
        const auto& n=g.scanEntries[static_cast<size_t>(i)];
        if(!requireSupported || n.supported)return n.ssidUtf8;
        throw std::runtime_error("The selected network is not supported. Use WPA2-Personal/AES.");
    }
    int p=SelectedIndex(g.profiles);
    if(p>=0 && static_cast<size_t>(p)<g.db.profiles.size())return g.db.profiles[static_cast<size_t>(p)].ssidUtf8;
    throw std::runtime_error("Select a network or saved profile first.");
}
void SetBusy(bool busy,const std::wstring& status=L"") {
    g.busy=busy;
    EnableWindow(g.scan,!busy);EnableWindow(g.connect,!busy);EnableWindow(g.disconnect,!busy);
    EnableWindow(g.saveProfile,!busy);EnableWindow(g.deleteProfile,!busy);
    if(!status.empty())SetText(g.status,status);
}
void AddColumn(HWND list,int index,int width,const wchar_t* text) {
    LVCOLUMNW c{};c.mask=LVCF_TEXT|LVCF_WIDTH|LVCF_SUBITEM;c.pszText=const_cast<wchar_t*>(text);c.cx=S(width);c.iSubItem=index;
    ListView_InsertColumn(list,index,&c);
}
void AddItem(HWND list,int row,int col,const std::wstring& text) {
    if(col==0){LVITEMW i{};i.mask=LVIF_TEXT;i.iItem=row;i.pszText=const_cast<wchar_t*>(text.c_str());ListView_InsertItem(list,&i);}
    else ListView_SetItemText(list,row,col,const_cast<wchar_t*>(text.c_str()));
}
void RefreshProfiles() {
    try{g.db=ProfileStore::Load();}catch(const std::exception&e){SetText(g.status,L"Profile error: "+WidenError(e));g.db={};}
    ListView_DeleteAllItems(g.profiles);
    for(size_t i=0;i<g.db.profiles.size();++i){
        const auto&p=g.db.profiles[i];
        AddItem(g.profiles,static_cast<int>(i),0,Utf8ToWide(p.ssidUtf8));
        AddItem(g.profiles,static_cast<int>(i),1,p.autoConnect?L"Yes":L"No");
    }
    if(!g.db.country.empty())SetText(g.country,Utf8ToWide(g.db.country));
}
void RefreshNetworks(const std::vector<NetworkEntry>& networks) {
    g.scanEntries=networks;ListView_DeleteAllItems(g.networks);
    for(size_t i=0;i<networks.size();++i){
        const auto& n=networks[i];
        AddItem(g.networks,static_cast<int>(i),0,n.display);
        AddItem(g.networks,static_cast<int>(i),1,n.band);
        AddItem(g.networks,static_cast<int>(i),2,std::to_wstring(n.channel));
        AddItem(g.networks,static_cast<int>(i),3,std::to_wstring(n.rssi)+L" dBm");
        AddItem(g.networks,static_cast<int>(i),4,n.security);
    }
}
void ApplySelectionHint() {
    try{
        std::string ssid=SelectedSsid(false);
        const Profile* p=ProfileStore::Find(g.db,ssid);
        SendMessageW(g.autoBox,BM_SETCHECK,p&&p->autoConnect?BST_CHECKED:BST_UNCHECKED,0);

        int i=SelectedIndex(g.networks);
        if(i>=0 && static_cast<size_t>(i)<g.scanEntries.size()) {
            const auto& n=g.scanEntries[static_cast<size_t>(i)];
            SetText(g.selected,L"Selected Wi-Fi: "+n.display+L"  —  "+n.band+
                L", Channel "+std::to_wstring(n.channel)+L", "+std::to_wstring(n.rssi)+L" dBm");
        } else {
            SetText(g.selected,L"Selected saved profile: "+Utf8ToWide(ssid));
        }
        SetText(g.hint,p?L"Saved key available. Leave Password blank to use it.":
            L"Enter the WPA2 password for the green selected Wi-Fi, then Connect or Save Profile.");
    }catch(...){
        SetText(g.selected,L"No Wi-Fi selected.");
        SetText(g.hint,L"Select a Wi-Fi network above. The selected network will be highlighted green.");
    }
}
template<class F>
void StartAsync(OpKind kind,const std::wstring& startText,F fn) {
    if(g.busy.exchange(true))return;
    SetBusy(true,startText);
    HWND hwnd=g.hwnd;
    std::thread([hwnd,kind,fn=std::move(fn)]() mutable {
        auto result=std::make_unique<AsyncResult>();result->kind=kind;
        try{fn(*result);result->ok=true;}
        catch(const std::exception&e){result->ok=false;result->message=WidenError(e);}
        catch(...){result->ok=false;result->message=L"Unexpected operation failure.";}
        PostMessageW(hwnd,WM_APP_DONE,0,reinterpret_cast<LPARAM>(result.release()));
    }).detach();
}
std::array<uint8_t,32> KeyForSsid(const std::string& ssid,const std::wstring& password) {
    if(!password.empty())return DerivePmk(ssid,password);
    const Profile* p=ProfileStore::Find(g.db,ssid);
    if(!p)throw std::runtime_error("Enter the WPA2 password or select a saved profile.");
    return UnprotectPmk(p->protectedPmk);
}
void DoScan() {
    std::wstring country=UpperCountry();
    if(!ValidCountry(country)){MessageBoxW(g.hwnd,L"Country must be exactly two uppercase letters for your physical location.",L"RPi5 Wi-Fi",MB_ICONWARNING);return;}
    StartAsync(OpKind::Scan,L"Scanning nearby networks...",[country](AsyncResult&r){
        Driver d;auto report=ScanNetworks(d,country,[&](const std::wstring& m){r.message=m;});r.networks=std::move(report.networks);
        r.message=L"Scan complete.";
    });
}
void DoConnect() {
    try{
        std::wstring country=UpperCountry();if(!ValidCountry(country))throw std::runtime_error("Country must be exactly two uppercase letters.");
        std::string ssid=SelectedSsid();
        std::wstring password=GetText(g.password);
        auto pmk=KeyForSsid(ssid,password);
        SetText(g.password,L"");
        StartAsync(OpKind::Connect,L"Connecting...",[country,ssid,pmk](AsyncResult&r) mutable {
            Driver d;ConnectNetwork(d,country,ssid,pmk,[&](const std::wstring&m){r.message=m;});
            SecureZeroMemory(pmk.data(),pmk.size());r.message=L"Connected and authenticated.";
        });
        SecureZeroMemory(pmk.data(),pmk.size());
    }catch(const std::exception&e){MessageBoxW(g.hwnd,WidenError(e).c_str(),L"RPi5 Wi-Fi",MB_ICONWARNING);}
}
void DoDisconnect() {
    StartAsync(OpKind::Disconnect,L"Disconnecting...",[](AsyncResult&r){Driver d;DisconnectNetwork(d);r.message=L"Disconnected and ready.";});
}
void DoSaveProfile() {
    try{
        std::wstring country=UpperCountry();if(!ValidCountry(country))throw std::runtime_error("Country must be exactly two uppercase letters.");
        std::string ssid=SelectedSsid(false);std::wstring password=GetText(g.password);
        bool autoConnect=SendMessageW(g.autoBox,BM_GETCHECK,0,0)==BST_CHECKED;
        ProfileDb db=ProfileStore::Load();db.country=WideToUtf8(country);
        const Profile* existing=ProfileStore::Find(db,ssid);
        if(password.empty() && existing){
            Profile* p=ProfileStore::Find(db,ssid);p->autoConnect=autoConnect;p->lastUsed=NowFileTime();ProfileStore::Save(db);
        }else{
            auto pmk=DerivePmk(ssid,password);ProfileStore::Upsert(db,ssid,pmk,autoConnect);SecureZeroMemory(pmk.data(),pmk.size());ProfileStore::Save(db);
        }
        SetText(g.password,L"");RefreshProfiles();SetText(g.status,L"Profile saved securely.");
    }catch(const std::exception&e){MessageBoxW(g.hwnd,WidenError(e).c_str(),L"Save profile",MB_ICONWARNING);}
}
void DoDeleteProfile() {
    int p=SelectedIndex(g.profiles);if(p<0||static_cast<size_t>(p)>=g.db.profiles.size()){MessageBoxW(g.hwnd,L"Select a saved profile first.",L"Delete profile",MB_ICONINFORMATION);return;}
    std::string ssid=g.db.profiles[static_cast<size_t>(p)].ssidUtf8;
    if(MessageBoxW(g.hwnd,(L"Delete saved profile \""+Utf8ToWide(ssid)+L"\"?").c_str(),L"Delete profile",MB_YESNO|MB_ICONQUESTION)!=IDYES)return;
    try{ProfileDb db=ProfileStore::Load();ProfileStore::Remove(db,ssid);ProfileStore::Save(db);RefreshProfiles();SetText(g.status,L"Profile deleted.");}
    catch(const std::exception&e){MessageBoxW(g.hwnd,WidenError(e).c_str(),L"Delete profile",MB_ICONWARNING);}
}
void PollStatus() {
    if(g.busy)return;
    try{
        Driver d;LiveState s=d.Status();
        if(s.status!=0){wchar_t b[96]{};swprintf_s(b,L"Driver error 0x%08X (phase %u).",s.status,s.phase);SetText(g.status,b);}
        else if(s.authenticated)SetText(g.status,L"Authenticated. Windows is using the CYW43455 Ethernet-style adapter.");
        else if(s.phase==500)SetText(g.status,L"Disconnected and ready.");
        else SetText(g.status,L"Driver starting - phase "+std::to_wstring(s.phase)+L".");
    }catch(const std::exception&e){SetText(g.status,L"Driver unavailable: "+WidenError(e));}
}
void Layout(HWND hwnd) {
    RECT rc{};GetClientRect(hwnd,&rc);int w=rc.right-rc.left;int h=rc.bottom-rc.top;
    MoveWindow(g.status,S(14),S(10),w-S(28),S(42),TRUE);
    MoveWindow(GetDlgItem(hwnd,2001),S(14),S(61),S(132),S(24),TRUE);
    MoveWindow(g.country,S(150),S(57),S(64),S(29),TRUE);
    MoveWindow(g.scan,S(230),S(56),S(120),S(31),TRUE);
    MoveWindow(g.networks,S(14),S(98),w-S(28),S(270),TRUE);

    MoveWindow(g.selected,S(14),S(380),w-S(28),S(30),TRUE);
    MoveWindow(GetDlgItem(hwnd,2002),S(14),S(422),S(120),S(24),TRUE);
    MoveWindow(g.password,S(140),S(417),w-S(154),S(30),TRUE);

    MoveWindow(g.autoBox,S(14),S(461),S(220),S(28),TRUE);
    MoveWindow(g.hint,S(245),S(456),w-S(259),S(46),TRUE);

    MoveWindow(g.connect,S(14),S(515),S(110),S(34),TRUE);
    MoveWindow(g.disconnect,S(132),S(515),S(110),S(34),TRUE);
    MoveWindow(g.saveProfile,S(250),S(515),S(165),S(34),TRUE);

    MoveWindow(GetDlgItem(hwnd,2003),S(14),S(566),S(160),S(24),TRUE);
    MoveWindow(g.deleteProfile,w-S(144),S(560),S(130),S(32),TRUE);
    MoveWindow(g.profiles,S(14),S(600),w-S(28),max(S(105),h-S(614)),TRUE);
}
LRESULT CALLBACK WndProc(HWND hwnd,UINT msg,WPARAM wp,LPARAM lp) {
    switch(msg){
    case WM_CREATE:{
        g.hwnd=hwnd;
        g.dpi=GetDpiForWindow(hwnd);
        g.font=CreateFontW(-S(16),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe UI");
        auto C=[&](LPCWSTR cls,LPCWSTR text,DWORD style,int id)->HWND{
            HWND h=CreateWindowExW(0,cls,text,WS_CHILD|WS_VISIBLE|style,0,0,0,0,hwnd,reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),GetModuleHandleW(nullptr),nullptr);
            SendMessageW(h,WM_SETFONT,reinterpret_cast<WPARAM>(g.font),TRUE);return h;
        };
        g.status=C(L"STATIC",L"Starting...",SS_LEFT,IDC_STATUS);
        C(L"STATIC",L"Country (2 letters):",SS_LEFT,2001);
        g.country=C(L"EDIT",L"",WS_BORDER|ES_UPPERCASE|ES_AUTOHSCROLL,IDC_COUNTRY);
        SendMessageW(g.country,EM_SETLIMITTEXT,2,0);
        g.scan=C(L"BUTTON",L"Scan / Refresh",BS_PUSHBUTTON,IDC_SCAN);
        g.networks=CreateWindowExW(WS_EX_CLIENTEDGE,WC_LISTVIEWW,L"",WS_CHILD|WS_VISIBLE|LVS_REPORT|LVS_SINGLESEL|LVS_SHOWSELALWAYS,0,0,0,0,hwnd,reinterpret_cast<HMENU>(IDC_NETWORKS),GetModuleHandleW(nullptr),nullptr);
        SendMessageW(g.networks,WM_SETFONT,reinterpret_cast<WPARAM>(g.font),TRUE);ListView_SetExtendedListViewStyle(g.networks,LVS_EX_FULLROWSELECT|LVS_EX_DOUBLEBUFFER);
        AddColumn(g.networks,0,310,L"SSID");AddColumn(g.networks,1,95,L"Band");AddColumn(g.networks,2,80,L"Channel");AddColumn(g.networks,3,95,L"Signal");AddColumn(g.networks,4,225,L"Security");
        g.selected=C(L"STATIC",L"No Wi-Fi selected.",SS_LEFT,IDC_SELECTED);
        C(L"STATIC",L"Password:",SS_LEFT,2002);
        g.password=C(L"EDIT",L"",WS_BORDER|ES_PASSWORD|ES_AUTOHSCROLL,IDC_PASSWORD);SendMessageW(g.password,EM_SETLIMITTEXT,63,0);
        g.autoBox=C(L"BUTTON",L"Auto-connect after reboot",BS_AUTOCHECKBOX,IDC_AUTO);
        g.hint=C(L"STATIC",L"Select a scanned network or saved profile.",SS_LEFT,IDC_HINT);
        g.connect=C(L"BUTTON",L"Connect",BS_DEFPUSHBUTTON,IDC_CONNECT);
        g.disconnect=C(L"BUTTON",L"Disconnect",BS_PUSHBUTTON,IDC_DISCONNECT);
        g.saveProfile=C(L"BUTTON",L"Save / Update Profile",BS_PUSHBUTTON,IDC_SAVE_PROFILE);
        C(L"STATIC",L"Saved profiles:",SS_LEFT,2003);
        g.deleteProfile=C(L"BUTTON",L"Delete Profile",BS_PUSHBUTTON,IDC_DELETE_PROFILE);
        g.profiles=CreateWindowExW(WS_EX_CLIENTEDGE,WC_LISTVIEWW,L"",WS_CHILD|WS_VISIBLE|LVS_REPORT|LVS_SINGLESEL|LVS_SHOWSELALWAYS,0,0,0,0,hwnd,reinterpret_cast<HMENU>(IDC_PROFILES),GetModuleHandleW(nullptr),nullptr);
        SendMessageW(g.profiles,WM_SETFONT,reinterpret_cast<WPARAM>(g.font),TRUE);ListView_SetExtendedListViewStyle(g.profiles,LVS_EX_FULLROWSELECT|LVS_EX_DOUBLEBUFFER);
        AddColumn(g.profiles,0,600,L"SSID");AddColumn(g.profiles,1,135,L"Auto-connect");
        RefreshProfiles();Layout(hwnd);SetTimer(hwnd,1,1000,nullptr);PollStatus();return 0;}
    case WM_SIZE:Layout(hwnd);return 0;
    case WM_DPICHANGED:{
        g.dpi=HIWORD(wp);
        auto* suggested=reinterpret_cast<RECT*>(lp);
        SetWindowPos(hwnd,nullptr,suggested->left,suggested->top,suggested->right-suggested->left,
            suggested->bottom-suggested->top,SWP_NOZORDER|SWP_NOACTIVATE);
        Layout(hwnd);return 0;}
    case WM_CTLCOLORSTATIC:{
        HDC dc=reinterpret_cast<HDC>(wp);
        HWND child=reinterpret_cast<HWND>(lp);
        if(child==g.selected) {
            SetTextColor(dc,RGB(0,128,0));SetBkMode(dc,TRANSPARENT);
            return reinterpret_cast<LRESULT>(GetSysColorBrush(COLOR_WINDOW));
        }
        break;}
    case WM_TIMER:if(wp==1)PollStatus();return 0;
    case WM_COMMAND:
        if(HIWORD(wp)==BN_CLICKED){
            switch(LOWORD(wp)){case IDC_SCAN:DoScan();break;case IDC_CONNECT:DoConnect();break;case IDC_DISCONNECT:DoDisconnect();break;case IDC_SAVE_PROFILE:DoSaveProfile();break;case IDC_DELETE_PROFILE:DoDeleteProfile();break;}
        }return 0;
    case WM_NOTIFY:{
        auto* h=reinterpret_cast<NMHDR*>(lp);
        if((h->idFrom==IDC_NETWORKS||h->idFrom==IDC_PROFILES) && h->code==NM_CUSTOMDRAW) {
            auto* draw=reinterpret_cast<NMLVCUSTOMDRAW*>(lp);
            if(draw->nmcd.dwDrawStage==CDDS_PREPAINT)return CDRF_NOTIFYITEMDRAW;
            if(draw->nmcd.dwDrawStage==CDDS_ITEMPREPAINT && (draw->nmcd.uItemState&CDIS_SELECTED)) {
                draw->clrText=RGB(0,96,0);
                draw->clrTextBk=RGB(214,245,214);
                return CDRF_NEWFONT;
            }
        }
        if(h->code==LVN_ITEMCHANGED) {
            auto* change=reinterpret_cast<NMLISTVIEW*>(lp);
            if((change->uNewState&LVIS_SELECTED) && !(change->uOldState&LVIS_SELECTED)) {
                if(h->idFrom==IDC_NETWORKS) {
                    ListView_SetItemState(g.profiles,-1,0,LVIS_SELECTED|LVIS_FOCUSED);
                } else if(h->idFrom==IDC_PROFILES) {
                    ListView_SetItemState(g.networks,-1,0,LVIS_SELECTED|LVIS_FOCUSED);
                }
            }
            if(h->idFrom==IDC_NETWORKS||h->idFrom==IDC_PROFILES)ApplySelectionHint();
        }
        return 0;}
    case WM_APP_DONE:{
        std::unique_ptr<AsyncResult> r(reinterpret_cast<AsyncResult*>(lp));SetBusy(false);
        if(r->ok){if(r->kind==OpKind::Scan)RefreshNetworks(r->networks);SetText(g.status,r->message.empty()?L"Done.":r->message);}
        else{SetText(g.status,L"Operation failed.");MessageBoxW(hwnd,r->message.c_str(),L"RPi5 Wi-Fi",MB_ICONERROR);}
        RefreshProfiles();return 0;}
    case WM_CLOSE:
        if(g.busy){MessageBoxW(hwnd,L"A Wi-Fi operation is still running. Wait for it to finish.",L"RPi5 Wi-Fi",MB_ICONINFORMATION);return 0;}
        DestroyWindow(hwnd);return 0;
    case WM_DESTROY:
        KillTimer(hwnd,1);if(g.font)DeleteObject(g.font);PostQuitMessage(0);return 0;
    }
    return DefWindowProcW(hwnd,msg,wp,lp);
}

} // namespace

int APIENTRY wWinMain(HINSTANCE instance,HINSTANCE,LPWSTR cmd,int show) {
    std::wstring args=cmd?cmd:L"";
    if(args.find(L"--self-test")!=std::wstring::npos)return RunCommonSelfTests();
    if(args.find(L"--autoconnect")!=std::wstring::npos)return RunAutoConnect();

    INITCOMMONCONTROLSEX ic{sizeof(ic),ICC_LISTVIEW_CLASSES};InitCommonControlsEx(&ic);
    WNDCLASSEXW wc{sizeof(wc)};wc.hInstance=instance;wc.lpszClassName=L"RPi5WiFiNativeWindow";wc.lpfnWndProc=WndProc;
    wc.hIcon=LoadIconW(instance,MAKEINTRESOURCEW(IDI_APP));wc.hIconSm=wc.hIcon;wc.hCursor=LoadCursorW(nullptr,IDC_ARROW);
    wc.hbrBackground=reinterpret_cast<HBRUSH>(COLOR_WINDOW+1);
    if(!RegisterClassExW(&wc))return 1;
    HWND hwnd=CreateWindowExW(0,wc.lpszClassName,L"RPi5 Wi-Fi",WS_OVERLAPPEDWINDOW&~WS_MAXIMIZEBOX,
        CW_USEDEFAULT,CW_USEDEFAULT,940,790,nullptr,nullptr,instance,nullptr);
    if(!hwnd)return 2;ShowWindow(hwnd,show);UpdateWindow(hwnd);
    MSG msg{};while(GetMessageW(&msg,nullptr,0,0)>0){TranslateMessage(&msg);DispatchMessageW(&msg);}return static_cast<int>(msg.wParam);
}
