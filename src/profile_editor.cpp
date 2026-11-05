#include "profile_editor.h"
#include "ui_theme.h"
#include "viewport.h"
#include "ui_messages.h"
#include "icon_resources.h"
#include "programs.h"
#include <commdlg.h>
#include <algorithm>
#include <stdexcept>

namespace profile_editor {
namespace {
constexpr int PanelWidth=420,PanelHeight=526;
constexpr int Right=PanelWidth-16,BodyWidth=PanelWidth-32;
constexpr int Enable = 201, Rules = 203, Executable = 204, Browse = 205, Last = 206;
constexpr int Action = 207, LookChoice = 208, Store = 209, Remove = 210, NewRule = 211, Apply = 212, Close = 213;
constexpr int Running=214,ProgramList=215,SortPrograms=216,Background=217,RefreshPrograms=218,ChooseProgram=219,CancelProgram=220;
HWND editor, enabled, returnNote, rules, executable, browse, last, action, look, store, remove, fresh, applyButton, closeButton, note;
HWND exeLabel, actionLabel, lookLabel;
HWND friendly,runButton,pickerTitle,programList,sortPrograms,background,refreshPrograms,chooseProgram,cancelProgram,programPath,pickerNote;
bool picking=false;
std::vector<programs::Program> runningPrograms;
HFONT font;
applications::Configuration draft;
std::vector<Look> library;
std::filesystem::path lastApplication;
std::function<bool(const applications::Configuration&)> applyChanges;
std::wstring controlText(HWND control) {
    const int length = GetWindowTextLengthW(control); std::wstring text(static_cast<size_t>(length) + 1, L'\0');
    GetWindowTextW(control, text.data(), length + 1); text.resize(length); return text;
}
void message(const std::wstring& text) { SetWindowTextW(note, text.c_str()); }
void setExecutable(const std::filesystem::path& path){SetWindowTextW(executable,path.c_str());SetWindowTextW(friendly,programs::name(path).c_str());}
void pickerSelection(){const auto row=SendMessageW(programList,LB_GETCURSEL,0,0);const bool valid=row>=0&&static_cast<size_t>(row)<runningPrograms.size();EnableWindow(chooseProgram,valid);SetWindowTextW(programPath,valid?runningPrograms[row].path.c_str():L"");}
void refreshProgramsList(bool enumerate){
    const bool memory=SendMessageW(sortPrograms,CB_GETCURSEL,0,0)==0;
    try{if(enumerate)runningPrograms=programs::running(SendMessageW(background,BM_GETCHECK,0,0)==BST_CHECKED,memory);else programs::sort(runningPrograms,memory);
        SendMessageW(programList,WM_SETREDRAW,FALSE,0);SendMessageW(programList,LB_RESETCONTENT,0,0);
        for(const auto& item:runningPrograms){const auto text=item.name+L"  |  "+(item.memoryKnown?std::to_wstring((item.memory+1048575)/1048576)+L" MB":L"Memory unavailable");SendMessageW(programList,LB_ADDSTRING,0,reinterpret_cast<LPARAM>(text.c_str()));}
        SendMessageW(programList,WM_SETREDRAW,TRUE,0);InvalidateRect(programList,nullptr,FALSE);
        SetWindowTextW(pickerNote,(std::to_wstring(runningPrograms.size())+L" programs. Memory is approximate.").c_str());
    }catch(const std::exception& error){const std::string text(error.what());SetWindowTextW(pickerNote,std::wstring(text.begin(),text.end()).c_str());}
    pickerSelection();
}
void choose(HWND combo, const std::filesystem::path& path) {
    int index = -1;
    for (size_t i = 0; i < library.size(); ++i) if (applications::samePath(library[i].path, path)) index = static_cast<int>(i);
    SendMessageW(combo, CB_SETCURSEL, index, 0);
}
std::filesystem::path selectedLook(HWND combo) {
    const auto index = SendMessageW(combo, CB_GETCURSEL, 0, 0);
    return index >= 0 && static_cast<size_t>(index) < library.size() ? library[index].path : std::filesystem::path{};
}
void refreshRules(int selection = -1) {
    SendMessageW(rules,WM_SETREDRAW,FALSE,0);
    SendMessageW(rules, LB_RESETCONTENT, 0, 0);
    for (const auto& rule : draft.rules) {
        const auto text = programs::name(rule.executable) + L"  |  " + (rule.action == applications::Action::Look ? rule.look.stem().wstring() :
            rule.action == applications::Action::Bypass ? std::wstring(L"Bypass look") : rule.action == applications::Action::Disable ? std::wstring(L"Pause MonitorMage") : std::wstring(L"Pause HDR gamma"));
        SendMessageW(rules, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(text.c_str()));
    }
    SendMessageW(rules, LB_SETCURSEL, selection, 0);
    SendMessageW(rules,WM_SETREDRAW,TRUE,0);InvalidateRect(rules,nullptr,FALSE);
    EnableWindow(remove, selection >= 0);
}
void actionControls() { EnableWindow(look, SendMessageW(action, CB_GETCURSEL, 0, 0) == 0); }
void layout() {
    const UINT dpi = GetDpiForWindow(editor);
    const auto offset=ui::viewportOffset(editor);
    auto place = [&](HWND control, int x, int y, int w, int h) { MoveWindow(control, MulDiv(x, dpi, 96)-offset.x, MulDiv(y, dpi, 96)-offset.y, MulDiv(w, dpi, 96), MulDiv(h, dpi, 96), TRUE); };
    HFONT previous = font; font = ui::makeFont(dpi);
    for (HWND control : {enabled, returnNote, rules, executable, browse, last, action, look, store, remove, fresh, applyButton, closeButton, note, exeLabel, actionLabel, lookLabel,friendly,runButton,pickerTitle,programList,sortPrograms,background,refreshPrograms,chooseProgram,cancelProgram,programPath,pickerNote})
        SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
    if (previous) DeleteObject(previous);
    const auto width=[&](HWND control,int minimum=68,int maximum=240){return ui::contentWidth(control,font,minimum,maximum);};
    for(HWND control:{enabled,returnNote,rules,executable,browse,last,action,look,store,remove,fresh,applyButton,closeButton,note,exeLabel,actionLabel,lookLabel,friendly,runButton})ShowWindow(control,picking?SW_HIDE:SW_SHOW);
    for(HWND control:{pickerTitle,programList,sortPrograms,background,refreshPrograms,chooseProgram,cancelProgram,programPath,pickerNote})ShowWindow(control,picking?SW_SHOW:SW_HIDE);
    SetWindowTextW(editor,picking?L"MonitorMage - running programs":L"MonitorMage - application profiles");
    SendMessageW(applyButton,BM_SETSTYLE,picking?BS_PUSHBUTTON:BS_DEFPUSHBUTTON,FALSE);
    SendMessageW(chooseProgram,BM_SETSTYLE,picking?BS_DEFPUSHBUTTON:BS_PUSHBUTTON,FALSE);
    place(enabled,16,16,BodyWidth,28);place(returnNote,16,50,BodyWidth,34);
    place(rules,16,92,BodyWidth,130);place(exeLabel,16,234,60,22);place(friendly,80,234,Right-80,22);place(executable,16,260,BodyWidth,26);
    const int browseWidth=width(browse),lastWidth=width(last);
    place(browse,16,294,browseWidth,28);place(last,28+browseWidth,294,lastWidth,28);place(runButton,40+browseWidth+lastWidth,294,width(runButton),28);
    const int actionWidth=width(action,110,180),lookX=32+actionWidth;
    place(actionLabel,16,330,actionWidth,20);place(action,16,350,actionWidth,180);place(lookLabel,lookX,330,Right-lookX,20);place(look,lookX,350,Right-lookX,220);
    const int storeWidth=width(store),removeWidth=width(remove);
    place(store,16,386,storeWidth,30);place(remove,28+storeWidth,386,removeWidth,30);place(fresh,40+storeWidth+removeWidth,386,width(fresh),30);
    const int closeWidth=width(closeButton),applyWidth=width(applyButton);
    place(note,16,428,BodyWidth,44);place(applyButton,Right-12-closeWidth-applyWidth,480,applyWidth,30);place(closeButton,Right-closeWidth,480,closeWidth,30);
    const int sortWidth=width(sortPrograms,112,180),refreshWidth=width(refreshPrograms);
    place(pickerTitle,16,16,BodyWidth,26);place(sortPrograms,16,52,sortWidth,180);place(background,16,84,width(background,140,200),28);place(refreshPrograms,Right-refreshWidth,84,refreshWidth,28);
    const int cancelWidth=width(cancelProgram),chooseWidth=width(chooseProgram);
    place(programList,16,122,BodyWidth,280);place(programPath,16,414,BodyWidth,26);place(pickerNote,16,450,BodyWidth,24);place(chooseProgram,Right-12-cancelWidth-chooseWidth,480,chooseWidth,30);place(cancelProgram,Right-cancelWidth,480,cancelWidth,30);
}
LRESULT CALLBACK proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    LRESULT themeResult = 0;
    if (ui::message(hwnd, msg, wp, lp, themeResult)) return themeResult;
    if(msg==WM_MEASUREITEM){auto* item=reinterpret_cast<MEASUREITEMSTRUCT*>(lp);if(item->CtlType==ODT_LISTBOX){item->itemHeight=MulDiv(22,GetDpiForWindow(hwnd),96);return TRUE;}}
    if(msg==WM_DRAWITEM){const auto& item=*reinterpret_cast<DRAWITEMSTRUCT*>(lp);if(item.CtlType==ODT_LISTBOX){
        const bool selected=(item.itemState&ODS_SELECTED)!=0;HBRUSH fill=CreateSolidBrush(selected?ui::SelectedTab:ui::Surface);FillRect(item.hDC,&item.rcItem,fill);DeleteObject(fill);
        if(item.itemID==static_cast<UINT>(-1))return TRUE;
        const auto length=SendMessageW(item.hwndItem,LB_GETTEXTLEN,item.itemID,0);if(length==LB_ERR)return TRUE;
        std::wstring text(static_cast<size_t>(length)+1,L'\0');SendMessageW(item.hwndItem,LB_GETTEXT,item.itemID,reinterpret_cast<LPARAM>(text.data()));text.resize(static_cast<size_t>(length));
        const auto old=SelectObject(item.hDC,font?font:GetStockObject(DEFAULT_GUI_FONT));SetBkMode(item.hDC,TRANSPARENT);SetTextColor(item.hDC,ui::Text);
        RECT label=item.rcItem;const int inset=MulDiv(6,GetDpiForWindow(hwnd),96);label.left+=inset;label.right-=inset;
        const auto split=text.rfind(L"  |  ");if(split!=std::wstring::npos){const auto detail=text.substr(split+5);text.resize(split);
            SIZE size{};GetTextExtentPoint32W(item.hDC,detail.c_str(),static_cast<int>(detail.size()),&size);RECT right=label;right.left=right.right-std::min(static_cast<int>(size.cx)+inset,static_cast<int>((label.right-label.left)*2/5));label.right=right.left-inset;
            SetTextColor(item.hDC,ui::Muted);DrawTextW(item.hDC,detail.c_str(),-1,&right,DT_SINGLELINE|DT_VCENTER|DT_RIGHT|DT_END_ELLIPSIS|DT_NOPREFIX);SetTextColor(item.hDC,ui::Text);}
        DrawTextW(item.hDC,text.c_str(),-1,&label,DT_SINGLELINE|DT_VCENTER|DT_END_ELLIPSIS|DT_NOPREFIX);
        if(item.itemState&ODS_FOCUS){RECT focus=item.rcItem;InflateRect(&focus,-1,-1);DrawFocusRect(item.hDC,&focus);}SelectObject(item.hDC,old);return TRUE;
    }}
    if (msg == WM_SIZE) { ui::frame(hwnd); return 0; }
    if (msg == WM_CREATE) {
        ui::fixedWindowSize(hwnd,PanelWidth,PanelHeight);
        ui::enableViewport(hwnd,PanelWidth,PanelHeight);
        editor = hwnd; const auto instance = reinterpret_cast<LPCREATESTRUCTW>(lp)->hInstance;
        auto control = [&](const wchar_t* cls, const wchar_t* text, DWORD style, int id = 0) {
            if(wcscmp(cls,L"COMBOBOX")==0)style|=CBS_OWNERDRAWFIXED|CBS_HASSTRINGS;
            if(wcscmp(cls,L"EDIT")==0)style&=~WS_BORDER; // Shared rounded frame owns the entire border.
            HWND child = CreateWindowExW(0, cls, text, WS_CHILD | WS_VISIBLE | style, 0, 0, 1, 1, hwnd,
                reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), instance, nullptr);
            ui::control(child); return child;
        };
        enabled = control(L"BUTTON", L"Automatically select application profiles", WS_TABSTOP | BS_AUTOCHECKBOX, Enable);
        returnNote = control(L"STATIC", L"Switching away from a profiled program restores your previous look.", 0);
        rules = control(L"LISTBOX", L"", WS_BORDER | WS_TABSTOP | LBS_NOTIFY | LBS_OWNERDRAWFIXED | LBS_HASSTRINGS | WS_VSCROLL | LBS_NOINTEGRALHEIGHT, Rules);
        exeLabel = control(L"STATIC", L"Program", 0); executable = control(L"EDIT", L"", WS_BORDER | WS_TABSTOP | ES_AUTOHSCROLL | ES_READONLY, Executable);
        browse = control(L"BUTTON", L"Browse...", WS_TABSTOP, Browse); last = control(L"BUTTON", L"Last app", WS_TABSTOP, Last);
        friendly=control(L"STATIC",L"",SS_ENDELLIPSIS);runButton=control(L"BUTTON",L"Running programs...",WS_TABSTOP,Running);
        actionLabel = control(L"STATIC", L"Action", 0); action = control(L"COMBOBOX", L"", WS_TABSTOP | CBS_DROPDOWNLIST, Action);
        for (const auto text : {L"Use look", L"Bypass look", L"Pause MonitorMage", L"Pause HDR gamma"}) SendMessageW(action, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(text));
        SendMessageW(action, CB_SETCURSEL, 0, 0);
        lookLabel = control(L"STATIC", L"Look", 0); look = control(L"COMBOBOX", L"", WS_TABSTOP | CBS_DROPDOWNLIST | WS_VSCROLL, LookChoice);
        for (const auto& entry : library) SendMessageW(look, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(entry.name.c_str()));
        SendMessageW(look, CB_SETCURSEL, 0, 0);
        SendMessageW(enabled, BM_SETCHECK, draft.enabled ? BST_CHECKED : BST_UNCHECKED, 0);
        store = control(L"BUTTON", L"Add / update rule", WS_TABSTOP, Store); remove = control(L"BUTTON", L"Remove rule", WS_TABSTOP, Remove);
        fresh = control(L"BUTTON", L"New rule", WS_TABSTOP, NewRule);
        note = control(L"STATIC", L"Choose a program and an action. Click Add / update rule, then Apply to save.", 0);
        applyButton = control(L"BUTTON", L"Apply", WS_TABSTOP | BS_DEFPUSHBUTTON, Apply); closeButton = control(L"BUTTON", L"Close", WS_TABSTOP, Close);
        pickerTitle=control(L"STATIC",L"Choose a running program",0);
        sortPrograms=control(L"COMBOBOX",L"",WS_TABSTOP|CBS_DROPDOWNLIST,SortPrograms);
        for(const auto* text:{L"Memory usage",L"Name"})SendMessageW(sortPrograms,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(text));SendMessageW(sortPrograms,CB_SETCURSEL,0,0);
        background=control(L"BUTTON",L"Background apps",WS_TABSTOP|BS_AUTOCHECKBOX,Background);
        refreshPrograms=control(L"BUTTON",L"Refresh",WS_TABSTOP,RefreshPrograms);
        programList=control(L"LISTBOX",L"Running user programs",WS_BORDER|WS_TABSTOP|LBS_NOTIFY|LBS_OWNERDRAWFIXED|LBS_HASSTRINGS|WS_VSCROLL|LBS_NOINTEGRALHEIGHT,ProgramList);
        programPath=control(L"EDIT",L"",WS_BORDER|WS_TABSTOP|ES_READONLY|ES_AUTOHSCROLL);
        pickerNote=control(L"STATIC",L"",SS_ENDELLIPSIS);
        chooseProgram=control(L"BUTTON",L"Choose",WS_TABSTOP,ChooseProgram);cancelProgram=control(L"BUTTON",L"Back",WS_TABSTOP,CancelProgram);
        const std::pair<HWND,const wchar_t*> help[]{
            {enabled,L"Automatic profile selection"},
            {rules,L"Program rules"},
            {executable,L"Program location"},
            {browse,L"Browse for a program"},
            {last,L"Use last active program"},
            {runButton,L"Choose a running program"},
            {action,L"Action for this program"},
            {look,L"Preset for this program"},
            {store,L"Add / update rule"},
            {remove,L"Remove selected rule"},
            {fresh,L"New program rule"},
            {applyButton,L"Save and apply profiles"},
            {closeButton,L"Close profiles"},
            {programList,L"Running programs"},
            {sortPrograms,L"Sort programs"},
            {background,L"Include background programs"},
            {refreshPrograms,L"Refresh running programs"},
            {programPath,L"Selected program location"},
            {chooseProgram,L"Choose this program"},
            {cancelProgram,L"Back to profile rules"}};
        for(const auto& item:help)ui::help(item.first,item.second);
        EnableWindow(last, !lastApplication.empty()); refreshRules(); actionControls(); layout(); return 0;
    }
    if (msg == WM_COMMAND) {
        const int id = LOWORD(wp), event = HIWORD(wp);
        if(id==Running){picking=true;layout();refreshProgramsList(true);SetFocus(programList);return 0;}
        if(id==CancelProgram||(picking&&id==IDCANCEL)){picking=false;layout();SetFocus(runButton);return 0;}
        if(id==RefreshPrograms||id==Background){refreshProgramsList(true);return 0;}
        if(id==SortPrograms&&event==CBN_SELCHANGE){refreshProgramsList(false);return 0;}
        if(id==ProgramList&&event==LBN_SELCHANGE){pickerSelection();return 0;}
        if(id==ChooseProgram||(id==ProgramList&&event==LBN_DBLCLK)||(picking&&id==IDOK)){
            const auto row=SendMessageW(programList,LB_GETCURSEL,0,0);if(row>=0&&static_cast<size_t>(row)<runningPrograms.size()){setExecutable(runningPrograms[row].path);picking=false;layout();SetFocus(action);}return 0;
        }
        if (id == Close || id == IDCANCEL) { DestroyWindow(hwnd); return 0; }
        if (id == Action && event == CBN_SELCHANGE) actionControls();
        if (id == Last) setExecutable(lastApplication);
        if (id == Browse) {
            wchar_t file[32768]{}; OPENFILENAMEW dialog{}; dialog.lStructSize = sizeof(dialog); dialog.hwndOwner = hwnd;
            dialog.lpstrFilter = L"Programs (*.exe)\0*.exe\0\0"; dialog.lpstrFile = file; dialog.nMaxFile = static_cast<DWORD>(std::size(file));
            dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
            if (GetOpenFileNameW(&dialog)) setExecutable(file);
        }
        if (id == NewRule) { setExecutable({}); SendMessageW(rules, LB_SETCURSEL, static_cast<WPARAM>(-1), 0); EnableWindow(remove, FALSE); }
        if (id == Rules && event == LBN_SELCHANGE) {
            const auto index = SendMessageW(rules, LB_GETCURSEL, 0, 0);
            if (index >= 0 && static_cast<size_t>(index) < draft.rules.size()) {
                const auto& rule = draft.rules[index]; setExecutable(rule.executable);
                SendMessageW(action, CB_SETCURSEL, static_cast<int>(rule.action), 0); choose(look, rule.look);
                EnableWindow(remove, TRUE); actionControls();
            }
        }
        if (id == Remove) {
            const auto index = SendMessageW(rules, LB_GETCURSEL, 0, 0);
            if (index >= 0 && static_cast<size_t>(index) < draft.rules.size()) { draft.rules.erase(draft.rules.begin() + index); refreshRules(); setExecutable({}); }
        }
        try {
            if (id == Store) {
                applications::Rule rule; rule.executable = controlText(executable);
                rule.action = static_cast<applications::Action>(SendMessageW(action, CB_GETCURSEL, 0, 0));
                if (rule.action == applications::Action::Look) rule.look = selectedLook(look);
                auto candidate = draft; size_t index = candidate.rules.size();
                for (size_t i = 0; i < candidate.rules.size(); ++i) if (applications::samePath(candidate.rules[i].executable, rule.executable)) index = i;
                if (index == candidate.rules.size()) candidate.rules.push_back(rule); else candidate.rules[index] = rule;
                applications::validate(candidate); draft = candidate; refreshRules(static_cast<int>(index));
                message(L"Rule updated. Click Apply to save your changes.");
            }
            if (id == Apply || id == IDOK) {
                auto candidate = draft; candidate.enabled = SendMessageW(enabled, BM_GETCHECK, 0, 0) == BST_CHECKED;
                applications::validate(candidate);
                if (applyChanges(candidate)) { draft = candidate; message(L"Saved. A look you choose yourself lasts until you switch programs."); }
                else message(L"Profiles could not be saved. Check the message in the main window.");
            }
        } catch (const std::exception& error) { message(ui::messages::error(error.what())); }
        return 0;
    }
    if (msg == WM_DPICHANGED) {
        const auto rect = reinterpret_cast<RECT*>(lp); SetWindowPos(hwnd, nullptr, rect->left, rect->top, rect->right - rect->left, rect->bottom - rect->top, SWP_NOZORDER | SWP_NOACTIVATE); layout(); ui::frame(hwnd); return 0;
    }
    if (msg == WM_CLOSE) { DestroyWindow(hwnd); return 0; }
    if (msg == WM_DESTROY) { if (font) DeleteObject(font); font = nullptr; editor = nullptr; applyChanges = {};runningPrograms.clear();picking=false;return 0; }
    return DefWindowProcW(hwnd, msg, wp, lp);
}
}
void refreshAppearance(){if(editor){layout();ui::refreshCaptionFont(editor);InvalidateRect(editor,nullptr,TRUE);}}
HWND window() { return editor; }
void previewPrograms(const std::vector<programs::Program>& entries){if(editor){runningPrograms=entries;picking=true;layout();refreshProgramsList(false);SendMessageW(programList,LB_SETCURSEL,0,0);pickerSelection();}}
HWND show(HWND owner, HINSTANCE instance, const applications::Configuration& config, const std::vector<Look>& looks,
    const std::filesystem::path& previous, std::function<bool(const applications::Configuration&)> apply, bool hidden) {
    if (editor) { if (!hidden) { ShowWindow(editor, SW_SHOW); SetForegroundWindow(editor); } return editor; }
    draft = config; library = looks; lastApplication = previous; applyChanges = std::move(apply);picking=false;
    WNDCLASSW cls{}; cls.hInstance = instance; cls.lpfnWndProc = proc; cls.lpszClassName = L"LutApplicationProfiles";
    cls.hCursor = LoadCursorW(nullptr, IDC_ARROW); cls.hbrBackground = ui::background();
    cls.hIcon = LoadIconW(instance, MAKEINTRESOURCEW(IDI_PROFILE_DIAMOND));
    if (!RegisterClassW(&cls) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return nullptr;
    const UINT dpi = GetDpiForWindow(owner); RECT rect{0, 0, MulDiv(PanelWidth, dpi, 96), MulDiv(PanelHeight, dpi, 96)};
    constexpr DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU;
    ui::adjustWindowRect(rect, dpi); RECT parent{}; GetWindowRect(owner, &parent);
    editor = CreateWindowExW(0, cls.lpszClassName, L"MonitorMage - application profiles", style,
        parent.left, parent.top, rect.right - rect.left, rect.bottom - rect.top, owner, nullptr, instance, nullptr);
    if(editor)SendMessageW(editor,WM_SETICON,ICON_SMALL,reinterpret_cast<LPARAM>(LoadImageW(instance,MAKEINTRESOURCEW(IDI_PROFILE_DIAMOND),IMAGE_ICON,MulDiv(16,dpi,96),MulDiv(16,dpi,96),LR_SHARED)));
    if (editor && !hidden) { ShowWindow(editor, SW_SHOW); SetForegroundWindow(editor); }
    return editor;
}
}
