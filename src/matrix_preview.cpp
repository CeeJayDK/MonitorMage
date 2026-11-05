#include "matrix_preview.h"
#include "matrix_math.h"
#include "matrix_api.h"
#include "ui_theme.h"
#include "windows_controls.h"
#include <magnification.h>
#include <commctrl.h>
#include <shellapi.h>
#include <filesystem>
#include <sstream>
#include <iomanip>
#include <memory>
#include <string>
#include <vector>
#include <psapi.h>
namespace matrix_preview {
namespace {
constexpr DWORD Magic=0x4d435032;
constexpr UINT Timer=1;
constexpr int Mode=301,Duration=302,Start=303,Stop=304,Reports=305,Status=306,Note=307,Swatches=308,Shortcut=309,SetShortcut=310,ShortcutLabel=311;
constexpr int HotkeyA=0x4d31,HotkeyB=0x4d32;
enum CancelReason:LONG {NoReason,StopButton,ShortcutPress,DisplayChange,SettingsChange,WindowClose,SessionEnd};
enum Phase:LONG {Starting,Applied,Finished};
enum Result:LONG {None,Restored,External,Failed,Blocked,NotStarted,Stopped,ReportFailed};
struct Shared {DWORD magic=Magic;int mode=0;DWORD seconds=15;volatile LONG phase=Starting,result=None,cancelReason=NoReason;ULONGLONG appliedAt=0;DWORD error=0;};
struct Handle {HANDLE h=nullptr;explicit Handle(HANDLE value=nullptr):h(value){}~Handle(){if(h&&h!=INVALID_HANDLE_VALUE)CloseHandle(h);}Handle(const Handle&)=delete;Handle& operator=(const Handle&)=delete;};
struct Mapping {Shared* p=nullptr;explicit Mapping(HANDLE h){if(h)p=static_cast<Shared*>(MapViewOfFile(h,FILE_MAP_ALL_ACCESS,0,0,sizeof(Shared)));}~Mapping(){if(p)UnmapViewOfFile(p);}};
std::filesystem::path executable(){wchar_t path[32768]{};const auto n=GetModuleFileNameW(nullptr,path,32768);if(!n||n>=32768)throw std::runtime_error("Cannot locate MonitorMage");return path;}
std::filesystem::path reportFolder(){return executable().parent_path()/L"reports"/L"matrix-preview";}
std::wstring object(const wchar_t* token,const wchar_t* suffix){return L"Local\\MonitorMageMatrix-"+std::wstring(token)+suffix;}
bool validToken(const wchar_t* token){if(!token||!*token||wcslen(token)>80)return false;for(const auto* p=token;*p;++p)if((*p<L'0'||*p>L'9')&&*p!=L'-')return false;return true;}
std::wstring quote(const std::wstring& text){if(text.find(L'"')!=text.npos)throw std::runtime_error("Invalid preview path");return L"\""+text+L"\"";}
using Api=matrix_color::WindowsApi;
struct Fake final:matrix_color::Api {matrix_color::Matrix value=matrix_color::identity();bool read(matrix_color::Matrix& m)override{m=value;return true;}bool write(const matrix_color::Matrix& m)override{value=m;return true;}};
void writeLog(HANDLE file,std::ostringstream& text){const auto bytes=text.str();DWORD written=0;if(file!=INVALID_HANDLE_VALUE&&(!WriteFile(file,bytes.data(),static_cast<DWORD>(bytes.size()),&written,nullptr)||written!=bytes.size()||!FlushFileBuffers(file)))throw std::runtime_error("Cannot write preview report");text.str("");text.clear();}
void matrixLog(std::ostringstream& log,const char* name,const matrix_color::Matrix& matrix){log<<name<<"=\n";for(const auto& row:matrix){for(float v:row)log<<v<<' ';log<<'\n';}}
void displayLog(std::ostringstream& log){UINT32 pc=0,mc=0;if(GetDisplayConfigBufferSizes(QDC_ONLY_ACTIVE_PATHS,&pc,&mc))return;
    std::vector<DISPLAYCONFIG_PATH_INFO> paths(pc);std::vector<DISPLAYCONFIG_MODE_INFO> modes(mc);if(QueryDisplayConfig(QDC_ONLY_ACTIVE_PATHS,&pc,paths.data(),&mc,modes.data(),nullptr))return;
    log<<"desktop_resolution="<<GetSystemMetrics(SM_CXSCREEN)<<'x'<<GetSystemMetrics(SM_CYSCREEN)<<'\n';
    DWORD foreground=0;GetWindowThreadProcessId(GetForegroundWindow(),&foreground);log<<"foreground_pid="<<foreground<<'\n';
    log<<"display_paths="<<pc<<'\n';for(UINT32 i=0;i<pc;++i){const auto& p=paths[i];DISPLAYCONFIG_GET_ADVANCED_COLOR_INFO color{};
        color.header={DISPLAYCONFIG_DEVICE_INFO_GET_ADVANCED_COLOR_INFO,sizeof(color),p.targetInfo.adapterId,p.targetInfo.id};const auto error=DisplayConfigGetDeviceInfo(&color.header);
        log<<"display["<<i<<"] source="<<p.sourceInfo.id<<" target="<<p.targetInfo.id<<" advanced_color_query="<<error;
        if(p.sourceInfo.modeInfoIdx<mc&&modes[p.sourceInfo.modeInfoIdx].infoType==DISPLAYCONFIG_MODE_INFO_TYPE_SOURCE)log<<" resolution="<<modes[p.sourceInfo.modeInfoIdx].sourceMode.width<<'x'<<modes[p.sourceInfo.modeInfoIdx].sourceMode.height;
        if(!error)log<<" supported="<<color.advancedColorSupported<<" enabled="<<color.advancedColorEnabled<<" bits_per_channel="<<color.bitsPerColorChannel;log<<'\n';}}
ULONGLONG cpu(){FILETIME c{},e{},k{},u{};if(!GetProcessTimes(GetCurrentProcess(),&c,&e,&k,&u))return 0;ULARGE_INTEGER a{},b{};a.LowPart=k.dwLowDateTime;a.HighPart=k.dwHighDateTime;b.LowPart=u.dwLowDateTime;b.HighPart=u.dwHighDateTime;return a.QuadPart+b.QuadPart;}
struct Session {
    Handle mapping,cancel,process;std::unique_ptr<Mapping> view;
    bool busy()const{return process.h&&WaitForSingleObject(process.h,0)==WAIT_TIMEOUT;}
    void stop(CancelReason reason=SessionEnd){if(view&&view->p)InterlockedCompareExchange(&view->p->cancelReason,reason,NoReason);if(cancel.h)SetEvent(cancel.h);}
    ~Session(){stop();}
};
std::unique_ptr<Session> launch(int mode,DWORD seconds,DWORD parent,bool fake){
    auto result=std::make_unique<Session>();const auto token=std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64());
    result->mapping.h=CreateFileMappingW(INVALID_HANDLE_VALUE,nullptr,PAGE_READWRITE,0,sizeof(Shared),object(token.c_str(),L"-map").c_str());
    if(!result->mapping.h||GetLastError()==ERROR_ALREADY_EXISTS)throw std::runtime_error("Preview session unavailable");
    result->view=std::make_unique<Mapping>(result->mapping.h);result->cancel.h=CreateEventW(nullptr,TRUE,FALSE,object(token.c_str(),L"-cancel").c_str());
    if(!result->view->p||!result->cancel.h)throw std::runtime_error("Cannot prepare preview");
    *result->view->p=Shared{};result->view->p->mode=mode;result->view->p->seconds=seconds;
    auto command=quote(executable().wstring())+(fake?L" --matrix-worker-probe ":L" --matrix-worker ")+std::to_wstring(parent)+L" "+token;
    STARTUPINFOW start{sizeof(start)};start.dwFlags=STARTF_USESHOWWINDOW|STARTF_FORCEOFFFEEDBACK;start.wShowWindow=SW_HIDE;PROCESS_INFORMATION process{};
    if(!CreateProcessW(executable().c_str(),command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,nullptr,&start,&process))throw std::runtime_error("Cannot start preview worker");
    result->process.h=process.hProcess;CloseHandle(process.hThread);return result;
}
HWND previewWindow=nullptr;bool standalone=false;
struct Window {HWND mode=nullptr,duration=nullptr,start=nullptr,stop=nullptr,status=nullptr;HFONT font=nullptr;std::unique_ptr<Session> session;bool closing=false,testing=false,probe=false,hotkeyRegistered=false;WORD shortcut=MAKEWORD('M',HOTKEYF_CONTROL|HOTKEYF_ALT|HOTKEYF_SHIFT);int hotkeyId=HotkeyA;};
Window* windowState(HWND h){return reinterpret_cast<Window*>(GetWindowLongPtrW(h,GWLP_USERDATA));}
void status(HWND window,const wchar_t* text){SetWindowTextW(GetDlgItem(window,Status),text);}
bool bindShortcut(HWND window,Window& state,WORD value){
    const BYTE key=LOBYTE(value),flags=HIBYTE(value);const UINT mods=((flags&HOTKEYF_CONTROL)?MOD_CONTROL:0)|((flags&HOTKEYF_ALT)?MOD_ALT:0)|((flags&HOTKEYF_SHIFT)?MOD_SHIFT:0)|MOD_NOREPEAT;
    if(!key||key==VK_F12||key==VK_CONTROL||key==VK_MENU||key==VK_SHIFT||!(flags&(HOTKEYF_CONTROL|HOTKEYF_ALT|HOTKEYF_SHIFT))||(flags&~(HOTKEYF_CONTROL|HOTKEYF_ALT|HOTKEYF_SHIFT|HOTKEYF_EXT)))return false;
    if(state.hotkeyRegistered&&state.shortcut==value)return true;const int next=state.hotkeyRegistered?(state.hotkeyId==HotkeyA?HotkeyB:HotkeyA):HotkeyA;
    if(!RegisterHotKey(window,next,mods,key))return false;if(state.hotkeyRegistered)UnregisterHotKey(window,state.hotkeyId);
    state.shortcut=value;state.hotkeyId=next;state.hotkeyRegistered=true;return true;
}
void buttons(Window& state,bool active){EnableWindow(state.start,!active&&!state.testing);EnableWindow(state.mode,!active);EnableWindow(state.duration,!active);EnableWindow(state.stop,active);EnableWindow(GetDlgItem(GetParent(state.start),Shortcut),!active);EnableWindow(GetDlgItem(GetParent(state.start),SetShortcut),!active);if(active&&GetForegroundWindow()==GetAncestor(state.start,GA_ROOT))SetFocus(state.stop);}
void finish(HWND window,Window& state){DWORD code=1;GetExitCodeProcess(state.session->process.h,&code);const auto result=state.session->view->p->result;
    const auto error=state.session->view->p->error;state.session.reset();KillTimer(window,Timer);buttons(state,false);
    if(result==Restored&&code==0)status(window,L"Preview ended. Previous colors restored.");
    else if(result==External)status(window,L"Preview ended. Another app's color change was kept.");
    else if(result==Blocked)status(window,error==2?L"Close other color-matrix or Magnifier tools first.":L"The preview could not start. See its report.");
    else if(result==NotStarted)status(window,L"Preview could not start. No colors were changed. See its report.");
    else if(result==Stopped)status(window,L"Preview ended early. Previous colors restored. See its report.");
    else if(result==ReportFailed)status(window,L"Previous colors restored, but the report could not be saved.");
    else if(result==None)status(window,L"The preview worker stopped unexpectedly. Check the picture.");
    else status(window,L"The preview could not confirm restoration. See its report.");
    if(state.closing)DestroyWindow(window);
}
void layout(HWND window,Window& state){const UINT dpi=GetDpiForWindow(window);const auto px=[&](int n){return MulDiv(n,dpi,96);};
    if(state.font)DeleteObject(state.font);state.font=ui::makeFont(dpi);
    for(int id:{Note,Mode,Duration,Start,Stop,Reports,Status,Shortcut,SetShortcut,ShortcutLabel})SendDlgItemMessageW(window,id,WM_SETFONT,reinterpret_cast<WPARAM>(state.font),TRUE);
    const auto place=[&](int id,int x,int y,int w,int h){MoveWindow(GetDlgItem(window,id),px(x),px(y),px(w),px(h),TRUE);};
    place(Note,18,14,484,48);place(Mode,18,72,282,160);place(Duration,314,72,188,120);
    place(Swatches,18,164,484,30);
    place(Start,18,112,232,32);place(Stop,266,112,236,32);place(Status,18,208,484,48);place(ShortcutLabel,18,268,66,28);place(Shortcut,90,268,250,28);place(SetShortcut,354,268,148,28);place(Reports,314,310,188,30);ui::frame(window);
}
LRESULT CALLBACK proc(HWND window,UINT msg,WPARAM wp,LPARAM lp){auto state=windowState(window);LRESULT themed=0;if(ui::message(window,msg,wp,lp,themed))return themed;
    switch(msg){
    case WM_CREATE:{auto created=std::make_unique<Window>();state=created.get();state->testing=reinterpret_cast<LPCREATESTRUCTW>(lp)->lpCreateParams!=nullptr;ui::fixedWindowSize(window,520,358);SetWindowLongPtrW(window,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(state));created.release();previewWindow=window;
        const auto child=[&](const wchar_t* cls,const wchar_t* text,DWORD flags,int id){if(wcscmp(cls,L"COMBOBOX")==0)flags|=CBS_OWNERDRAWFIXED|CBS_HASSTRINGS;
            auto h=CreateWindowExW(0,cls,text,WS_CHILD|WS_VISIBLE|flags,0,0,1,1,window,reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),GetModuleHandleW(nullptr),nullptr);ui::control(h);return h;};
        child(L"STATIC",L"Temporary test on all displays. It stops automatically.\nEscape or Stop ends it early. Shortcut starts/stops it inside a game.",SS_NOPREFIX,Note);
        state->mode=child(L"COMBOBOX",L"Preview effect",WS_TABSTOP|CBS_DROPDOWNLIST,Mode);
        for(const auto* text:{L"Grayscale",L"Half saturation",L"Saturation +20%"})SendMessageW(state->mode,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(text));SendMessageW(state->mode,CB_SETCURSEL,0,0);
        state->duration=child(L"COMBOBOX",L"Preview duration",WS_TABSTOP|CBS_DROPDOWNLIST,Duration);for(const auto* text:{L"15 seconds",L"30 seconds"})SendMessageW(state->duration,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(text));SendMessageW(state->duration,CB_SETCURSEL,0,0);
        state->start=child(L"BUTTON",L"Preview",WS_TABSTOP|BS_PUSHBUTTON,Start);state->stop=child(L"BUTTON",L"Stop",WS_TABSTOP|BS_DEFPUSHBUTTON,Stop);EnableWindow(state->stop,FALSE);
        child(L"STATIC",L"Color swatches",SS_OWNERDRAW,Swatches);
        state->status=child(L"STATIC",L"Ready. No preview is running.",SS_NOPREFIX,Status);child(L"BUTTON",L"Open reports",WS_TABSTOP|BS_PUSHBUTTON,Reports);
        child(L"STATIC",L"Shortcut",SS_CENTERIMAGE,ShortcutLabel);auto shortcut=child(HOTKEY_CLASSW,L"Preview shortcut",WS_TABSTOP,Shortcut);SendMessageW(shortcut,HKM_SETHOTKEY,state->shortcut,0);
        child(L"BUTTON",L"Set shortcut",WS_TABSTOP|BS_PUSHBUTTON,SetShortcut);ui::help(shortcut,L"Preview shortcut");ui::help(GetDlgItem(window,SetShortcut),L"Set preview shortcut");
        if(!state->testing&&!bindShortcut(window,*state,state->shortcut))status(window,L"Shortcut unavailable. Choose another and click Set shortcut.");
        ui::help(state->mode,L"Preview effect");ui::help(state->duration,L"Preview duration");ui::help(state->start,L"Start timed preview");ui::help(state->stop,L"Stop and restore colors");ui::help(GetDlgItem(window,Reports),L"Open preview reports");layout(window,*state);return 0;}
    case WM_DRAWITEM:{const auto item=reinterpret_cast<DRAWITEMSTRUCT*>(lp);if(item->CtlID!=Swatches)break;
        FillRect(item->hDC,&item->rcItem,ui::background());constexpr COLORREF colors[]{RGB(0,0,0),RGB(128,128,128),RGB(255,255,255),RGB(210,45,45),RGB(45,180,65),RGB(45,80,210),RGB(185,145,115),RGB(220,180,45)};
        const int width=item->rcItem.right-item->rcItem.left,gap=MulDiv(4,GetDpiForWindow(window),96);
        for(int i=0;i<8;++i){RECT r{item->rcItem.left+width*i/8,item->rcItem.top,item->rcItem.left+width*(i+1)/8-gap,item->rcItem.bottom};auto brush=CreateSolidBrush(colors[i]);FillRect(item->hDC,&r,brush);DeleteObject(brush);}return TRUE;}
    case WM_HOTKEY:
        if(state->testing||!state->hotkeyRegistered||static_cast<int>(wp)!=state->hotkeyId)return 0;
        if(state->closing)return 0;
        if(state->session&&!state->session->busy())finish(window,*state);
        if(state->session){state->session->stop(ShortcutPress);status(window,L"Stopping preview...");}
        else SendMessageW(window,WM_COMMAND,Start,0);return 0;
    case WM_COMMAND:
        if(LOWORD(wp)==SetShortcut&&!state->testing&&!state->session){const WORD value=static_cast<WORD>(SendDlgItemMessageW(window,Shortcut,HKM_GETHOTKEY,0,0));
            if(bindShortcut(window,*state,value))status(window,L"Shortcut ready. It works while the game has focus.");
            else{SendDlgItemMessageW(window,Shortcut,HKM_SETHOTKEY,state->shortcut,0);status(window,L"Shortcut unavailable. Choose another combination.");}return 0;}
        if(LOWORD(wp)==Mode&&HIWORD(wp)==CBN_SELCHANGE){status(window,SendMessageW(state->mode,CB_GETCURSEL,0,0)==2?L"Saturation +20% may clip strong colors.":L"Ready. No preview is running.");return 0;}
        if(LOWORD(wp)==Start&&!state->session&&!state->testing){try{const int mode=static_cast<int>(SendMessageW(state->mode,CB_GETCURSEL,0,0));const DWORD seconds=SendMessageW(state->duration,CB_GETCURSEL,0,0)==1?30:15;
            state->session=launch(mode,seconds,GetCurrentProcessId(),state->probe);if(!SetTimer(window,Timer,100,nullptr)){state->session->stop();throw std::runtime_error("Cannot monitor preview");}buttons(*state,true);status(window,L"Starting preview...");
            }catch(...){if(state->session){state->session->stop();state->session.reset();}status(window,L"Preview could not start. Check the reports folder.");}return 0;}
        if(LOWORD(wp)==Stop||LOWORD(wp)==IDCANCEL||LOWORD(wp)==IDOK){if(state->session){state->session->stop(StopButton);status(window,L"Stopping preview...");}return 0;}
        if(LOWORD(wp)==Reports){try{std::filesystem::create_directories(reportFolder());ShellExecuteW(window,L"open",reportFolder().c_str(),nullptr,nullptr,SW_SHOWNORMAL);}catch(...){status(window,L"The reports folder could not be opened.");}return 0;}break;
    case WM_TIMER:if(wp==Timer&&state->session){if(!state->session->busy()){finish(window,*state);return 0;}const auto shared=state->session->view->p;
        if(InterlockedCompareExchange(&shared->phase,0,0)==Applied){const auto elapsed=(GetTickCount64()-shared->appliedAt)/1000;const auto left=elapsed<shared->seconds?shared->seconds-elapsed:0;
            const auto text=L"Previewing. Restoring colors in "+std::to_wstring(left)+L" seconds.";wchar_t current[160]{};GetWindowTextW(state->status,current,160);if(text!=current)status(window,text.c_str());}return 0;}break;
    case WM_DISPLAYCHANGE:case WM_SETTINGCHANGE:if(state&&state->session){state->session->stop(msg==WM_DISPLAYCHANGE?DisplayChange:SettingsChange);status(window,L"Display settings changed. Stopping preview...");}return 0;
    case WM_CLOSE:if(state->session&&state->session->busy()){state->closing=true;state->session->stop(WindowClose);status(window,L"Stopping preview...");return 0;}DestroyWindow(window);return 0;
    case WM_DPICHANGED:{const auto r=reinterpret_cast<RECT*>(lp);SetWindowPos(window,nullptr,r->left,r->top,r->right-r->left,r->bottom-r->top,SWP_NOZORDER|SWP_NOACTIVATE);layout(window,*state);return 0;}
    case WM_DESTROY:KillTimer(window,Timer);if(state->hotkeyRegistered)UnregisterHotKey(window,state->hotkeyId);if(state->session)state->session->stop();if(state->font)DeleteObject(state->font);delete state;SetWindowLongPtrW(window,GWLP_USERDATA,0);previewWindow=nullptr;if(standalone)PostQuitMessage(0);return 0;
    }
    return DefWindowProcW(window,msg,wp,lp);
}
}
int worker(DWORD parentId,const wchar_t* token,bool fake){
    if(!parentId||!validToken(token))return 2;
    Handle parent(OpenProcess(SYNCHRONIZE,FALSE,parentId));Handle cancel(OpenEventW(SYNCHRONIZE,FALSE,object(token,L"-cancel").c_str()));
    Handle mapping(OpenFileMappingW(FILE_MAP_ALL_ACCESS,FALSE,object(token,L"-map").c_str()));Mapping view(mapping.h);
    if(!parent.h||!cancel.h||!view.p||view.p->magic!=Magic||view.p->seconds<1||view.p->seconds>30||view.p->mode<0||view.p->mode>2)return 3;
    auto& shared=*view.p;Api api;Fake mock;matrix_color::Api& backend=fake?static_cast<matrix_color::Api&>(mock):api;
    const auto target=matrix_color::preview(shared.mode);matrix_color::Matrix baseline{};bool attempted=false;Handle file(INVALID_HANDLE_VALUE);std::ostringstream log;
    const auto end=[&](Result result,DWORD error){
        if(result==Failed&&!attempted&&!shared.appliedAt)result=NotStarted;
        log<<"final_result="<<result<<" error="<<error<<" (1=restored,2=external,3=failed,4=blocked,5=not_started,6=stopped,7=report_failed)\n";
        try{if(!fake)writeLog(file.h,log);}catch(...){if(result==Restored||result==Stopped)result=ReportFailed;}
        shared.error=error;InterlockedExchange(&shared.result,result);InterlockedExchange(&shared.phase,Finished);return result==Restored?0:1;};
    try{
        if(!fake){std::filesystem::create_directories(reportFolder());const auto path=reportFolder()/(L"preview-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64())+L".txt");
            file.h=CreateFileW(path.c_str(),GENERIC_WRITE,FILE_SHARE_READ,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);if(file.h==INVALID_HANDLE_VALUE)return end(Failed,GetLastError());
            log<<"MonitorMage timed matrix preview\nmode="<<shared.mode<<" seconds="<<shared.seconds<<"\nEncoded-RGB experiment; not an HDR colorimetric transform.\n";displayLog(log);
            const auto filters=windows_controls::inspect();log<<"windows_filters available="<<filters.available<<" on="<<filters.on<<" type="<<filters.filter<<'\n';writeLog(file.h,log);
            const bool initialized=api.open();log<<"initialize_ok="<<initialized<<'\n';if(!initialized)return end(Failed,GetLastError());float scale=0;int x=0,y=0;if(!api.zoom(&scale,&x,&y)||scale!=1||x||y)return end(Blocked,2);
        }
        if(!backend.read(baseline))return end(Failed,GetLastError());
        if(matrix_color::difference(baseline,matrix_color::identity())>1e-5f)return end(Blocked,2);
        if(WaitForSingleObject(parent.h,0)!=WAIT_TIMEOUT||WaitForSingleObject(cancel.h,0)==WAIT_OBJECT_0)return end(Restored,0);
        matrixLog(log,"baseline",baseline);matrixLog(log,"requested",target);if(!fake)writeLog(file.h,log);
        attempted=true;const auto cpuStart=cpu(),start=GetTickCount64();const bool accepted=backend.write(target);matrix_color::Matrix readback{};
        const bool verified=backend.read(readback)&&matrix_color::difference(readback,target)<1e-5f;
        log<<"set_return="<<accepted<<" readback_matches="<<verified<<'\n';
        if(!verified)throw std::runtime_error("Preview readback mismatch");
        shared.appliedAt=GetTickCount64();InterlockedExchange(&shared.phase,Applied);
        if(!fake)writeLog(file.h,log);
        HANDLE wait[]{parent.h,cancel.h};const auto deadline=GetTickCount64()+shared.seconds*1000;DWORD wake=WAIT_TIMEOUT;
        for(;;){const auto now=GetTickCount64();if(now>=deadline)break;
            wake=MsgWaitForMultipleObjects(2,wait,FALSE,static_cast<DWORD>(deadline-now),QS_ALLINPUT);
            if(wake!=WAIT_OBJECT_0+2)break;
            MSG message{};while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE)){TranslateMessage(&message);DispatchMessageW(&message);}
        }
        log<<"cancel_reason="<<InterlockedCompareExchange(&shared.cancelReason,0,0)<<" (0=none,1=button,2=shortcut,3=display_change,4=settings_change,5=window_close,6=session_end)\n";
        log<<"wait_result="<<wake<<" elapsed_ms="<<GetTickCount64()-start<<" worker_cpu_ms="<<(cpu()-cpuStart)/10000.0<<'\n';
        const auto restored=matrix_color::restore(backend,baseline,target);attempted=false;const bool cleaned=fake||api.close();
        log<<"restore_result="<<static_cast<int>(restored)<<" uninitialize_ok="<<cleaned<<"\nRestoration is API readback, not a visual measurement.\nWorker CPU excludes DWM/GPU rendering cost.\n";if(!fake)writeLog(file.h,log);
        return end(!cleaned?Failed:restored==matrix_color::Restoration::Restored?Restored:restored==matrix_color::Restoration::ExternalChange?External:Failed,0);
    }catch(const std::exception& error){
        auto restored=matrix_color::Restoration::Restored;if(attempted)restored=matrix_color::restore(backend,baseline,target);const bool cleaned=fake||api.close();
        log<<"error="<<error.what()<<" restoration="<<static_cast<int>(restored)<<" cleanup="<<cleaned<<'\n';try{if(!fake)writeLog(file.h,log);}catch(...){}
        return end(!attempted?NotStarted:!cleaned?Failed:restored==matrix_color::Restoration::Restored?Stopped:restored==matrix_color::Restoration::ExternalChange?External:Failed,GetLastError());
    }
}
void show(HWND owner,bool hidden){if(previewWindow){if(!hidden){ShowWindow(previewWindow,SW_SHOW);SetForegroundWindow(previewWindow);}return;}
    WNDCLASSW cls{};cls.hInstance=GetModuleHandleW(nullptr);cls.lpszClassName=L"MonitorMageMatrixPreview";cls.lpfnWndProc=proc;cls.hCursor=LoadCursorW(nullptr,IDC_ARROW);cls.hbrBackground=ui::background();RegisterClassW(&cls);
    const UINT dpi=owner?GetDpiForWindow(owner):GetDpiForSystem();RECT rect{0,0,MulDiv(520,dpi,96),MulDiv(358,dpi,96)};ui::adjustWindowRect(rect,dpi);
    const auto h=CreateWindowExW(WS_EX_CONTROLPARENT,cls.lpszClassName,L"MonitorMage - Matrix preview",WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU|WS_CLIPCHILDREN,CW_USEDEFAULT,CW_USEDEFAULT,rect.right-rect.left,rect.bottom-rect.top,owner,nullptr,cls.hInstance,reinterpret_cast<void*>(static_cast<INT_PTR>(hidden?1:0)));
    if(h&&hidden)windowState(h)->testing=true;
    if(h&&!hidden){ShowWindow(h,SW_SHOW);SetForegroundWindow(h);}
}
HWND window(){return previewWindow;}
bool dialogMessage(MSG& message){return previewWindow&&GetAncestor(message.hwnd,GA_ROOT)==previewWindow&&IsDialogMessageW(previewWindow,&message)!=FALSE;}
int run(HINSTANCE){standalone=true;INITCOMMONCONTROLSEX controls{sizeof(controls),ICC_STANDARD_CLASSES};InitCommonControlsEx(&controls);show(nullptr);if(!previewWindow)return 1;
    MSG message{};while(GetMessageW(&message,nullptr,0,0)>0){if(!dialogMessage(message)){TranslateMessage(&message);DispatchMessageW(&message);}}return 0;}
