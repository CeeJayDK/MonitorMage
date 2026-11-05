#pragma once
#include <windows.h>
#include <memory>
#include <filesystem>
#include <array>
namespace matrix_effect {
constexpr UINT Changed=WM_APP+40;
// Value 3 was the retired Windows color-filter block; retain report numbering.
enum class State : LONG { Off, Starting, Active, OtherTool=4, Failed, ExternalChange };
class Controller {
    friend int selfTest();
    friend int benchmark(const wchar_t*);
    struct Impl;
    std::unique_ptr<Impl> impl_;
public:
    Controller();~Controller();
    // -1 means off. The worker sleeps between events; no idle polling.
    void update(HWND owner,int saturation,bool refresh=false,bool fake=false,const std::array<int,3>* monochrome=nullptr);
    State state()const;
    void stop();
    // GetMessage semantics, also waking once on an unexpected worker exit.
    // No polling timer, extra thread or thread-pool wait is required.
    int getMessage(MSG&);
    std::filesystem::path report()const;
};
int worker(DWORD parent,const wchar_t* token,bool fake);
int selfTest();
int benchmark(const wchar_t* path);
}
