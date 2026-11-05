#include "windows_controls.h"
#include <shellapi.h>
#include <stdexcept>
namespace windows_controls {
namespace {
constexpr auto Key=L"Software\\Microsoft\\ColorFiltering";
struct Value {DWORD data=0;bool present=false;};
Value read(HKEY key,const wchar_t* name) {
    Value value;DWORD type=0,size=sizeof(value.data);
    const auto result=RegQueryValueExW(key,name,nullptr,&type,reinterpret_cast<BYTE*>(&value.data),&size);
    if(result==ERROR_FILE_NOT_FOUND)return {};
    if(result!=ERROR_SUCCESS||type!=REG_DWORD||size!=sizeof(DWORD))throw std::runtime_error("Windows color filter settings could not be read");
    value.present=true;return value;
}
std::wstring broker() {wchar_t folder[MAX_PATH]{};const auto n=GetSystemDirectoryW(folder,MAX_PATH);if(!n||n>=MAX_PATH)throw std::runtime_error("Cannot locate Windows system folder");return std::wstring(folder)+L"\\atbroker.exe";}
void write(HKEY key,const wchar_t* name,DWORD value){if(RegSetValueExW(key,name,0,REG_DWORD,reinterpret_cast<const BYTE*>(&value),sizeof(value)))throw std::runtime_error("Windows color filter settings could not be saved");}
void undo(HKEY key,const wchar_t* name,Value value){if(value.present)RegSetValueExW(key,name,0,REG_DWORD,reinterpret_cast<const BYTE*>(&value.data),sizeof(value.data));else RegDeleteValueW(key,name);}
}
State inspect() {
    State state;
    try {
        if(GetFileAttributesW(broker().c_str())==INVALID_FILE_ATTRIBUTES){state.reason=L"Windows color filters are unavailable.";return state;}
        HKEY key=nullptr;const auto result=RegOpenKeyExW(HKEY_CURRENT_USER,Key,0,KEY_QUERY_VALUE,&key);
        if(result==ERROR_FILE_NOT_FOUND){state.available=true;return state;}
        if(result!=ERROR_SUCCESS)throw std::runtime_error("Windows color filter settings are unavailable");
        Value active,filter;
        try{active=read(key,L"Active");filter=read(key,L"FilterType");}catch(...){RegCloseKey(key);throw;}
        RegCloseKey(key);
        if(active.data>1||filter.data>5)throw std::runtime_error("Windows uses an unrecognized color filter setting");
        state.on=active.data!=0;state.filter=filter.data;state.available=true;
    }catch(const std::exception& error){const std::string message=error.what();state.reason.assign(message.begin(),message.end());}
    return state;
}
Controller::~Controller(){if(process_)CloseHandle(process_);}
void Controller::stopWaiting(){if(process_)CloseHandle(process_);process_=nullptr;}
Completion completion(const State& observed,bool on,DWORD filter,bool exited,DWORD code){
    if(!observed.available)return Completion::Unreadable;
    if(observed.on!=on||observed.filter!=filter)return Completion::DifferentSettings;
    return exited&&code==0?Completion::ConfirmedRequest:Completion::SettingsSaved;
}
const wchar_t* completionNotice(Completion value){
    switch(value){
    case Completion::ConfirmedRequest:return L"";
    case Completion::SettingsSaved:return L"Settings saved. Check the picture or open Windows settings.";
    case Completion::DifferentSettings:return L"Showing the current Windows color filter settings.";
    default:return L"Windows color filter settings could not be read.";
    }
}
void Controller::set(const State& expected,bool on,DWORD filter,bool selectingFilter) {
    if(busy())throw std::runtime_error("Windows is already updating color filters");
    if(filter>5)throw std::invalid_argument("Invalid Windows color filter");
    const auto current=inspect();
    if(!expected.available||!current.available)throw std::runtime_error("Windows color filter settings could not be read");
    const auto exe=broker();auto command=L"\""+exe+L"\" /colorfiltershortcut /resettransferkeys";
    HKEY key=nullptr;if(RegCreateKeyExW(HKEY_CURRENT_USER,Key,0,nullptr,0,KEY_QUERY_VALUE|KEY_SET_VALUE,nullptr,&key,nullptr))throw std::runtime_error("Windows color filter settings could not be opened");
    Value active,kind;bool captured=false;
    try {
        active=read(key,L"Active");kind=read(key,L"FilterType");captured=true;
        // A toggle preserves the latest Windows filter choice, even if our
        // visible snapshot is stale. Only a dropdown selection changes type.
        if(!selectingFilter){if(kind.data>5)throw std::runtime_error("Windows uses an unrecognized color filter setting");filter=kind.data;}
        write(key,L"FilterType",filter);write(key,L"Active",on?1:0);
        STARTUPINFOW startup{sizeof(startup)};startup.dwFlags=STARTF_USESHOWWINDOW|STARTF_FORCEOFFFEEDBACK;startup.wShowWindow=SW_HIDE;
        PROCESS_INFORMATION process{};
        if(!CreateProcessW(exe.c_str(),command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,nullptr,&startup,&process))throw std::runtime_error("Windows could not apply the color filter; use Windows settings");
        CloseHandle(process.hThread);process_=process.hProcess;expectedOn_=on;expectedFilter_=filter;attempts_=0;deadline_=GetTickCount64()+6000;notice_.clear();
    }catch(...){if(captured){undo(key,L"Active",active);undo(key,L"FilterType",kind);}RegCloseKey(key);throw;}
    RegCloseKey(key);
}
bool Controller::poll() {
    if(!process_)return true;
    const bool exited=WaitForSingleObject(process_,0)==WAIT_OBJECT_0;
    if(!exited&&GetTickCount64()<deadline_)return false;
    // The broker can finish before Windows publishes its final saved state.
    // Give it three timer turns to settle; never keep polling while idle.
    if(exited&&++attempts_<3)return false;
    DWORD code=STILL_ACTIVE;GetExitCodeProcess(process_,&code);stopWaiting();
    const auto state=inspect();
    const auto result=completion(state,expectedOn_,expectedFilter_,exited,code);
    notice_=completionNotice(result);
    if(result==Completion::Unreadable)throw std::runtime_error("Windows color filter settings could not be read");
    return true;
}
void openColorFilterSettings(HWND owner) {
    const auto result=ShellExecuteW(owner,L"open",L"ms-settings:easeofaccess-colorfilter",nullptr,nullptr,SW_SHOWNORMAL);
    if(reinterpret_cast<INT_PTR>(result)<=32)throw std::runtime_error("Windows settings could not be opened");
}
}