int selfTest(const wchar_t*){
    // No Magnification API is loaded or called by the fake workers below.
    try{
        const auto identity=matrix_color::identity();if(matrix_color::difference(identity,matrix_color::saturation(1))>1e-6f)return 1;
        const auto gray=matrix_color::preview(0);for(int r=0;r<3;++r)if(gray[r][0]!=gray[r][1]||gray[r][1]!=gray[r][2])return 2;
        for(int mode=0;mode<3;++mode){const auto m=matrix_color::preview(mode);for(int output=0;output<3;++output){float neutral=0;for(int input=0;input<3;++input)neutral+=m[input][output];if(std::abs(neutral-1)>1e-6f)return 3;}if(m[3][3]!=1||m[4][4]!=1)return 4;}
        bool rejected=false;try{matrix_color::preview(3);}catch(...){rejected=true;}if(!rejected)return 5;
        Fake api;const auto ours=matrix_color::preview(2);api.value=ours;if(matrix_color::restore(api,identity,ours)!=matrix_color::Restoration::Restored||matrix_color::difference(api.value,identity)>1e-6f)return 6;
        const auto other=matrix_color::preview(1);api.value=other;if(matrix_color::restore(api,identity,ours)!=matrix_color::Restoration::ExternalChange||matrix_color::difference(api.value,other)>1e-6f)return 7;
        struct Fault:matrix_color::Api {matrix_color::Matrix value=matrix_color::preview(2);int reads=0,writes=0;bool firstReadFails=false,dropWrite=false,falseReturn=false;
            bool read(matrix_color::Matrix& m)override{if(firstReadFails&&reads++==0)return false;m=value;return true;}
            bool write(const matrix_color::Matrix& m)override{++writes;if(!dropWrite)value=m;return !falseReturn&&!dropWrite;}};
        Fault unreadable;unreadable.firstReadFails=true;if(matrix_color::restore(unreadable,identity,ours)!=matrix_color::Restoration::Restored||unreadable.writes!=1)return 16;
        Fault refused;refused.dropWrite=true;if(matrix_color::restore(refused,identity,ours)!=matrix_color::Restoration::Failed)return 17;
        Fault falseFlag;falseFlag.falseReturn=true;if(matrix_color::restore(falseFlag,identity,ours)!=matrix_color::Restoration::Restored)return 18;
        const auto applied=[](Session& session){const auto deadline=GetTickCount64()+5000;while(GetTickCount64()<deadline){if(InterlockedCompareExchange(&session.view->p->phase,0,0)==Applied)return true;if(!session.busy())return false;Sleep(5);}return false;};
        const auto ended=[](Session& session){if(WaitForSingleObject(session.process.h,5000)!=WAIT_OBJECT_0)return false;DWORD code=99;GetExitCodeProcess(session.process.h,&code);return code==0&&session.view->p->phase==Finished&&session.view->p->result==Restored;};
        auto cancel=launch(0,30,GetCurrentProcessId(),true);if(!applied(*cancel))return 8;cancel->stop();if(!ended(*cancel))return 9;cancel.reset();
        auto timed=launch(1,1,GetCurrentProcessId(),true);if(!ended(*timed))return 10;timed.reset();
        auto command=quote(executable().wstring())+L" --matrix-preview-owner";STARTUPINFOW startup{sizeof(startup)};startup.dwFlags=STARTF_USESHOWWINDOW|STARTF_FORCEOFFFEEDBACK;startup.wShowWindow=SW_HIDE;PROCESS_INFORMATION process{};
        if(!CreateProcessW(executable().c_str(),command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,nullptr,&startup,&process))return 11;
        struct DisposableOwner {HANDLE h;~DisposableOwner(){if(WaitForSingleObject(h,0)==WAIT_TIMEOUT)TerminateProcess(h,0);CloseHandle(h);}};
        DisposableOwner owner{process.hProcess};CloseHandle(process.hThread);auto orphan=launch(2,30,process.dwProcessId,true);const bool started=applied(*orphan);
        // Only the disposable child created immediately above is terminated.
        TerminateProcess(owner.h,0);if(!started||!ended(*orphan))return 12;orphan.reset();
        INITCOMMONCONTROLSEX controls{sizeof(controls),ICC_STANDARD_CLASSES};InitCommonControlsEx(&controls);show(nullptr,true);if(!previewWindow)return 13;
        auto state=windowState(previewWindow);state->testing=true;buttons(*state,false);SendMessageW(previewWindow,WM_COMMAND,Start,0);
        if(state->session||!*ui::helpText(state->start)||state->hotkeyRegistered)return 14;
        state->testing=false;state->probe=true;bool bound=false;WORD chosen=0;
        for(int key=VK_F20;key<=VK_F24;++key){chosen=MAKEWORD(key,HOTKEYF_CONTROL|HOTKEYF_ALT|HOTKEYF_SHIFT);if(bindShortcut(previewWindow,*state,chosen)){bound=true;break;}}
        if(!bound)return 19;
        const HWND collision=CreateWindowExW(0,L"STATIC",L"",0,0,0,0,0,HWND_MESSAGE,nullptr,GetModuleHandleW(nullptr),nullptr);
        if(!collision)return 20;
        bool collided=false;for(int key=VK_F13;key<=VK_F19;++key){const auto blockedShortcut=MAKEWORD(key,HOTKEYF_CONTROL|HOTKEYF_SHIFT);
            if(RegisterHotKey(collision,1,MOD_CONTROL|MOD_SHIFT|MOD_NOREPEAT,key)){collided=!bindShortcut(previewWindow,*state,blockedShortcut)&&state->shortcut==chosen;UnregisterHotKey(collision,1);break;}}
        DestroyWindow(collision);if(!collided||bindShortcut(previewWindow,*state,MAKEWORD(VK_F12,HOTKEYF_CONTROL)))return 21;
        SendMessageW(previewWindow,WM_HOTKEY,state->hotkeyId,0);if(!state->session||!applied(*state->session))return 22;
        SendMessageW(previewWindow,WM_HOTKEY,state->hotkeyId,0);if(!ended(*state->session)||state->session->view->p->cancelReason!=ShortcutPress)return 23;
        SendMessageW(previewWindow,WM_TIMER,Timer,0);if(state->session)return 24;
        RECT normal{};GetWindowRect(previewWindow,&normal);SetWindowPos(previewWindow,nullptr,0,0,100,100,SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE);
        if(!ui::windowNeedsSizeRepair(previewWindow))return 25;SendMessageW(previewWindow,WM_SYSCOMMAND,SC_RESTORE,0);
        RECT repaired{};GetWindowRect(previewWindow,&repaired);if(repaired.right-repaired.left!=normal.right-normal.left||repaired.bottom-repaired.top!=normal.bottom-normal.top)return 26;
        DestroyWindow(previewWindow);
        if(!RegisterHotKey(nullptr,HotkeyA,MOD_CONTROL|MOD_ALT|MOD_SHIFT|MOD_NOREPEAT,LOBYTE(chosen)))return 27;UnregisterHotKey(nullptr,HotkeyA);return 0;
    }catch(...){return 15;}
}
}
