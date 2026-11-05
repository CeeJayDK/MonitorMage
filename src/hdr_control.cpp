#include "hdr_control.h"
#include <vector>
#include <stdexcept>
namespace hdr {
namespace {
struct Modern {DISPLAYCONFIG_DEVICE_INFO_HEADER header;UINT32 flags=0,encoding=0,bits=0,mode=0;};
bool win10(){using Query=LONG(WINAPI*)(OSVERSIONINFOW*);auto query=reinterpret_cast<Query>(GetProcAddress(GetModuleHandleW(L"ntdll.dll"),"RtlGetVersion"));OSVERSIONINFOW version{};version.dwOSVersionInfoSize=sizeof(version);return query && query(&version)==0 && version.dwBuildNumber<22000;}
}
State inspect(const std::wstring& source) {
    State state;state.source=source;state.reason=L"MonitorMage cannot read the Windows HDR setting.";
    for(int attempt=0;attempt<3;++attempt) {
        UINT32 count=0,modes=0;if(GetDisplayConfigBufferSizes(QDC_ONLY_ACTIVE_PATHS,&count,&modes))return state;
        std::vector<DISPLAYCONFIG_PATH_INFO> paths(count);std::vector<DISPLAYCONFIG_MODE_INFO> info(modes);
        const LONG status=QueryDisplayConfig(QDC_ONLY_ACTIVE_PATHS,&count,paths.data(),&modes,info.data(),nullptr);
        if(status==ERROR_INSUFFICIENT_BUFFER)continue;if(status)return state;
        unsigned matches=0;
        for(UINT32 i=0;i<count;++i) {
            const auto& path=paths[i];DISPLAYCONFIG_SOURCE_DEVICE_NAME name{};
            name.header={DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_NAME,sizeof(name),path.sourceInfo.adapterId,path.sourceInfo.id};
            if(DisplayConfigGetDeviceInfo(&name.header) || source!=name.viewGdiDeviceName)continue;
            if(++matches>1){state.available=false;state.reason=L"Windows cannot switch HDR while these displays show the same picture. Use one display or Extend mode.";return state;}
            state.adapter=path.targetInfo.adapterId;state.target=path.targetInfo.id;
            Modern modern{};modern.header={static_cast<DISPLAYCONFIG_DEVICE_INFO_TYPE>(15),sizeof(modern),state.adapter,state.target};
            if(DisplayConfigGetDeviceInfo(&modern.header)==ERROR_SUCCESS) {
                state.modern=true;state.on=modern.mode==2;state.available=(modern.flags&(1u<<4)) && (state.on || !(modern.flags&(1u<<3))) && modern.mode<=2;
            } else if(win10()) {
                DISPLAYCONFIG_GET_ADVANCED_COLOR_INFO old{};old.header={DISPLAYCONFIG_DEVICE_INFO_GET_ADVANCED_COLOR_INFO,sizeof(old),state.adapter,state.target};
                if(!DisplayConfigGetDeviceInfo(&old.header)){state.on=old.advancedColorEnabled!=0;state.available=old.advancedColorSupported && !old.advancedColorForceDisabled;}
            }
            state.reason=state.available?L"Toggle Windows HDR on this display.":L"Windows HDR is not available with these display settings.";
        }
        return state;
    }
    state.reason=L"Display configuration is changing.";return state;
}
void set(const State& expected,bool on) {
    if(!expected.available)throw std::runtime_error("HDR switching is unavailable for this display mode");
    const auto current=inspect(expected.source);
    if(!expected.available || !current.available || current.adapter.HighPart!=expected.adapter.HighPart || current.adapter.LowPart!=expected.adapter.LowPart || current.target!=expected.target || current.on!=expected.on)throw std::runtime_error("Display state changed; refresh and try HDR again");
    // Local ABI also works when building against older SDK feature guards.
    struct Request {DISPLAYCONFIG_DEVICE_INFO_HEADER header;UINT32 enabled;};
    Request request{{static_cast<DISPLAYCONFIG_DEVICE_INFO_TYPE>(current.modern?16:10),sizeof(Request),current.adapter,current.target},on?1u:0u};
    const LONG result=DisplayConfigSetDeviceInfo(&request.header);
    if(result!=ERROR_SUCCESS)throw std::runtime_error("Windows refused the HDR change (error "+std::to_string(result)+")");
}
}
