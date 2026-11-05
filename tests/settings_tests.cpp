#include "app_settings.h"
#include <iostream>

namespace {
void check(bool ok){if(!ok)throw std::runtime_error("Settings check failed");}
struct Fixture {
    std::wstring key=L"Software\\MonitorMageTests\\"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64());
    ~Fixture(){RegDeleteKeyW(HKEY_CURRENT_USER,key.c_str());}
};
}
int main(){
    try{
        Fixture fixture;
        const std::wstring exe=L"C:\\Example folder\\MonitorMage.exe",other=L"C:\\Other\\MonitorMage.exe";
        check(app_settings::startupCommand(exe)==L"\"C:\\Example folder\\MonitorMage.exe\"");
        check(!app_settings::startsWithWindows(exe,fixture.key.c_str()));
        app_settings::startWithWindows(exe,true,fixture.key.c_str());
        check(app_settings::startsWithWindows(exe,fixture.key.c_str()));
        check(!app_settings::startsWithWindows(other,fixture.key.c_str()));
        app_settings::startWithWindows(other,true,fixture.key.c_str());
        check(app_settings::startsWithWindows(other,fixture.key.c_str()));
        app_settings::startWithWindows(other,false,fixture.key.c_str());
        check(!app_settings::startsWithWindows(other,fixture.key.c_str()));
        app_settings::startWithWindows(other,false,fixture.key.c_str());
        std::cout<<"Isolated startup registration checks passed\n";return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
