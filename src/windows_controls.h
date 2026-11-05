#pragma once
#include <windows.h>
#include <string>
namespace windows_controls {
struct State {bool available=false,on=false;DWORD filter=0;std::wstring reason;};
inline constexpr const wchar_t* FilterNames[]{L"Grayscale",L"Inverted",L"Grayscale inverted",L"Red-green (deuteranopia)",L"Red-green (protanopia)",L"Blue-yellow (tritanopia)"};
State inspect();
enum class Completion { ConfirmedRequest, SettingsSaved, DifferentSettings, Unreadable };
// Saved values and helper completion are separate observations. Neither is
// a measurement of the displayed picture.
Completion completion(const State&,bool requestedOn,DWORD requestedFilter,bool helperExited,DWORD helperCode);
const wchar_t* completionNotice(Completion);
class Controller {
    HANDLE process_=nullptr;
    unsigned attempts_=0;
    bool expectedOn_=false;DWORD expectedFilter_=0;
    ULONGLONG deadline_=0;
    std::wstring notice_;
public:
    ~Controller();
    bool busy() const {return process_!=nullptr;}
    void set(const State& expected,bool on,DWORD filter,bool selectingFilter=false);
    // Nonblocking; false while Windows is processing the request.
    bool poll();
    void stopWaiting();
    const std::wstring& notice()const{return notice_;}
};
void openColorFilterSettings(HWND owner);
}
