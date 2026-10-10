#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <setupapi.h>
#include <cfgmgr32.h>
#include "RPi5WiFiPlatform.h"

#pragma comment(lib, "setupapi.lib")
#pragma comment(lib, "cfgmgr32.lib")

namespace rpiwifi {
namespace {
// Return complete, type-checked properties. Missing service is normal for an
// unbound device; any other read failure prevents installation/control access.
DWORD ReadProperty(HDEVINFO set, SP_DEVINFO_DATA& device, DWORD property,
                   DWORD expectedType, std::vector<wchar_t>& value) {
    DWORD bytes=0,type=0;
    if(!SetupDiGetDeviceRegistryPropertyW(set,&device,property,&type,nullptr,0,&bytes)) {
        DWORD error=GetLastError();
        if(error==ERROR_INVALID_DATA){value.assign(2,0);return ERROR_SUCCESS;}
        if(error!=ERROR_INSUFFICIENT_BUFFER)return error;
    }
    if(bytes==0||bytes%sizeof(wchar_t)!=0||bytes>65536)return ERROR_INVALID_DATA;
    value.assign(bytes/sizeof(wchar_t)+2,0);
    if(!SetupDiGetDeviceRegistryPropertyW(set,&device,property,&type,
        reinterpret_cast<PBYTE>(value.data()),bytes,nullptr))return GetLastError();
    return type==expectedType?ERROR_SUCCESS:ERROR_INVALID_DATA;
}
} // namespace

PlatformStatus QueryPlatform() {
    PlatformStatus result;
    HDEVINFO set=SetupDiGetClassDevsW(nullptr,L"ACPI",nullptr,DIGCF_ALLCLASSES|DIGCF_PRESENT);
    if(set==INVALID_HANDLE_VALUE){result.enumerationError=GetLastError();return result;}
    std::vector<PlatformDevice> devices;
    DWORD error=ERROR_SUCCESS;
    for(DWORD index=0;;++index) {
        SP_DEVINFO_DATA info{};info.cbSize=sizeof(info);
        if(!SetupDiEnumDeviceInfo(set,index,&info)) {
            error=GetLastError();if(error==ERROR_NO_MORE_ITEMS)error=ERROR_SUCCESS;
            break;
        }
        std::vector<wchar_t> ids;
        error=ReadProperty(set,info,SPDRP_HARDWAREID,REG_MULTI_SZ,ids);
        if(error)break;
        PlatformDevice device;
        for(const wchar_t* p=ids.data();*p;p+=wcslen(p)+1)device.hardwareIds.emplace_back(p);
        if(!HasHardwareId(device,kHardwareId)&&!HasHardwareId(device,L"ACPI\\RPI0011"))continue;
        std::vector<wchar_t> service;
        error=ReadProperty(set,info,SPDRP_SERVICE,REG_SZ,service);
        if(error)break;
        device.service=service.data();
        ULONG flags=0,problem=0;
        CONFIGRET cr=CM_Get_DevNode_Status(&flags,&problem,info.DevInst,0);
        if(cr!=CR_SUCCESS){error=CM_MapCrToWin32Err(cr,ERROR_GEN_FAILURE);break;}
        device.started=(flags&DN_STARTED)!=0;
        device.problemCode=problem;
        devices.push_back(std::move(device));
    }
    SetupDiDestroyDeviceInfoList(set);
    result=AssessPlatform(devices);result.enumerationError=error;
    return result;
}
} // namespace rpiwifi
