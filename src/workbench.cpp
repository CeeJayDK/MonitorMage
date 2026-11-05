#include <windows.h>
#include <commctrl.h>
#include <commdlg.h>
#include <shellapi.h>
#include <uxtheme.h>
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <locale>
#include <sstream>
#include <string>
#include <vector>
#include <fstream>
#include <psapi.h>
#include "color_math.h"
#include "live_lut.h"
#include "processing_state.h"
#include "presets.h"
#include "lut1d.h"
#include "application_profiles.h"
#include "profile_editor.h"
#include "programs.h"
#include "ui_theme.h"
#include "viewport.h"
#include "ui_messages.h"
#include "unsaved_dialog.h"
#include "hdr_control.h"
#include "windows_controls.h"
#include "matrix_preview.h"
#include "matrix_effect.h"
#include "matrix_recovery.h"
#include "rgb_plot.h"
#include "paint_buffer.h"
#include "effect_list.h"
#include "tray_popup.h"
#include "app_icons.h"
#include "app_version.h"
#include "app_settings.h"
#include "night_outputs.h"
#include <array>
#include <chrono>
#include <windowsx.h>

#ifdef _MSC_VER
#pragma comment(linker, "\"/manifestdependency:type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")
#endif

namespace {
constexpr int SidebarWidth=194;
constexpr int PaneOffset=SidebarWidth-186;
constexpr int ClientWidth=768+PaneOffset;
constexpr int ClientHeight=552;
constexpr UINT TrayMessage = WM_APP + 1;
constexpr UINT ForegroundMessage = WM_APP + 2;
constexpr int Layers = 101, Contrast = 102, Pivot = 103, White = 104, Gamma = 105;
constexpr int Reset = 106, Export = 107, Open = 108, Exit = 109;
constexpr int GammaOther = 110;
constexpr int SourceChoice = 111, TestContrast = 112;
constexpr int Bypass = 113, HdrFix = 114;
constexpr int Shortcut = 115, SetShortcut = 116;
constexpr int PresetChoice = 117, SavePreset = 118, SavePresetAs = 119, OpenPreset = 120, Autosave = 121;
constexpr int RevertPreset = 122;
constexpr int ApplicationProfiles = 123;
constexpr int ActiveLook = 124;
constexpr int SettingsPage=125,FontChoice=126,CurveChannel=127,Import1D=128,Export1D=129,AdjustmentBase=200;
constexpr int ToggleWindowsHdr=130;
constexpr int MonitorStatus=131;
constexpr int OutputInfo=139;
constexpr int ColorFilterChoice=142,ColorFilterSettings=143;
constexpr UINT_PTR ColorFilterTimer=907;
constexpr UINT_PTR NightTimer=908;
constexpr UINT_PTR MatrixRecoveryTimer=909;
matrix_effect::Recovery matrixRecovery;
constexpr int NightSchedule=148,NightStart=149,NightEnd=150,NightInclude=151,NightDisplay=152;
constexpr int MonitorMode=133;
constexpr int MonitorProfile=138;
constexpr int WebsiteLink=153;
constexpr int StartWithWindows=134;
constexpr int StartMinimized=154;
constexpr int CorrectionLayers=135;
constexpr int OrderHint=136;
constexpr int SelectedEffectToggle=137;
constexpr int WindowsCorrectionLayers=146,SaturationReport=147;
constexpr UINT_PTR ApplyTimer = 901, DisplayTimer = 902;
constexpr UINT_PTR IdleTimer = 903;
constexpr UINT_PTR PresetTimer = 904;
constexpr UINT_PTR ForegroundTimer = 905;
constexpr UINT_PTR HdrTimer=906;
hdr::State hdrState;
hdr::State hdrTransition;
bool hdrPending=false,hdrDesired=false,hdrResume=false;
unsigned hdrAttempts=0;
std::wstring hdrSource;
HINSTANCE instance;
app_icons::Cache iconCache;
HWND window, layers, contrastSlider, pivotSlider, whiteSlider, gammaChoice, gammaOther, canvas;
HWND correctionLayers,windowsLayers,effectsLabel;
HWND selectedEffectToggle,saturationReportButton;
HWND title, description, amountLabel, pivotLabel, whiteLabel, gammaLabel, status, resetButton, exportButton, listLabel;
HWND sourceChoice,testNote;
HWND monitorMode,monitorProfile,websiteLink;
HWND bypassButton, hdrFixControl;
HWND shortcutControl, shortcutButton, shortcutLabel;
HWND presetLabel, presetChoice, savePresetButton, saveAsButton, openPresetButton, autosaveControl;
HWND revertPresetButton;
HWND profilesButton;
HWND activeLookLabel, orderHint;
HWND settingsButton,fontChoice,fontLabel,curveChannel,importButton,export1DButton;
HWND hdrButton,powerButton,monitorTips,infoButton;
HWND monitorSettingsButton;
HWND versionLabel,startupControl,startMinimizedControl;
HWND filterChoice,filterSettingsButton,correctionNote;
windows_controls::State filterState;
night::Settings nightConfig;
night::Outputs nightOutputs;
double nightLevel=0;
std::wstring nightMessage;
HWND nightSchedule,nightStart,nightEnd,nightInclude,nightStartLabel,nightEndLabel,nightDisplay;
windows_controls::Controller filterController;
bool filterError=false;
std::wstring filterMessage;
bool startupEnabled=false;
bool startMinimized=false;
bool editorReady=false, runtimeReady=false, deferEditor=false;
unsigned editorSyncDepth=0;
struct EditorSync {EditorSync(){++editorSyncDepth;}~EditorSync(){--editorSyncDepth;}};
int selectedSource=0;
void ensureEditor();
void ensureCommonControls(){static const bool initialized=[](){INITCOMMONCONTROLSEX common{sizeof(common),ICC_BAR_CLASSES|ICC_HOTKEY_CLASS};return InitCommonControlsEx(&common)!=FALSE;}();(void)initialized;}
void updateTray();
int selectedSourceIndex(){return sourceChoice?static_cast<int>(SendMessageW(sourceChoice,CB_GETCURSEL,0,0)):selectedSource;}
void selectSource(int index){selectedSource=index;if(sourceChoice)SendMessageW(sourceChoice,CB_SETCURSEL,index,0);}
bool closing=false;
void layoutMonitorControls();
std::array<HWND,5> adjustmentSliders{},adjustmentLabels{},adjustmentValues{};
HWND amountValue,pivotValue,whiteValue;
bool settingsPage=false;int adjustmentPage=-1,editingChannel=0,draggedPoint=-1,keyboardPoint=4;
std::vector<int> effectOrder{0,2,3,4,5,6,7,1};
int presetRole();
applications::Configuration appConfig;
applications::Focus appFocus;
applications::Action profileAction = applications::Action::Look;
HWINEVENTHOOK foregroundHook = nullptr;
HWND pendingForeground = nullptr;
bool correctionsRequested = false;
bool powerSuspended = false;
std::wstring profilePausedIdentity, profileMessage;
std::array<std::vector<presets::Draft>, 2> presetLibrary;
std::array<size_t, 2> presetIndex{};
size_t regularLookIndex = 0;
bool applicationContextActive = false;
bool autosave = false;
bool sliderDragging = false, lookDirtyBeforeDrag = false;
bool presetUiTest = false;
bool profileUiTest = false, effectUiTest = false, surfaceUiTest = false;
std::wstring testSettingsPath;
std::wstring presetMessage;
WORD activeShortcut = MAKEWORD(VK_F8, HOTKEYF_CONTROL | HOTKEYF_ALT);
WORD preferredShortcut = activeShortcut;
int activeHotkeyId = 1;
DWORD hotkeyError = 0;
std::wstring hotkeyMessage;
HANDLE activeTest = nullptr;
using Source = live::Output;
std::vector<Source> sources;
live::Session liveSession;
matrix_effect::Controller matrixSession;
std::wstring matrixMessage;
live::Output liveOutput;
bool contrastEnabled = true, correctionFirst = false;
bool bypassed = false, hdrFixEnabled = true, hotkeyRegistered = false, resumeAfterWake = false;
std::wstring liveMessage = L"Off. Turn on MonitorMage to apply your adjustments.";
bool liveError=false,statusError=false;
bool graphStatusVisible=false;
bool displayRetryPending=false,modeResetPending=false;
int displayAttempts=0;
std::wstring lastPresetError;
HFONT font,hintFont;
plot::Scratch curveScratch;
size_t lastGraphPaintBytes=0;unsigned graphLayoutVersion=0;
std::array<RECT,3> graphAuditDamage{};unsigned graphAuditCount=0;
void invalidateGraph();
NOTIFYICONDATAW tray{};
UINT explorerRestart;
bool trayInstalled = false;
bool renderOnly = false;
std::wstring idleReport;
ULONGLONG idleCpuStart = 0, idleStart = 0;
DWORD idleFaultStart=0;
int dpi = 96, selected = 0, contrastPosition = 0, pivotPosition = 50, whitePosition = 200, gammaPosition = 0;

int presetRole() {return selected==1?1:0;}
int processingRow(int role) {return static_cast<int>(std::find(effectOrder.begin(),effectOrder.end(),role)-effectOrder.begin());}
std::vector<int> creativeOrder(){auto result=effectOrder;result.erase(std::remove(result.begin(),result.end(),1),result.end());result.push_back(10);result.push_back(11);return result;}
HWND effectControl(int role){return (role==8||role==9)?windowsLayers:role==1?correctionLayers:layers;}
int effectRow(int role){if(role==8||role==9)return role-8;if(role==1)return 0;const auto order=creativeOrder();return static_cast<int>(std::find(order.begin(),order.end(),role)-order.begin());}
int px(int value) { return MulDiv(value, dpi, 96); }
std::wstring shortcutPath() {
    if (!testSettingsPath.empty()) return testSettingsPath;
    wchar_t path[32768]{};
    const DWORD length = GetModuleFileNameW(nullptr, path, static_cast<DWORD>(std::size(path)));
    if (!length || length >= std::size(path)) return L"";
    std::wstring result(path); const auto slash = result.find_last_of(L"\\/");
    return slash == std::wstring::npos ? L"" : result.substr(0, slash + 1) + L"MonitorMage.ini";
}
void labels();
void refreshColorFilters(){filterState=windows_controls::inspect();}
void changeWindowsFilter(bool on,DWORD filter,bool selectingFilter=false) {
    if(renderOnly||filterController.busy())return;
    try{filterController.set(filterState,on,filter,selectingFilter);filterError=false;filterMessage=L"Windows is applying the color filter...";
        if(!SetTimer(window,ColorFilterTimer,150,nullptr)){filterController.stopWaiting();throw std::runtime_error("The Windows request was sent but could not be monitored; check Windows settings");}}
    catch(const std::exception& error){filterMessage=ui::messages::error(error.what());filterError=true;}
    refreshColorFilters();labels();
}
void applyLive();
void refreshPresetChoice();
void reconcileLive();
void applyApplicationProfile(bool force = false);
void manualLookOverride();
bool lookBypassed();
bool effectiveHdrFix();
presets::Kind presetKind(int role) { return role == 0 ? presets::Kind::Look : presets::Kind::Correction; }
presets::Draft& currentPreset(int role) { return presetLibrary[role][presetIndex[role]]; }
std::filesystem::path presetFolder(int role) {
    return std::filesystem::path(shortcutPath()).parent_path() / L"presets" / (role == 0 ? L"looks" : L"corrections");
}
void presetError(const std::exception& error) {
    presetMessage=ui::messages::error(error.what());
    lastPresetError=presetMessage;
}
bool persistPresetChoices() {
    if (renderOnly && !presetUiTest) return false;
    const auto path = shortcutPath();
    std::wstring text = L"[Hotkey]\r\nBypass=" + std::to_wstring(preferredShortcut) + L"\r\n\r\n[Presets]\r\n";
    for (int role = 0; role < 2; ++role) {
        auto stored = role == 0 ? presetLibrary[0][regularLookIndex].path : currentPreset(role).path;
        if (!stored.empty()) {
            const auto relative = stored.lexically_relative(std::filesystem::path(path).parent_path());
            if (!relative.empty() && !relative.is_absolute() && *relative.begin() != L"..") stored = relative;
        }
        const auto value = stored.wstring();
        text += (role == 0 ? L"Look=" : L"Correction=") + value + L"\r\n";
    }
    text += autosave ? L"Autosave=1\r\n" : L"Autosave=0\r\n";
    text+=L"\r\n[Startup]\r\nMinimized="+std::to_wstring(startMinimized)+L"\r\n";
    text+=L"\r\n[NightLight]\r\nEnabled="+std::to_wstring(nightConfig.enabled)+L"\r\nScheduled="+std::to_wstring(nightConfig.scheduled)+L"\r\nStrength="+std::to_wstring(nightConfig.strength)+L"\r\nStart="+std::to_wstring(nightConfig.start)+L"\r\nEnd="+std::to_wstring(nightConfig.end)+L"\r\nFade="+std::to_wstring(nightConfig.fade)+L"\r\nExcluded=";
    for(const auto& key:nightConfig.excluded)text+=key+L",";text+=L"\r\n";
    text += L"\r\n[Effects]\r\nCorrectionFirst=" + std::wstring(correctionFirst ? L"1" : L"0") + L"\r\n";
    text+=L"Order=";for(size_t i=0;i<effectOrder.size();++i){if(i)text+=L",";text+=std::to_wstring(effectOrder[i]);}text+=L"\r\n\r\n[Appearance]\r\nFont="+std::wstring(ui::fontFamily())+L"\r\n";
    try { text += applications::encode(appConfig, path); }
    catch (const std::exception& error) { presetError(error); return false; }
    // Win32 INI readers require a UTF-16 BOM to retain arbitrary Unicode paths.
    std::string bytes("\xff\xfe", 2);
    for (wchar_t unit : text) { bytes.push_back(static_cast<char>(unit & 255)); bytes.push_back(static_cast<char>(unit >> 8)); }
    try {
        if (path.empty()) throw std::runtime_error("Settings path unavailable");
        presets::writeBytes(path, bytes, true);
        WritePrivateProfileStringW(nullptr, nullptr, nullptr, path.c_str());
        return true;
    } catch (const std::exception& error) { presetError(error); return false; }
}
size_t findPreset(int role, const std::filesystem::path& path) {
    const auto full = std::filesystem::absolute(path).lexically_normal().wstring();
    auto& entries = presetLibrary[role];
    for (size_t i = 1; i < entries.size(); ++i) {
        const auto candidate = std::filesystem::absolute(entries[i].path).lexically_normal().wstring();
        if (CompareStringOrdinal(full.c_str(), -1, candidate.c_str(), -1, TRUE) == CSTR_EQUAL) return i;
    }
    return entries.size();
}
size_t readPreset(int role, const std::filesystem::path& path) {
    const size_t index = findPreset(role, path);
    if(index<presetLibrary[role].size() && presetLibrary[role][index].loaded)return index;
    const auto data = presets::read(path);
    if (data.kind != presetKind(role)) throw std::runtime_error(role == 0 ? "This is a correction preset. Select HDR gamma correction to open it." : "This is a look preset. Select Contrast to open it.");
    if (index == presetLibrary[role].size()) presetLibrary[role].push_back({std::filesystem::absolute(path), data, data});
    else presetLibrary[role][index]={std::filesystem::absolute(path),data,data};
    // An already-open preset keeps its unsaved session draft.
    return index;
}
void indexPreset(int role,const std::filesystem::path& path){
    if(findPreset(role,path)!=presetLibrary[role].size())return;
    const auto inactive=presets::inactive(presetKind(role));
    presetLibrary[role].push_back({std::filesystem::absolute(path),inactive,inactive,false});
}
void discoverPresets(int role) {
    try {
        const auto folder=presetFolder(role);if(!std::filesystem::exists(folder))return;
        std::vector<std::filesystem::path> paths;
        for(const auto& entry:std::filesystem::directory_iterator(folder))if(entry.is_regular_file() && _wcsicmp(entry.path().extension().c_str(),L".lutpreset")==0)paths.push_back(entry.path());
        std::sort(paths.begin(),paths.end());for(const auto& path:paths)indexPreset(role,path);
    }catch(const std::exception& error){presetError(error);}
}
void loadPresetControls() {
    const auto& look = currentPreset(0).current; const auto& correction = currentPreset(1).current;
    contrastPosition = look.contrast; pivotPosition = look.pivot; contrastEnabled = look.contrastEnabled;
    hdrFixEnabled = correction.hdrFix; gammaPosition = correction.gamma == 22 ? 0 : 1;
    if(!editorReady)return;
    SendMessageW(contrastSlider, TBM_SETPOS, TRUE, contrastPosition);
    SendMessageW(pivotSlider, TBM_SETPOS, TRUE, pivotPosition);
    SendMessageW(gammaChoice, BM_SETCHECK, gammaPosition == 0 ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessageW(gammaOther, BM_SETCHECK, gammaPosition == 1 ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessageW(hdrFixControl, BM_SETCHECK, correction.applyInSdr ? BST_CHECKED : BST_UNCHECKED, 0);
}
void initializePresets() {
    if(!renderOnly){const auto path=shortcutPath();
        nightConfig.enabled=GetPrivateProfileIntW(L"NightLight",L"Enabled",0,path.c_str())==1;
        nightConfig.scheduled=GetPrivateProfileIntW(L"NightLight",L"Scheduled",0,path.c_str())==1;
        nightConfig.strength=std::clamp(static_cast<int>(GetPrivateProfileIntW(L"NightLight",L"Strength",35,path.c_str())),0,100);
        nightConfig.start=std::clamp(static_cast<int>(GetPrivateProfileIntW(L"NightLight",L"Start",1260,path.c_str())),0,1439);
        nightConfig.end=std::clamp(static_cast<int>(GetPrivateProfileIntW(L"NightLight",L"End",420,path.c_str())),0,1439);
        if(nightConfig.end==nightConfig.start)nightConfig.end=(nightConfig.start+600)%1440;
        nightConfig.fade=std::clamp(static_cast<int>(GetPrivateProfileIntW(L"NightLight",L"Fade",10,path.c_str())),0,60);
        wchar_t excluded[8192]{};GetPrivateProfileStringW(L"NightLight",L"Excluded",L"",excluded,8192,path.c_str());
        std::wistringstream keys(excluded);std::wstring key;while(std::getline(keys,key,L','))if(key.size()==16&&key.find_first_not_of(L"0123456789abcdef")==std::wstring::npos)nightConfig.excluded.insert(key);
    }
    for (int role = 0; role < 2; ++role) {
        const auto data = presets::inactive(presetKind(role)); presetLibrary[role].push_back({{}, data, data});
        if (renderOnly && !presetUiTest) continue;
        try {
            const auto folder = presetFolder(role);
            if ((!deferEditor || editorReady) && std::filesystem::exists(folder)) {
                std::vector<std::filesystem::path> paths;
                for (const auto& entry : std::filesystem::directory_iterator(folder))
                    if (entry.is_regular_file() && _wcsicmp(entry.path().extension().c_str(), L".lutpreset") == 0) paths.push_back(entry.path());
                std::sort(paths.begin(), paths.end());
                for (const auto& path : paths) {
                    indexPreset(role,path);
                }
            }
            wchar_t path[32768]{};
            GetPrivateProfileStringW(L"Presets", role == 0 ? L"Look" : L"Correction", L"", path, static_cast<DWORD>(std::size(path)), shortcutPath().c_str());
            if (*path) {
                std::filesystem::path savedPath(path);
                if (savedPath.is_relative()) savedPath = std::filesystem::path(shortcutPath()).parent_path() / savedPath;
                presetIndex[role] = readPreset(role, savedPath);
            } else {
                const auto initial = folder / L"Off.lutpreset";
                if(std::filesystem::exists(initial)) presetIndex[role] = readPreset(role, initial);
            }
        } catch (const std::exception& error) { presetError(error); }
    }
    if (!renderOnly || presetUiTest) autosave = GetPrivateProfileIntW(L"Presets", L"Autosave", 0, shortcutPath().c_str()) == 1;
    if (!renderOnly || presetUiTest) startMinimized=GetPrivateProfileIntW(L"Startup",L"Minimized",0,shortcutPath().c_str())==1;
    if (!renderOnly || presetUiTest) correctionFirst = GetPrivateProfileIntW(L"Effects", L"CorrectionFirst", 0, shortcutPath().c_str()) == 1;
    if(!renderOnly || presetUiTest) {
        wchar_t order[128]{};GetPrivateProfileStringW(L"Effects",L"Order",L"",order,128,shortcutPath().c_str());
        if(*order) {
            std::wistringstream in(order);std::wstring part;std::vector<int> candidate;std::array<bool,8> seen{};bool valid=true;
            while(std::getline(in,part,L',')){try{size_t consumed=0;int v=std::stoi(part,&consumed);if(consumed!=part.size()||v<0||v>7||seen[v]){valid=false;break;}seen[v]=true;candidate.push_back(v);}catch(...){valid=false;break;}}
            if(valid && candidate.size()==8)effectOrder=candidate;
        } else if(correctionFirst)effectOrder={1,0,2,3,4,5,6,7};
        wchar_t family[80]{};GetPrivateProfileStringW(L"Appearance",L"Font",L"Bahnschrift",family,80,shortcutPath().c_str());
        for(const wchar_t* candidate:{L"Bahnschrift",L"Segoe UI",L"Segoe UI Semilight"})if(wcscmp(family,candidate)==0)ui::setFontFamily(candidate);
    }
    correctionFirst=processingRow(1)<processingRow(0);
    regularLookIndex = presetIndex[0];
    SendMessageW(autosaveControl, BM_SETCHECK, autosave ? BST_CHECKED : BST_UNCHECKED, 0);
    loadPresetControls();
}
// Avoid repainting unchanged controls during continuous slider input.
void setText(HWND control, const wchar_t* text) {
    std::wstring previous(GetWindowTextLengthW(control)+1, L'\0');
    GetWindowTextW(control, previous.data(), static_cast<int>(previous.size()));
    previous.resize(wcslen(previous.c_str()));
    if (previous != text) SetWindowTextW(control, text);
}
void setEnabled(HWND control, bool enabled) {
    if ((IsWindowEnabled(control) != FALSE) != enabled) EnableWindow(control, enabled);
}
void setVisible(HWND control, bool visible) {
    if (((GetWindowLongPtrW(control, GWL_STYLE) & WS_VISIBLE) != 0) != visible)
        ShowWindow(control, visible ? SW_SHOW : SW_HIDE);
}
void refreshPresetChoice() {
    if(!editorReady)return;
    if (sliderDragging) return;
    if (presetLibrary[presetRole()].empty()) return;
    setText(presetLabel, selected != 1 ? L"Look preset" : L"Correction preset");
    std::vector<std::wstring> names;
    for (const auto& entry : presetLibrary[presetRole()])
        names.push_back((entry.path.empty() ? std::wstring(L"Unsaved") : entry.path.stem().wstring()) + (entry.dirty() ? L" *" : L""));
    bool changed = SendMessageW(presetChoice, CB_GETCOUNT, 0, 0) != static_cast<LRESULT>(names.size());
    for (size_t i = 0; !changed && i < names.size(); ++i) {
        const auto length = SendMessageW(presetChoice, CB_GETLBTEXTLEN, i, 0);
        std::wstring previous(static_cast<size_t>(length)+1, L'\0');
        SendMessageW(presetChoice, CB_GETLBTEXT, i, reinterpret_cast<LPARAM>(previous.data()));
        previous.resize(static_cast<size_t>(length));
        changed = previous != names[i];
    }
    if (changed) {
        SendMessageW(presetChoice, CB_RESETCONTENT, 0, 0);
        for (const auto& name : names) SendMessageW(presetChoice, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(name.c_str()));
    }
    if (SendMessageW(presetChoice, CB_GETCURSEL, 0, 0) != static_cast<LRESULT>(presetIndex[presetRole()]))
        SendMessageW(presetChoice, CB_SETCURSEL, presetIndex[presetRole()], 0);
    const auto& entry = currentPreset(presetRole());
    setEnabled(revertPresetButton, entry.dirty());
}
bool saveExistingPreset(int role) {
    auto& entry = currentPreset(role);
    if (entry.path.empty()) return false;
    try {
        if (presets::read(entry.path) != entry.saved) throw std::runtime_error("This preset was changed outside MonitorMage. Use Save as to keep both versions.");
        presets::write(entry.path, entry.current, true); entry.markSaved(); presetMessage.clear(); return true;
    } catch (const std::exception& error) { presetError(error); return false; }
}
void savePresetAsPath(int role, const std::filesystem::path& path, bool replace) {
    const size_t sourceIndex = presetIndex[role];
    const auto data = currentPreset(role).current;
    const size_t index = findPreset(role, path);
    if (index != presetLibrary[role].size() && index != sourceIndex && presetLibrary[role][index].dirty())
        throw std::runtime_error("You have unsaved changes to that preset. Choose another filename.");
    presets::write(path, data, replace);
    if (index == presetLibrary[role].size()) presetLibrary[role].push_back({std::filesystem::absolute(path), data, data});
    else presetLibrary[role][index] = {std::filesystem::absolute(path), data, data};
    if (index != sourceIndex) presetLibrary[role][sourceIndex].current = presetLibrary[role][sourceIndex].saved;
    presetIndex[role] = index;
    if (role == 0 && !applicationContextActive) regularLookIndex = index;
}
void flushPresetAutosave() {
    KillTimer(window, PresetTimer);
    if (sliderDragging || (renderOnly && !presetUiTest)) return;
    for (int role = 0; role < 2; ++role)
        if (currentPreset(role).canAutosave(autosave)) saveExistingPreset(role);
}
void presetEdited(int role) {
    if (renderOnly && !presetUiTest) return;
    auto& data = currentPreset(role).current;
    if (role == 0) { data.contrast = contrastPosition; data.pivot = pivotPosition; data.contrastEnabled = contrastEnabled; }
    else { data.gamma = gammaPosition == 0 ? 22 : 24; data.hdrFix = hdrFixEnabled; }
    presetMessage.clear(); refreshPresetChoice();
    if (!sliderDragging && currentPreset(role).canAutosave(autosave)) SetTimer(window, PresetTimer, 350, nullptr);
}
void presetFileDialog(bool saveAs) {
    if (renderOnly) return;
    flushPresetAutosave();
    try {
        const auto folder = presetFolder(presetRole()); std::filesystem::create_directories(folder);
        wchar_t file[32768]{};
        if (saveAs) wcscpy_s(file, selected != 1 ? L"My look.lutpreset" : L"My correction.lutpreset");
        OPENFILENAMEW dialog{}; dialog.lStructSize = sizeof(dialog); dialog.hwndOwner = window;
        dialog.lpstrFilter = L"MonitorMage preset (*.lutpreset)\0*.lutpreset\0\0";
        dialog.lpstrFile = file; dialog.nMaxFile = static_cast<DWORD>(std::size(file)); dialog.lpstrDefExt = L"lutpreset";
        dialog.lpstrInitialDir = folder.c_str(); dialog.lpstrTitle = saveAs ? L"Save preset as" : L"Open preset";
        dialog.Flags = OFN_NOCHANGEDIR | OFN_PATHMUSTEXIST | (saveAs ? OFN_OVERWRITEPROMPT : OFN_FILEMUSTEXIST);
        if (!(saveAs ? GetSaveFileNameW(&dialog) : GetOpenFileNameW(&dialog))) return;
        if (saveAs) {
            const bool exists = GetFileAttributesW(file) != INVALID_FILE_ATTRIBUTES;
            savePresetAsPath(presetRole(), file, exists);
        } else presetIndex[presetRole()] = readPreset(presetRole(), file);
        presetMessage.clear(); if (selected != 1) manualLookOverride();
        persistPresetChoices(); loadPresetControls(); refreshPresetChoice(); reconcileLive();
    } catch (const std::exception& error) { presetError(error); }
    labels();
}
bool allowExit() {
    flushPresetAutosave();
    bool dirty = false;
    for (const auto& entries : presetLibrary) for (const auto& entry : entries) dirty = dirty || entry.dirty();
    if (!dirty || renderOnly) return true;
    return unsaved::confirm(window);
}
UINT shortcutModifiers(WORD shortcut) {
    const BYTE flags = HIBYTE(shortcut);
    return MOD_NOREPEAT | ((flags & HOTKEYF_CONTROL) ? MOD_CONTROL : 0) |
        ((flags & HOTKEYF_ALT) ? MOD_ALT : 0) | ((flags & HOTKEYF_SHIFT) ? MOD_SHIFT : 0);
}
bool validShortcut(WORD shortcut) {
    const BYTE key = LOBYTE(shortcut), flags = HIBYTE(shortcut);
    return key && key != VK_F12 && key != VK_CONTROL && key != VK_MENU && key != VK_SHIFT &&
        (flags & (HOTKEYF_CONTROL | HOTKEYF_ALT | HOTKEYF_SHIFT)) &&
        !(flags & ~(HOTKEYF_CONTROL | HOTKEYF_ALT | HOTKEYF_SHIFT | HOTKEYF_EXT));
}
std::wstring shortcutName(WORD shortcut) {
    std::wstring name;
    if (HIBYTE(shortcut) & HOTKEYF_CONTROL) name += L"Ctrl+";
    if (HIBYTE(shortcut) & HOTKEYF_ALT) name += L"Alt+";
    if (HIBYTE(shortcut) & HOTKEYF_SHIFT) name += L"Shift+";
    wchar_t keyName[80]{};
    LONG scan = static_cast<LONG>(MapVirtualKeyW(LOBYTE(shortcut), MAPVK_VK_TO_VSC) << 16);
    if (HIBYTE(shortcut) & HOTKEYF_EXT) scan |= 1 << 24;
    if (GetKeyNameTextW(scan, keyName, static_cast<int>(std::size(keyName)))) name += keyName;
    else name += L"key " + std::to_wstring(LOBYTE(shortcut));
    return name;
}
bool bindShortcut(WORD shortcut) {
    if (!validShortcut(shortcut)) { hotkeyError = ERROR_INVALID_PARAMETER; return false; }
    if (hotkeyRegistered && shortcut == activeShortcut) return true;
    const int nextId = hotkeyRegistered ? (activeHotkeyId == 1 ? 2 : 1) : 1;
    if (!RegisterHotKey(window, nextId, shortcutModifiers(shortcut), LOBYTE(shortcut))) {
        hotkeyError = GetLastError(); return false;
    }
    if (hotkeyRegistered) UnregisterHotKey(window, activeHotkeyId);
    activeHotkeyId = nextId; activeShortcut = shortcut; hotkeyRegistered = true; hotkeyError = 0;
    return true;
}
void bindPreferredShortcut(WORD preferred) {
    const WORD candidates[] = {preferred, MAKEWORD(VK_F8, HOTKEYF_CONTROL | HOTKEYF_ALT),
        MAKEWORD(VK_F8, HOTKEYF_CONTROL | HOTKEYF_SHIFT), MAKEWORD(VK_F9, HOTKEYF_CONTROL | HOTKEYF_ALT),
        MAKEWORD(VK_F9, HOTKEYF_CONTROL | HOTKEYF_SHIFT)};
    for (const WORD candidate : candidates) {
        if (bindShortcut(candidate)) {
            if (candidate != preferred) hotkeyMessage = L"Your shortcut is unavailable. Using " + shortcutName(candidate) + L".";
            return;
        }
    }
    hotkeyMessage = L"The shortcut could not be set. Choose another key combination in Settings.";
}
void startShortcut() {
    const auto path = shortcutPath();
    preferredShortcut = static_cast<WORD>(GetPrivateProfileIntW(L"Hotkey", L"Bypass", activeShortcut, path.c_str()));
    bindPreferredShortcut(preferredShortcut);
}
int shortcutSelfTest() {
    // Real Windows registration collisions, with no key injection or display changes.
    window = CreateWindowExW(0, L"STATIC", L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, instance, nullptr);
    HWND other = CreateWindowExW(0, L"STATIC", L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, instance, nullptr);
    if (!window || !other) return 1;
    WORD blocked = 0;
    for (int key = VK_F8; key <= VK_F11; ++key) {
        const WORD candidate = MAKEWORD(key, HOTKEYF_CONTROL | HOTKEYF_ALT | HOTKEYF_SHIFT);
        if (RegisterHotKey(other, 99, shortcutModifiers(candidate), LOBYTE(candidate))) { blocked = candidate; break; }
    }
    int result = 2;
    if (blocked) {
        bindPreferredShortcut(blocked);
        if (hotkeyRegistered && activeShortcut != blocked && !hotkeyMessage.empty()) {
            const WORD previous = activeShortcut;
            const bool rejected = !bindShortcut(blocked) && hotkeyError == ERROR_HOTKEY_ALREADY_REGISTERED;
            const bool retained = hotkeyRegistered && activeShortcut == previous;
            const bool invalidRejected = !bindShortcut(MAKEWORD(VK_F12, HOTKEYF_CONTROL)) && activeShortcut == previous;
            UnregisterHotKey(other, 99);
            const bool replaced = bindShortcut(blocked);
            const bool oldReleased = RegisterHotKey(other, 99, shortcutModifiers(previous), LOBYTE(previous)) != FALSE;
            result = rejected && retained && invalidRejected && replaced && oldReleased ? 0 : 3;
        }
    }
    UnregisterHotKey(other, 99);
    if(result==0){bool shiftBound=false;for(int key=VK_F20;key<=VK_F24;++key)if(bindShortcut(MAKEWORD(key,HOTKEYF_SHIFT))){shiftBound=HIBYTE(activeShortcut)==HOTKEYF_SHIFT && (shortcutModifiers(activeShortcut)&MOD_SHIFT);break;}if(!shiftBound)result=4;}
    if (hotkeyRegistered) UnregisterHotKey(window, activeHotkeyId);
    hotkeyRegistered = false; DestroyWindow(other); DestroyWindow(window); window = nullptr;
    return result;
}
DWORD processFaults(){PROCESS_MEMORY_COUNTERS counters{};counters.cb=sizeof(counters);GetProcessMemoryInfo(GetCurrentProcess(),&counters,sizeof(counters));return counters.PageFaultCount;}
ULONGLONG processCpu() {
    FILETIME created{}, ended{}, kernel{}, user{};
    if (!GetProcessTimes(GetCurrentProcess(), &created, &ended, &kernel, &user)) return 0;
    ULARGE_INTEGER k{}, u{}; k.LowPart = kernel.dwLowDateTime; k.HighPart = kernel.dwHighDateTime;
    u.LowPart = user.dwLowDateTime; u.HighPart = user.dwHighDateTime; return k.QuadPart + u.QuadPart;
}
double amount() { return contrastPosition / 100.0; }
double pivot() { return pivotPosition / 100.0; }
double gamma() { return gammaPosition == 0 ? 2.2 : 2.4; }
live::Settings currentSettings();
Source previewOutput() {
    if(liveSession.active())return liveOutput;
    Source output;const auto index=selectedSourceIndex();
    if(index>=0&&static_cast<size_t>(index)<sources.size())output=sources[index];
    if(selected==1 && !currentPreset(1).current.applyInSdr)output.hdr=true;output.white=whitePosition;return output;
}

void place(HWND control, int x, int y, int width, int height, bool body = false) {
    if(x>=198)x+=PaneOffset;
    const auto offset=ui::viewportOffset(window);
    if(control==canvas)SetWindowPos(control,nullptr,px(x)-offset.x,px(y+(body?56:0))-offset.y,px(width),px(height),SWP_NOZORDER|SWP_NOACTIVATE|SWP_NOCOPYBITS);
    else MoveWindow(control, px(x)-offset.x, px(y + (body ? 56 : 0))-offset.y, px(width), px(height), TRUE);
}
std::wstring decimal(double value, int precision = 2) {
    std::wostringstream text; text.imbue(std::locale::classic());
    text << std::fixed << std::setprecision(precision) << value; return text.str();
}
// One discovery snapshot per synchronous operation. Hardware reads/writes in
// live::Session still verify the current mode and ramp independently.
struct SourceQueryBatch {
    static inline unsigned depth=0;
    static inline bool valid=false;
    static inline std::vector<Source> snapshot;
    SourceQueryBatch(){if(depth++==0)valid=false;}
    ~SourceQueryBatch(){if(--depth==0){valid=false;snapshot.clear();}}
};
std::vector<Source> activeSources(bool fresh=false) {
    try {
        if(SourceQueryBatch::depth){
            if(fresh || !SourceQueryBatch::valid){SourceQueryBatch::snapshot=live::outputs();SourceQueryBatch::valid=true;}
            return SourceQueryBatch::snapshot;
        }
        return live::outputs();
    }
    catch (const std::exception& error) {
        liveMessage=ui::messages::error(error.what());liveError=true;return {};
    }
}
void refreshSources() {
    SourceQueryBatch queryBatch;
    if(nightDisplay)SendMessageW(nightDisplay,CB_RESETCONTENT,0,0);
    const auto oldIndex = selectedSourceIndex();
    const std::wstring previous = oldIndex >= 0 && static_cast<size_t>(oldIndex) < sources.size() ? sources[oldIndex].name : L"";
    sources = activeSources(true); SendMessageW(sourceChoice, CB_RESETCONTENT, 0, 0);
    int select = 0;
    for (size_t i = 0; i < sources.size(); ++i) {
        SendMessageW(sourceChoice, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(sources[i].label.c_str()));
        if ((liveSession.active() && sources[i].identity == liveOutput.identity) || sources[i].name == previous) select = static_cast<int>(i);
    }
    selectSource(static_cast<int>(select));
    hdrState=sources.empty()?hdr::State{}:hdr::inspect(sources[select].name);
}
bool lookBypassed() { return bypassed || profileAction == applications::Action::Bypass; }
bool effectiveHdrFix() { return hdrFixEnabled && profileAction != applications::Action::PauseHdr; }
bool lutProcessingActive() {
    static live::ProcessingState cache;
    if(!liveSession.active())return cache.active(false,liveOutput,{});
    return cache.active(true,liveOutput,currentSettings());
}
bool matrixWanted(){const auto& look=currentPreset(0).current;return correctionsRequested&&!powerSuspended&&!hdrPending&&profileAction!=applications::Action::Disable&&!lookBypassed()&&((look.saturationEnabled&&look.saturation!=100)||look.monochromeEnabled);}
void syncMatrix(bool refresh=false){
    if(renderOnly)return;
    if(!matrixWanted())KillTimer(window,MatrixRecoveryTimer);
    try{const auto& look=currentPreset(0).current;const bool wanted=matrixWanted();matrixSession.update(window,wanted?(look.saturationEnabled?look.saturation:100):-1,refresh,false,wanted&&look.monochromeEnabled?&look.monochrome:nullptr);matrixMessage.clear();}
    catch(const std::exception&){matrixMessage=L"The color matrix could not start. Your color curves can still work.";}
}
void settleMatrix(){
    KillTimer(window,MatrixRecoveryTimer);
    if(renderOnly||!matrixWanted())return;
    matrixRecovery.restart();
    SetTimer(window,MatrixRecoveryTimer,matrixRecovery.nextDelay(),nullptr);
}
bool processingActive(){return lutProcessingActive()||nightOutputs.active()||(!renderOnly&&matrixWanted()&&matrixSession.state()==matrix_effect::State::Active);}
app_icons::State iconState(bool active) {
    const auto index = selectedSourceIndex();
    const bool hdr = index >= 0 && static_cast<size_t>(index) < sources.size() && sources[index].hdr;
    return app_icons::choose(active, hdr,
        applicationContextActive && !appFocus.manual());
}
app_icons::State iconState(){return iconState(processingActive());}
HICON stateIcon(int metric,app_icons::State state) {
    const UINT windowDpi = GetDpiForWindow(window);
    return iconCache.get(instance, state, GetSystemMetricsForDpi(metric, windowDpi ? windowDpi : 96));
}
HICON stateIcon(int metric){return stateIcon(metric,iconState());}
live::Settings currentSettings() {
    live::Settings s{contrastEnabled?amount():0,pivot(),gamma(),effectiveHdrFix(),lookBypassed(),correctionFirst};
    s.adjustments=currentPreset(0).current.adjustments;s.order=effectOrder;s.applyInSdr=currentPreset(1).current.applyInSdr;
    const auto index=selectedSourceIndex();const auto output=liveSession.active()?liveOutput:index>=0&&static_cast<size_t>(index)<sources.size()?sources[index]:Source{};
    if(night::includes(nightConfig,output.identity))s.nightWarmth=nightLevel;
    return s;
}
void labels();
void nightClock(){
    KillTimer(window,NightTimer);SYSTEMTIME now{};GetLocalTime(&now);
    const auto phase=night::phase(nightConfig,now.wHour*3600.+now.wMinute*60.+now.wSecond);nightLevel=phase.level;
    if(!renderOnly&&!closing&&correctionsRequested&&!powerSuspended&&!hdrPending&&profileAction!=applications::Action::Disable&&phase.nextMs)SetTimer(window,NightTimer,phase.nextMs,nullptr);
}
void syncNightOutputs(){
    if(renderOnly)return;
    try{
        if(closing||!correctionsRequested||powerSuspended||hdrPending||profileAction==applications::Action::Disable||nightLevel<=0){
            if(nightOutputs.stop()!=0)throw std::runtime_error("Night light could not restore all displays");
        }else{
            const auto index=selectedSourceIndex();const auto primary=liveSession.active()?liveOutput:index>=0&&static_cast<size_t>(index)<sources.size()?sources[index]:Source{};
            nightOutputs.update(activeSources(),primary,nightConfig,nightLevel);
        }
        nightMessage.clear();
    }catch(const std::exception& error){nightMessage=ui::messages::error(error.what());}
}
struct NightSync {
    static int& depth(){static int count=0;return count;}
    NightSync(){++depth();}
    ~NightSync(){if(--depth()==0&&!closing){syncNightOutputs();labels();}}
};
int disableLive(bool retainRequest = false) {
    KillTimer(window, ApplyTimer);
    if(!retainRequest)KillTimer(window,NightTimer);
    const int nightStopped=nightOutputs.stop();
    if(!renderOnly&&(!retainRequest||!matrixWanted()))matrixSession.stop();
    const int primaryStopped=liveSession.stop();const int result=nightStopped?nightStopped:primaryStopped;
    liveError=result!=0&&result!=10&&result!=11;
    if (!retainRequest || (result != 0 && result != 10 && result != 11)) {
        correctionsRequested = false; profilePausedIdentity.clear();
        displayRetryPending=false;modeResetPending=false;displayAttempts=0;
    }
    liveMessage = result == 0 ? L"Off. Your previous display colors have been restored." : result == 10 ? L"Off. Color changes made by another app have been kept."
        : result == 11 ? L"Paused after a display change. Check the picture, then turn MonitorMage on again." : L"MonitorMage could not confirm that your previous display colors were restored. Recovery details are in the temporary folder.";
    return result;
}
void enableLive();
bool waitForDisplay() {
    if(!correctionsRequested || ++displayAttempts>10)return false;
    displayRetryPending=true;liveError=false;liveMessage=L"Updating your adjustments after a display change...";
    if(!renderOnly)SetTimer(window,DisplayTimer,500,nullptr);
    labels();return true;
}
void applyLive() {
    SourceQueryBatch queryBatch;
    nightClock();NightSync nightSync;
    syncMatrix();
    if (!liveSession.active()) {
        if(correctionsRequested&&!renderOnly&&!powerSuspended&&!hdrPending&&profileAction!=applications::Action::Disable)enableLive();
        return;
    }
    try {
        const auto current = activeSources();
        const auto at = std::find_if(current.begin(), current.end(), [&](const Source& output) { return output.identity == liveOutput.identity; });
        if (at == current.end()) {
            const auto replacement=std::find_if(current.begin(),current.end(),[&](const Source& output){return live::sameDisplays(output,liveOutput);});
            if(replacement==current.end() || !replacement->supported) {
                if(waitForDisplay())return;
                throw std::runtime_error("The selected display did not become available after the mode change");
            }
            const auto next=*replacement;
            if(++displayAttempts>10)throw std::runtime_error("The display mode did not settle after repeated changes");
            modeResetPending=next.hdr!=liveOutput.hdr;displayRetryPending=true;
            const int stopped=disableLive(true);
            if(stopped!=0 && stopped!=10 && stopped!=11)throw std::runtime_error("Display restoration could not be verified during the mode change");
            refreshSources();
            for(size_t i=0;i<sources.size();++i)if(sources[i].identity==next.identity)selectSource(static_cast<int>(i));
            enableLive();return;
        }
        liveOutput = *at; liveSession.update(liveOutput, currentSettings());liveError=false;
        displayRetryPending=false;modeResetPending=false;displayAttempts=0;
        if(!lutProcessingActive()){
            const int stopped=liveSession.stop();if(stopped!=0&&stopped!=10&&stopped!=11)throw std::runtime_error("Your previous display colors could not be restored");
            liveMessage=L"Ready. Turn on an effect or correction to change the picture.";
            labels();return;
        }
        liveMessage = liveOutput.hdr && profileAction == applications::Action::PauseHdr
            ? L"This application profile pauses HDR gamma. Your other adjustments stay on."
            : lookBypassed() ? L"Effects paused. Monitor corrections can still be active."
                              : liveOutput.hdr ? L"Your adjustments are on. In HDR, they affect ordinary content while preserving bright HDR highlights."
                                               : currentSettings().hdrFix && currentSettings().applyInSdr?L"Your adjustments are on, including HDR gamma because Also apply in SDR mode is selected.":L"Your adjustments are on. HDR gamma is skipped while Windows HDR is off.";
    } catch (const std::exception& error) {
        // A second mode change can race the query/read/write sequence. Retry
        // only transition failures, with a bounded, event-driven timer.
        const auto latest=activeSources(true);
        const bool modeGone=std::none_of(latest.begin(),latest.end(),[&](const Source& output){return output.identity==liveOutput.identity;});
        if((displayRetryPending || modeGone) && waitForDisplay())return;
        disableLive(); liveMessage=ui::messages::error(error.what());liveError=true;
    }
    labels();
}
void enableLive() {
    SourceQueryBatch queryBatch;
    nightClock();NightSync nightSync;
    syncMatrix();
    if (renderOnly || liveSession.active()) return;
    try {
        const auto index = selectedSourceIndex();
        if (index < 0 || static_cast<size_t>(index) >= sources.size()) throw std::runtime_error("Select an active display group");
        if (activeTest && WaitForSingleObject(activeTest, 0) == WAIT_TIMEOUT) throw std::runtime_error("Close the timed diagnostic before enabling live correction");
        liveOutput = sources[index];
        static live::ProcessingState pending;
        const auto settings=currentSettings();
        if(!modeResetPending && !pending.active(true,liveOutput,settings)){
            displayRetryPending=false;displayAttempts=0;liveError=false;
            liveMessage=L"Ready. Turn on an effect or correction to change the picture.";labels();return;
        }
        nightOutputs.release(liveOutput,&liveSession);
        liveSession.begin(liveOutput, settings,modeResetPending);
        modeResetPending=false;
        applyLive();
    } catch (const std::exception& error) {
        if(displayRetryPending){disableLive(true);if(waitForDisplay())return;}
        disableLive(); liveMessage=ui::messages::error(error.what());liveError=true;
    }
    labels();
}
void toggleBypass() {
    const bool previous = lookBypassed();
    if (profileAction == applications::Action::Bypass) { appFocus.overrideLook(); profileAction = applications::Action::Look; }
    bypassed = !previous; applyLive(); labels();
}
void reconcileLive() {
    SourceQueryBatch queryBatch;
    syncMatrix();
    if (powerSuspended) { labels(); return; }
    if (profileAction == applications::Action::Disable) {
        if (liveSession.active()||nightOutputs.active()) { profilePausedIdentity = liveOutput.identity; disableLive(true); }
        if (correctionsRequested) {
            const auto index = selectedSourceIndex();
            if (profilePausedIdentity.empty() && index >= 0 && static_cast<size_t>(index) < sources.size()) profilePausedIdentity = sources[index].identity;
            liveMessage = L"This application profile pauses MonitorMage. Your adjustments resume when you switch to another program.";
        }
    } else if (correctionsRequested) {
        if (liveSession.active()) applyLive();
        else { profilePausedIdentity.clear(); enableLive(); }
    }
    labels();
}
void toggleCorrections() {
    if(hdrPending || (renderOnly && !presetUiTest))return;
    correctionsRequested = !correctionsRequested;
    if (!correctionsRequested) disableLive(); else reconcileLive();
    labels();
}
void startMaster(){
    correctionsRequested=true;liveError=false;
    liveMessage=L"Ready. Turn on an effect or correction to change the picture.";
    reconcileLive();
}
void toggleWindowsHdr() {
    if(renderOnly || hdrPending)return;
    try {
        const auto index=selectedSourceIndex();
        if(index<0 || static_cast<size_t>(index)>=sources.size())throw std::runtime_error("Select a display before switching HDR");
        const auto state=hdr::inspect(sources[index].name);
        if(!state.available){liveMessage=state.reason;labels();return;}
        hdrResume=correctionsRequested;hdrSource=state.source;hdrDesired=!state.on;hdrTransition=state;
        if(disableLive()!=0)throw std::runtime_error("Restore the display baseline before changing HDR mode");
        modeResetPending=hdrResume;
        hdrPending=true;hdrAttempts=0;hdr::set(state,hdrDesired);
        liveMessage=L"Switching Windows HDR...";if(!SetTimer(window,HdrTimer,600,nullptr))throw std::runtime_error("HDR was requested but its completion timer could not start; corrections remain off");
    }catch(const std::exception& error){hdrPending=false;hdrResume=false;modeResetPending=false;liveMessage=ui::messages::error(error.what());liveError=true;}
    labels();
}
void manualLookOverride() {
    if (appConfig.enabled && applicationContextActive) appFocus.overrideLook();
    else { regularLookIndex = presetIndex[0]; appFocus.resetOverride(); }
    profileAction = applications::Action::Look; profileMessage.clear();
}
void applyApplicationProfile(bool force) {
    if (appConfig.enabled && appFocus.manual() && !force) return;
    if (force) appFocus.resetOverride();
    auto decision = applications::resolve(appConfig, appFocus.executable());
    // Switching between programs without profiles does not change the look.
    // Avoid re-querying displays and reapplying curves on every focus change.
    if(!force && !decision.matched && !applicationContextActive)return;
    flushPresetAutosave(); profileMessage.clear();
    if (!decision.matched) {
        if (applicationContextActive) presetIndex[0] = regularLookIndex;
        applicationContextActive = false;
    } else {
        // Snapshot once across an entire chain of profiled applications, including manual overrides.
        if (!applicationContextActive) regularLookIndex = presetIndex[0];
        applicationContextActive = true;
    }
    if (decision.matched && decision.action == applications::Action::Look) {
        try { presetIndex[0] = readPreset(0, decision.look); }
        catch (const std::exception&) {
            presetIndex[0] = regularLookIndex;
            profileMessage = L"The preset for this program could not be loaded. Your previous look has been restored.";
        }
    }
    profileAction = decision.action;
    loadPresetControls(); refreshPresetChoice(); reconcileLive(); labels();
}
void acceptForeground(const std::filesystem::path& path) {
    if (appFocus.changed(path)) { applyApplicationProfile(); labels(); }
}
void inspectForeground(HWND recent = nullptr) {
    HWND foreground = GetForegroundWindow(); if (!foreground) return;
    DWORD process = 0; GetWindowThreadProcessId(foreground, &process);
    if (process == GetCurrentProcessId() && recent && IsWindow(recent)) {
        foreground = recent; GetWindowThreadProcessId(foreground, &process);
    }
    if (!process || process == GetCurrentProcessId()) return;
    wchar_t cls[128]{}; GetClassNameW(foreground, cls, static_cast<int>(std::size(cls)));
    if (wcscmp(cls, L"Shell_TrayWnd") == 0 || wcscmp(cls, L"Shell_SecondaryTrayWnd") == 0 || wcscmp(cls, L"MultitaskingViewFrame") == 0) return;
    HANDLE handle = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, process);
    wchar_t path[32768]{}; DWORD length = static_cast<DWORD>(std::size(path));
    const bool known = handle && QueryFullProcessImageNameW(handle, 0, path, &length);
    if (handle) CloseHandle(handle);
    // An inaccessible process ends the temporary application override and restores the regular look.
    acceptForeground(known ? std::filesystem::path(path) : std::filesystem::path(L"Unknown process " + std::to_wstring(process)));
}
void CALLBACK foregroundEvent(HWINEVENTHOOK, DWORD, HWND foreground, LONG, LONG, DWORD, DWORD) {
    if (window) PostMessageW(window, ForegroundMessage, 0, reinterpret_cast<LPARAM>(foreground));
}
bool applyProfileConfiguration(const applications::Configuration& candidate) {
    try {
        applications::validate(candidate);
        if (candidate.enabled && !foregroundHook && !renderOnly) throw std::runtime_error("Windows foreground notifications are unavailable; automatic profiles cannot be enabled");
        for (const auto& rule : candidate.rules) if (rule.action == applications::Action::Look) readPreset(0, rule.look);
        const auto previous = appConfig; appConfig = candidate;
        if (!persistPresetChoices()) { appConfig = previous; labels(); return false; }
        appFocus.resetOverride(); profileMessage.clear();
        applyApplicationProfile(true);
        labels(); return true;
    } catch (const std::exception& error) { presetError(error); labels(); return false; }
}
void showProfiles(bool hidden = false) {
    ensureCommonControls();
    discoverPresets(0);
    std::vector<profile_editor::Look> looks;
    for (const auto& entry : presetLibrary[0]) if (!entry.path.empty()) looks.push_back({entry.path.stem().wstring(), entry.path});
    const auto last = appFocus.executable().is_absolute() ? appFocus.executable() : std::filesystem::path{};
    profile_editor::show(window, instance, appConfig, looks, last, applyProfileConfiguration, hidden);
}
bool& adjustmentEnabled(int effect) {
    auto& a=currentPreset(0).current.adjustments;
    switch(effect){case 2:return a.balanceEnabled;case 3:return a.gammaEnabled;case 4:return a.levelsEnabled;case 5:return a.toneEnabled;case 6:return a.curvesEnabled;default:return a.importEnabled;}
}
void toggleEffect(int role) {
    if(role==8){changeWindowsFilter(!filterState.on,filterState.filter);return;}
    if(role==9){nightConfig.enabled=!nightConfig.enabled;persistPresetChoices();applyLive();labels();return;}
    if(role>=10){auto& look=currentPreset(0).current;auto& enabled=role==10?look.saturationEnabled:look.monochromeEnabled;enabled=!enabled;presetEdited(0);syncMatrix();labels();return;}
    if(role==0)contrastEnabled=!contrastEnabled;
    else if(role==1)hdrFixEnabled=!hdrFixEnabled;
    else adjustmentEnabled(role)=!adjustmentEnabled(role);
    presetEdited(role==1?1:0);applyLive();labels();
}
void moveEffect(int role,int target) {
    if(target<0 || target>=static_cast<int>(effectOrder.size()))return;
    const auto before=effectOrder;const auto position=std::find(effectOrder.begin(),effectOrder.end(),role);
    effectOrder.erase(position);effectOrder.insert(effectOrder.begin()+target,role);
    correctionFirst=std::find(effectOrder.begin(),effectOrder.end(),1)<std::find(effectOrder.begin(),effectOrder.end(),0);
    if(!renderOnly && !persistPresetChoices()){effectOrder=before;correctionFirst=std::find(before.begin(),before.end(),1)<std::find(before.begin(),before.end(),0);labels();return;}
    applyLive();labels();
}
void moveCreative(int role,int target) {
    auto order=creativeOrder();if(role==1||role>=10||target<0||target>=static_cast<int>(order.size())-2)return;
    const auto before=effectOrder;const auto found=std::find(order.begin(),order.end(),role);if(found==order.end())return;
    order.erase(found);order.insert(order.begin()+target,role);size_t index=0;
    for(auto& effect:effectOrder)if(effect!=1)effect=order[index++];
    correctionFirst=processingRow(1)<processingRow(0);
    if(!renderOnly&&!persistPresetChoices()){effectOrder=before;correctionFirst=processingRow(1)<processingRow(0);labels();return;}
    applyLive();labels();
}
struct Parameter {const wchar_t* label;int minimum,maximum;int* value;int defaultValue=0;};
std::vector<Parameter> parameters() {
    auto& a=currentPreset(0).current.adjustments;
    if(selected==2)return {{L"Red",50,150,&a.balance[0],100},{L"Green",50,150,&a.balance[1],100},{L"Blue",50,150,&a.balance[2],100},{L"Warmth",-50,50,&a.balance[3]},{L"Tint",-50,50,&a.balance[4]}};
    if(selected==3)return {{L"Midtone gamma",50,200,&a.midtone,100}};
    if(selected==4)return {{L"Input black",0,95,&a.levels[0]},{L"Input white",5,100,&a.levels[1],100},{L"Output black",0,95,&a.levels[2]},{L"Output white",5,100,&a.levels[3],100}};
    if(selected==5)return {{L"Shadows",-50,50,&a.shadows},{L"Highlights",-50,50,&a.highlights}};
    if(selected==10)return {{L"Saturation",0,color::SaturationMaximum,&currentPreset(0).current.saturation,100}};
    if(selected==11){auto& mix=currentPreset(0).current.monochrome;return {{L"Red",0,200,&mix[0],21},{L"Green",0,200,&mix[1],72},{L"Blue",0,200,&mix[2],7}};}
    if(selected==9)return {{L"Warmth",0,100,&nightConfig.strength,35},{L"Fade minutes",0,60,&nightConfig.fade,10}};
    return {};
}
std::vector<HWND> tabStops() {
    // Sidebar, then the right pane in reading order. The graph is an input
    // only for curve editing, between the monitor's upper and lower buttons.
    std::vector<HWND> order{correctionLayers,windowsLayers,layers,presetChoice,autosaveControl,savePresetButton,saveAsButton,exportButton,revertPresetButton};
    if(settingsPage)order.insert(order.end(),{fontChoice,shortcutControl,shortcutButton,startupControl,bypassButton,startMinimizedControl,openPresetButton,saturationReportButton});
    else {
        order.insert(order.end(),{selectedEffectToggle,resetButton});
        order.insert(order.end(),{contrastSlider,pivotSlider,whiteSlider,gammaPosition==0?gammaChoice:gammaOther,hdrFixControl});
        order.insert(order.end(),adjustmentSliders.begin(),adjustmentSliders.end());
        order.insert(order.end(),{curveChannel,importButton,export1DButton});
        if(selected==8)order.insert(order.end(),{filterChoice,filterSettingsButton});
        if(selected==9)order.insert(order.end(),{nightSchedule,nightStart,nightEnd,nightDisplay,nightInclude});
    }
    order.insert(order.end(),{hdrButton,infoButton,powerButton});
    if(!settingsPage&&selected==6)order.push_back(canvas);
    order.insert(order.end(),{monitorSettingsButton,profilesButton,sourceChoice,websiteLink,monitorProfile});
    order.erase(std::remove_if(order.begin(),order.end(),[](HWND control){
        if(!control)return true;
        for(auto at=control;at&&at!=window;at=GetParent(at))
            if(!(GetWindowLongPtrW(at,GWL_STYLE)&WS_VISIBLE)||!IsWindowEnabled(at))return true;
        return false;
    }),order.end());
    return order;
}
HWND nextTabStop(HWND from,bool reverse) {
    const auto order=tabStops();if(order.empty())return nullptr;
    const auto found=std::find(order.begin(),order.end(),from);
    if(found==order.end())return reverse?order.back():order.front();
    const auto index=static_cast<size_t>(found-order.begin());
    return order[(index+order.size()+(reverse?-1:1))%order.size()];
}
bool navigateTab(MSG& message) {
    if(message.message!=WM_KEYDOWN||message.wParam!=VK_TAB||GetAncestor(message.hwnd,GA_ROOT)!=window
        ||(GetKeyState(VK_CONTROL)&0x8000)||(GetKeyState(VK_MENU)&0x8000))return false;
    const auto from=GetFocus(),next=nextTabStop(from,(GetKeyState(VK_SHIFT)&0x8000)!=0);
    if(!next)return false;
    wchar_t cls[32]{};if(from)GetClassNameW(from,cls,32);
    if(wcscmp(cls,L"ComboBox")==0)SendMessageW(from,CB_SHOWDROPDOWN,FALSE,0);
    SendMessageW(window,WM_UPDATEUISTATE,MAKEWPARAM(UIS_CLEAR,UISF_HIDEFOCUS),0);
    SetFocus(next);ui::revealControl(window,next);return true;
}

bool effectLayoutValid=false;
void layoutEffectControls(){
    if(!font||!description)return;
    static int previousPage=-1;static bool previousSettings=false;static std::wstring previousText;
    wchar_t text[1024]{};GetWindowTextW(description,text,1024);
    if(effectLayoutValid&&previousPage==selected&&previousSettings==settingsPage&&previousText==text)return;
    effectLayoutValid=true;previousPage=selected;previousSettings=settingsPage;previousText=text;
    if(settingsPage){place(canvas,198,262,558,268);layoutMonitorControls();InvalidateRect(canvas,nullptr,FALSE);return;}
    HDC dc=GetDC(description);const auto old=SelectObject(dc,font);TEXTMETRICW metrics{};GetTextMetricsW(dc,&metrics);
    RECT measure{0,0,px(558),0};DrawTextW(dc,text,-1,&measure,DT_CALCRECT|DT_WORDBREAK|DT_NOPREFIX);
    SelectObject(dc,old);ReleaseDC(description,dc);
    // Reserve at least two font lines, then an explicit eight-DIP gap. The
    // native static's actual line metrics vary with font and display scaling.
    const int textHeight=std::max(measure.bottom,2*metrics.tmHeight)+px(2);
    const int height=(textHeight*96+dpi-1)/dpi,first=108+height+8;
    place(description,198,108,558,height);
    const auto row=[&](HWND label,HWND value,HWND slider,int index){const int top=first+index*24;
        place(label,198,top,106,22);place(value,306,top,56,22);place(slider,368,top,388,22);};
    row(amountLabel,amountValue,contrastSlider,0);row(pivotLabel,pivotValue,pivotSlider,1);row(whiteLabel,whiteValue,whiteSlider,0);
    for(size_t i=0;i<adjustmentSliders.size();++i)row(adjustmentLabels[i],adjustmentValues[i],adjustmentSliders[i],static_cast<int>(i));
    const auto width=[&](HWND control,int minimum=64,int maximum=240){return ui::contentWidth(control,font,minimum,maximum);};
    const int gammaWidth=width(gammaChoice,56,100);
    place(gammaLabel,198,first+24,130,22);place(gammaChoice,331,first+24,gammaWidth,22);place(gammaOther,331+gammaWidth+12,first+24,width(gammaOther,56,100),22);
    place(hdrFixControl,198,first+48,550,22);
    place(curveChannel,198,first,width(curveChannel,84,200),180);
    const int importWidth=width(importButton);place(importButton,198,first,importWidth,28);place(export1DButton,198+importWidth+12,first,width(export1DButton),28);
    if(selected==8){const int choiceWidth=width(filterChoice,160,350);place(filterChoice,198,first,choiceWidth,180);place(filterSettingsButton,198+choiceWidth+12,first,width(filterSettingsButton),28);place(correctionNote,198,first+40,558,42);}
    if(selected==9){
        place(nightSchedule,198,first+51,166,22);
        place(nightStartLabel,376,first+51,40,22);place(nightStart,418,first+50,72,24);
        place(nightEndLabel,514,first+51,40,22);place(nightEnd,556,first+50,72,24);
        place(nightDisplay,198,first+82,256,180);place(nightInclude,466,first+82,290,22);place(correctionNote,198,first+108,558,22);
    }
    const auto alignTrack=[&](HWND slider){RECT channel{},bounds{};SendMessageW(slider,TBM_GETCHANNELRECT,0,reinterpret_cast<LPARAM>(&channel));
        GetWindowRect(slider,&bounds);MapWindowPoints(nullptr,window,reinterpret_cast<POINT*>(&bounds),2);
        const int right=bounds.right-bounds.left-channel.right,h=bounds.bottom-bounds.top;
        MoveWindow(slider,px(376+PaneOffset)-ui::viewportOffset(window).x-channel.left,bounds.top+h/2-(channel.top+channel.bottom)/2,px(380)+channel.left+right,h,TRUE);};
    for(HWND slider:{contrastSlider,pivotSlider,whiteSlider})alignTrack(slider);for(HWND slider:adjustmentSliders)alignTrack(slider);
    const int rows=selected==9?6:selected==2?5:selected==4?4:selected==1?3:2;
    const int bottom=first+(rows-1)*24+22;
    const int graphTop=std::max(262,bottom+8);place(canvas,198,graphTop,558,530-graphTop);layoutMonitorControls();InvalidateRect(canvas,nullptr,FALSE);
}
void updateTray() {
    if(!trayInstalled)return;
    const auto& look=currentPreset(0);
    const auto origin=applicationContextActive?(appFocus.manual()?L"Manual override: ":L"Application profile: ")+programs::name(appFocus.executable()):std::wstring(L"Manual look");
    std::wstring lookState=(look.path.empty()?std::wstring(L"Unsaved look"):look.path.stem().wstring())+(look.dirty()?L" *":L"");
    if(profileAction==applications::Action::Disable)lookState+=L" (all adjustments paused)";
    else {if(profileAction==applications::Action::PauseHdr)lookState+=L" (HDR gamma paused)";if(lookBypassed())lookState+=L" (look bypassed)";}
    const HICON smallIcon=stateIcon(SM_CXSMICON);
    if (trayInstalled) {
        const std::wstring tip = origin + L" | " + lookState + (!correctionsRequested ? L" (off)" : L"");
        wchar_t nextTip[std::size(tray.szTip)]{}; wcsncpy_s(nextTip, tip.c_str(), _TRUNCATE);
        tray.uFlags = wcscmp(tray.szTip, nextTip) ? NIF_TIP : 0;
        wcscpy_s(tray.szTip, nextTip);
        if (smallIcon && tray.hIcon != smallIcon) { tray.hIcon = smallIcon; tray.uFlags |= NIF_ICON; }
        if (tray.uFlags) Shell_NotifyIconW(NIM_MODIFY, &tray);
    }
}
void labels() {
    if(!editorReady || (!renderOnly && !editorSyncDepth && !IsWindowVisible(window))){if(runtimeReady)updateTray();return;}
    const auto sourceIndex = selectedSourceIndex();
    const bool valid = sourceIndex >= 0 && static_cast<size_t>(sourceIndex) < sources.size();
    const bool correctionPowered=correctionsRequested&&!powerSuspended&&!hdrPending
        &&profileAction!=applications::Action::Disable&&profileAction!=applications::Action::PauseHdr
        &&valid&&sources[sourceIndex].supported&&(sources[sourceIndex].hdr||currentPreset(1).current.applyInSdr);
    std::array<bool,12> enabled{contrastEnabled,hdrFixEnabled};for(int i=2;i<8;++i)enabled[i]=adjustmentEnabled(i);enabled[10]=currentPreset(0).current.saturationEnabled;enabled[11]=currentPreset(0).current.monochromeEnabled;
    effects::update(layers,creativeOrder(),enabled,false,selected,correctionsRequested);
    effects::update(correctionLayers,{1},enabled,profileAction==applications::Action::PauseHdr,selected,correctionPowered);
    const bool nightPowered=correctionsRequested&&nightLevel>0&&!powerSuspended&&!hdrPending&&profileAction!=applications::Action::Disable
        &&std::any_of(sources.begin(),sources.end(),[](const Source& output){return output.supported&&night::includes(nightConfig,output.identity);});
    enabled[8]=filterState.on;enabled[9]=nightConfig.enabled;effects::update(windowsLayers,{8,9},enabled,false,selected,nightPowered);
    setText(title,effects::Names[selected]);
    setText(selectedEffectToggle,(std::wstring(effects::Names[selected])+L" on/off").c_str());
    ui::toggleButton(selectedEffectToggle,selected==8?filterState.on:enabled[selected],selected==8?true:selected==9?nightPowered:selected==1?correctionPowered:correctionsRequested);
    setVisible(selectedEffectToggle,!settingsPage);setVisible(resetButton,!settingsPage&&(selected<8||selected==9||selected>=10));
    setEnabled(selectedEffectToggle,selected!=8||(filterState.available&&!filterController.busy()&&!renderOnly));
    if(selected<2)setText(description, selected == 0 ? L"Make dark and bright tones stand apart. Pivot sets the tone that stays unchanged. Zero has no effect."
                                             : L"Adjust shadows in ordinary content when Windows HDR is on. Gamma changes the shadow brightness.");
    const wchar_t* explanations[]{L"",L"",L"Balance red, green and blue, or make the picture warmer, cooler or tinted. Brightness is kept within range.",L"Brighten or darken the middle tones while keeping black and white unchanged. 1.00 has no effect.",L"Input sets which tones become black or white. Output sets how dark or bright they can be.",L"Adjust dark and bright areas separately while keeping pure black and white unchanged.",L"Drag points to change the curve. Double-click resets a point; right-click resets the whole curve.",L"Load color curves from a file. Supports 1D cube, spi1d, CAL, CSV and PNG/TIFF ramps, but not 3D LUTs."};
    if(selected>=2&&selected<8)setText(description,explanations[selected]);
    if(selected==8)setText(description,L"Windows color filters apply to all displays for your account. They stay on when MonitorMage is off or closed.");
    if(selected==10)setText(description,L"All displays: 0% grayscale, 100% unchanged. Strong boosts may clip.");
    if(selected==11)setText(description,L"Make a black-and-white look by mixing red, green and blue. White stays white. All zeros use the standard mix. Applies to all displays.");
    if(selected==9)setText(description,L"Warmer colors in the evening. Applies to all included displays, independently of your look. Linked displays change together.");
    const auto controls=parameters();
    const bool changingAdjustmentPage=adjustmentPage!=(settingsPage?-1:selected);
    for(size_t i=0;i<adjustmentSliders.size();++i) {
        const bool visible=i<controls.size()&&!settingsPage;setVisible(adjustmentSliders[i],visible&&!changingAdjustmentPage);setVisible(adjustmentLabels[i],visible);setVisible(adjustmentValues[i],visible);
        if(visible){const auto& p=controls[i];COLORREF colour=ui::Accent;
            if(selected==9&&i==0)colour=RGB(255,165,82);
            if(selected==11){constexpr COLORREF rgb[]{RGB(245,95,104),RGB(92,210,126),RGB(90,155,248)};colour=rgb[i];}
            if(selected==2){constexpr COLORREF rgb[]{RGB(245,95,104),RGB(92,210,126),RGB(90,155,248)};
                if(i<3)colour=rgb[i];else if(*p.value){const auto target=i==3?(*p.value>0?RGB(255,165,82):RGB(113,173,255)):(*p.value>0?RGB(228,112,220):RGB(104,215,139));const double mix=.45+.55*std::abs(*p.value)/50.0;const auto blend=[&](int a,int b){return static_cast<BYTE>(std::lround(a+(b-a)*mix));};colour=RGB(blend(GetRValue(ui::Muted),GetRValue(target)),blend(((ui::Muted>>8)&255),GetGValue(target)),blend(GetBValue(ui::Muted),GetBValue(target)));}else colour=ui::Muted;}
            ui::sliderColour(adjustmentSliders[i],colour);
            ui::sliderDetails(adjustmentSliders[i],p.defaultValue,(selected==2||selected==11)&&i<3?L"RGB"[i]:0);
            if(adjustmentPage!=selected)SendMessageW(adjustmentSliders[i],TBM_SETRANGE,TRUE,MAKELPARAM(p.minimum,p.maximum));
            if(SendMessageW(adjustmentSliders[i],TBM_GETPOS,0,0)!=*p.value)SendMessageW(adjustmentSliders[i],TBM_SETPOS,TRUE,*p.value);
            setText(adjustmentLabels[i],p.label);setText(adjustmentValues[i],(selected>=10||(selected==9&&i==0)?std::to_wstring(*p.value)+L"%":selected==9?std::to_wstring(*p.value):decimal(*p.value/100.0)).c_str());}
    }
    static constexpr const wchar_t* parameterHelp[][5]{
        {L"Red balance",L"Green balance",L"Blue balance",L"Warmth",L"Tint"},
        {L"Midtone brightness"},
        {L"Input black level",L"Input white level",L"Output black level",L"Output white level"},
        {L"Shadow brightness",L"Highlight brightness"}};
    if(selected>=2&&selected<=5)for(size_t i=0;i<controls.size();++i)ui::help(adjustmentSliders[i],parameterHelp[selected-2][i]);
    if(selected==10)ui::help(adjustmentSliders[0],L"Color strength");
    if(selected==11)for(int i=0;i<3;++i){const wchar_t* help[]{L"Red contribution",L"Green contribution",L"Blue contribution"};ui::help(adjustmentSliders[i],help[i]);}
    if(selected==9){ui::help(adjustmentSliders[0],L"Night light warmth");ui::help(adjustmentSliders[1],L"Fade duration");}
    adjustmentPage=settingsPage?-1:selected;
    setVisible(saturationReportButton,settingsPage);
    setVisible(curveChannel,selected==6&&!settingsPage);setVisible(importButton,selected==7&&!settingsPage);setVisible(export1DButton,selected==7&&!settingsPage);
    for(HWND control:{layers,correctionLayers,windowsLayers,listLabel,effectsLabel,orderHint})setVisible(control,true);
    setVisible(presetLabel,false);
    setVisible(title,!settingsPage);setVisible(description,!settingsPage);
    setVisible(activeLookLabel,false);setVisible(openPresetButton,settingsPage);setVisible(settingsButton,false);
    for(HWND control:{shortcutLabel,shortcutControl,shortcutButton,fontChoice,fontLabel,bypassButton})setVisible(control,settingsPage);
    // Personal corrections have their own settings, outside LUT presets.
    for(HWND control:{presetChoice,autosaveControl,savePresetButton,saveAsButton,exportButton,revertPresetButton})setVisible(control,settingsPage||selected<8||selected>=10);
    setVisible(versionLabel,settingsPage);setVisible(startupControl,settingsPage);
    setVisible(startMinimizedControl,settingsPage);
    setVisible(openPresetButton,settingsPage);
    for(HWND control:{filterChoice,filterSettingsButton,correctionNote})setVisible(control,false);
    for(HWND control:{nightSchedule,nightStart,nightEnd,nightStartLabel,nightEndLabel,nightInclude,nightDisplay})setVisible(control,!settingsPage&&selected==9);
    if(!settingsPage&&selected==9){
        if(SendMessageW(nightDisplay,CB_GETCOUNT,0,0)!=static_cast<LRESULT>(sources.size())){
            SendMessageW(nightDisplay,CB_RESETCONTENT,0,0);for(const auto& output:sources)SendMessageW(nightDisplay,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(output.label.c_str()));
            SendMessageW(nightDisplay,CB_SETCURSEL,valid?sourceIndex:0,0);
        }
        const auto nightIndex=SendMessageW(nightDisplay,CB_GETCURSEL,0,0);const bool nightValid=nightIndex>=0&&static_cast<size_t>(nightIndex)<sources.size();
        setVisible(correctionNote,true);setText(correctionNote,nightMessage.empty()?(nightConfig.scheduled?(nightLevel>0?L"Schedule active":L"Waiting for the schedule"):L"Manual mode"):nightMessage.c_str());
        for(const auto& entry:{std::pair<HWND,bool>{nightSchedule,nightConfig.scheduled},{nightInclude,nightValid&&night::includes(nightConfig,sources[nightIndex].identity)}})
            if(SendMessageW(entry.first,BM_GETCHECK,0,0)!=(entry.second?BST_CHECKED:BST_UNCHECKED))SendMessageW(entry.first,BM_SETCHECK,entry.second?BST_CHECKED:BST_UNCHECKED,0);
        if(GetFocus()!=nightStart)setText(nightStart,night::timeText(nightConfig.start).c_str());
        if(GetFocus()!=nightEnd)setText(nightEnd,night::timeText(nightConfig.end).c_str());
        setEnabled(nightStart,nightConfig.scheduled);setEnabled(nightEnd,nightConfig.scheduled);setEnabled(nightInclude,nightValid&&sources[nightIndex].supported);
    }
    if(!settingsPage&&selected==8){setVisible(filterChoice,true);setVisible(filterSettingsButton,true);setVisible(correctionNote,!filterMessage.empty()||!filterState.available);}
    setText(versionLabel,L"MonitorMage " MONITORMAGE_VERSION_W);
    setEnabled(filterChoice,filterState.available&&!filterController.busy()&&!renderOnly);
    if(SendMessageW(filterChoice,CB_GETCURSEL,0,0)!=filterState.filter)SendMessageW(filterChoice,CB_SETCURSEL,filterState.filter,0);
    if(selected!=9||settingsPage)setText(correctionNote,(!filterMessage.empty()?filterMessage:!filterState.available?filterState.reason:L"Windows color filters stay on when MonitorMage is off or closed.").c_str());
    const auto startupCheck=startupEnabled?BST_CHECKED:BST_UNCHECKED;
    if(SendMessageW(startupControl,BM_GETCHECK,0,0)!=startupCheck)SendMessageW(startupControl,BM_SETCHECK,startupCheck,0);
    setEnabled(startupControl,!renderOnly);
    const auto minimizedCheck=startMinimized?BST_CHECKED:BST_UNCHECKED;
    if(SendMessageW(startMinimizedControl,BM_GETCHECK,0,0)!=minimizedCheck)SendMessageW(startMinimizedControl,BM_SETCHECK,minimizedCheck,0);
    setText(settingsButton,settingsPage?L"Back to effects":ui::wording::Settings);
    setText(amountLabel,L"Contrast");setText(amountValue,decimal(amount()).c_str());
    setText(pivotLabel,L"Pivot");setText(pivotValue,decimal(pivot()).c_str());
    std::wstring statusText = liveMessage;
    statusError=liveError;
    const bool applying=processingActive(),profileActive=applicationContextActive && !appFocus.manual();
    if(!liveError&&liveSession.active()&&!applying)statusText=L"Ready. No adjustments are changing the picture.";
    if (!renderOnly && !hotkeyMessage.empty()) statusText += L" " + hotkeyMessage;
    if (!liveError&&!presetMessage.empty()){statusText = presetMessage;statusError=presetMessage==lastPresetError;}
    else if (!liveError&&!profileMessage.empty()){statusText = profileMessage;statusError=false;}
    if(matrixWanted()){
        const auto state=renderOnly?matrix_effect::State::Active:matrixSession.state();
        if(!matrixMessage.empty()){statusText=matrixMessage;statusError=true;}
        else if(state==matrix_effect::State::OtherTool||state==matrix_effect::State::ExternalChange){statusText=L"Color effects are paused because another color tool is active. Your color curves can still work.";statusError=true;}
        else if(state==matrix_effect::State::Failed){statusText=L"Color effects could not be applied. Your color curves can still work.";statusError=true;}
        else if(state==matrix_effect::State::Active&&!liveError){statusText=L"Your color effects are on. They apply to all displays; some fullscreen games may skip them. The graph shows color curves only.";statusError=false;}
    }
    if(!renderOnly&&!matrixWanted()&&matrixSession.state()==matrix_effect::State::Failed){statusText=L"Your previous colors could not be confirmed. See the color effect report in Settings.";statusError=true;}
    if(!matrixMessage.empty()){statusText=matrixMessage;statusError=true;}
    if(filterError){statusText=filterMessage;statusError=true;}
    if(!nightMessage.empty()&&!renderOnly){statusText=nightMessage;statusError=true;}
    const auto& look = currentPreset(0);
    std::wstring profileText=L"Desktop profile";
    if(profileActive){
        profileText=L"Profile: "+programs::name(appFocus.executable())+L" | ";
        profileText+=profileAction==applications::Action::PauseHdr?L"HDR gamma paused":
            profileAction==applications::Action::Disable?L"All adjustments paused":
            profileAction==applications::Action::Bypass?L"Look bypassed":
            look.path.empty()?L"Unsaved look":look.path.stem().wstring();
    }
    setText(monitorProfile,profileText.c_str());
    const auto lookName = (look.path.empty() ? std::wstring(L"Unsaved look") : look.path.stem().wstring()) + ((sliderDragging ? lookDirtyBeforeDrag : look.dirty()) ? L" *" : L"");
    std::wstring origin = applicationContextActive ? (appFocus.manual() ? L"Manual override: " : L"Application profile: ") + programs::name(appFocus.executable()) : L"Manual look";
    std::wstring lookState = lookName;
    if (profileAction == applications::Action::Disable) lookState += L" (all adjustments paused)";
    else {
        if (profileAction == applications::Action::PauseHdr) lookState += L" (HDR gamma paused)";
        if (lookBypassed()) lookState += L" (look bypassed)";
    }
    setText(hdrFixControl,L"Also apply when Windows HDR is off (SDR)");
    const auto activeText = origin + L" | " + lookState;
    setText(activeLookLabel, activeText.c_str());
    const auto caption = L"MonitorMage - " + activeText; setText(window, caption.c_str());
    const auto state=iconState(applying);
    const HICON smallIcon = stateIcon(SM_CXSMICON,state), largeIcon = stateIcon(SM_CXICON,state);
    if (smallIcon && reinterpret_cast<HICON>(SendMessageW(window, WM_GETICON, ICON_SMALL, 0)) != smallIcon)
        SendMessageW(window, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(smallIcon));
    if (largeIcon && reinterpret_cast<HICON>(SendMessageW(window, WM_GETICON, ICON_BIG, 0)) != largeIcon)
        SendMessageW(window, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(largeIcon));
    const auto shortcutText = hotkeyRegistered ? L"Bypass: " + shortcutName(activeShortcut) : std::wstring(L"Bypass shortcut");
    setText(shortcutLabel, shortcutText.c_str());
    setText(status, statusText.c_str());
    graphStatusVisible=statusError||hdrPending||displayRetryPending;
    setVisible(infoButton,!graphStatusVisible);
    setText(infoButton,statusText.c_str());
    // Expose the painted status as the keyboard-focusable graph's accessible name.
    setText(canvas, statusText.c_str());
    for (HWND control : {amountLabel, amountValue, contrastSlider, pivotLabel, pivotValue, pivotSlider}) setVisible(control, selected == 0&&!settingsPage);
    for (HWND control : {whiteLabel, whiteValue, whiteSlider, gammaLabel, gammaChoice, gammaOther, hdrFixControl}) setVisible(control, selected == 1&&!settingsPage);
    ui::captionSettings(window,SettingsPage,settingsPage);
    const bool canPower=!hdrPending && !renderOnly && (correctionsRequested || (valid && sources[sourceIndex].supported));
    ui::iconButton(powerButton,ui::Glyph::Power,correctionsRequested,canPower);
    ui::iconButton(hdrButton,ui::Glyph::Sun,valid && sources[sourceIndex].hdr,!renderOnly && !hdrPending && hdrState.available);
    ui::iconButton(profilesButton,ui::Glyph::Diamond,profileActive,true);
    ui::iconButton(monitorSettingsButton,ui::Glyph::Settings,settingsPage,true);
    ui::iconButton(infoButton,ui::Glyph::Info,false,true);
    ui::captionActions(window,{{{ToggleWindowsHdr,valid && sources[sourceIndex].hdr,!renderOnly && !hdrPending && hdrState.available},{ApplicationProfiles,profileActive,true},{TestContrast,correctionsRequested,canPower}}});
    setEnabled(sourceChoice, !liveSession.active() && !hdrPending);
    setEnabled(whiteSlider, !(valid && sources[sourceIndex].hdr));
    setText(bypassButton, lookBypassed() ? L"Resume look" : L"Bypass look");
    std::wstring note = L"No active display group available.";
    if (valid) {
        const auto& source = sources[sourceIndex];
        note = source.label + L"\n";
        if (settingsPage && std::count(source.identity.begin(), source.identity.end(), L'\n') > 1) note += L"Linked displays change together.\n";
        note += source.hdr ? L"HDR: SDR white follows Windows." : L"SDR: hardware colour curves.";
        if (!source.supported) note += L" Mode not supported.";
    }
    setText(testNote, note.c_str());
    setVisible(testNote,false);
    setVisible(sourceChoice,true);
    std::wstring mode=valid?(sources[sourceIndex].hdr?L"HDR":L"SDR"):L"";
    setText(monitorMode,mode.c_str());
    if (valid && sources[sourceIndex].hdr) {
        whitePosition = static_cast<int>(std::lround(sources[sourceIndex].white));
        if (SendMessageW(whiteSlider, TBM_GETPOS, 0, 0) != whitePosition) SendMessageW(whiteSlider, TBM_SETPOS, TRUE, whitePosition);
        const auto detected = decimal(sources[sourceIndex].white, 0) + L" nits";
        setText(whiteValue, detected.c_str());
    } else {
        const auto text = std::to_wstring(whitePosition) + L" nits"; setText(whiteValue, text.c_str());
    }
    updateTray();
    layoutEffectControls();
    if(changingAdjustmentPage)for(size_t i=0;i<adjustmentSliders.size();++i)setVisible(adjustmentSliders[i],i<controls.size()&&!settingsPage);
    invalidateGraph();
}
void layout() {
    if(!editorReady)return;
    ++graphLayoutVersion;effectLayoutValid=false;
    if (font) DeleteObject(font);
    font = ui::makeFont(static_cast<UINT>(dpi));
    if(hintFont)DeleteObject(hintFont);LOGFONTW hintStyle{};GetObjectW(font,sizeof(hintStyle),&hintStyle);hintStyle.lfHeight=-px(ui::style::SecondaryTextSize);hintFont=CreateFontIndirectW(&hintStyle);
    SendMessageW(selectedEffectToggle,WM_SETFONT,reinterpret_cast<WPARAM>(font),TRUE);
    for(HWND slider:adjustmentSliders)SendMessageW(slider,WM_SETFONT,reinterpret_cast<WPARAM>(font),TRUE);
    for(HWND slider:{contrastSlider,pivotSlider,whiteSlider})SendMessageW(slider,WM_SETFONT,reinterpret_cast<WPARAM>(font),TRUE);
    ui::styleTooltip(monitorTips);
    for (HWND control : {layers, title, description, amountLabel, pivotLabel, whiteLabel, gammaLabel, gammaChoice, gammaOther, status, monitorMode, monitorProfile, resetButton, exportButton, listLabel, sourceChoice, testNote, bypassButton, hdrFixControl, shortcutControl, shortcutButton, shortcutLabel, presetLabel, presetChoice, savePresetButton, saveAsButton, openPresetButton, autosaveControl, revertPresetButton, profilesButton, activeLookLabel, orderHint})
        SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
    for(HWND control:{correctionLayers,windowsLayers,effectsLabel})SendMessageW(control,WM_SETFONT,reinterpret_cast<WPARAM>(font),TRUE);
    // Presets and their actions belong to the right pane; the sidebar is the stack.
    place(presetChoice,198,21,244,240);
    place(savePresetButton,558,16,32,42);place(saveAsButton,598,16,32,42);
    place(autosaveControl,454,16,92,28);place(exportButton,644,16,32,42);
    place(activeLookLabel,20, 48, 444, 24);
    place(revertPresetButton,724,16,32,42);
    place(testNote,20,480,180,82);
    place(title,198,64,470,24);place(selectedEffectToggle,676,62,40,28);place(resetButton,724,62,32,42);
    SendMessageW(saturationReportButton,WM_SETFONT,reinterpret_cast<WPARAM>(font),TRUE);
    for(HWND control:{amountValue,pivotValue,whiteValue})SendMessageW(control,WM_SETFONT,reinterpret_cast<WPARAM>(font),TRUE);
    place(listLabel,13,16,160,28);place(correctionLayers,0,48,SidebarWidth,36);place(windowsLayers,0,84,SidebarWidth,72);
    const int effectRows=std::min(static_cast<int>(creativeOrder().size()),10),effectHeight=effectRows*effects::RowHeight;
    place(effectsLabel,13,168,160,24);place(layers,0,196,SidebarWidth,effectHeight);
    place(orderHint,0,196+effectHeight+6,SidebarWidth,18);
    place(testNote,20,settingsPage?480:500,180,settingsPage?82:62);
    const auto width=[&](HWND control,int minimum=64,int maximum=240){return ui::contentWidth(control,font,minimum,maximum);};
    place(fontLabel,198,98,220,20);place(fontChoice,198,125,width(fontChoice,128,220),180);
    place(openPresetButton,198,228,width(openPresetButton),28);place(bypassButton,456,164,width(bypassButton,100),28);
    place(saturationReportButton,456,228,width(saturationReportButton),28);
    place(versionLabel,198,64,220,24);place(startupControl,198,164,220,26);
    place(startMinimizedControl,198,196,220,26);
    place(shortcutLabel,456,98,300,20);place(shortcutControl,456,120,200,28);place(shortcutButton,664,120,92,28);

    for(size_t i=0;i<adjustmentSliders.size();++i) {
        SendMessageW(adjustmentLabels[i],WM_SETFONT,reinterpret_cast<WPARAM>(font),TRUE);
        SendMessageW(adjustmentValues[i],WM_SETFONT,reinterpret_cast<WPARAM>(font),TRUE);
    }
    for(HWND control:{settingsButton,fontChoice,fontLabel,curveChannel,importButton,export1DButton})SendMessageW(control,WM_SETFONT,reinterpret_cast<WPARAM>(font),TRUE);
    for(HWND control:{versionLabel,startupControl,startMinimizedControl})SendMessageW(control,WM_SETFONT,reinterpret_cast<WPARAM>(font),TRUE);
    for(HWND control:{filterChoice,filterSettingsButton,correctionNote})SendMessageW(control,WM_SETFONT,reinterpret_cast<WPARAM>(font),TRUE);
    for(HWND control:{nightSchedule,nightStart,nightEnd,nightStartLabel,nightEndLabel,nightInclude,nightDisplay})SendMessageW(control,WM_SETFONT,reinterpret_cast<WPARAM>(font),TRUE);
    SendMessageW(orderHint,WM_SETFONT,reinterpret_cast<WPARAM>(hintFont?hintFont:font),TRUE);
    ui::refreshCaptionFont(window);
    refreshPresetChoice(); labels();
    for(HWND control:{monitorMode,monitorProfile})InvalidateRect(control,nullptr,FALSE);
    InvalidateRect(window,nullptr,TRUE);
}
void showEditor() {
    SourceQueryBatch queryBatch;
    EditorSync sync;
    ensureEditor();
    refreshColorFilters();
    refreshSources(); applyLive(); labels();
    ShowWindow(window, SW_SHOW); ShowWindow(window, SW_RESTORE); SetForegroundWindow(window);
}
bool installTray() {
    tray.cbSize = sizeof(tray); tray.hWnd = window; tray.uID = 1;
    tray.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP | NIF_SHOWTIP; tray.uCallbackMessage = TrayMessage;
    tray.hIcon = stateIcon(SM_CXSMICON);
    if (!tray.hIcon) tray.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    wcscpy_s(tray.szTip, L"MonitorMage - disabled");
    trayInstalled = Shell_NotifyIconW(NIM_ADD, &tray) != FALSE;
    if (trayInstalled) { tray.uVersion = NOTIFYICON_VERSION_4; Shell_NotifyIconW(NIM_SETVERSION, &tray); }
    return trayInstalled;
}
std::vector<ui::MenuItem> trayItems() {
    return {{Open,L"Open editor",L"",false,false,true,ui::Glyph::Editor},
        {ApplicationProfiles,ui::wording::ApplicationProfiles,L"",applicationContextActive&&!appFocus.manual(),false,true,ui::Glyph::Diamond},
        {ToggleWindowsHdr,L"Windows HDR",hdrPending?L"SWITCHING":L"",hdrState.on,false,hdrState.available&&!hdrPending,ui::Glyph::Sun},
        {Bypass,lookBypassed()?L"Resume look":L"Bypass look",hotkeyRegistered?shortcutName(activeShortcut):L"",lookBypassed(),false,true,ui::Glyph::Bypass},
        {TestContrast,L"Master power",!processingActive()&&correctionsRequested?L"READY":L"",processingActive(),false,true,ui::Glyph::Power},
        {Exit,L"Exit",L"",false,true,true,ui::Glyph::Close}};
}
void trayMenu() {
    if(!renderOnly){refreshSources();refreshColorFilters();labels();}
    POINT point{}; GetCursorPos(&point);
    const UINT command = ui::popup(window,stateIcon(SM_CXSMICON),trayItems(),point);
    if (command == Open) showEditor(); else if (command == Exit && allowExit()) DestroyWindow(window);
    else if (command == ApplicationProfiles) showProfiles();
    else if (command == TestContrast) toggleCorrections();
    else if (command == Bypass) toggleBypass();
    else if (command == ToggleWindowsHdr) toggleWindowsHdr();
}
void exportCurve() {
    wchar_t file[32768]{};
    wcscpy_s(file, selected == 0 ? L"creative-curves.csv" : L"compiled-curves.csv");
    OPENFILENAMEW dialog{}; dialog.lStructSize = sizeof(dialog); dialog.hwndOwner = window;
    dialog.lpstrFilter = L"Curve CSV (*.csv)\0*.csv\0\0"; dialog.lpstrFile = file;
    dialog.nMaxFile = static_cast<DWORD>(std::size(file)); dialog.lpstrDefExt = L"csv";
    dialog.Flags = OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    if (!GetSaveFileNameW(&dialog)) return;
    HANDLE handle = CreateFileW(file, GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (handle == INVALID_HANDLE_VALUE) { MessageBoxW(window, L"Choose a different filename to keep the existing file.", L"Export curve", MB_OK | MB_ICONINFORMATION); return; }
    std::ostringstream csv; csv.imbue(std::locale::classic()); csv << std::setprecision(17);
    csv << "input,output_red,output_green,output_blue,model,contrast,pivot,sdr_white_nits,gamma,hdr_fix,bypass,contrast_enabled,correction_first\n";
    const auto output=previewOutput();const auto settings=currentSettings();const bool hdrModel=output.hdr;
    for (int i = 0; i < 1024; ++i) {
        const double x = i / 1023.0;
        csv << x << ',' << live::evaluate(x,0,output,settings) << ',' << live::evaluate(x,1,output,settings) << ',' << live::evaluate(x,2,output,settings) << ',' << (hdrModel ? (liveSession.active() ? "combined_hdr_pq" : "hdr_pq") : "creative_1d") << ',';
        csv << amount() << ',' << pivot() << ',';
        if (hdrModel) csv << (liveSession.active() ? liveOutput.white : whitePosition) << ',' << gamma();
        else csv << ',';
        csv << ',' << effectiveHdrFix() << ',' << lookBypassed() << ',' << contrastEnabled << ',' << correctionFirst << '\n';
    }
    const auto data = csv.str(); DWORD written = 0;
    const bool ok = WriteFile(handle, data.data(), static_cast<DWORD>(data.size()), &written, nullptr) && written == data.size();
    CloseHandle(handle);
    MessageBoxW(window, ok ? L"Curve exported." : L"The curve could not be saved completely. Check free space and try again.", L"Export curve", MB_OK | (ok ? MB_ICONINFORMATION : MB_ICONERROR));
}
void transfer1D(bool importing) {
    if(renderOnly)return;
    wchar_t file[32768]{};if(!importing)wcscpy_s(file,L"MonitorMage-look");
    OPENFILENAMEW dialog{sizeof(dialog)};dialog.hwndOwner=window;dialog.lpstrFile=file;dialog.nMaxFile=static_cast<DWORD>(std::size(file));
    dialog.lpstrFilter=importing?L"All supported LUTs\0*.cube;*.spi1d;*.cal;*.csv;*.png;*.tif;*.tiff\0Cube 1D (*.cube)\0*.cube\0OpenColorIO (*.spi1d)\0*.spi1d\0Argyll display curves (*.cal)\0*.cal\0RGB CSV (*.csv)\0*.csv\0PNG LUT image (*.png)\0*.png\0TIFF LUT image (*.tif;*.tiff)\0*.tif;*.tiff\0\0"
        :L"Cube 1D (*.cube)\0*.cube\0OpenColorIO (*.spi1d)\0*.spi1d\0Argyll curves (*.cal)\0*.cal\0RGB CSV (*.csv)\0*.csv\0PNG 16-bit ramp (*.png)\0*.png\0TIFF 16-bit ramp (*.tif)\0*.tif\0\0";
    dialog.nFilterIndex=1;dialog.Flags=OFN_EXPLORER|OFN_NOCHANGEDIR|(importing?OFN_FILEMUSTEXIST:OFN_OVERWRITEPROMPT);
    if(!(importing?GetOpenFileNameW(&dialog):GetSaveFileNameW(&dialog)))return;
    try {
        std::filesystem::path path(file);
        if(importing){auto loaded=lut1d::read(path);currentPreset(0).current.adjustments.imported=std::move(loaded.channels);presetEdited(0);presetMessage=L"Color curves imported.";applyLive();}
        else {
            if(!path.has_extension()){const wchar_t* extensions[]{L".cube",L".spi1d",L".cal",L".csv",L".png",L".tif"};path+=extensions[std::clamp(static_cast<int>(dialog.nFilterIndex)-1,0,5)];
                if(std::filesystem::exists(path)&&MessageBoxW(window,L"Replace the existing LUT file?",L"Export look LUT",MB_YESNO|MB_ICONQUESTION)!=IDYES)return;}
            auto settings=currentSettings();settings.hdrFix=false;settings.bypass=false;settings.nightWarmth=0;Source output;lut1d::Channels channels;
            for(int i=0;i<1024;++i)for(int c=0;c<3;++c)channels[c].push_back(live::evaluate(i/1023.0,c,output,settings));
            lut1d::write(path,channels);presetMessage=L"Color curves exported. Saturation, Channel mixer and monitor corrections are not included.";
        }
    }catch(const std::exception& error){presetError(error);}labels();
}

struct MonitorGeometry {
    RECT body,screen,plot,foot;
    int left,right,top,bottom,size;
};
MonitorGeometry monitorGeometry(HWND hwnd) {
    RECT client{};GetClientRect(hwnd,&client);MonitorGeometry g{};
    g.body={0,0,client.right,client.bottom-px(30)};
    g.screen=g.body;InflateRect(&g.screen,-px(6),-px(6));
    // Six DIP of internal padding plus a two-DIP inset gives an eight-DIP
    // visible corner gap (previously sixteen), retaining rounded screen corners.
    g.size=px(24);const int inset=px(2);
    g.left=g.screen.left+inset;g.right=g.screen.right-inset-g.size;
    g.top=g.screen.top+inset;g.bottom=g.screen.bottom-inset-g.size;
    // Reserve a small axis-label strip above the separate profile row.
    g.plot={g.left+g.size/2+px(20),g.top+g.size/2+px(20),g.right+g.size/2-px(20),g.bottom+g.size/2-px(32)};
    const int center=client.right/2;
    g.foot={center-px(57),client.bottom-px(15),center+px(57),client.bottom-px(3)};
    return g;
}
RECT curvePlot(HWND hwnd) { return monitorGeometry(hwnd).plot; }
RECT graphUpdateBounds(HWND hwnd){auto area=curvePlot(hwnd);InflateRect(&area,px(8),px(8));RECT client{},visible{};GetClientRect(hwnd,&client);IntersectRect(&visible,&area,&client);return visible;}
RECT graphWarningBounds(HWND hwnd,const wchar_t* text){const auto g=monitorGeometry(hwnd);RECT area{g.left+g.size+px(12),g.top,g.right-px(12),g.bottom},measured=area;
    HDC dc=GetDC(hwnd);const auto old=SelectObject(dc,font);DrawTextW(dc,text,-1,&measured,DT_CENTER|DT_WORDBREAK|DT_CALCRECT|DT_NOPREFIX);SelectObject(dc,old);ReleaseDC(hwnd,dc);
    area.top+=std::max(0,(g.size-static_cast<int>(measured.bottom-measured.top))/2);area.bottom=std::min<LONG>(g.bottom,area.top+measured.bottom-measured.top);return area;
}
void invalidateGraph(){
    if(!canvas||!font)return;
    if(renderOnly)graphAuditCount=0;
    const auto queue=[&](const RECT& area){if(renderOnly&&graphAuditCount<graphAuditDamage.size())graphAuditDamage[graphAuditCount++]=area;InvalidateRect(canvas,&area,FALSE);};
    const RECT plot=graphUpdateBounds(canvas);queue(plot);
    static std::wstring previous;static RECT oldBounds{};static bool hadWarning=false;static unsigned previousLayout=0;
    wchar_t text[2048]{};if(graphStatusVisible)GetWindowTextW(status,text,2048);
    if(hadWarning!=graphStatusVisible||previous!=text||previousLayout!=graphLayoutVersion){
        if(hadWarning)queue(oldBounds);
        if(graphStatusVisible){oldBounds=graphWarningBounds(canvas,text);queue(oldBounds);}
        previous=text;hadWarning=graphStatusVisible;previousLayout=graphLayoutVersion;
    }
}
void layoutMonitorControls() {
    if(!canvas)return;RECT client{};GetClientRect(canvas,&client);
    const auto g=monitorGeometry(canvas);
    const auto label=[&](HWND control,int x,int y,int width,int height){
        SetWindowPos(control,nullptr,x,y,width,height,SWP_NOZORDER|SWP_NOACTIVATE|SWP_NOCOPYBITS);
        InvalidateRect(control,nullptr,FALSE);
    };
    label(websiteLink,g.foot.left+px(2),g.foot.top+px(1),g.foot.right-g.foot.left-px(4),g.foot.bottom-g.foot.top-px(2));
    MoveWindow(hdrButton,g.left,g.top,g.size,g.size,TRUE);
    MoveWindow(powerButton,g.right,g.top,g.size,g.size,TRUE);
    MoveWindow(infoButton,(g.screen.left+g.screen.right-g.size)/2,g.top,g.size,g.size,TRUE);
    MoveWindow(profilesButton,g.right,g.bottom,g.size,g.size,TRUE);
    MoveWindow(monitorSettingsButton,g.left,g.bottom,g.size,g.size,TRUE);
    MoveWindow(status,g.left+g.size+px(12),g.top,g.right-g.left-g.size-px(24),g.size,TRUE);
    label(monitorMode,g.left+g.size+px(32),g.bottom,g.right-g.left-g.size-px(64),g.size);
    const int center=client.right/2;
    RECT choice{};GetWindowRect(sourceChoice,&choice);const int height=choice.bottom-choice.top;
    const int rowTop=(g.body.bottom+g.foot.bottom-height)/2;
    MoveWindow(sourceChoice,0,rowTop,center-px(68),px(180),TRUE);
    label(monitorProfile,center+px(68),rowTop,client.right-center-px(68),height);
}
void changeCurvePoint(HWND hwnd,LPARAM point) {
    if(selected!=6)return;const RECT plot=curvePlot(hwnd);auto& curve=currentPreset(0).current.adjustments.curves[editingChannel];
    if(curve.empty()){for(int i=0;i<9;++i)curve.push_back(i/8.0);}
    if(draggedPoint<0)draggedPoint=std::clamp(static_cast<int>(std::lround((GET_X_LPARAM(point)-plot.left)*8.0/(plot.right-plot.left))),0,8);
    keyboardPoint=draggedPoint;const auto index=static_cast<size_t>(draggedPoint);const double low=index?curve[index-1]:0,high=index+1<curve.size()?curve[index+1]:1;
    curve[index]=std::clamp((plot.bottom-GET_Y_LPARAM(point))/static_cast<double>(plot.bottom-plot.top),low,high);
    presetEdited(0);labels();if(correctionsRequested)SetTimer(window,ApplyTimer,100,nullptr);
}
LRESULT CALLBACK profileLinkProc(HWND hwnd,UINT message,WPARAM wParam,LPARAM lParam,UINT_PTR,DWORD_PTR){
    if(message==WM_SETCURSOR){SetCursor(LoadCursorW(nullptr,IDC_HAND));return TRUE;}
    if(message==WM_GETDLGCODE&&lParam){const auto* key=reinterpret_cast<MSG*>(lParam);if(key->wParam==VK_RETURN||key->wParam==VK_SPACE)return DLGC_WANTMESSAGE;}
    if(message==WM_KEYDOWN&&(wParam==VK_RETURN||wParam==VK_SPACE)){SendMessageW(GetParent(hwnd),WM_COMMAND,MAKEWPARAM(GetDlgCtrlID(hwnd),STN_CLICKED),reinterpret_cast<LPARAM>(hwnd));return 0;}
    if(message==WM_MOUSEMOVE&&!GetPropW(hwnd,L"LutProfileHover")){SetPropW(hwnd,L"LutProfileHover",reinterpret_cast<HANDLE>(1));TRACKMOUSEEVENT track{sizeof(track),TME_LEAVE,hwnd,0};TrackMouseEvent(&track);InvalidateRect(hwnd,nullptr,FALSE);}
    if(message==WM_MOUSELEAVE){RemovePropW(hwnd,L"LutProfileHover");InvalidateRect(hwnd,nullptr,FALSE);}
    if(message==WM_SETFOCUS||message==WM_KILLFOCUS)InvalidateRect(hwnd,nullptr,FALSE);
    if(message==WM_NCDESTROY){RemovePropW(hwnd,L"LutProfileHover");RemoveWindowSubclass(hwnd,profileLinkProc,80);}
    return DefSubclassProc(hwnd,message,wParam,lParam);
}
LRESULT CALLBACK monitorLabelPaintProc(HWND hwnd,UINT message,WPARAM wParam,LPARAM lParam,UINT_PTR,DWORD_PTR){
    if(message==WM_ERASEBKGND)return 1;
    if(message==WM_PAINT||message==WM_PRINTCLIENT||message==WM_PRINT){
        PAINTSTRUCT paint{};HDC target=message==WM_PAINT?BeginPaint(hwnd,&paint):reinterpret_cast<HDC>(wParam);
        RECT bounds{};GetClientRect(hwnd,&bounds);
        // Native static controls can preserve/copy parent pixels while the
        // monitor is resized. Paint these labels only in their own local DC.
        paint::Buffer frame(target,bounds);
        if(frame&&frame.buffered()){
            DRAWITEMSTRUCT item{};item.CtlType=ODT_STATIC;item.CtlID=GetDlgCtrlID(hwnd);
            item.itemAction=ODA_DRAWENTIRE;item.hwndItem=hwnd;item.hDC=frame.dc();item.rcItem=bounds;
            SendMessageW(GetParent(hwnd),WM_DRAWITEM,item.CtlID,reinterpret_cast<LPARAM>(&item));
        }
        frame.finish();if(message==WM_PAINT)EndPaint(hwnd,&paint);return 0;
    }
    if(message==WM_NCDESTROY)RemoveWindowSubclass(hwnd,monitorLabelPaintProc,81);
    return DefSubclassProc(hwnd,message,wParam,lParam);
}
LRESULT CALLBACK curveProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    if(message==WM_DRAWITEM&&reinterpret_cast<DRAWITEMSTRUCT*>(lParam)->hwndItem==websiteLink){
        const auto& item=*reinterpret_cast<DRAWITEMSTRUCT*>(lParam);
        const int saved=SaveDC(item.hDC);
        IntersectClipRect(item.hDC,item.rcItem.left,item.rcItem.top,item.rcItem.right,item.rcItem.bottom);
        HBRUSH back=CreateSolidBrush(RGB(80,87,96));FillRect(item.hDC,&item.rcItem,back);DeleteObject(back);
        LOGFONTW style{};GetObjectW(font,sizeof(style),&style);style.lfHeight=-px(8);
        style.lfUnderline=GetPropW(websiteLink,L"LutProfileHover")!=nullptr;
        HFONT small=CreateFontIndirectW(&style);const auto old=SelectObject(item.hDC,small?small:font);
        SetBkMode(item.hDC,TRANSPARENT);SetTextColor(item.hDC,RGB(0,0,0));
        RECT area=item.rcItem;DrawTextW(item.hDC,L"CeeJay.dk",-1,&area,DT_SINGLELINE|DT_CENTER|DT_VCENTER|DT_NOPREFIX);
        if(GetFocus()==websiteLink)DrawFocusRect(item.hDC,&area);
        SelectObject(item.hDC,old);if(small)DeleteObject(small);
        if(saved)RestoreDC(item.hDC,saved);return TRUE;
    }
    if(message==WM_SETCURSOR&&reinterpret_cast<HWND>(wParam)==hwnd&&LOWORD(lParam)==HTCLIENT&&selected==6&&!settingsPage){
        POINT point{};GetCursorPos(&point);ScreenToClient(hwnd,&point);const auto plot=curvePlot(hwnd);
        if(PtInRect(&plot,point)||draggedPoint>=0){SetCursor(LoadCursorW(nullptr,IDC_CROSS));return TRUE;}
    }
    if(message==WM_DRAWITEM && (reinterpret_cast<DRAWITEMSTRUCT*>(lParam)->hwndItem==monitorMode || reinterpret_cast<DRAWITEMSTRUCT*>(lParam)->hwndItem==monitorProfile)) {
        const auto& item=*reinterpret_cast<DRAWITEMSTRUCT*>(lParam);const bool profile=item.hwndItem==monitorProfile;
        const int saved=SaveDC(item.hDC);
        IntersectClipRect(item.hDC,item.rcItem.left,item.rcItem.top,item.rcItem.right,item.rcItem.bottom);
        HBRUSH back=profile?ui::background():CreateSolidBrush(RGB(7,9,12));FillRect(item.hDC,&item.rcItem,back);if(!profile)DeleteObject(back);
        const auto old=SelectObject(item.hDC,reinterpret_cast<HFONT>(SendMessageW(item.hwndItem,WM_GETFONT,0,0)));SetBkMode(item.hDC,TRANSPARENT);SetTextColor(item.hDC,ui::Muted);
        wchar_t text[2048]{};GetWindowTextW(item.hwndItem,text,2048);RECT area=item.rcItem;
        if(profile){
            const bool hover=GetPropW(monitorProfile,L"LutProfileHover")!=nullptr;
            SetTextColor(item.hDC,hover||GetFocus()==monitorProfile?ui::Text:ui::Muted);
            RECT measured{};DrawTextW(item.hDC,text,-1,&measured,DT_SINGLELINE|DT_CALCRECT|DT_NOPREFIX);
            const int icon=px(12),gap=px(5),width=std::min(static_cast<int>(measured.right-measured.left)+px(4),static_cast<int>(area.right-area.left)-icon-gap);
            const int start=area.right-width-icon-gap,mid=(area.top+area.bottom)/2;
            ui::paintMenuGlyph(monitorProfile,item.hDC,{start,mid-icon/2,start+icon,mid+(icon+1)/2},ui::Glyph::Diamond,applicationContextActive&&!appFocus.manual(),true,ui::Background);
            area.left=start+icon+gap;area.right=area.left+width;
            DrawTextW(item.hDC,text,-1,&area,DT_SINGLELINE|DT_VCENTER|DT_END_ELLIPSIS|DT_NOPREFIX);
            if(hover){HPEN pen=CreatePen(PS_SOLID,1,ui::FocusBorder);const auto previous=SelectObject(item.hDC,pen);const int y=mid+px(8);MoveToEx(item.hDC,area.left,y,nullptr);LineTo(item.hDC,area.right,y);SelectObject(item.hDC,previous);DeleteObject(pen);}
            if(GetFocus()==monitorProfile)DrawFocusRect(item.hDC,&area);
        }else DrawTextW(item.hDC,text,-1,&area,DT_SINGLELINE|DT_VCENTER|DT_END_ELLIPSIS|DT_NOPREFIX|DT_CENTER);
        SelectObject(item.hDC,old);if(saved)RestoreDC(item.hDC,saved);return TRUE;
    }
    LRESULT themed=0;if((message==WM_NOTIFY || message==WM_DRAWITEM || message==WM_MEASUREITEM || (message>=WM_CTLCOLORMSGBOX && message<=WM_CTLCOLORSTATIC)) && ui::message(hwnd,message,wParam,lParam,themed))return themed;
    if(message==WM_COMMAND)return SendMessageW(GetParent(hwnd),message,wParam,lParam);
    if(message==WM_NOTIFY)return SendMessageW(GetParent(hwnd),message,wParam,lParam);
    if(message==WM_SIZE){layoutMonitorControls();InvalidateRect(hwnd,nullptr,FALSE);return 0;}
    if(message==WM_SETFOCUS||message==WM_KILLFOCUS){invalidateGraph();return 0;}
    if(selected==6) {
        if(message==WM_GETDLGCODE)return DLGC_WANTARROWS;
        if(message==WM_LBUTTONDBLCLK){
            const auto plot=curvePlot(hwnd);POINT point{GET_X_LPARAM(lParam),GET_Y_LPARAM(lParam)};
            auto hit=plot;InflateRect(&hit,px(6),px(6));
            if(PtInRect(&hit,point)){
                auto& curve=currentPreset(0).current.adjustments.curves[editingChannel];
                if(!curve.empty()){
                    const auto i=static_cast<size_t>(std::clamp(static_cast<int>(std::lround((point.x-plot.left)*8.0/(plot.right-plot.left))),0,8));
                    curve[i]=std::clamp(i/8.0,i?curve[i-1]:0,i+1<curve.size()?curve[i+1]:1);
                    keyboardPoint=static_cast<int>(i);draggedPoint=-1;sliderDragging=false;
                    if(GetCapture()==hwnd)ReleaseCapture();presetEdited(0);applyLive();labels();
                }
            }
            return 0;
        }
        if(message==WM_LBUTTONDOWN){const auto plot=curvePlot(hwnd);POINT point{GET_X_LPARAM(lParam),GET_Y_LPARAM(lParam)};if(PtInRect(&plot,point)){
            SetFocus(hwnd);lookDirtyBeforeDrag=currentPreset(0).dirty();sliderDragging=true;KillTimer(window,PresetTimer);draggedPoint=-1;SetCapture(hwnd);changeCurvePoint(hwnd,lParam);}return 0;}
        if(message==WM_MOUSEMOVE && GetCapture()==hwnd){changeCurvePoint(hwnd,lParam);return 0;}
        if(message==WM_LBUTTONUP || message==WM_CAPTURECHANGED || message==WM_CANCELMODE){if(draggedPoint>=0){draggedPoint=-1;sliderDragging=false;if(GetCapture()==hwnd)ReleaseCapture();presetEdited(0);labels();}return 0;}
        if(message==WM_RBUTTONUP){currentPreset(0).current.adjustments.curves[editingChannel].clear();presetEdited(0);applyLive();labels();return 0;}
        if(message==WM_KEYDOWN && (wParam==VK_UP||wParam==VK_DOWN||wParam==VK_LEFT||wParam==VK_RIGHT)) {
            auto& curve=currentPreset(0).current.adjustments.curves[editingChannel];if(curve.empty())for(int i=0;i<9;++i)curve.push_back(i/8.0);
            if(wParam==VK_LEFT||wParam==VK_RIGHT)keyboardPoint=std::clamp(keyboardPoint+(wParam==VK_RIGHT?1:-1),0,8);
            else {const size_t i=static_cast<size_t>(keyboardPoint);curve[i]=std::clamp(curve[i]+(wParam==VK_UP?.01:-.01),i?curve[i-1]:0,i+1<curve.size()?curve[i+1]:1);presetEdited(0);if(correctionsRequested)SetTimer(window,ApplyTimer,100,nullptr);}
            labels();return 0;
        }
    }
    if (message == WM_ERASEBKGND) return 1;
    if (message != WM_PAINT && message != WM_PRINTCLIENT) return DefWindowProcW(hwnd, message, wParam, lParam);
    PAINTSTRUCT paint{}; HDC dc = message == WM_PAINT ? BeginPaint(hwnd, &paint) : reinterpret_cast<HDC>(wParam);
    RECT client{}; GetClientRect(hwnd, &client);
    // Buffer only while painting; no persistent frame loop or render resources.
    paint::Buffer frame(dc,client);
    // Keep the previous image if off-screen allocation fails; never fall
    // back to drawing the graph directly into the visible window.
    if(!frame||!frame.buffered()){frame.finish();if(message==WM_PAINT)EndPaint(hwnd,&paint);return 0;}
    if(renderOnly)lastGraphPaintBytes=frame.bytes();
    HDC buffer=frame.dc();FillRect(buffer, &client, ui::background());
    const auto geometry=monitorGeometry(hwnd);const RECT body=geometry.body;
    auto rounded = [&](RECT rect, COLORREF fill, COLORREF outline, int radius) {
        HBRUSH brush=CreateSolidBrush(fill);HPEN pen=CreatePen(PS_SOLID,1,outline);
        auto oldBrush=SelectObject(buffer,brush),oldPen=SelectObject(buffer,pen);
        RoundRect(buffer,rect.left,rect.top,rect.right,rect.bottom,px(radius*2),px(radius*2));
        SelectObject(buffer,oldBrush);SelectObject(buffer,oldPen);DeleteObject(brush);DeleteObject(pen);
    };
    const int center=client.right/2;
    RECT neck{center-px(17),body.bottom-px(2),center+px(17),client.bottom-px(7)};
    const RECT foot=geometry.foot;
    rounded(neck,RGB(65,70,77),RGB(86,92,100),2);rounded(foot,RGB(80,87,96),RGB(112,120,130),3);
    rounded(body,RGB(82,89,98),RGB(120,128,138),7);
    const RECT screen=geometry.screen;rounded(screen,RGB(7,9,12),RGB(32,36,42),3);
    const auto plotRect=curvePlot(hwnd);const int left=plotRect.left,right=plotRect.right,top=plotRect.top,bottom=plotRect.bottom;
    HPEN grid = CreatePen(PS_SOLID, 1, RGB(58, 62, 69)); HGDIOBJ oldPen = SelectObject(buffer, grid);
    for (int i = 0; i <= 4; ++i) {
        const int x = left + (right - left) * i / 4, y = bottom - (bottom - top) * i / 4;
        MoveToEx(buffer, x, top, nullptr); LineTo(buffer, x, bottom);
        MoveToEx(buffer, left, y, nullptr); LineTo(buffer, right, y);
    }
    HPEN identity = CreatePen(PS_DOT, 1, ui::Border); SelectObject(buffer, identity);
    MoveToEx(buffer, left, bottom, nullptr); LineTo(buffer, right, top);
    const auto settings=currentSettings();const auto output=previewOutput();
    const COLORREF inks[]{RGB(255,0,0),RGB(0,255,0),RGB(0,0,255)};
    plot::Paths paths;std::vector<POINT> editablePath;bool needsGuide=false;
    for(auto& path:paths)path.reserve(513);
    if(selected==6)editablePath.reserve(513);
    const auto position=[&](double x,double y){return POINT{left+static_cast<int>(std::lround((right-left)*x)),bottom-static_cast<int>(std::lround((bottom-top)*y))};};
    for(int i=0;i<=512;++i) {
        const double x=i/512.0;
        POINT editable{};
        if(selected==6){editable=position(x,color::sampleSpline(settings.adjustments.curves[editingChannel],x));editablePath.push_back(editable);}
        for(int c=0;c<3;++c) {
            // In the editor show the Master + RGB transform even when its
            // switch is off, so editing remains possible before enabling it.
            const double y=selected==6?color::sampleSpline(settings.adjustments.curves[c+1],color::sampleSpline(settings.adjustments.curves[0],x)):live::evaluate(x,c,output,settings);
            paths[c].push_back(position(x,y));
            if(selected==6 && (editingChannel==0 || editingChannel==c+1) && std::abs(paths[c].back().y-editable.y)>1)needsGuide=true;
        }
    }
    if(selected==6 && needsGuide) {
        HPEN guide=CreatePen(PS_DOT,1,ui::Muted);SelectObject(buffer,guide);
        Polyline(buffer,editablePath.data(),static_cast<int>(editablePath.size()));SelectObject(buffer,oldPen);DeleteObject(guide);
    }
    plot::rgb(buffer,client.right,client.bottom,paths,px(2),&curveScratch);
    if(selected==6) {
        const auto& editable=settings.adjustments.curves[editingChannel];
        HPEN handles=CreatePen(PS_SOLID,px(2),editingChannel?inks[editingChannel-1]:RGB(255,255,255));SelectObject(buffer,handles);
        const auto previousBrush=SelectObject(buffer,GetStockObject(BLACK_BRUSH));
        for(int i=0;i<9;++i){const auto at=position(i/8.0,color::sampleSpline(editable,i/8.0));const int x=at.x,y=at.y;Ellipse(buffer,x-px(3),y-px(3),x+px(4),y+px(4));if(GetFocus()==hwnd&&i==keyboardPoint){RECT focus{x-px(6),y-px(6),x+px(7),y+px(7)};DrawFocusRect(buffer,&focus);}}
        SelectObject(buffer,previousBrush);SelectObject(buffer,oldPen);DeleteObject(handles);
    }
    SelectObject(buffer,oldPen);DeleteObject(grid);DeleteObject(identity);
    LOGFONTW axisStyle{};GetObjectW(font,sizeof(axisStyle),&axisStyle);axisStyle.lfHeight=-px(ui::style::SecondaryTextSize);
    HFONT axisFont=CreateFontIndirectW(&axisStyle);HGDIOBJ oldFont = SelectObject(buffer,axisFont?axisFont:font); SetBkMode(buffer, TRANSPARENT); SetTextColor(buffer, ui::Text);
    const auto axis=[&](const wchar_t* value,int cx,int cy){RECT area{cx-px(8),cy-px(8),cx+px(8),cy+px(8)};DrawTextW(buffer,value,1,&area,DT_SINGLELINE|DT_CENTER|DT_VCENTER|DT_NOPREFIX);};
    const int lowerCenter=geometry.bottom+geometry.size/2,sideCenter=geometry.left+geometry.size/2;
    axis(L"0",left,lowerCenter);axis(L"1",right,lowerCenter);
    axis(L"1",sideCenter,top);axis(L"0",sideCenter,bottom);
    axisStyle.lfHeight=-px(ui::style::CaptionTextSize);
    HFONT toneFont=CreateFontIndirectW(&axisStyle);SelectObject(buffer,toneFont?toneFont:font);SetTextColor(buffer,ui::Muted);
    const wchar_t* tones[]{L"Shadows",L"Midtones",L"Highlights"};
    for(int i=0;i<3;++i){
        RECT label{left+(right-left)*i/3,bottom+px(2),left+(right-left)*(i+1)/3,bottom+px(14)};
        DrawTextW(buffer,tones[i],-1,&label,DT_SINGLELINE|DT_VCENTER|DT_NOPREFIX|(i==0?DT_LEFT:i==1?DT_CENTER:DT_RIGHT));
    }
    SelectObject(buffer, oldFont);
    if(toneFont)DeleteObject(toneFont);
    if(axisFont)DeleteObject(axisFont);
    // Paint last, over the graph itself: long warnings may wrap below the
    // header row without a child window clipping their second line.
    wchar_t statusText[2048]{};GetWindowTextW(status,statusText,2048);
    RECT statusArea{geometry.left+geometry.size+px(12),geometry.top,
        geometry.right-px(12),geometry.bottom};
    RECT measured=statusArea;
    const auto statusFont=SelectObject(buffer,font);
    SetBkMode(buffer,TRANSPARENT);SetTextColor(buffer,statusError?ui::Error:ui::Muted);
    DrawTextW(buffer,statusText,-1,&measured,DT_CENTER|DT_WORDBREAK|DT_CALCRECT|DT_NOPREFIX);
    statusArea.top+=std::max(0,(geometry.size-static_cast<int>(measured.bottom-measured.top))/2);
    if(graphStatusVisible)DrawTextW(buffer,statusText,-1,&statusArea,DT_CENTER|DT_WORDBREAK|DT_NOPREFIX);
    SelectObject(buffer,statusFont);
    frame.finish();
    if (message == WM_PAINT) EndPaint(hwnd, &paint); return 0;
}
bool renderPreview(const wchar_t* path, HWND target = nullptr) {
    if (!target) target = window;
    RECT outer{}; GetWindowRect(target, &outer);
    RECT rect{0, 0, outer.right - outer.left, outer.bottom - outer.top};
    HDC screen = GetDC(nullptr); HDC dc = CreateCompatibleDC(screen);
    HBITMAP bitmap = CreateCompatibleBitmap(screen, rect.right, rect.bottom);
    ReleaseDC(nullptr, screen);
    HGDIOBJ previous = SelectObject(dc, bitmap);
    FillRect(dc, &rect, ui::background());
    SendMessageW(target, WM_PRINT, reinterpret_cast<WPARAM>(dc), PRF_NONCLIENT | PRF_CLIENT | PRF_CHILDREN | PRF_ERASEBKGND);
    SelectObject(dc, previous);
    BITMAPINFO info{}; info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = rect.right; info.bmiHeader.biHeight = rect.bottom;
    info.bmiHeader.biPlanes = 1; info.bmiHeader.biBitCount = 32; info.bmiHeader.biCompression = BI_RGB;
    std::vector<unsigned char> pixels(static_cast<std::size_t>(rect.right) * rect.bottom * 4);
    const bool captured = GetDIBits(dc, bitmap, 0, static_cast<UINT>(rect.bottom), pixels.data(), &info, DIB_RGB_COLORS) != 0;
    DeleteObject(bitmap); DeleteDC(dc);
    if (!captured) return false;
    BITMAPFILEHEADER header{}; header.bfType = 0x4d42;
    header.bfOffBits = sizeof(header) + sizeof(BITMAPINFOHEADER);
    header.bfSize = header.bfOffBits + static_cast<DWORD>(pixels.size());
    HANDLE file = CreateFileW(path, GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    DWORD count = 0;
    const bool written = WriteFile(file, &header, sizeof(header), &count, nullptr) && count == sizeof(header) &&
        WriteFile(file, &info.bmiHeader, sizeof(BITMAPINFOHEADER), &count, nullptr) && count == sizeof(BITMAPINFOHEADER) &&
        WriteFile(file, pixels.data(), static_cast<DWORD>(pixels.size()), &count, nullptr) && count == pixels.size();
    CloseHandle(file); return written;
}
HWND control(const wchar_t* cls, const wchar_t* text, DWORD style, int id,HWND parent=nullptr) {
    if(wcscmp(cls,L"COMBOBOX")==0)style|=CBS_OWNERDRAWFIXED|CBS_HASSTRINGS;
    HWND child = CreateWindowExW(0, cls, text, WS_CHILD | WS_VISIBLE | style, 0, 0, 1, 1, parent?parent:window,
                           reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), instance, nullptr);
    ui::control(child); return child;
}
void ensureEditor() {
    if(editorReady)return;
    EditorSync sync;
    ensureCommonControls();
    editorReady=true;
    HWND hwnd=window;
        ui::fixedWindowSize(hwnd,ClientWidth,ClientHeight);
        ui::enableViewport(hwnd,ClientWidth,ClientHeight);
        presetLabel = control(L"STATIC", L"Look preset", 0, 0);
        presetChoice = control(L"COMBOBOX", L"", WS_TABSTOP | CBS_DROPDOWNLIST | WS_VSCROLL, PresetChoice);
        savePresetButton = control(L"BUTTON", L"Save", WS_TABSTOP | BS_PUSHBUTTON, SavePreset);
        saveAsButton = control(L"BUTTON", L"Save as...", WS_TABSTOP | BS_PUSHBUTTON, SavePresetAs);
        openPresetButton = control(L"BUTTON", L"Preset folder...", WS_TABSTOP | BS_PUSHBUTTON, OpenPreset);
        autosaveControl = control(L"BUTTON", L"Autosave", WS_TABSTOP | BS_AUTOCHECKBOX, Autosave);
        revertPresetButton = control(L"BUTTON", L"Revert edits", WS_TABSTOP | BS_PUSHBUTTON, RevertPreset);
        activeLookLabel = control(L"STATIC", L"", SS_ENDELLIPSIS | SS_NOPREFIX, ActiveLook);
        listLabel = control(L"STATIC", L"Corrections", SS_CENTERIMAGE, 0);
        effectsLabel=control(L"STATIC",L"Effects",SS_CENTERIMAGE,0);
        layers = control(L"LISTBOX", L"", WS_TABSTOP | LBS_NOTIFY | LBS_OWNERDRAWFIXED | LBS_HASSTRINGS | LBS_NOINTEGRALHEIGHT | WS_VSCROLL, Layers);
        effects::attach(layers, toggleEffect, moveCreative);
        correctionLayers=control(L"LISTBOX",L"Corrections",WS_TABSTOP|LBS_NOTIFY|LBS_OWNERDRAWFIXED|LBS_HASSTRINGS|LBS_NOINTEGRALHEIGHT,CorrectionLayers);
        effects::attach(correctionLayers,toggleEffect,[](int,int){});
        windowsLayers=control(L"LISTBOX",L"Windows account corrections",WS_TABSTOP|LBS_NOTIFY|LBS_OWNERDRAWFIXED|LBS_HASSTRINGS|LBS_NOINTEGRALHEIGHT,WindowsCorrectionLayers);
        effects::attach(windowsLayers,toggleEffect,[](int,int){});
        orderHint = control(L"STATIC", L"Drag / Alt+arrows reorder", SS_OWNERDRAW, OrderHint);
        title = control(L"STATIC", L"", 0, 0); description = control(L"STATIC", L"", 0, 0);
        amountLabel = control(L"STATIC", L"Contrast", SS_CENTERIMAGE, 0); pivotLabel = control(L"STATIC", L"Pivot", SS_CENTERIMAGE, 0);
        whiteLabel = control(L"STATIC", L"SDR white", SS_CENTERIMAGE, 0); gammaLabel = control(L"STATIC", L"Target gamma", SS_CENTERIMAGE, 0);
        amountValue=control(L"STATIC",L"",SS_RIGHT|SS_CENTERIMAGE,0);pivotValue=control(L"STATIC",L"",SS_RIGHT|SS_CENTERIMAGE,0);whiteValue=control(L"STATIC",L"",SS_RIGHT|SS_CENTERIMAGE,0);
        contrastSlider = control(TRACKBAR_CLASSW, L"", WS_TABSTOP | TBS_NOTICKS, Contrast);
        pivotSlider = control(TRACKBAR_CLASSW, L"", WS_TABSTOP | TBS_NOTICKS, Pivot);
        whiteSlider = control(TRACKBAR_CLASSW, L"", WS_TABSTOP | TBS_NOTICKS, White);
        SendMessageW(contrastSlider, TBM_SETRANGE, TRUE, MAKELPARAM(-25, 25));
        SendMessageW(pivotSlider, TBM_SETRANGE, TRUE, MAKELPARAM(20, 80)); SendMessageW(pivotSlider, TBM_SETPOS, TRUE, pivotPosition);
        SendMessageW(whiteSlider, TBM_SETRANGE, TRUE, MAKELPARAM(80, 480)); SendMessageW(whiteSlider, TBM_SETPOS, TRUE, whitePosition);
        ui::sliderDetails(contrastSlider,0);ui::sliderDetails(pivotSlider,50);ui::sliderDetails(whiteSlider,200);
        gammaChoice = control(L"BUTTON", L"2.2", WS_GROUP | WS_TABSTOP | BS_AUTORADIOBUTTON, Gamma);
        gammaOther = control(L"BUTTON", L"2.4", BS_AUTORADIOBUTTON, GammaOther);
        SendMessageW(gammaChoice, BM_SETCHECK, BST_CHECKED, 0);
        canvas = control(L"LutCurvePreview", L"", WS_TABSTOP|WS_GROUP|WS_CLIPCHILDREN, 0); status = control(L"STATIC", L"", SS_OWNERDRAW, MonitorStatus,canvas);
        // Retain the status text handle for state queries; the canvas paints it
        // and exposes it to accessibility without a separately clipped surface.
        ShowWindow(status,SW_HIDE);

        monitorMode=control(L"STATIC",L"",SS_OWNERDRAW,MonitorMode,canvas);
        monitorProfile=control(L"STATIC",L"Desktop profile",SS_OWNERDRAW|SS_NOTIFY|WS_TABSTOP,MonitorProfile,canvas);
        SetWindowSubclass(monitorProfile,profileLinkProc,80,0);
        websiteLink=control(L"STATIC",L"CeeJay.dk",SS_OWNERDRAW|SS_NOTIFY|WS_TABSTOP,WebsiteLink,canvas);
        SetWindowSubclass(websiteLink,profileLinkProc,80,0);
        for(HWND label:{monitorMode,monitorProfile,websiteLink}){
            SetWindowLongPtrW(label,GWL_STYLE,GetWindowLongPtrW(label,GWL_STYLE)|WS_CLIPSIBLINGS);
            SetWindowSubclass(label,monitorLabelPaintProc,81,0);
        }
        SetWindowLongPtrW(canvas,GWL_EXSTYLE,GetWindowLongPtrW(canvas,GWL_EXSTYLE)|WS_EX_CONTROLPARENT);
        hdrButton=control(L"BUTTON",L"Toggle Windows HDR on the selected display",WS_TABSTOP|BS_PUSHBUTTON,ToggleWindowsHdr,canvas);
        powerButton=control(L"BUTTON",L"Master power on/off (Alt+P)",WS_TABSTOP|BS_PUSHBUTTON,TestContrast,canvas);
        profilesButton=control(L"BUTTON",ui::wording::ApplicationProfiles,WS_TABSTOP|BS_PUSHBUTTON,ApplicationProfiles,canvas);
        infoButton=control(L"BUTTON",L"Picture status",WS_TABSTOP|BS_PUSHBUTTON,OutputInfo,canvas);
        monitorSettingsButton=control(L"BUTTON",ui::wording::Settings,WS_TABSTOP|BS_PUSHBUTTON,SettingsPage,canvas);
        monitorTips=CreateWindowExW(WS_EX_TOPMOST,TOOLTIPS_CLASSW,nullptr,WS_POPUP|TTS_ALWAYSTIP|TTS_NOPREFIX,CW_USEDEFAULT,CW_USEDEFAULT,CW_USEDEFAULT,CW_USEDEFAULT,window,nullptr,instance,nullptr);
        ui::styleTooltip(monitorTips);
        SendMessageW(monitorTips,TTM_SETMAXTIPWIDTH,0,px(360));
        for(HWND button:{hdrButton,powerButton,profilesButton,monitorSettingsButton,infoButton,monitorProfile,websiteLink}){TOOLINFOW tip{sizeof(tip)};tip.uFlags=TTF_IDISHWND|TTF_SUBCLASS;tip.hwnd=canvas;tip.uId=reinterpret_cast<UINT_PTR>(button);tip.lpszText=LPSTR_TEXTCALLBACKW;SendMessageW(monitorTips,TTM_ADDTOOLW,0,reinterpret_cast<LPARAM>(&tip));}
        resetButton = control(L"BUTTON", L"Reset adjustment", WS_TABSTOP | BS_PUSHBUTTON, Reset);
        selectedEffectToggle=control(L"BUTTON",L"Selected effect on/off",WS_TABSTOP|BS_AUTOCHECKBOX,SelectedEffectToggle);
        ui::toggleButton(selectedEffectToggle,false);
        exportButton = control(L"BUTTON", L"Export curve...", WS_TABSTOP | BS_PUSHBUTTON, Export);
        {const std::pair<HWND,ui::Glyph> toolbar[]{{savePresetButton,ui::Glyph::Save},{saveAsButton,ui::Glyph::SaveAs},{exportButton,ui::Glyph::Export},{revertPresetButton,ui::Glyph::Revert},{resetButton,ui::Glyph::Reset}};
        for(const auto& entry:toolbar){ui::iconButton(entry.first,entry.second,false,IsWindowEnabled(entry.first)!=FALSE);}
        const std::pair<HWND,const wchar_t*> hints[]{{savePresetButton,L"Save preset"},{saveAsButton,L"Save preset as..."},{exportButton,L"Export curve..."},{revertPresetButton,L"Revert unsaved changes"},{resetButton,L"Reset this adjustment"},{selectedEffectToggle,L"Turn this adjustment on or off"}};
        for(const auto& entry:hints){TOOLINFOW tip{sizeof(tip)};tip.uFlags=TTF_IDISHWND|TTF_SUBCLASS;tip.hwnd=window;tip.uId=reinterpret_cast<UINT_PTR>(entry.first);tip.lpszText=const_cast<wchar_t*>(entry.second);SendMessageW(monitorTips,TTM_ADDTOOLW,0,reinterpret_cast<LPARAM>(&tip));}}

        sourceChoice = control(L"COMBOBOX", L"", WS_TABSTOP | CBS_DROPDOWNLIST | WS_VSCROLL, SourceChoice,canvas);
        testNote = control(L"STATIC", L"", 0, 0);
        bypassButton = control(L"BUTTON", L"Bypass look", WS_TABSTOP | BS_PUSHBUTTON, Bypass);
        shortcutLabel = control(L"STATIC", L"Bypass shortcut", 0, 0);
        shortcutControl = control(HOTKEY_CLASSW, L"", WS_TABSTOP, Shortcut);
        SendMessageW(shortcutControl,HKM_SETRULES,HKCOMB_NONE,HOTKEYF_CONTROL);
        shortcutButton = control(L"BUTTON", L"Set shortcut", WS_TABSTOP | BS_PUSHBUTTON, SetShortcut);
        hdrFixControl = control(L"BUTTON", L"Also apply when Windows HDR is off (SDR)", WS_TABSTOP | BS_AUTOCHECKBOX, HdrFix);
        settingsButton=control(L"BUTTON",ui::wording::Settings,WS_TABSTOP|BS_PUSHBUTTON,SettingsPage);
        versionLabel=control(L"STATIC",L"MonitorMage " MONITORMAGE_VERSION_W,0,0);
        startupControl=control(L"BUTTON",L"Start with Windows",WS_TABSTOP|BS_AUTOCHECKBOX,StartWithWindows);
        startMinimizedControl=control(L"BUTTON",L"Start minimized",WS_TABSTOP|BS_AUTOCHECKBOX,StartMinimized);
        saturationReportButton=control(L"BUTTON",L"Color effect report...",WS_TABSTOP|BS_PUSHBUTTON,SaturationReport);ui::help(saturationReportButton,L"Color effect report");
        filterChoice=control(L"COMBOBOX",L"",WS_TABSTOP|CBS_DROPDOWNLIST,ColorFilterChoice);
        for(const auto* name:windows_controls::FilterNames)SendMessageW(filterChoice,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(name));
        filterSettingsButton=control(L"BUTTON",L"Windows settings...",WS_TABSTOP|BS_PUSHBUTTON,ColorFilterSettings);
        nightSchedule=control(L"BUTTON",L"Use schedule",WS_TABSTOP|BS_AUTOCHECKBOX,NightSchedule);
        nightStartLabel=control(L"STATIC",L"From",SS_CENTERIMAGE,0);nightEndLabel=control(L"STATIC",L"Until",SS_CENTERIMAGE,0);
        nightStart=control(L"EDIT",L"21:00",WS_TABSTOP|ES_AUTOHSCROLL,NightStart);nightEnd=control(L"EDIT",L"07:00",WS_TABSTOP|ES_AUTOHSCROLL,NightEnd);
        SendMessageW(nightStart,EM_SETLIMITTEXT,5,0);SendMessageW(nightEnd,EM_SETLIMITTEXT,5,0);
        nightDisplay=control(L"COMBOBOX",L"",WS_TABSTOP|CBS_DROPDOWNLIST,NightDisplay);
        nightInclude=control(L"BUTTON",L"Use on this display",WS_TABSTOP|BS_AUTOCHECKBOX,NightInclude);
        ui::help(nightDisplay,L"Night light display");
        ui::help(nightSchedule,L"Use daily schedule");ui::help(nightStart,L"Start time (24-hour)");ui::help(nightEnd,L"End time (24-hour)");ui::help(nightInclude,L"Include this display");
        correctionNote=control(L"STATIC",L"",0,0);
        if(!renderOnly){try{startupEnabled=app_settings::startsWithWindows(app_settings::executable());}catch(const std::exception& error){presetError(error);}}
        fontLabel=control(L"STATIC",L"Interface font",0,0);fontChoice=control(L"COMBOBOX",L"",WS_TABSTOP|CBS_DROPDOWNLIST,FontChoice);
        for(const wchar_t* name:{L"Bahnschrift",L"Segoe UI",L"Segoe UI Semilight"})SendMessageW(fontChoice,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(name));
        curveChannel=control(L"COMBOBOX",L"",WS_TABSTOP|CBS_DROPDOWNLIST,CurveChannel);
        for(const wchar_t* name:{L"Master",L"Red",L"Green",L"Blue"})SendMessageW(curveChannel,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(name));
        SendMessageW(curveChannel,CB_SETCURSEL,0,0);
        importButton=control(L"BUTTON",L"Import 1D LUT...",WS_TABSTOP|BS_PUSHBUTTON,Import1D);
        export1DButton=control(L"BUTTON",L"Export look LUT...",WS_TABSTOP|BS_PUSHBUTTON,Export1D);
        for(size_t i=0;i<adjustmentSliders.size();++i){adjustmentLabels[i]=control(L"STATIC",L"",SS_CENTERIMAGE,0);adjustmentValues[i]=control(L"STATIC",L"",SS_RIGHT|SS_CENTERIMAGE,0);adjustmentSliders[i]=control(TRACKBAR_CLASSW,L"",WS_TABSTOP|TBS_NOTICKS,AdjustmentBase+static_cast<int>(i));}
        {const std::pair<HWND,const wchar_t*> help[]{
            {presetChoice,L"Choose preset"},
            {autosaveControl,L"Autosave preset changes"},
            {sourceChoice,L"Choose display"},
            {fontChoice,L"Interface font"},
            {shortcutControl,L"Pause effects shortcut"},
            {shortcutButton,L"Set shortcut"},
            {startupControl,L"Start with Windows"},
            {startMinimizedControl,L"Start in the tray"},
            {openPresetButton,L"Open preset folder"},
            {bypassButton,L"Pause / resume effects"},
            {filterChoice,L"Choose color filter"},
            {filterSettingsButton,L"Windows color filter settings"},
            {contrastSlider,L"Contrast"},
            {pivotSlider,L"Contrast pivot"},
            {whiteSlider,L"SDR white brightness"},
            {gammaChoice,L"Gamma 2.2"},
            {gammaOther,L"Gamma 2.4"},
            {hdrFixControl,L"Also apply in SDR"},
            {curveChannel,L"Choose curve channel"},
            {importButton,L"Import 1D LUT"},
            {export1DButton,L"Export effects as 1D LUT"}};
        for(const auto& item:help)ui::help(item.first,item.second);}
        discoverPresets(0);discoverPresets(1);
        loadPresetControls();
        SendMessageW(autosaveControl,BM_SETCHECK,autosave?BST_CHECKED:BST_UNCHECKED,0);
        {const int family=wcscmp(ui::fontFamily(),L"Bahnschrift")==0?0:wcscmp(ui::fontFamily(),L"Segoe UI")==0?1:2;SendMessageW(fontChoice,CB_SETCURSEL,family,0);}
        refreshColorFilters();
        for(const auto& source:sources)SendMessageW(sourceChoice,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(source.label.c_str()));
        selectSource(selectedSource);
        SendMessageW(shortcutControl, HKM_SETHOTKEY, activeShortcut, 0);
        layout();labels();
}
LRESULT CALLBACK windowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    static const HBRUSH sidebar=CreateSolidBrush(ui::Sidebar);
    const auto paintBackground=[&](HDC dc){RECT rect{};GetClientRect(hwnd,&rect);FillRect(dc,&rect,ui::background());rect.right=std::max(0L,px(SidebarWidth)-ui::viewportOffset(hwnd).x);FillRect(dc,&rect,sidebar);};
    if(message==WM_ERASEBKGND || message==WM_PRINTCLIENT){paintBackground(reinterpret_cast<HDC>(wParam));return 1;}
    if(message==WM_PAINT){PAINTSTRUCT paint{};const auto dc=BeginPaint(hwnd,&paint);paintBackground(dc);EndPaint(hwnd,&paint);return 0;}
    if((message==WM_CTLCOLORSTATIC && (reinterpret_cast<HWND>(lParam)==listLabel || reinterpret_cast<HWND>(lParam)==effectsLabel || reinterpret_cast<HWND>(lParam)==orderHint)) || (message==WM_CTLCOLORLISTBOX && (reinterpret_cast<HWND>(lParam)==layers || reinterpret_cast<HWND>(lParam)==correctionLayers || reinterpret_cast<HWND>(lParam)==windowsLayers))){
        const auto dc=reinterpret_cast<HDC>(wParam);SetBkColor(dc,ui::Sidebar);SetTextColor(dc,ui::Text);return reinterpret_cast<LRESULT>(sidebar);
    }
    LRESULT themeResult = 0;
    // Client geometry must already match the eventual custom title bar, even
    // while its painting, fonts and interactive controls are deferred.
    if ((editorReady || message==WM_NCCALCSIZE) && ui::message(hwnd, message, wParam, lParam, themeResult)) return themeResult;
    if (explorerRestart && message == explorerRestart) { installTray(); return 0; }
    switch (message) {
    case WM_CREATE:
        window=hwnd;
        initializePresets();runtimeReady=true;
        if(!renderOnly){try{appConfig=applications::read(shortcutPath());}catch(const std::exception& error){presetError(error);}}
        refreshSources();
        if(!renderOnly)startShortcut();
        if(!deferEditor)ensureEditor();
        if(!renderOnly && !installTray())ensureEditor();
        return 0;
    case WM_MEASUREITEM:
        if (reinterpret_cast<MEASUREITEMSTRUCT*>(lParam)->CtlID == Layers || reinterpret_cast<MEASUREITEMSTRUCT*>(lParam)->CtlID==CorrectionLayers) { reinterpret_cast<MEASUREITEMSTRUCT*>(lParam)->itemHeight = px(effects::RowHeight); return TRUE; }
        break;
    case WM_DRAWITEM:
        if(reinterpret_cast<DRAWITEMSTRUCT*>(lParam)->hwndItem==orderHint) {
            const auto& item=*reinterpret_cast<DRAWITEMSTRUCT*>(lParam);HBRUSH brush=CreateSolidBrush(ui::Sidebar);FillRect(item.hDC,&item.rcItem,brush);DeleteObject(brush);
            const auto previous=SelectObject(item.hDC,reinterpret_cast<HFONT>(SendMessageW(orderHint,WM_GETFONT,0,0)));SetBkMode(item.hDC,TRANSPARENT);SetTextColor(item.hDC,ui::Muted);
            wchar_t text[128]{};GetWindowTextW(orderHint,text,128);RECT area=item.rcItem;
            DrawTextW(item.hDC,text,-1,&area,DT_SINGLELINE|DT_CENTER|DT_VCENTER|DT_END_ELLIPSIS|DT_NOPREFIX);
            SelectObject(item.hDC,previous);return TRUE;
        }
        if (effects::draw(*reinterpret_cast<DRAWITEMSTRUCT*>(lParam))) return TRUE;
        break;
    case WM_NOTIFY:
        if(reinterpret_cast<NMHDR*>(lParam)->hwndFrom==monitorTips && reinterpret_cast<NMHDR*>(lParam)->code==TTN_GETDISPINFOW) {
            auto tip=reinterpret_cast<NMTTDISPINFOW*>(lParam);const HWND button=reinterpret_cast<HWND>(tip->hdr.idFrom);
            static std::wstring text;
            if(button==infoButton){wchar_t details[2048]{};GetWindowTextW(status,details,2048);text=details;}
            else if(button==websiteLink)text=L"Visit CeeJay.dk";
            else text=button==hdrButton?std::wstring(ui::hdrTooltip(hdrState.on,hdrState.available)):button==powerButton?std::wstring(correctionsRequested?L"Turn off (Alt+P)":L"Turn on (Alt+P)"):button==monitorSettingsButton?std::wstring(ui::wording::Settings):std::wstring(ui::wording::ApplicationProfiles);
            tip->lpszText=text.data();return 0;
        }break;
    case WM_HSCROLL:
        if (LOWORD(wParam) == TB_THUMBTRACK && !sliderDragging) {
            lookDirtyBeforeDrag = currentPreset(0).dirty(); sliderDragging = true;
            if(selected!=9)KillTimer(hwnd, PresetTimer);
        }
        if (LOWORD(wParam) == TB_THUMBPOSITION || LOWORD(wParam) == TB_ENDTRACK) sliderDragging = false;
        {const int index=GetDlgCtrlID(reinterpret_cast<HWND>(lParam))-AdjustmentBase;const auto fields=parameters();
        if(index>=0&&static_cast<size_t>(index)<fields.size()){
            int value=static_cast<int>(SendMessageW(adjustmentSliders[index],TBM_GETPOS,0,0));
            if(selected==4){if(index%2==0)value=std::min(value,*fields[index+1].value-5);else value=std::max(value,*fields[index-1].value+5);}
            *fields[index].value=value;
            if(selected==9){if(!renderOnly&&LOWORD(wParam)!=TB_THUMBTRACK)persistPresetChoices();nightClock();}
            else presetEdited(0);
        }}
        contrastPosition = static_cast<int>(SendMessageW(contrastSlider, TBM_GETPOS, 0, 0));
        pivotPosition = static_cast<int>(SendMessageW(pivotSlider, TBM_GETPOS, 0, 0));
        whitePosition = static_cast<int>(SendMessageW(whiteSlider, TBM_GETPOS, 0, 0));
        if (reinterpret_cast<HWND>(lParam) == contrastSlider || reinterpret_cast<HWND>(lParam) == pivotSlider) presetEdited(0);
        labels(); if (correctionsRequested) SetTimer(hwnd, ApplyTimer, 100, nullptr); return 0;
    case WM_COMMAND:
        if(LOWORD(wParam)==NightDisplay&&HIWORD(wParam)==CBN_SELCHANGE){labels();return 0;}
        if(LOWORD(wParam)==NightSchedule||LOWORD(wParam)==NightInclude){
            if(LOWORD(wParam)==NightSchedule)nightConfig.scheduled=SendMessageW(nightSchedule,BM_GETCHECK,0,0)==BST_CHECKED;
            else{const auto index=SendMessageW(nightDisplay,CB_GETCURSEL,0,0);if(index>=0&&static_cast<size_t>(index)<sources.size())night::include(nightConfig,sources[index].identity,SendMessageW(nightInclude,BM_GETCHECK,0,0)==BST_CHECKED);}
            if(!renderOnly)persistPresetChoices();applyLive();labels();return 0;
        }
        if((LOWORD(wParam)==NightStart||LOWORD(wParam)==NightEnd)&&HIWORD(wParam)==EN_KILLFOCUS){
            if(closing)return 0;
            wchar_t text[16]{};GetWindowTextW(reinterpret_cast<HWND>(lParam),text,16);const int value=night::parseTime(text);
            const bool start=LOWORD(wParam)==NightStart;
            if(value<0||value==(start?nightConfig.end:nightConfig.start)){nightMessage=L"Use HH:MM, with different start and end times.";labels();return 0;}
            if(value==(start?nightConfig.start:nightConfig.end))return 0;
            (start?nightConfig.start:nightConfig.end)=value;nightMessage.clear();if(!renderOnly)persistPresetChoices();applyLive();labels();return 0;
        }
        if(LOWORD(wParam)==SaturationReport){
            if(!renderOnly)try{const auto report=matrixSession.report();ShellExecuteW(window,L"open",report.c_str(),nullptr,nullptr,SW_SHOWNORMAL);}
            catch(const std::exception&){matrixMessage=L"The saturation report could not be saved.";labels();}return 0;
        }
        if(LOWORD(wParam)==OutputInfo){SendMessageW(monitorTips,TTM_POPUP,0,0);return 0;}
        if(LOWORD(wParam)==SelectedEffectToggle&&HIWORD(wParam)==BN_CLICKED){toggleEffect(selected);return 0;}
        if(LOWORD(wParam)==ToggleWindowsHdr){toggleWindowsHdr();return 0;}
        if(LOWORD(wParam)==PresetChoice && HIWORD(wParam)==CBN_DROPDOWN){discoverPresets(presetRole());refreshPresetChoice();labels();return 0;}
        if(LOWORD(wParam)==Import1D || LOWORD(wParam)==Export1D){transfer1D(LOWORD(wParam)==Import1D);return 0;}
        if(LOWORD(wParam)==SettingsPage){settingsPage=!settingsPage;refreshColorFilters();layout();SetFocus(settingsPage?fontChoice:effectControl(selected));return 0;}
        if(LOWORD(wParam)==ColorFilterChoice&&HIWORD(wParam)==CBN_SELCHANGE){const auto choice=SendMessageW(filterChoice,CB_GETCURSEL,0,0);if(choice>=0)changeWindowsFilter(filterState.on,static_cast<DWORD>(choice),true);return 0;}
        if(LOWORD(wParam)==ColorFilterSettings){if(!renderOnly)try{windows_controls::openColorFilterSettings(window);}catch(const std::exception& error){filterMessage=ui::messages::error(error.what());filterError=true;labels();}return 0;}
        if(LOWORD(wParam)==StartWithWindows){
            if(!renderOnly){try{const bool enabled=SendMessageW(startupControl,BM_GETCHECK,0,0)==BST_CHECKED;app_settings::startWithWindows(app_settings::executable(),enabled);startupEnabled=enabled;presetMessage.clear();}catch(const std::exception& error){presetError(error);}}
            labels();return 0;
        }
        if(LOWORD(wParam)==StartMinimized&&HIWORD(wParam)==BN_CLICKED){
            const bool previous=startMinimized;startMinimized=SendMessageW(startMinimizedControl,BM_GETCHECK,0,0)==BST_CHECKED;
            if(!persistPresetChoices())startMinimized=previous;labels();return 0;
        }
        if(LOWORD(wParam)==FontChoice&&HIWORD(wParam)==CBN_SELCHANGE){wchar_t name[80]{};GetWindowTextW(fontChoice,name,80);ui::setFontFamily(name);persistPresetChoices();layout();profile_editor::refreshAppearance();return 0;}
        if(LOWORD(wParam)==CurveChannel&&HIWORD(wParam)==CBN_SELCHANGE){editingChannel=static_cast<int>(SendMessageW(curveChannel,CB_GETCURSEL,0,0));invalidateGraph();return 0;}
        if (LOWORD(wParam) == ApplicationProfiles) { showProfiles(); return 0; }
        if(LOWORD(wParam)==MonitorProfile&&HIWORD(wParam)==STN_CLICKED){showProfiles(renderOnly);return 0;}
        if(LOWORD(wParam)==WebsiteLink&&HIWORD(wParam)==STN_CLICKED){if(!renderOnly)ShellExecuteW(hwnd,L"open",L"https://CeeJay.dk",nullptr,nullptr,SW_SHOWNORMAL);return 0;}
        if (LOWORD(wParam) == RevertPreset) {
            auto& entry = currentPreset(presetRole()); entry.current = entry.saved;
            presetMessage.clear(); if (selected != 1) manualLookOverride();
            loadPresetControls(); refreshPresetChoice(); reconcileLive(); labels(); return 0;
        }
        if (LOWORD(wParam) == PresetChoice && HIWORD(wParam) == CBN_SELCHANGE) {
            const auto index = SendMessageW(presetChoice, CB_GETCURSEL, 0, 0);
            if (index >= 0 && static_cast<size_t>(index) < presetLibrary[presetRole()].size()) {
                const int role=presetRole();
                try{if(index>0)readPreset(role,presetLibrary[role][index].path);}
                catch(const std::exception& error){presetError(error);refreshPresetChoice();labels();return 0;}
                flushPresetAutosave(); presetIndex[role] = static_cast<size_t>(index);
                if (selected != 1) manualLookOverride();
                persistPresetChoices(); loadPresetControls(); refreshPresetChoice(); reconcileLive(); labels();
            }
            return 0;
        }
        if (LOWORD(wParam) == SavePreset) {
            if (currentPreset(presetRole()).path.empty()) presetFileDialog(true);
            else { saveExistingPreset(presetRole()); refreshPresetChoice(); labels(); }
            return 0;
        }
        if (LOWORD(wParam) == SavePresetAs) { presetFileDialog(true); return 0; }
        if (LOWORD(wParam) == OpenPreset) {const auto folder=presetFolder(0).parent_path();try{std::filesystem::create_directories(folder);ShellExecuteW(window,L"explore",folder.c_str(),nullptr,nullptr,SW_SHOWNORMAL);}catch(const std::exception& error){presetError(error);labels();}return 0;}
        if (LOWORD(wParam) == Autosave && HIWORD(wParam) == BN_CLICKED) {
            autosave = SendMessageW(autosaveControl, BM_GETCHECK, 0, 0) == BST_CHECKED;
            flushPresetAutosave(); persistPresetChoices(); refreshPresetChoice(); labels(); return 0;
        }
        if (LOWORD(wParam) == SetShortcut && HIWORD(wParam) == BN_CLICKED) {
            const WORD requested = static_cast<WORD>(SendMessageW(shortcutControl, HKM_GETHOTKEY, 0, 0));
            if (renderOnly) return 0;
            if (bindShortcut(requested)) {
                preferredShortcut = requested;
                hotkeyMessage = persistPresetChoices()
                    ? L"" : L"This shortcut works now, but could not be saved for next time.";
            } else {
                hotkeyMessage = hotkeyError == ERROR_INVALID_PARAMETER ? L"Choose a key with Ctrl, Alt or Shift. Windows reserves F12."
                    : L"That shortcut is unavailable. " + std::wstring(hotkeyRegistered ? L"Your previous shortcut still works." : L"Choose another combination.");
            }
            labels(); return 0;
        }
        if (LOWORD(wParam) == Exit) { if (allowExit()) DestroyWindow(hwnd); return 0; }
        if (LOWORD(wParam) == SourceChoice && HIWORD(wParam) == CBN_SELCHANGE){const auto index=selectedSourceIndex();hdrState=index>=0&&static_cast<size_t>(index)<sources.size()?hdr::inspect(sources[index].name):hdr::State{};reconcileLive();}
        if (LOWORD(wParam) == TestContrast && HIWORD(wParam) == BN_CLICKED) {
            toggleCorrections();
        }
        if (LOWORD(wParam) == Bypass && HIWORD(wParam) == BN_CLICKED) toggleBypass();
        if (LOWORD(wParam) == HdrFix && HIWORD(wParam) == BN_CLICKED) {
            currentPreset(1).current.applyInSdr = SendMessageW(hdrFixControl, BM_GETCHECK, 0, 0) == BST_CHECKED;
            presetEdited(1); applyLive(); labels();
        }
        if ((LOWORD(wParam) == Layers || LOWORD(wParam)==CorrectionLayers || LOWORD(wParam)==WindowsCorrectionLayers) && HIWORD(wParam) == LBN_SELCHANGE) { const auto list=LOWORD(wParam)==Layers?layers:LOWORD(wParam)==WindowsCorrectionLayers?windowsLayers:correctionLayers;const auto row = SendMessageW(list, LB_GETCURSEL, 0, 0);
            if (row >= 0) selected = static_cast<int>(SendMessageW(list, LB_GETITEMDATA, row, 0));
            refreshColorFilters();if(settingsPage){settingsPage=false;layout();}else{refreshPresetChoice(); labels();} }
        if ((LOWORD(wParam) == Gamma || LOWORD(wParam) == GammaOther) && HIWORD(wParam) == BN_CLICKED) {
            gammaPosition = LOWORD(wParam) == Gamma ? 0 : 1; presetEdited(1); applyLive(); labels();
        }
        if (LOWORD(wParam) == Export) exportCurve();
        if (LOWORD(wParam) == Reset && (selected<8||selected==9||selected>=10)) {
            if(selected==9){nightConfig.strength=35;nightConfig.fade=10;if(!renderOnly)persistPresetChoices();applyLive();labels();return 0;}
            if(selected==11)currentPreset(0).current.monochrome={21,72,7};
            else if(selected==10)currentPreset(0).current.saturation=100;
            else
            if (selected == 0) { contrastPosition = 0; pivotPosition = 50; SendMessageW(contrastSlider, TBM_SETPOS, TRUE, 0); SendMessageW(pivotSlider, TBM_SETPOS, TRUE, 50); }
            else if(selected>=2){auto& a=currentPreset(0).current.adjustments;const color::Adjustments defaults;
                if(selected==2)a.balance=defaults.balance;else if(selected==3)a.midtone=100;else if(selected==4)a.levels=defaults.levels;else if(selected==5){a.shadows=0;a.highlights=0;}else if(selected==6)a.curves={};else a.imported={};}
            else {
                whitePosition = 200; gammaPosition = 0; SendMessageW(whiteSlider, TBM_SETPOS, TRUE, 200);
                SendMessageW(gammaChoice, BM_SETCHECK, BST_CHECKED, 0); SendMessageW(gammaOther, BM_SETCHECK, BST_UNCHECKED, 0);
            }
            presetEdited(presetRole()); applyLive(); labels();
        }
        return 0;
    case WM_TIMECHANGE:
        if(!renderOnly){nightClock();applyLive();}return 0;
    case WM_DISPLAYCHANGE:
        curveScratch.reset();
        [[fallthrough]];
    case WM_SETTINGCHANGE:
    case WM_DEVICECHANGE:
        if (!renderOnly) {displayAttempts=0;SetTimer(hwnd, DisplayTimer, 500, nullptr);}
        return 0;
    case WM_TIMER:
        if(wParam==NightTimer){KillTimer(hwnd,NightTimer);applyLive();return 0;}
        if(wParam==ColorFilterTimer){try{if(!filterController.poll())return 0;filterMessage=filterController.notice();filterError=false;}catch(const std::exception& error){filterMessage=ui::messages::error(error.what());filterError=true;}KillTimer(hwnd,ColorFilterTimer);refreshColorFilters();syncMatrix(true);labels();return 0;}
        if(wParam==HdrTimer) {
            KillTimer(hwnd,HdrTimer);const auto state=hdr::inspect(hdrSource);
            if(state.available && state.on==hdrDesired && state.target==hdrTransition.target && state.adapter.HighPart==hdrTransition.adapter.HighPart && state.adapter.LowPart==hdrTransition.adapter.LowPart) {
                hdrPending=false;refreshSources();
                const auto index=selectedSourceIndex();
                if(hdrResume && index>=0 && static_cast<size_t>(index)<sources.size() && sources[index].name==hdrSource){correctionsRequested=true;reconcileLive();}
                else liveMessage=hdrDesired?L"Windows HDR is on.":L"Windows HDR is off.";
                hdrResume=false;labels();
            }else if(++hdrAttempts<6)SetTimer(hwnd,HdrTimer,600,nullptr);
            else{hdrPending=false;hdrResume=false;modeResetPending=false;refreshSources();liveMessage=L"MonitorMage could not confirm the HDR change. Check Windows display settings, then turn MonitorMage on again.";liveError=true;labels();}
            return 0;
        }
        if (wParam == ForegroundTimer) { KillTimer(hwnd, ForegroundTimer); inspectForeground(pendingForeground); syncMatrix(true);settleMatrix(); pendingForeground = nullptr; return 0; }
        if(wParam==MatrixRecoveryTimer){
            KillTimer(hwnd,MatrixRecoveryTimer);
            const auto state=matrixSession.state();
            if(!matrixWanted()||state==matrix_effect::State::OtherTool||state==matrix_effect::State::ExternalChange)return 0;
            syncMatrix(true);
            if(const auto delay=matrixRecovery.nextDelay())SetTimer(hwnd,MatrixRecoveryTimer,delay,nullptr);
            return 0;
        }
        if (wParam == PresetTimer) { flushPresetAutosave(); refreshPresetChoice(); labels(); return 0; }
        if (wParam == IdleTimer) {
            KillTimer(hwnd, IdleTimer);
            const double cpuMs = (processCpu() - idleCpuStart) / 10000.0;
            PROCESS_MEMORY_COUNTERS_EX memory{}; memory.cb = sizeof(memory);
            const bool measured = GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&memory), sizeof(memory)) != FALSE;
            {
                std::ofstream report(idleReport); report.imbue(std::locale::classic());
                report << "{\"elapsed_ms\":" << GetTickCount64() - idleStart << ",\"cpu_ms\":" << cpuMs
                       << ",\"working_set_mib\":" << memory.WorkingSetSize / 1048576.0
                       << ",\"private_commit_mib\":" << memory.PrivateUsage / 1048576.0
                       << ",\"page_faults\":" << memory.PageFaultCount-idleFaultStart
                       << ",\"memory_query_ok\":" << (measured ? "true" : "false")
                       << ",\"foreground_hook_ok\":" << (foregroundHook ? "true" : "false")
                       << ",\"mode\":\"hidden editor, corrections disabled, no recovery helper\"}\n";
            }
            DestroyWindow(hwnd); return 0;
        }
        if (wParam == ApplyTimer) { KillTimer(hwnd, ApplyTimer); applyLive(); }
        if (wParam == DisplayTimer) {
            KillTimer(hwnd, DisplayTimer); refreshSources();
            syncMatrix(true);
            settleMatrix();
            refreshColorFilters();
            if (resumeAfterWake) resumeAfterWake = false;
            reconcileLive(); labels();
        }
        return 0;
    case matrix_effect::Changed:
        if(matrixSession.state()==matrix_effect::State::OtherTool||matrixSession.state()==matrix_effect::State::ExternalChange)KillTimer(hwnd,MatrixRecoveryTimer);
        labels();return 0;
    case WM_ACTIVATE:
        if(LOWORD(wParam)!=WA_INACTIVE && filterChoice){refreshColorFilters();syncMatrix(true);labels();}
        break;
    case WM_HOTKEY: if (hotkeyRegistered && wParam == static_cast<WPARAM>(activeHotkeyId)) toggleBypass(); return 0;
    case ForegroundMessage:
        if (!renderOnly || !idleReport.empty()) { pendingForeground = reinterpret_cast<HWND>(lParam); SetTimer(hwnd, ForegroundTimer, 100, nullptr); }
        return 0;
    case WM_POWERBROADCAST:
        if (wParam == PBT_APMSUSPEND) {
            powerSuspended = true;
            resumeAfterWake = correctionsRequested;
            if (liveSession.active()) profilePausedIdentity = liveOutput.identity;
            disableLive(true); labels();
        }
        if (wParam == PBT_APMRESUMEAUTOMATIC || wParam == PBT_APMRESUMESUSPEND) {
            powerSuspended = false;
            if (resumeAfterWake) SetTimer(hwnd, DisplayTimer, 1500, nullptr);
        }
        return TRUE;
    case WM_CONTEXTMENU: if(renderOnly && reinterpret_cast<HWND>(wParam)==hwnd){trayMenu();return 0;}break;
    case WM_QUERYENDSESSION: flushPresetAutosave(); disableLive(); return TRUE;
    case TrayMessage:
        if (LOWORD(lParam) == NIN_SELECT || LOWORD(lParam) == NIN_KEYSELECT) showEditor();
        else if (LOWORD(lParam) == WM_CONTEXTMENU) trayMenu(); return 0;
    case WM_DPICHANGED: {
        dpi = HIWORD(wParam); const auto rect = reinterpret_cast<RECT*>(lParam);
        SetWindowPos(hwnd, nullptr, rect->left, rect->top, rect->right - rect->left, rect->bottom - rect->top, SWP_NOZORDER | SWP_NOACTIVATE);
        layout(); if(editorReady)ui::frame(hwnd); return 0;
    }
    case WM_SHOWWINDOW:
        if(wParam){EditorSync sync;ensureEditor();labels();}
        else curveScratch.reset();
        break;
    case WM_SIZE: if(wParam==SIZE_MINIMIZED)curveScratch.reset();if(editorReady)ui::frame(hwnd); return 0;
    case WM_CLOSE: if (trayInstalled) ShowWindow(hwnd, SW_HIDE); else if (allowExit()) DestroyWindow(hwnd); return 0;
    case WM_DESTROY:
        closing=true;
        KillTimer(hwnd,MatrixRecoveryTimer);
        // Request matrix restoration before waiting on independent LUT helpers.
        if(!renderOnly)matrixSession.stop();
        KillTimer(hwnd,NightTimer);nightOutputs.stop();
        curveScratch.reset();
        KillTimer(hwnd,ColorFilterTimer);
        if (foregroundHook) { UnhookWinEvent(foregroundHook); foregroundHook = nullptr; }
        if (profile_editor::window()) DestroyWindow(profile_editor::window());
        KillTimer(hwnd, ForegroundTimer);
        KillTimer(hwnd, ApplyTimer); KillTimer(hwnd, DisplayTimer); KillTimer(hwnd, IdleTimer); KillTimer(hwnd, PresetTimer); liveSession.stop();
        if (hotkeyRegistered) { UnregisterHotKey(hwnd, activeHotkeyId); hotkeyRegistered = false; }
        if (activeTest) { CloseHandle(activeTest); activeTest = nullptr; }
        if (trayInstalled) Shell_NotifyIconW(NIM_DELETE, &tray);
        if (hintFont) { DeleteObject(hintFont); hintFont=nullptr; }
        if (font) { DeleteObject(font); font = nullptr; } PostQuitMessage(0); return 0;
    }
    return DefWindowProcW(hwnd, message, wParam, lParam);
}
bool preparePresetUiTest() {
    try {
        wchar_t temporary[32768]{};
        if (!GetTempPathW(static_cast<DWORD>(std::size(temporary)), temporary)) return false;
        const auto folder = std::filesystem::path(temporary) / (L"LutPresetUi-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64()));
        if (!std::filesystem::create_directory(folder)) return false;
        testSettingsPath = (folder / L"MonitorMage.ini").wstring();
        std::filesystem::create_directories(presetFolder(0)); std::filesystem::create_directories(presetFolder(1));
        presets::Data a, b, c; a.contrast = 10; b.contrast = -10; c.kind = presets::Kind::Correction; c.gamma = 24; c.hdrFix = false;
        const auto look = presetFolder(0) / L"A.lutpreset", correction = presetFolder(1) / L"C.lutpreset";
        presets::write(look, a, false); presets::write(presetFolder(0) / L"B.lutpreset", b, false); presets::write(correction, c, false);
        return WritePrivateProfileStringW(L"Presets", L"Look", look.c_str(), testSettingsPath.c_str()) &&
            WritePrivateProfileStringW(L"Presets", L"Correction", correction.c_str(), testSettingsPath.c_str());
    } catch (...) { return false; }
}
int runPresetUiTest() {
    // Drive our own hidden controls. renderOnly prevents every live LUT operation.
    auto command = [&](int id, int notification = BN_CLICKED) { SendMessageW(window, WM_COMMAND, MAKEWPARAM(id, notification), 0); };
    auto switchLook = [&](const wchar_t* name) {
        SendMessageW(presetChoice, CB_SETCURSEL, findPreset(0, presetFolder(0) / name), 0);
        command(PresetChoice, CBN_SELCHANGE);
    };
    auto editContrast = [&](int value) {
        SendMessageW(contrastSlider, TBM_SETPOS, TRUE, value);
        SendMessageW(window, WM_HSCROLL, TB_THUMBPOSITION, reinterpret_cast<LPARAM>(contrastSlider));
    };
    const auto look = presetFolder(0) / L"A.lutpreset", correction = presetFolder(1) / L"C.lutpreset";
    try {
        if (contrastPosition != 10 || gammaPosition != 1 || hdrFixEnabled || autosave) return 10;
        for(bool minimized:{true,false}){
            SendMessageW(startMinimizedControl,BM_SETCHECK,minimized?BST_CHECKED:BST_UNCHECKED,0);command(StartMinimized);
            if(startMinimized!=minimized||GetPrivateProfileIntW(L"Startup",L"Minimized",2,shortcutPath().c_str())!=static_cast<UINT>(minimized))return 282;
            // Saving another preference must retain the startup choice.
            if(!persistPresetChoices()||GetPrivateProfileIntW(L"Startup",L"Minimized",2,shortcutPath().c_str())!=static_cast<UINT>(minimized))return 283;
        }
        const auto unopened=findPreset(0,presetFolder(0)/L"B.lutpreset");
        if(unopened==presetLibrary[0].size() || presetLibrary[0][unopened].loaded || presetLibrary[0][unopened].dirty())return 191;
        // An invalid unopened file must neither slow startup with parsing nor
        // replace the active draft when the user selects it later.
        const auto invalid=presetFolder(0)/L"Invalid.lutpreset";
        presets::writeBytes(invalid,"not a MonitorMage preset",false);discoverPresets(0);refreshPresetChoice();
        const auto badIndex=findPreset(0,invalid),originalIndex=presetIndex[0];const auto originalData=currentPreset(0).current;
        SendMessageW(presetChoice,CB_SETCURSEL,badIndex,0);command(PresetChoice,CBN_SELCHANGE);
        if(presetIndex[0]!=originalIndex || currentPreset(0).current!=originalData || presetLibrary[0][badIndex].loaded)return 192;
        std::filesystem::remove(invalid);presetLibrary[0].erase(presetLibrary[0].begin()+static_cast<std::ptrdiff_t>(badIndex));presetMessage.clear();refreshPresetChoice();
        editContrast(20);
        if (!currentPreset(0).dirty() || presets::read(look).contrast != 10) return 11;
        switchLook(L"B.lutpreset"); if (contrastPosition != -10 || !currentPreset(0).loaded) return 12;
        switchLook(L"A.lutpreset"); if (contrastPosition != 20 || !currentPreset(0).dirty()) return 13;
        command(RevertPreset); if (contrastPosition != 10 || currentPreset(0).dirty()) return 14;
        editContrast(23); savePresetAsPath(0, presetFolder(0) / L"Copy.lutpreset", false);
        if (currentPreset(0).dirty() || presets::read(currentPreset(0).path).contrast != 23) return 23;
        switchLook(L"A.lutpreset"); if (contrastPosition != 10 || currentPreset(0).dirty() || presets::read(look).contrast != 10) return 24;
        editContrast(20); command(SavePreset);
        if (presets::read(look).contrast != 20 || currentPreset(0).dirty()) return 15;
        SendMessageW(autosaveControl, BM_SETCHECK, BST_CHECKED, 0); command(Autosave);
        editContrast(15); SendMessageW(window, WM_TIMER, PresetTimer, 0);
        if (presets::read(look).contrast != 15 || currentPreset(0).dirty()) return 16;
        SendMessageW(correctionLayers, LB_SETCURSEL, effectRow(1), 0); command(CorrectionLayers, LBN_SELCHANGE);
        command(Gamma); SendMessageW(window, WM_TIMER, PresetTimer, 0);
        if (presets::read(correction).gamma != 22 || presets::read(look).contrast != 15) return 17;
        SendMessageW(autosaveControl, BM_SETCHECK, BST_UNCHECKED, 0); command(Autosave);
        toggleEffect(1);
        if (!currentPreset(1).dirty() || presets::read(correction).hdrFix) return 18;
        bool rejected = false; try { readPreset(0, correction); } catch (...) { rejected = true; }
        if (!rejected || contrastPosition != 15) return 19;
        // Simulate restoring settings in a new editor session using the saved configuration.
        presetLibrary = {}; presetIndex = {}; initializePresets();
        if (contrastPosition != 15 || gammaPosition != 0 || hdrFixEnabled || autosave) return 20;
        const auto unicodePath = presetFolder(0) / L"Farver-\u00e6\u00f8\u00e5.lutpreset";
        savePresetAsPath(0, unicodePath, false); preferredShortcut = MAKEWORD(VK_F10, HOTKEYF_CONTROL | HOTKEYF_SHIFT);
        if (!persistPresetChoices()) return 25;
        presetLibrary = {}; presetIndex = {}; initializePresets();
        if (currentPreset(0).path != unicodePath || contrastPosition != 15 ||
            GetPrivateProfileIntW(L"Hotkey", L"Bypass", 0, testSettingsPath.c_str()) != preferredShortcut) return 26;
        wchar_t stored[32768]{};
        GetPrivateProfileStringW(L"Presets", L"Look", L"", stored, static_cast<DWORD>(std::size(stored)), testSettingsPath.c_str());
        if (std::filesystem::path(stored).is_absolute()) return 27;
        // A fresh installation selects the bundled all-off presets. Saved
        // effect switches, including a mixture of on/off, win on later loads.
        for(int role=0;role<2;++role)presets::write(presetFolder(role)/L"Off.lutpreset",presets::inactive(presetKind(role)),false);
        WritePrivateProfileStringW(L"Presets",L"Look",nullptr,testSettingsPath.c_str());
        WritePrivateProfileStringW(L"Presets",L"Correction",nullptr,testSettingsPath.c_str());
        presetLibrary={};presetIndex={};initializePresets();
        for(int role=0;role<2;++role)if(currentPreset(role).current!=presets::inactive(presetKind(role))||currentPreset(role).path.filename()!=L"Off.lutpreset")return 150;
        if(contrastEnabled||hdrFixEnabled)return 151;
        auto& mixed=currentPreset(0).current;mixed.contrastEnabled=true;mixed.contrast=12;mixed.adjustments.balanceEnabled=true;mixed.adjustments.balance[3]=15;
        currentPreset(1).current.hdrFix=true;
        const auto savedMixed=mixed;
        if(!saveExistingPreset(0)||!saveExistingPreset(1)||!persistPresetChoices())return 152;
        presetLibrary={};presetIndex={};initializePresets();
        if(currentPreset(0).current!=savedMixed||!contrastEnabled||!hdrFixEnabled)return 153;
        SendMessageW(hdrFixControl,BM_SETCHECK,BST_CHECKED,0);command(HdrFix);
        if(!hdrFixEnabled || !currentPreset(1).current.applyInSdr || !currentSettings().applyInSdr || !currentPreset(1).dirty())return 163;
        if(!saveExistingPreset(1))return 164;
        presetLibrary={};presetIndex={};initializePresets();
        if(!hdrFixEnabled || !currentPreset(1).current.applyInSdr || SendMessageW(hdrFixControl,BM_GETCHECK,0,0)!=BST_CHECKED)return 165;
        const auto beforeFolderImport=currentPreset(1).current;
        presets::write(presetFolder(1)/L"Added from folder.lutpreset",presets::inactive(presets::Kind::Correction),false);
        SendMessageW(window,WM_COMMAND,MAKEWPARAM(PresetChoice,CBN_DROPDOWN),0);
        if(findPreset(1,presetFolder(1)/L"Added from folder.lutpreset")==presetLibrary[1].size() || currentPreset(1).current!=beforeFolderImport)return 168;
        if (liveSession.active()) return 21;
        return 0;
    } catch (...) { return 22; }
}
int runProfileUiTest() {
    using applications::Action;
    auto command = [&](int id, int event = BN_CLICKED) { SendMessageW(window, WM_COMMAND, MAKEWPARAM(id, event), 0); };
    auto chooseLook = [&](const wchar_t* name) {
        SendMessageW(presetChoice, CB_SETCURSEL, findPreset(0, presetFolder(0) / name), 0); command(PresetChoice, CBN_SELCHANGE);
    };
    auto indicator = [&] { wchar_t text[1024]{}; GetWindowTextW(activeLookLabel, text, static_cast<int>(std::size(text))); return std::wstring(text); };
    auto monitorIndicator = [&] { wchar_t text[1024]{}; GetWindowTextW(monitorProfile,text,static_cast<int>(std::size(text)));return std::wstring(text); };
    try {
        // The regular look may be an unsaved experiment; an app override must leave it intact.
        SendMessageW(contrastSlider, TBM_SETPOS, TRUE, 20);
        SendMessageW(window, WM_HSCROLL, TB_THUMBPOSITION, reinterpret_cast<LPARAM>(contrastSlider));
        presets::Data manual; manual.contrast = 23; presets::write(presetFolder(0) / L"Copy.lutpreset", manual, false);
        readPreset(0, presetFolder(0) / L"Copy.lutpreset");
        applications::Configuration config; config.enabled = true;
        config.rules = {{L"C:\\Tests\\Game.exe", presetFolder(0) / L"B.lutpreset", Action::Look},
            {L"C:\\Tests\\Movie.exe", {}, Action::Bypass}, {L"C:\\Tests\\HDR.exe", {}, Action::Disable},
            {L"C:\\Tests\\Second.exe", presetFolder(0) / L"A.lutpreset", Action::Look}};
        if (!applyProfileConfiguration(config)) return 30;
        acceptForeground(L"C:\\Tests\\Game.exe");
        if (contrastPosition != -10 || gammaPosition != 1 || hdrFixEnabled) return 31;
        if (indicator().find(L"Application profile: Game.exe") == std::wstring::npos || indicator().find(L" | B") == std::wstring::npos) return 48;
        if(monitorIndicator()!=L"Profile: Game.exe | B")return 215;
        if (!persistPresetChoices()) return 49;
        wchar_t storedLook[32768]{};
        GetPrivateProfileStringW(L"Presets", L"Look", L"", storedLook, static_cast<DWORD>(std::size(storedLook)), shortcutPath().c_str());
        if (std::filesystem::path(storedLook).filename() != L"A.lutpreset") return 50;
        chooseLook(L"Copy.lutpreset"); acceptForeground(L"c:\\tests\\GAME.EXE");
        if (contrastPosition != 23 || !appFocus.manual()) return 32;
        if(monitorIndicator()!=L"Desktop profile")return 216;
        if (indicator().find(L"Manual override: Game.exe") == std::wstring::npos || indicator().find(L"Copy") == std::wstring::npos) return 51;
        SendMessageW(correctionLayers, LB_SETCURSEL, effectRow(1), 0); command(CorrectionLayers, LBN_SELCHANGE);
        if (indicator().find(L"Manual override: Game.exe") == std::wstring::npos || indicator().find(L"Copy") == std::wstring::npos) return 52;
        SendMessageW(layers, LB_SETCURSEL, 0, 0); command(Layers, LBN_SELCHANGE);
        acceptForeground(L"C:\\Tests\\Other.exe");
        if (contrastPosition != 20 || appFocus.manual() || applicationContextActive || !currentPreset(0).dirty()) return 33;
        if(monitorIndicator()!=L"Desktop profile")return 217;
        if (indicator().find(L"Manual look | A *") == std::wstring::npos || presets::read(presetFolder(0) / L"A.lutpreset").contrast != 10) return 53;
        // Consecutive profiled programs must not replace the original return look.
        acceptForeground(L"C:\\Tests\\Game.exe"); acceptForeground(L"C:\\Tests\\Second.exe");
        if (contrastPosition != 20 || !applicationContextActive) return 54;
        acceptForeground(L"C:\\Tests\\Game.exe");
        acceptForeground(L"C:\\Tests\\Movie.exe");
        if (!currentSettings().bypass || profileAction != Action::Bypass || hdrFixEnabled || gammaPosition != 1) return 34;
        acceptForeground(L"C:\\Tests\\Other.exe");
        if (contrastPosition != 20 || currentSettings().bypass || applicationContextActive) return 55;
        acceptForeground(L"C:\\Tests\\Movie.exe");
        chooseLook(L"A.lutpreset");
        if (currentSettings().bypass || !appFocus.manual()) return 35;
        // Synthetic output only: renderOnly still forbids opening a LUT session.
        sources.clear(); live::Output output; output.identity = L"test identity"; output.name = L"test source"; output.supported = true; sources.push_back(output);
        SendMessageW(sourceChoice, CB_RESETCONTENT, 0, 0); SendMessageW(sourceChoice, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Test display")); SendMessageW(sourceChoice, CB_SETCURSEL, 0, 0);
        sources[0].hdr = true;
        if (iconState() != app_icons::State::DisabledHdr) return 57;
        sources[0].hdr = false;
        if (iconState() != app_icons::State::Disabled) return 58;
        acceptForeground(L"C:\\Tests\\HDR.exe"); toggleCorrections();
        if (!correctionsRequested || profileAction != Action::Disable || liveSession.active() || profilePausedIdentity != output.identity) return 36;
        acceptForeground(L"C:\\Tests\\Game.exe");
        if (!correctionsRequested || profileAction != Action::Look || !profilePausedIdentity.empty()) return 37;
        acceptForeground(L"C:\\Tests\\HDR.exe"); sources[0].identity = L"changed mode"; reconcileLive();
        if (!correctionsRequested || liveSession.active()) return 38;
        acceptForeground(L"C:\\Tests\\Game.exe");
        if (!correctionsRequested) return 39; // Mode changes retain the user's master-power request.
        powerSuspended = true; correctionsRequested = true; profilePausedIdentity = L"pre-suspend identity"; reconcileLive();
        if (!correctionsRequested || profilePausedIdentity != L"pre-suspend identity") return 47;
        powerSuspended = false; disableLive();
        const auto restored = applications::read(shortcutPath());
        if (!restored.enabled || restored.rules.size() != 4) return 40;
        showProfiles(true); HWND editor = profile_editor::window(); if (!editor) return 41;
        setText(GetDlgItem(editor, 204), L"C:\\Tests\\New.exe");
        SendMessageW(GetDlgItem(editor, 207), CB_SETCURSEL, 1, 0); // Bypass action.
        SendMessageW(editor, WM_COMMAND, MAKEWPARAM(209, BN_CLICKED), 0);
        if (SendMessageW(GetDlgItem(editor, 203), LB_GETCOUNT, 0, 0) != 5) return 42;
        SendMessageW(editor, WM_COMMAND, MAKEWPARAM(212, BN_CLICKED), 0);
        if (appConfig.rules.size() != 5 || appConfig.rules.back().action != Action::Bypass) return 43;
        SendMessageW(editor, WM_COMMAND, MAKEWPARAM(210, BN_CLICKED), 0);
        SendMessageW(editor, WM_COMMAND, MAKEWPARAM(212, BN_CLICKED), 0);
        if (appConfig.rules.size() != 4) return 44;
        SendMessageW(editor, WM_COMMAND, MAKEWPARAM(213, BN_CLICKED), 0);
        if (profile_editor::window() || liveSession.active() || correctionsRequested) return 45;
        // HDR-only pausing must leave the preset and creative look intact, including drafts.
        config.rules.push_back({L"C:\\Tests\\NativeHDR.exe", {}, Action::PauseHdr});
        if (!applyProfileConfiguration(config)) return 59;
        toggleEffect(1);
        const auto correctionBeforePause = currentPreset(1).current;
        const auto contrastBeforePause = contrastPosition;
        acceptForeground(L"C:\\Tests\\NativeHDR.exe");
        if (!hdrFixEnabled || currentSettings().hdrFix || currentSettings().bypass ||
            contrastPosition != contrastBeforePause || currentPreset(1).current != correctionBeforePause ||
            indicator().find(L"HDR gamma paused") == std::wstring::npos) return 60;
        toggleBypass();
        if (!currentSettings().bypass || currentSettings().hdrFix) return 61;
        toggleBypass();
        if (currentSettings().bypass || currentSettings().hdrFix) return 62;
        acceptForeground(L"C:\\Tests\\Other.exe");
        if (!currentSettings().hdrFix || contrastPosition != 20 || applicationContextActive) return 63;
        acceptForeground(L"C:\\Tests\\NativeHDR.exe");
        chooseLook(L"Copy.lutpreset");
        if (!currentSettings().hdrFix || !appFocus.manual() || contrastPosition != 23) return 64;
        acceptForeground(L"C:\\Tests\\Other.exe");
        // Exercise the actual editor and persisted action, not just policy resolution.
        showProfiles(true); editor = profile_editor::window(); if (!editor) return 65;
        setText(GetDlgItem(editor, 204), L"C:\\Tests\\EditorHDR.exe");
        SendMessageW(GetDlgItem(editor, 207), CB_SETCURSEL, 3, 0);
        SendMessageW(editor, WM_COMMAND, MAKEWPARAM(207, CBN_SELCHANGE), 0);
        if (IsWindowEnabled(GetDlgItem(editor, 208))) return 66;
        SendMessageW(editor, WM_COMMAND, MAKEWPARAM(209, BN_CLICKED), 0);
        SendMessageW(editor, WM_COMMAND, MAKEWPARAM(212, BN_CLICKED), 0);
        const auto hdrRules = applications::read(shortcutPath());
        if (hdrRules.rules.back().action != Action::PauseHdr) return 67;
        SendMessageW(editor, WM_COMMAND, MAKEWPARAM(213, BN_CLICKED), 0);
        // Turning off auto-selection releases the temporary application look too.
        auto disabled = config; disabled.enabled = false;
        if (!applyProfileConfiguration(disabled) || contrastPosition != 20 || applicationContextActive || lookBypassed()) return 56;
        return 0;
    } catch (...) { return 46; }
}
int runEffectUiTest() {
    const auto choose=[&](int role){auto list=effectControl(role);SendMessageW(list,LB_SETCURSEL,effectRow(role),0);SendMessageW(window,WM_COMMAND,MAKEWPARAM(GetDlgCtrlID(list),LBN_SELCHANGE),0);};
    const auto click=[&](int role){auto list=effectControl(role);const int row=effectRow(role);SendMessageW(list,LB_SETTOPINDEX,row,0);RECT r{};SendMessageW(list,LB_GETITEMRECT,row,reinterpret_cast<LPARAM>(&r));RECT client{};GetClientRect(list,&client);
        SendMessageW(list,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(client.right-px(18),(r.top+r.bottom)/2));SendMessageW(list,WM_LBUTTONUP,0,MAKELPARAM(client.right-px(18),(r.top+r.bottom)/2));};
    const auto edit=[&](size_t index,int value){SendMessageW(adjustmentSliders[index],TBM_SETPOS,TRUE,value);SendMessageW(window,WM_HSCROLL,TB_THUMBPOSITION,reinterpret_cast<LPARAM>(adjustmentSliders[index]));};
    try {
        if(correctionFirst||!contrastEnabled||selected!=0||effectOrder.size()!=8||effectOrder.back()!=1)return 70;
        // Real rapid input is DOWN, UP, DBLCLK, UP, not two DOWN messages.
        // Verify every press on every switch, including off-screen rows after
        // scrolling, and that each toggle selects its effect's controls.
        for(int effect=0;effect<8;++effect) {
            const auto enabled=[&](){return effect==0?contrastEnabled:effect==1?hdrFixEnabled:adjustmentEnabled(effect);};
            const bool initial=enabled();auto list=effectControl(effect);
            const int row=effectRow(effect);SendMessageW(list,LB_SETTOPINDEX,row,0);
            RECT bounds{},client{};SendMessageW(list,LB_GETITEMRECT,row,reinterpret_cast<LPARAM>(&bounds));GetClientRect(list,&client);
            const LPARAM at=MAKELPARAM(client.right-px(18),(bounds.top+bounds.bottom)/2);
            for(int press=0;press<12;++press) {
                SendMessageW(list,press%2?WM_LBUTTONDBLCLK:WM_LBUTTONDOWN,MK_LBUTTON,at);
                SendMessageW(list,WM_LBUTTONUP,0,at);
                if(enabled()!=(press%2==0?!initial:initial)||selected!=effect)return 161;
            }
            if((SendMessageW(selectedEffectToggle,BM_GETCHECK,0,0)==BST_CHECKED)!=initial)return 201;
            // The heading switch uses the same stage state as the sidebar,
            // retaining every parameter and leaving the other preset alone.
            const auto look=currentPreset(0).current,correction=currentPreset(1).current;
            SendMessageW(selectedEffectToggle,BM_CLICK,0,0);
            if(enabled()==initial||selected!=effect||(SendMessageW(selectedEffectToggle,BM_GETCHECK,0,0)==BST_CHECKED)!=enabled())return 202;
            if(effect==1?currentPreset(0).current!=look:currentPreset(1).current!=correction)return 203;
            SendMessageW(selectedEffectToggle,BM_CLICK,0,0);
            if(enabled()!=initial||currentPreset(0).current!=look||currentPreset(1).current!=correction)return 204;
        }
        const auto before=currentPreset(0).current;choose(1);click(0);
        if(selected!=0||contrastEnabled||currentSettings().contrast!=0||!currentPreset(0).dirty()||!(GetWindowLongPtrW(contrastSlider,GWL_STYLE)&WS_VISIBLE))return 71;
        click(0);if(!contrastEnabled||selected!=0||currentPreset(0).dirty())return 73;
        choose(0);SendMessageW(layers,WM_KEYDOWN,VK_SPACE,0);
        if(contrastEnabled||!saveExistingPreset(0)||presets::read(currentPreset(0).path).contrastEnabled)return 74;
        SendMessageW(contrastSlider,TBM_SETPOS,TRUE,17);SendMessageW(window,WM_HSCROLL,TB_THUMBPOSITION,reinterpret_cast<LPARAM>(contrastSlider));
        if(contrastEnabled||currentPreset(0).current.contrast!=17||currentSettings().contrast!=0)return 75;
        SendMessageW(layers,LB_SETTOPINDEX,0,0);
        SendMessageW(layers,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(px(12),px(effects::RowHeight/2)));
        SendMessageW(layers,WM_MOUSEMOVE,MK_LBUTTON,MAKELPARAM(px(12),px(effects::RowHeight*3/2)));
        SendMessageW(layers,WM_LBUTTONUP,0,MAKELPARAM(px(12),px(effects::RowHeight*3/2)));
        if(effectRow(0)!=1||effectOrder.front()!=2||selected!=0)return 76;
        moveEffect(1,0);if(!correctionFirst||effectOrder.front()!=1||selected!=0)return 77;
        choose(1);const bool hdr=hdrFixEnabled;click(1);if(selected!=1||hdrFixEnabled==hdr||currentPreset(1).current.hdrFix!=hdrFixEnabled)return 78;
        for(int i=0;i<7;++i)SendMessageW(correctionLayers,WM_SYSKEYDOWN,VK_DOWN,0);
        if(!correctionFirst||effectOrder.front()!=1||selected!=1)return 79;
        // New effects retain disabled parameters, own their role, and survive save/load.
        choose(2);click(2);edit(0,85);
        if(selected!=2||presetRole()!=0||currentPreset(0).current.adjustments.balanceEnabled||currentPreset(0).current.adjustments.balance[0]!=85)return 84;
        choose(4);edit(0,95);edit(1,5);
        if(currentPreset(0).current.adjustments.levels[1]-currentPreset(0).current.adjustments.levels[0]<5)return 85;
        SendMessageW(window,WM_COMMAND,Reset,0);
        choose(6);editingChannel=1;const auto plot=curvePlot(canvas);
        const LPARAM at=MAKELPARAM((plot.left+plot.right)/2,plot.bottom-static_cast<int>((plot.bottom-plot.top)*.57));
        SendMessageW(canvas,WM_LBUTTONDOWN,MK_LBUTTON,at);SendMessageW(canvas,WM_LBUTTONUP,0,at);
        const auto& points=currentPreset(0).current.adjustments.curves[1];
        if(points.size()!=9||points[4]<=.5||sliderDragging||draggedPoint!=-1)return 86;
        const auto otherPoints=points;
        SendMessageW(canvas,WM_LBUTTONDBLCLK,MK_LBUTTON,at);SendMessageW(canvas,WM_LBUTTONUP,0,at);
        const auto& resetPoints=currentPreset(0).current.adjustments.curves[1];
        if(resetPoints[4]!=.5||sliderDragging||draggedPoint!=-1)return 275;
        for(size_t i=0;i<resetPoints.size();++i)if(i!=4&&resetPoints[i]!=otherPoints[i])return 276;
        currentPreset(0).current.adjustments.imported=lut1d::decode("LUT_1D_SIZE 2\n0 0 0\n1 .9 .8\n");
        if(!saveExistingPreset(0))return 87;const auto saved=currentPreset(0).current;const auto savedOrder=effectOrder;
        if(presets::read(currentPreset(0).path)!=saved||!persistPresetChoices())return 88;
        presetLibrary={};presetIndex={};effectOrder={0,2,3,4,5,6,7,1};initializePresets();labels();
        if(currentPreset(0).current!=saved||effectOrder!=savedOrder||contrastEnabled)return 81;
        SendMessageW(window,WM_COMMAND,SettingsPage,0);if(!settingsPage||!(GetWindowLongPtrW(layers,GWL_STYLE)&WS_VISIBLE)||(GetWindowLongPtrW(title,GWL_STYLE)&WS_VISIBLE))return 89;
        SendMessageW(fontChoice,CB_SETCURSEL,1,0);SendMessageW(window,WM_COMMAND,MAKEWPARAM(FontChoice,CBN_SELCHANGE),0);
        if(wcscmp(ui::fontFamily(),L"Segoe UI"))return 146;
        SendMessageW(window,WM_COMMAND,SettingsPage,0);if(settingsPage)return 147;
        for(int role:{0,1,2,3,4,5,9,10,11}){
            selected=role;labels();const auto fields=parameters();
            std::vector<HWND> sliders;
            if(role==0)sliders={contrastSlider,pivotSlider};else if(role==1)sliders={whiteSlider};
            else for(size_t i=0;i<fields.size();++i)sliders.push_back(adjustmentSliders[i]);
            for(HWND slider:sliders){
                std::vector<int> previousValues;for(const auto& field:fields)previousValues.push_back(*field.value);
                const int baseline=static_cast<int>(reinterpret_cast<INT_PTR>(GetPropW(slider,L"LutSliderDefault"))-32769);
                const int maximum=static_cast<int>(SendMessageW(slider,TBM_GETRANGEMAX,0,0));
                SendMessageW(slider,TBM_SETPOS,TRUE,baseline==maximum?baseline-1:baseline+1);
                SendMessageW(slider,WM_RBUTTONDOWN,MK_RBUTTON,0);SendMessageW(slider,WM_RBUTTONUP,0,0);
                if(SendMessageW(slider,TBM_GETPOS,0,0)!=baseline)return 265;
                for(size_t i=0;i<fields.size();++i)if(*fields[i].value!=(slider==adjustmentSliders[i]?baseline:previousValues[i]))return 267;
                if(role==0&&(slider==contrastSlider?contrastPosition:pivotPosition)!=baseline)return 268;
                if(role==1&&whitePosition!=baseline)return 269;
                SendMessageW(slider,TBM_SETPOS,TRUE,baseline==maximum?baseline-1:baseline+1);
                SendMessageW(slider,WM_LBUTTONDBLCLK,MK_LBUTTON,0);SendMessageW(slider,WM_LBUTTONUP,0,0);
                if(SendMessageW(slider,TBM_GETPOS,0,0)!=baseline)return 274;
                RECT thumb{};SendMessageW(slider,TBM_GETTHUMBRECT,0,reinterpret_cast<LPARAM>(&thumb));
                const LPARAM sliderPoint=MAKELPARAM((thumb.left+thumb.right)/2,(thumb.top+thumb.bottom)/2);
                SendMessageW(slider,WM_LBUTTONDOWN,MK_LBUTTON,sliderPoint);SendMessageW(slider,WM_LBUTTONUP,0,sliderPoint);
                SendMessageW(slider,TBM_SETPOS,TRUE,baseline==maximum?baseline-1:baseline+1);
                SendMessageW(slider,WM_LBUTTONDOWN,MK_LBUTTON,sliderPoint);SendMessageW(slider,WM_LBUTTONUP,0,sliderPoint);
                if(SendMessageW(slider,TBM_GETPOS,0,0)!=baseline)return 277;
            }
        }
        return liveSession.active()||correctionsRequested?82:0;
    }catch(...){return 83;}
}

LRESULT CALLBACK countSurfaceChanges(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR, DWORD_PTR data) {
    if (msg == WM_SETTEXT || msg == WM_ENABLE || msg == BM_SETCHECK || msg == CB_RESETCONTENT || msg == LB_SETCURSEL || msg == LB_SETITEMHEIGHT)
        ++*reinterpret_cast<int*>(data);
    return DefSubclassProc(hwnd, msg, wp, lp);
}
int runTabUiTest() {
    const auto savedSelected=selected;const bool savedPage=settingsPage;const auto savedFocus=GetFocus();
    BYTE savedKeys[256]{},keys[256]{};GetKeyboardState(savedKeys);SetKeyboardState(keys);int result=0;
    for(int page=0;page<11&&!result;++page){
        settingsPage=page==8;selected=settingsPage?0:page>=9?page+1:page;labels();
        const auto originalLook=currentPreset(0).current,originalCorrection=currentPreset(1).current;
        if(((GetWindowLongPtrW(resetButton,GWL_STYLE)&WS_VISIBLE)!=0)==settingsPage||
           ((GetWindowLongPtrW(selectedEffectToggle,GWL_STYLE)&WS_VISIBLE)!=0)==settingsPage){result=205;break;}
        if(!settingsPage){RECT heading{},reset{},toggle{};GetWindowRect(title,&heading);GetWindowRect(resetButton,&reset);GetWindowRect(selectedEffectToggle,&toggle);
            if((heading.top+heading.bottom)/2!=reset.top+px(14)||(toggle.top+toggle.bottom)/2!=reset.top+px(14)){result=206;break;}}
        const auto order=tabStops();
        if(order.empty()||order.front()!=correctionLayers||order.back()!=monitorProfile || nextTabStop(layers,false)!=presetChoice
            ||nextTabStop(presetChoice,false)!=autosaveControl||nextTabStop(autosaveControl,false)!=savePresetButton||nextTabStop(savePresetButton,false)!=saveAsButton){result=180;break;}
        const auto firstInput=settingsPage?fontChoice:selected==0?contrastSlider:selected==1?(IsWindowEnabled(whiteSlider)?whiteSlider:gammaPosition==0?gammaChoice:gammaOther):(selected<6||selected>=10)?adjustmentSliders[0]:selected==6?curveChannel:importButton;
        const auto lastToolbar=IsWindowEnabled(revertPresetButton)?revertPresetButton:exportButton;
        if(settingsPage?nextTabStop(lastToolbar,false)!=firstInput:
            (nextTabStop(lastToolbar,false)!=selectedEffectToggle||nextTabStop(selectedEffectToggle,false)!=resetButton||nextTabStop(resetButton,false)!=firstInput)){result=181;break;}
        for(size_t i=0;i<order.size();++i){
            if(nextTabStop(order[i],false)!=order[(i+1)%order.size()]||nextTabStop(order[i],true)!=order[(i+order.size()-1)%order.size()]){result=182;break;}
            if(!settingsPage && (order[i]==fontChoice||order[i]==shortcutControl||order[i]==startupControl)){result=183;break;}
            if(order[i]==canvas && (settingsPage||selected!=6)){result=184;break;}
        }
        // Exercise the actual key route on hidden native controls. No global
        // keyboard input or display changes are sent by this test.
        if(!result){
            SetFocus(order.front());MSG message{};message.message=WM_KEYDOWN;message.wParam=VK_TAB;
            for(size_t step=1;step<=order.size();++step){message.hwnd=GetFocus();if(!navigateTab(message)||GetFocus()!=order[step%order.size()]){result=185;break;}}
            keys[VK_SHIFT]=keys[VK_LSHIFT]=0x80;SetKeyboardState(keys);
            for(size_t step=1;step<=order.size()&&!result;++step){message.hwnd=GetFocus();if(!navigateTab(message)||GetFocus()!=order[(order.size()-step)%order.size()]){result=186;break;}}
            keys[VK_SHIFT]=keys[VK_LSHIFT]=0;SetKeyboardState(keys);
        }
        if(!result && (currentPreset(0).current!=originalLook || currentPreset(1).current!=originalCorrection))result=187;
    }
    settingsPage=savedPage;selected=savedSelected;labels();SetKeyboardState(savedKeys);if(savedFocus)SetFocus(savedFocus);
    return result;
}
bool partialGraphMatchesFull(){
    RECT bounds{};GetClientRect(canvas,&bounds);BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth=bounds.right;info.bmiHeader.biHeight=-bounds.bottom;info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;info.bmiHeader.biCompression=BI_RGB;
    HDC dc=CreateCompatibleDC(nullptr);void* raw=nullptr;HBITMAP bitmap=CreateDIBSection(dc,&info,DIB_RGB_COLORS,&raw,nullptr,0);
    if(!dc||!bitmap){if(bitmap)DeleteObject(bitmap);if(dc)DeleteDC(dc);return false;}
    const auto old=SelectObject(dc,bitmap);auto pixels=static_cast<uint32_t*>(raw);const size_t count=static_cast<size_t>(bounds.right)*bounds.bottom;
    const auto oldLook=currentPreset(0).current;const int oldPosition=contrastPosition;const bool oldEnabled=contrastEnabled,oldError=liveError;const auto oldMessage=liveMessage;
    SendMessageW(canvas,WM_PRINTCLIENT,reinterpret_cast<WPARAM>(dc),PRF_CLIENT);GdiFlush();const auto fullBytes=lastGraphPaintBytes;bool okay=true;
    for(int stage=0;stage<3;++stage){
        ValidateRect(canvas,nullptr);
        if(stage==0){contrastPosition=15;contrastEnabled=true;currentPreset(0).current.contrast=15;currentPreset(0).current.contrastEnabled=true;}
        else{liveError=stage==1;liveMessage=stage==1?L"This is a deliberately long warning used to verify that wrapped graph messages are fully erased when the warning is cleared.":oldMessage;}
        labels();HRGN dirty=CreateRectRgn(0,0,0,0);
        // Hidden native windows may discard their update region. In that
        // fixture case, replay the exact rectangles requested by the painter.
        if(GetUpdateRgn(canvas,dirty,FALSE)<=NULLREGION)for(unsigned i=0;i<graphAuditCount;++i){HRGN part=CreateRectRgnIndirect(&graphAuditDamage[i]);CombineRgn(dirty,dirty,part,RGN_OR);DeleteObject(part);}
        const int saved=SaveDC(dc);SelectClipRgn(dc,dirty);
        SendMessageW(canvas,WM_PRINTCLIENT,reinterpret_cast<WPARAM>(dc),PRF_CLIENT);GdiFlush();if(stage==0&&lastGraphPaintBytes>=fullBytes)okay=false;
        RestoreDC(dc,saved);DeleteObject(dirty);std::vector<uint32_t> partial(pixels,pixels+count);
        SendMessageW(canvas,WM_PRINTCLIENT,reinterpret_cast<WPARAM>(dc),PRF_CLIENT);GdiFlush();
        for(size_t i=0;i<count;++i)if((partial[i]&0xffffff)!=(pixels[i]&0xffffff))okay=false;
    }
    currentPreset(0).current=oldLook;contrastPosition=oldPosition;contrastEnabled=oldEnabled;liveError=oldError;liveMessage=oldMessage;labels();
    SelectObject(dc,old);DeleteObject(bitmap);DeleteDC(dc);return okay;
}
bool clippedListMatchesFull(){
    RECT bounds{},dirty{};GetClientRect(layers,&bounds);if(SendMessageW(layers,LB_GETITEMRECT,1,reinterpret_cast<LPARAM>(&dirty))==LB_ERR)return false;
    InflateRect(&dirty,-px(4),-px(3));
    BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);info.bmiHeader.biWidth=bounds.right;info.bmiHeader.biHeight=-bounds.bottom;
    info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;info.bmiHeader.biCompression=BI_RGB;
    HDC dc=CreateCompatibleDC(nullptr);void* pixels=nullptr;HBITMAP bitmap=CreateDIBSection(dc,&info,DIB_RGB_COLORS,&pixels,nullptr,0);
    if(!dc||!bitmap){if(bitmap)DeleteObject(bitmap);if(dc)DeleteDC(dc);return false;}
    const auto old=SelectObject(dc,bitmap);SendMessageW(layers,WM_PRINTCLIENT,reinterpret_cast<WPARAM>(dc),PRF_CLIENT);GdiFlush();
    const size_t count=static_cast<size_t>(bounds.right)*bounds.bottom;auto values=static_cast<uint32_t*>(pixels);std::vector<uint32_t> reference(values,values+count);
    std::fill(values,values+count,0);IntersectClipRect(dc,dirty.left,dirty.top,dirty.right,dirty.bottom);
    SendMessageW(layers,WM_PRINTCLIENT,reinterpret_cast<WPARAM>(dc),PRF_CLIENT);GdiFlush();bool same=true;
    for(int y=0;y<bounds.bottom;++y)for(int x=0;x<bounds.right;++x){const size_t at=static_cast<size_t>(y)*bounds.right+x;
        const bool inside=x>=dirty.left&&x<dirty.right&&y>=dirty.top&&y<dirty.bottom;
        if((values[at]&0xffffff)!=(inside?reference[at]&0xffffff:0))same=false;}
    SelectObject(dc,old);DeleteObject(bitmap);DeleteDC(dc);return same;
}
int runSurfaceUiTest() {
    if(!clippedListMatchesFull())return 255;
    if(!partialGraphMatchesFull())return 260;
    for(HWND control:{presetChoice,autosaveControl,sourceChoice,fontChoice,shortcutControl,shortcutButton,startupControl,openPresetButton,bypassButton,filterChoice,filterSettingsButton,saturationReportButton,contrastSlider,pivotSlider,whiteSlider,gammaChoice,gammaOther,hdrFixControl,curveChannel,importButton,export1DButton})
        if(!*ui::helpText(control)||wcslen(ui::helpText(control))>48)return 233;
    for(int effect=0;effect<10;++effect)if(!*ui::helpText(effectControl(effect),effect+1)||wcslen(ui::helpText(effectControl(effect),effect+1))>48||!*ui::helpText(effectControl(effect),100+effect))return 234;
    const int savedSelected=selected;const auto savedLook=currentPreset(0).current,savedCorrection=currentPreset(1).current;const auto savedFilter=filterState;
    for(int role:{8,9}){
        SendMessageW(windowsLayers,LB_SETCURSEL,role-8,0);SendMessageW(window,WM_COMMAND,MAKEWPARAM(WindowsCorrectionLayers,LBN_SELCHANGE),0);
        if(selected!=role||settingsPage||(GetWindowLongPtrW(presetChoice,GWL_STYLE)&WS_VISIBLE)||(role==8&&(GetWindowLongPtrW(resetButton,GWL_STYLE)&WS_VISIBLE)))return 235;
        const auto order=tabStops();if(std::find(order.begin(),order.end(),windowsLayers)==order.end()||std::find(order.begin(),order.end(),presetChoice)!=order.end())return 236;
        if(role==9&&!(GetWindowLongPtrW(selectedEffectToggle,GWL_STYLE)&WS_VISIBLE))return 237;
        if(role==8&&!(GetWindowLongPtrW(filterChoice,GWL_STYLE)&WS_VISIBLE))return 238;
    }
    if(currentPreset(0).current!=savedLook||currentPreset(1).current!=savedCorrection||filterState.on!=savedFilter.on||filterState.filter!=savedFilter.filter||filterController.busy())return 239;
    const auto savedNight=nightConfig;const bool savedBypass=bypassed;
    selected=9;nightConfig.enabled=false;nightConfig.scheduled=false;labels();
    for(const auto& pair:{std::pair<HWND,HWND>{nightStart,nightStartLabel},{nightEnd,nightEndLabel}}){
        RECT outer{},client{},label{};GetWindowRect(pair.first,&outer);GetClientRect(pair.first,&client);GetWindowRect(pair.second,&label);
        POINT origin{};ClientToScreen(pair.first,&origin);
        const int top=origin.y-outer.top,bottom=outer.bottom-origin.y-client.bottom;
        if(top<=0||std::abs(top-bottom)>1||std::abs((outer.top+outer.bottom)-(label.top+label.bottom))>2)return 279;
        wchar_t original[16]{},edited[16]{};GetWindowTextW(pair.first,original,16);SetWindowTextW(pair.first,L"21:00");
        SendMessageW(pair.first,EM_SETSEL,0,2);SendMessageW(pair.first,EM_REPLACESEL,FALSE,reinterpret_cast<LPARAM>(L"22"));GetWindowTextW(pair.first,edited,16);
        SetWindowTextW(pair.first,original);if(wcscmp(edited,L"22:00"))return 280;
    }
    SendMessageW(window,WM_COMMAND,SelectedEffectToggle,0);
    SendMessageW(adjustmentSliders[0],TBM_SETPOS,TRUE,70);SendMessageW(window,WM_HSCROLL,TB_ENDTRACK,reinterpret_cast<LPARAM>(adjustmentSliders[0]));
    if(!nightConfig.enabled||nightConfig.strength!=70||currentPreset(0).current!=savedLook||currentSettings().nightWarmth!=.7)return 270;
    bypassed=true;if(currentSettings().nightWarmth!=.7)return 271;
    SendMessageW(adjustmentSliders[0],WM_RBUTTONUP,0,0);if(nightConfig.strength!=35)return 272;
    SetWindowTextW(nightStart,L"24:99");SendMessageW(window,WM_COMMAND,MAKEWPARAM(NightStart,EN_KILLFOCUS),reinterpret_cast<LPARAM>(nightStart));
    if(nightConfig.start!=savedNight.start||nightMessage.empty())return 273;
    nightConfig=savedNight;bypassed=savedBypass;nightMessage.clear();nightClock();labels();
    selected=savedSelected;labels();
    // New global effect uses the same preset and keyboard/UI contracts, but
    // hidden UI tests must never start a hardware worker.
    selected=10;settingsPage=false;labels();
    if(effectControl(10)!=layers||effectRow(10)!=7||!*ui::helpText(layers,11)||parameters().size()!=1)return 247;
    if(!(GetWindowLongPtrW(presetChoice,GWL_STYLE)&WS_VISIBLE)||!(GetWindowLongPtrW(resetButton,GWL_STYLE)&WS_VISIBLE))return 248;
    const auto beforeMatrix=currentPreset(0).current;
    SendMessageW(window,WM_COMMAND,SelectedEffectToggle,0);
    if(currentPreset(0).current.saturationEnabled==beforeMatrix.saturationEnabled||matrixSession.state()!=matrix_effect::State::Off)return 249;
    SendMessageW(adjustmentSliders[0],TBM_SETPOS,FALSE,50);SendMessageW(window,WM_HSCROLL,TB_ENDTRACK,reinterpret_cast<LPARAM>(adjustmentSliders[0]));
    if(currentPreset(0).current.saturation!=50||matrixSession.state()!=matrix_effect::State::Off)return 250;
    SendMessageW(adjustmentSliders[0],TBM_SETPOS,FALSE,color::SaturationMaximum);SendMessageW(window,WM_HSCROLL,TB_ENDTRACK,reinterpret_cast<LPARAM>(adjustmentSliders[0]));
    if(currentPreset(0).current.saturation!=color::SaturationMaximum)return 252;
    SendMessageW(window,WM_COMMAND,Reset,0);if(currentPreset(0).current.saturation!=100)return 251;
    selected=11;labels();if(parameters().size()!=3||effectRow(11)!=8)return 288;
    SendMessageW(window,WM_COMMAND,SelectedEffectToggle,0);
    if(currentPreset(0).current.monochromeEnabled==beforeMatrix.monochromeEnabled||matrixSession.state()!=matrix_effect::State::Off)return 289;
    SendMessageW(adjustmentSliders[0],TBM_SETPOS,FALSE,90);SendMessageW(window,WM_HSCROLL,TB_ENDTRACK,reinterpret_cast<LPARAM>(adjustmentSliders[0]));
    if(currentPreset(0).current.monochrome[0]!=90)return 290;
    SendMessageW(window,WM_COMMAND,Reset,0);if(currentPreset(0).current.monochrome!=std::array<int,3>{21,72,7})return 291;
    currentPreset(0).current=beforeMatrix;selected=savedSelected;labels();
    showProfiles(true);const auto helpEditor=profile_editor::window();if(!helpEditor)return 240;
    for(int id:{201,203,204,205,206,207,208,209,210,211,212,213,214,215,216,217,218,219,220})if(!*ui::helpText(GetDlgItem(helpEditor,id))||wcslen(ui::helpText(GetDlgItem(helpEditor,id)))>48)return 241;
    DestroyWindow(helpEditor);
    // Simulate Windows shrinking our fixed layout without changing the real
    // display mode. A mode-change notification must restore the intended size.
    RECT before{};GetWindowRect(window,&before);SetWindowPos(window,nullptr,0,0,px(400),px(260),SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE);
    if(!ui::windowNeedsSizeRepair(window))return 242;
    ui::revealControl(window,sourceChoice);
    const auto scroll=ui::viewportOffset(window);
    RECT visibleControl{},viewport{};GetWindowRect(sourceChoice,&visibleControl);MapWindowPoints(nullptr,window,reinterpret_cast<POINT*>(&visibleControl),2);GetClientRect(window,&viewport);
    if(scroll.y<=0||visibleControl.top<0||visibleControl.bottom>viewport.bottom||visibleControl.left<0||visibleControl.right>viewport.right)return 284;
    SendMessageW(window,WM_DISPLAYCHANGE,32,MAKELPARAM(640,480));
    const auto deadline=GetTickCount64()+1500;while(ui::windowNeedsSizeRepair(window)&&GetTickCount64()<deadline){MSG event{};while(PeekMessageW(&event,nullptr,0,0,PM_REMOVE)){TranslateMessage(&event);DispatchMessageW(&event);}Sleep(5);}
    RECT after{};GetWindowRect(window,&after);if(after.right-after.left!=before.right-before.left||after.bottom-after.top!=before.bottom-before.top)return 243;
    if(ui::viewportOffset(window).x||ui::viewportOffset(window).y)return 285;
    SetWindowPos(window,nullptr,0,0,px(400),px(260),SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE);SendMessageW(window,WM_SYSCOMMAND,SC_RESTORE,0);
    GetWindowRect(window,&after);if(after.right-after.left!=before.right-before.left||after.bottom-after.top!=before.bottom-before.top)return 244;
    const SIZE preferred{1200,900};const RECT low{0,0,640,480},high{-1920,-100,1920,2060};
    const auto small=ui::fitWindowRect({300,200,1500,1100},preferred,low);if(small.left||small.top||small.right!=640||small.bottom!=480)return 245;
    const auto normalSize=ui::fitWindowRect(small,preferred,high);if(normalSize.right-normalSize.left!=1200||normalSize.bottom-normalSize.top!=900)return 246;
    // Verify real page/font clearance, including dropdown and button pages.
    for(int role:{0,1,2,3,4,5,6,7,8,9,10,11}){
        selected=role;settingsPage=false;labels();RECT textBounds{};GetWindowRect(description,&textBounds);
        HWND first=role==0?contrastSlider:role==1?whiteSlider:role==6?curveChannel:role==7?importButton:role==8?filterChoice:adjustmentSliders[0];
        RECT controlBounds{};GetWindowRect(first,&controlBounds);
        if(controlBounds.top-textBounds.bottom<px(6))return 253;
        wchar_t words[1024]{};GetWindowTextW(description,words,1024);RECT measured{0,0,textBounds.right-textBounds.left,0};
        HDC dc=GetDC(description);const auto prior=SelectObject(dc,font);DrawTextW(dc,words,-1,&measured,DT_CALCRECT|DT_WORDBREAK|DT_NOPREFIX);SelectObject(dc,prior);ReleaseDC(description,dc);
        if(measured.bottom>textBounds.bottom-textBounds.top)return 254;
    }
    selected=savedSelected;labels();
    const auto tabResult=runTabUiTest();if(tabResult)return tabResult;
    const bool priorPower=correctionsRequested;
    selected=8;labels();
    if(!(GetWindowLongPtrW(filterChoice,GWL_STYLE)&WS_VISIBLE)||IsWindowEnabled(selectedEffectToggle)||IsWindowEnabled(filterChoice))return 224;
    SendMessageW(window,WM_COMMAND,SelectedEffectToggle,0);
    if(filterController.busy()||correctionsRequested!=priorPower)return 225;
    SendMessageW(window,WM_COMMAND,SettingsPage,0);
    if(!settingsPage||(GetWindowLongPtrW(filterChoice,GWL_STYLE)&WS_VISIBLE)||!(GetWindowLongPtrW(fontChoice,GWL_STYLE)&WS_VISIBLE))return 226;
    SendMessageW(window,WM_COMMAND,SettingsPage,0);selected=savedSelected;labels();
    // Exercise master startup in the read-only harness, including both power
    // indicators. Test mode cannot begin a hardware session.
    startMaster();
    if(!correctionsRequested||liveSession.active()||!GetPropW(powerButton,L"LutMonitorOn")||!IsWindowEnabled(sourceChoice))return 193;
    if(wcsstr(ui::captionTooltip(window,102),L"off")==nullptr)return 194;
    toggleCorrections();if(correctionsRequested)return 195;
    // Display settling retries keep master power on, stop at a fixed limit,
    // and cancel cleanly when the user turns master power off.
    correctionsRequested=true;modeResetPending=true;displayAttempts=0;
    for(int i=0;i<10;++i)if(!waitForDisplay() || !correctionsRequested)return 208;
    if(waitForDisplay() || displayAttempts!=11)return 209;
    disableLive(true);if(!correctionsRequested || !modeResetPending)return 210;
    disableLive();if(correctionsRequested || displayRetryPending || modeResetPending || displayAttempts)return 211;
    for(int hit:{100,101,102,HTHELP,HTMINBUTTON,HTCLOSE,HTSYSMENU})if(!ui::captionTooltip(window,hit)||!*ui::captionTooltip(window,hit))return 196;
    if(wcscmp(ui::captionTooltip(window,HTHELP),ui::wording::Settings)||wcscmp(ui::captionTooltip(window,101),ui::wording::ApplicationProfiles))return 231;
    for(HWND button:{monitorSettingsButton,profilesButton,monitorProfile}){
        NMTTDISPINFOW tip{};tip.hdr.hwndFrom=monitorTips;tip.hdr.idFrom=reinterpret_cast<UINT_PTR>(button);tip.hdr.code=TTN_GETDISPINFOW;
        SendMessageW(window,WM_NOTIFY,0,reinterpret_cast<LPARAM>(&tip));
        if(!tip.lpszText||wcscmp(tip.lpszText,button==monitorSettingsButton?ui::wording::Settings:ui::wording::ApplicationProfiles))return 232;
    }
    SendMessageW(window,WM_NCMOUSEMOVE,100,0);
    const auto captionTip=reinterpret_cast<HWND>(GetPropW(window,L"LutCaptionTooltip"));
    if(!captionTip||SendMessageW(captionTip,TTM_GETTOOLCOUNT,0,0)!=1)return 197;
    for(HWND tip:{captionTip,monitorTips}){
        LOGFONTW tipFont{};GetObjectW(reinterpret_cast<HFONT>(SendMessageW(tip,WM_GETFONT,0,0)),sizeof(tipFont),&tipFont);
        if(tipFont.lfHeight!=-MulDiv(ui::style::SecondaryTextSize,GetDpiForWindow(tip),96))return 262;
        if(SendMessageW(tip,TTM_GETTIPBKCOLOR,0,0)!=ui::Surface||SendMessageW(tip,TTM_GETTIPTEXTCOLOR,0,0)!=ui::Text)return 263;
        SendMessageW(tip,TTM_SETTIPBKCOLOR,GetSysColor(COLOR_INFOBK),0);
        SendMessageW(tip,WM_THEMECHANGED,0,0);
        if(SendMessageW(tip,TTM_GETTIPBKCOLOR,0,0)!=ui::Surface)return 264;
        SetWindowPos(tip,nullptr,0,0,px(120),px(24),SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE);
        HRGN region=CreateRectRgn(0,0,0,0);const bool rounded=GetWindowRgn(tip,region)!=ERROR&&!PtInRegion(region,0,0)&&PtInRegion(region,px(60),px(12));DeleteObject(region);
        if(!rounded)return 266;
        if(!ui::tooltipEdgeSelfTest(tip))return 278;
    }
    // Owner-drawn links and status labels may share a DC during native painting.
    // Neither their colors nor their pixels may escape the supplied item bounds.
    {
        HDC screen=GetDC(canvas),dc=CreateCompatibleDC(screen);
        HBITMAP bitmap=CreateCompatibleBitmap(screen,64,32);const auto old=SelectObject(dc,bitmap);
        RECT all{0,0,64,32};FillRect(dc,&all,reinterpret_cast<HBRUSH>(GetStockObject(WHITE_BRUSH)));
        const COLORREF sentinel=RGB(19,71,123);SetTextColor(dc,sentinel);SetBkMode(dc,OPAQUE);
        bool isolated=true;
        for(HWND control:{websiteLink,monitorMode,monitorProfile}){
            DRAWITEMSTRUCT item{};item.CtlType=ODT_STATIC;item.hwndItem=control;item.hDC=dc;item.rcItem={8,8,56,24};
            SendMessageW(canvas,WM_DRAWITEM,GetDlgCtrlID(control),reinterpret_cast<LPARAM>(&item));
            isolated=isolated&&GetTextColor(dc)==sentinel&&GetBkMode(dc)==OPAQUE&&GetPixel(dc,0,0)==RGB(255,255,255);
        }
        SelectObject(dc,old);DeleteObject(bitmap);DeleteDC(dc);ReleaseDC(canvas,screen);
        if(!isolated)return 281;
    }
    SendMessageW(window,WM_NCMOUSELEAVE,0,0);
    if(GetPropW(window,L"LutCaptionTipHit"))return 198;
    const auto previousLive=liveMessage,previousPresetMessage=presetMessage;
    liveMessage=ui::messages::error("Driver rejected the correction");liveError=true;presetMessage=L"Preset saved";labels();
    if(!statusError)return 199;
    wchar_t failureText[256]{};GetWindowTextW(status,failureText,256);if(std::wstring(failureText)!=ui::messages::error("Driver rejected the correction"))return 203;
    // A wrapped warning must actually paint below the old one-row boundary.
    liveMessage=L"The previous correction carried into this display mode. Restore Windows calibration before re-enabling";labels();
    {
        RECT bounds{};GetClientRect(canvas,&bounds);const auto g=monitorGeometry(canvas);
        HDC screen=GetDC(canvas),dc=CreateCompatibleDC(screen);
        HBITMAP image=CreateCompatibleBitmap(screen,bounds.right,bounds.bottom);const auto old=SelectObject(dc,image);
        SendMessageW(canvas,WM_PRINTCLIENT,reinterpret_cast<WPARAM>(dc),PRF_CLIENT);
        bool secondLine=false;
        for(int y=g.top+g.size;y<std::min(g.bottom,g.top+px(64));++y)
            for(int x=g.left+g.size+px(12);x<g.right-px(12);++x)
                if(GetPixel(dc,x,y)==ui::Error)secondLine=true;
        SelectObject(dc,old);DeleteObject(image);DeleteDC(dc);ReleaseDC(canvas,screen);
        wchar_t accessible[256]{};GetWindowTextW(canvas,accessible,256);
        if(!secondLine||liveMessage!=accessible)return 207;
    }
    liveMessage=previousLive;presetMessage=previousPresetMessage;liveError=false;labels();if(statusError)return 200;
    RECT client{}, outer{};GetClientRect(window,&client);GetWindowRect(window,&outer);
    if(client.right != px(ClientWidth) || client.bottom != px(ClientHeight)) return 90;
    const auto hit = [&](int x,int y) { return SendMessageW(window,WM_NCHITTEST,0,MAKELPARAM(x,y)); };
    if(hit(outer.right-px(20),outer.top+px(18))!=HTCLOSE ||
       hit(outer.right-px(65),outer.top+px(18))!=HTMINBUTTON ||
       hit(outer.left+px(150),outer.top+px(18))!=HTCAPTION) return 91;
    if(hit(outer.right-px(164),outer.top+px(18))!=HTHELP || GetParent(powerButton)!=canvas || GetParent(hdrButton)!=canvas || GetParent(profilesButton)!=canvas)return 154;
    POINT gear{outer.right-px(164),outer.top+px(18)};ScreenToClient(window,&gear);
    const auto command=[&](){MSG event{};return PeekMessageW(&event,window,WM_COMMAND,WM_COMMAND,PM_REMOVE)&&LOWORD(event.wParam)==SettingsPage;};
    SendMessageW(window,WM_NCLBUTTONDOWN,HTHELP,0);SendMessageW(window,WM_LBUTTONUP,0,MAKELPARAM(px(250),px(100)));if(command())return 155;
    SendMessageW(window,WM_NCLBUTTONDOWN,HTHELP,0);SendMessageW(window,WM_LBUTTONUP,0,MAKELPARAM(gear.x,gear.y));if(!command())return 156;
    SendMessageW(window,WM_SYSCHAR,L's',0);if(!command())return 158;
    SendMessageW(window,WM_SYSCOMMAND,0x1F00,0);if(!command())return 159;
    const std::array<ui::CaptionAction,3> captionTest{{{ToggleWindowsHdr,false,true},{ApplicationProfiles,false,true},{TestContrast,false,true}}};
    if(GetParent(monitorSettingsButton)!=canvas)return 172;
    SendMessageW(canvas,WM_COMMAND,SettingsPage,0);if(!settingsPage)return 173;
    SendMessageW(canvas,WM_COMMAND,SettingsPage,0);if(settingsPage)return 174;
    ui::captionActions(window,captionTest);
    for(int i=0;i<3;++i){POINT at{outer.right-px(i==2?118:244-i*40),outer.top+px(18)};if(hit(at.x,at.y)!=100+i)return 170;ScreenToClient(window,&at);SendMessageW(window,WM_NCLBUTTONDOWN,100+i,0);SendMessageW(window,WM_LBUTTONUP,0,MAKELPARAM(at.x,at.y));MSG event{};if(!PeekMessageW(&event,window,WM_COMMAND,WM_COMMAND,PM_REMOVE)||LOWORD(event.wParam)!=captionTest[i].command)return 171;}
    labels();
    BYTE previousKeys[256]{},keys[256]{};GetKeyboardState(previousKeys);keys[VK_SHIFT]=keys[VK_LSHIFT]=0x80;
    SetKeyboardState(keys);SendMessageW(shortcutControl,WM_KEYDOWN,VK_SHIFT,0);SendMessageW(shortcutControl,WM_KEYDOWN,VK_F20,0);SendMessageW(shortcutControl,WM_KEYUP,VK_F20,0);
    keys[VK_SHIFT]=keys[VK_LSHIFT]=0;SetKeyboardState(keys);SendMessageW(shortcutControl,WM_KEYUP,VK_SHIFT,0);
    const WORD captured=static_cast<WORD>(SendMessageW(shortcutControl,HKM_GETHOTKEY,0,0));SetKeyboardState(previousKeys);
    SendMessageW(shortcutControl,HKM_SETHOTKEY,activeShortcut,0);
    if(captured!=MAKEWORD(VK_F20,HOTKEYF_SHIFT))return 172;
    // Render-only input must never reach a real Windows HDR or LUT write.
    SendMessageW(canvas,WM_COMMAND,ToggleWindowsHdr,0);if(hdrPending||liveSession.active())return 160;
    const auto savedHdrState=hdrState;
    const auto hdrMenuItem=[](){for(const auto& item:trayItems())if(item.id==ToggleWindowsHdr)return item;return ui::MenuItem{};};
    hdrState.available=true;hdrState.on=false;
    if(!hdrMenuItem().shortcut.empty() || hdrMenuItem().checked || hdrMenuItem().glyph!=ui::Glyph::Sun || !hdrMenuItem().enabled)return 218;
    hdrState.on=true;
    if(!hdrMenuItem().shortcut.empty() || !hdrMenuItem().checked || !hdrMenuItem().enabled)return 219;
    hdrPending=true;
    if(hdrMenuItem().shortcut!=L"SWITCHING" || hdrMenuItem().enabled)return 220;
    hdrPending=false;hdrState.available=false;
    if(hdrMenuItem().enabled)return 221;
    hdrState=savedHdrState;
    const bool editorVisible=IsWindowVisible(window)!=FALSE;
    SendMessageW(window,TrayMessage,0,MAKELPARAM(NIN_SELECT,1));
    if(hdrPending || liveSession.active() || !IsWindowVisible(window))return 222;
    if(!editorVisible)ShowWindow(window,SW_HIDE);
    bool unavailableRejected=false;try{hdr::set(hdr::State{},true);}catch(const std::runtime_error&){unavailableRejected=true;}if(!unavailableRejected)return 169;
    RECT preview{},text{};GetWindowRect(canvas,&preview);GetWindowRect(status,&text);
    RECT display{};GetWindowRect(sourceChoice,&display);
    RECT hint{},mode{};GetWindowRect(orderHint,&hint);GetWindowRect(monitorProfile,&mode);
    if(mode.top!=display.top||mode.bottom!=display.bottom)return 190;
    RECT effectBounds{};GetWindowRect(layers,&effectBounds);
    if(hint.left!=effectBounds.left||hint.right!=effectBounds.right||hint.top!=effectBounds.bottom+px(6))return 223;
    const auto geometry=monitorGeometry(canvas);
    if(std::abs((display.top+display.bottom)-(2*preview.top+geometry.body.bottom+geometry.foot.bottom))>1)return 220;
    LOGFONTW hintStyle{},normalStyle{};
    GetObjectW(reinterpret_cast<HFONT>(SendMessageW(orderHint,WM_GETFONT,0,0)),sizeof(hintStyle),&hintStyle);GetObjectW(font,sizeof(normalStyle),&normalStyle);
    if(std::abs(hintStyle.lfHeight)>=std::abs(normalStyle.lfHeight))return 221;
    {RECT area{};GetClientRect(layers,&area);const int originalHeight=area.bottom;
        SetWindowPos(layers,nullptr,0,0,area.right,originalHeight+px(effects::RowHeight),SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE);GetClientRect(layers,&area);
        HDC screen=GetDC(layers),buffer=CreateCompatibleDC(screen);HBITMAP image=CreateCompatibleBitmap(screen,area.right,area.bottom);auto old=SelectObject(buffer,image);
        SendMessageW(layers,WM_PRINTCLIENT,reinterpret_cast<WPARAM>(buffer),PRF_CLIENT);const bool darkTail=GetPixel(buffer,area.right/2,area.bottom-2)==ui::Sidebar;
        SelectObject(buffer,old);DeleteObject(image);DeleteDC(buffer);ReleaseDC(layers,screen);
        SetWindowPos(layers,nullptr,0,0,area.right,originalHeight,SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE);if(!darkTail)return 222;}
    if(GetParent(sourceChoice)!=canvas || !(GetWindowLongPtrW(sourceChoice,GWL_STYLE)&WS_VISIBLE) || display.bottom>preview.bottom || display.right>preview.left+(preview.right-preview.left)/2-px(57))return 175;
    if(GetParent(status)!=canvas || preview.right-preview.left!=px(558)||preview.bottom-preview.top!=px(268)||text.left<=preview.left||text.right>=preview.right||text.top<=preview.top||text.bottom>=preview.bottom)return 97;
    if((GetWindowLongPtrW(activeLookLabel,GWL_STYLE)&WS_VISIBLE)||(GetWindowLongPtrW(openPresetButton,GWL_STYLE)&WS_VISIBLE)||(GetWindowLongPtrW(settingsButton,GWL_STYLE)&WS_VISIBLE))return 167;
    // Updating or focusing any balance slider must not erase an adjacent
    // slider's thumb. Check actual HWND bounds, including each value label.
    const auto alignedRow=[&](HWND label,HWND value,HWND slider){
        RECT name{},number{},channel{};GetWindowRect(label,&name);GetWindowRect(value,&number);
        SendMessageW(slider,TBM_GETCHANNELRECT,0,reinterpret_cast<LPARAM>(&channel));
        MapWindowPoints(slider,nullptr,reinterpret_cast<POINT*>(&channel),2);
        return name.top==number.top && name.bottom==number.bottom &&
            std::abs((name.top+name.bottom)/2-(channel.top+channel.bottom)/2)<=1;
    };
    if(!alignedRow(amountLabel,amountValue,contrastSlider)||!alignedRow(pivotLabel,pivotValue,pivotSlider)||!alignedRow(whiteLabel,whiteValue,whiteSlider))return 188;
    for(size_t i=0;i<adjustmentSliders.size();++i) {
        if(!alignedRow(adjustmentLabels[i],adjustmentValues[i],adjustmentSliders[i]))return 189;
        RECT slider{},label{},overlap{};GetWindowRect(adjustmentSliders[i],&slider);
        for(size_t j=0;j<adjustmentSliders.size();++j) {
            GetWindowRect(adjustmentLabels[j],&label);
            if(IntersectRect(&overlap,&slider,&label))return 148;
            if(i!=j) {RECT other{};GetWindowRect(adjustmentSliders[j],&other);
                if(IntersectRect(&overlap,&slider,&other))return 149;}
        }
    }
    // First input changes the clean/dirty indicator. Subsequent slider input must
    // not reset unrelated controls or rebuild the preset dropdown.
    selected=0; SendMessageW(contrastSlider,TBM_SETPOS,TRUE,-1);
    SendMessageW(window,WM_HSCROLL,TB_THUMBPOSITION,reinterpret_cast<LPARAM>(contrastSlider));
    int mutations=0;
    const HWND watched[]{savePresetButton, saveAsButton, openPresetButton, revertPresetButton, profilesButton,
        bypassButton, hdrFixControl, resetButton, exportButton, presetChoice, layers, window};
    for (HWND control : watched) SetWindowSubclass(control,countSurfaceChanges,73,reinterpret_cast<DWORD_PTR>(&mutations));
    for (int i=-2;i>=-25;--i) {
        SendMessageW(contrastSlider,TBM_SETPOS,TRUE,i);
        SendMessageW(window,WM_HSCROLL,TB_THUMBPOSITION,reinterpret_cast<LPARAM>(contrastSlider));
    }
    for (HWND control : watched) RemoveWindowSubclass(control,countSurfaceChanges,73);
    if (mutations) return 98;
    // Drag across the saved value repeatedly: data/preview stay live, but
    // the dropdown, caption marker and effect selection stay unchanged.
    autosave=true;
    mutations=0;
    for (HWND control : watched) SetWindowSubclass(control,countSurfaceChanges,73,reinterpret_cast<DWORD_PTR>(&mutations));
    for (int position : {10,12,10,12}) {
        SendMessageW(contrastSlider,TBM_SETPOS,TRUE,position);
        SendMessageW(window,WM_HSCROLL,TB_THUMBTRACK,reinterpret_cast<LPARAM>(contrastSlider));
        if (currentPreset(0).current.contrast!=position || currentPreset(0).dirty()!=(position!=10)) return 140;
        flushPresetAutosave();
        if (presets::read(currentPreset(0).path).contrast!=10) return 141;
    }
    for (HWND control : watched) RemoveWindowSubclass(control,countSurfaceChanges,73);
    if (mutations || !sliderDragging) return 142;
    SendMessageW(contrastSlider,TBM_SETPOS,TRUE,10);
    SendMessageW(window,WM_HSCROLL,TB_ENDTRACK,reinterpret_cast<LPARAM>(contrastSlider));
    wchar_t marker[1024]{};GetWindowTextW(presetChoice,marker,1024);
    if (sliderDragging || currentPreset(0).dirty() || wcschr(marker,L'*')) return 143;
    SendMessageW(contrastSlider,TBM_SETPOS,TRUE,12);
    SendMessageW(window,WM_HSCROLL,TB_THUMBTRACK,reinterpret_cast<LPARAM>(contrastSlider));
    GetWindowTextW(presetChoice,marker,1024);if (wcschr(marker,L'*')) return 144;
    SendMessageW(contrastSlider,TBM_SETPOS,TRUE,13);
    SendMessageW(window,WM_HSCROLL,TB_THUMBPOSITION,reinterpret_cast<LPARAM>(contrastSlider));
    GetWindowTextW(presetChoice,marker,1024);
    if (sliderDragging || !currentPreset(0).dirty() || !wcschr(marker,L'*')) return 145;
    autosave=false;KillTimer(window,PresetTimer);
    // Exercise the synchronous native-button custom-draw route as well as WM_PAINT.
    HDC screen=GetDC(window), buffer=CreateCompatibleDC(screen);
    HBITMAP bitmap=CreateCompatibleBitmap(screen,px(125),px(30));const auto old=SelectObject(buffer,bitmap);
    NMCUSTOMDRAW draw{};draw.hdr.hwndFrom=resetButton;draw.hdr.code=NM_CUSTOMDRAW;draw.dwDrawStage=CDDS_PREPAINT;draw.hdc=buffer;
    LRESULT result=0;const bool handled=ui::message(window,WM_NOTIFY,0,reinterpret_cast<LPARAM>(&draw),result);
    const auto pixel=GetPixel(buffer,px(3),px(14));
    DRAWITEMSTRUCT item{};item.CtlType=ODT_COMBOBOX;item.hwndItem=curveChannel;item.itemID=0;item.itemState=ODS_SELECTED;item.hDC=buffer;item.rcItem={0,0,px(125),px(30)};
    LRESULT comboResult=0;if(!ui::message(window,WM_DRAWITEM,0,reinterpret_cast<LPARAM>(&item),comboResult)||GetPixel(buffer,px(124),px(14))!=RGB(66,71,79))return 201;
    COMBOBOXINFO comboInfo{sizeof(comboInfo)};if(!GetComboBoxInfo(curveChannel,&comboInfo))return 202;
    SetWindowPos(comboInfo.hwndList,nullptr,0,0,px(125),px(30),SWP_NOACTIVATE|SWP_NOZORDER);
    SendMessageW(comboInfo.hwndList,WM_PRINT,reinterpret_cast<WPARAM>(buffer),PRF_NONCLIENT|PRF_CLIENT);
    if(GetPixel(buffer,0,px(14))!=ui::FocusBorder)return 204;
    HRGN popupShape=CreateRectRgn(0,0,0,0);const bool roundedPopup=GetWindowRgn(comboInfo.hwndList,popupShape)==COMPLEXREGION&&!PtInRegion(popupShape,0,0)&&PtInRegion(popupShape,px(10),px(10));
    RECT popupBounds{};GetWindowRect(comboInfo.hwndList,&popupBounds);const int popupWidth=popupBounds.right-popupBounds.left,popupHeight=popupBounds.bottom-popupBounds.top;
    HDC cornerDC=CreateCompatibleDC(screen);HBITMAP cornerBitmap=CreateCompatibleBitmap(screen,popupWidth,popupHeight);const auto cornerOld=SelectObject(cornerDC,cornerBitmap);
    SendMessageW(comboInfo.hwndList,WM_PRINT,reinterpret_cast<WPARAM>(cornerDC),PRF_NONCLIENT|PRF_CLIENT);
    bool sameCornerInk=true;
    for(int inset=0;inset<=px(ui::style::CornerRadius)+1;++inset)for(int y:{inset,popupHeight-1-inset}){
        int left=0,right=popupWidth-1;while(left<popupWidth&&!PtInRegion(popupShape,left,y))++left;while(right>=0&&!PtInRegion(popupShape,right,y))--right;
        if(left>right||GetPixel(cornerDC,left,y)!=ui::FocusBorder||GetPixel(cornerDC,right,y)!=ui::FocusBorder)sameCornerInk=false;
    }
    SelectObject(cornerDC,cornerOld);DeleteObject(cornerBitmap);DeleteDC(cornerDC);
    DeleteObject(popupShape);if(!sameCornerInk)return 224;
    if(!roundedPopup)return 210;
    // Rendering power state must not alter the stored enabled flag.
    const bool checked=SendMessageW(selectedEffectToggle,BM_GETCHECK,0,0)==BST_CHECKED;
    const bool powered=GetPropW(selectedEffectToggle,L"LutToggleUnpowered")==nullptr;
    ui::toggleButton(selectedEffectToggle,true,false);SendMessageW(selectedEffectToggle,WM_PRINTCLIENT,reinterpret_cast<WPARAM>(buffer),PRF_CLIENT);
    const bool grayOn=GetPixel(buffer,px(12),px(14))==ui::Border&&SendMessageW(selectedEffectToggle,BM_GETCHECK,0,0)==BST_CHECKED;
    ui::toggleButton(selectedEffectToggle,true,true);SendMessageW(selectedEffectToggle,WM_PRINTCLIENT,reinterpret_cast<WPARAM>(buffer),PRF_CLIENT);
    const bool blueOn=GetPixel(buffer,px(12),px(14))==ui::Accent;
    ui::toggleButton(selectedEffectToggle,checked,powered);
    if(!grayOn||!blueOn)return 211;
    SendMessageW(shortcutButton,WM_PRINTCLIENT,reinterpret_cast<WPARAM>(buffer),PRF_CLIENT);const auto normalFill=GetPixel(buffer,px(10),px(5));
    SendMessageW(shortcutButton,WM_MOUSEMOVE,0,MAKELPARAM(px(10),px(5)));SendMessageW(shortcutButton,WM_PRINTCLIENT,reinterpret_cast<WPARAM>(buffer),PRF_CLIENT);
    const bool sameHoverFill=GetPixel(buffer,px(10),px(5))==normalFill;SendMessageW(shortcutButton,WM_MOUSELEAVE,0,0);
    if(!sameHoverFill)return 212;
    HRGN inputShape=CreateRectRgn(0,0,0,0);const bool roundedShortcut=GetWindowRgn(shortcutControl,inputShape)==COMPLEXREGION&&!PtInRegion(inputShape,0,0);DeleteObject(inputShape);
    if(!roundedShortcut)return 213;
    bool rgbOk=true;
    for(int mask=1;mask<8;++mask) {
        RECT area{0,0,px(125),px(30)};FillRect(buffer,&area,ui::background());plot::Paths paths;
        for(int c=0;c<3;++c)if(mask&(1<<c))paths[c]={{px(4),px(8)},{px(24),px(8)}};
        const COLORREF expected=RGB(mask&1?255:0,mask&2?255:0,mask&4?255:0);
        if(!plot::rgb(buffer,px(125),px(30),paths,px(2)) || GetPixel(buffer,px(12),px(8))!=expected || GetPixel(buffer,px(12),px(20))!=ui::Background)rgbOk=false;
    }
    RECT area{0,0,px(125),px(30)};FillRect(buffer,&area,ui::background());plot::Paths diagonal;diagonal[0]={{px(4),px(4)},{px(24),px(15)}};
    if(!plot::rgb(buffer,px(125),px(30),diagonal,px(2)))rgbOk=false;
    bool partial=false;for(int y=px(4);y<px(20);++y)for(int x=px(4);x<px(24);++x){const auto value=GetPixel(buffer,x,y);if(GetRValue(value)>GetRValue(ui::Background)+20 && GetRValue(value)<240)partial=true;}
    rgbOk=rgbOk&&partial;
    if(!plot::rgb(buffer,px(125),px(30),diagonal,px(2),&curveScratch)||!curveScratch.bytes()||curveScratch.bytes()>1048576)return 256;
    SendMessageW(window,WM_SHOWWINDOW,FALSE,0);if(curveScratch.bytes())return 257;
    if(!plot::rgb(buffer,px(125),px(30),diagonal,px(2),&curveScratch))return 258;
    SendMessageW(window,WM_SIZE,SIZE_MINIMIZED,0);if(curveScratch.bytes())return 259;
    SelectObject(buffer,old);DeleteObject(bitmap);DeleteDC(buffer);ReleaseDC(window,screen);
    if (!handled || result!=CDRF_SKIPDEFAULT || pixel!=ui::Surface) return 99;
    if(!rgbOk)return 162;
    const int popup = ui::popupSelfTest(window); if(popup) return 100+popup;
    const bool captionMenuOkay=ui::previewCaptionMenu(window,[&](HWND menu){
        const HWND list=GetDlgItem(menu,10);const auto count=SendMessageW(list,LB_GETCOUNT,0,0);bool close=false,power=false,maximize=false;
        for(int i=0;i<count;++i){const auto id=SendMessageW(list,LB_GETITEMDATA,i,0);close|=id==SC_CLOSE;power|=id==0x1F30;
            if(id==SC_MAXIMIZE){maximize=true;SendMessageW(list,LB_SETCURSEL,i,0);SendMessageW(list,WM_KEYDOWN,VK_RETURN,0);if(!IsWindow(menu))return false;}}
        return close&&power&&maximize;
    });
    if(!captionMenuOkay)return 219;
    const int unsavedResult=unsaved::selfTest(window);if(unsavedResult)return unsavedResult;
    SendMessageW(canvas,WM_COMMAND,MAKEWPARAM(MonitorProfile,STN_CLICKED),reinterpret_cast<LPARAM>(monitorProfile));
    HWND editor=profile_editor::window();if(!editor)return 92;
    if(!SendMessageW(editor,WM_GETICON,ICON_SMALL,0))return 227;
    const auto pathBefore=[]{wchar_t text[32768]{};GetWindowTextW(GetDlgItem(profile_editor::window(),204),text,32768);return std::wstring(text);}();
    profile_editor::previewPrograms({{L"C:\\Video\\Player.exe",L"Video player",1024*1024*400,true,true},{L"C:\\Games\\Game.exe",L"Example game",1024*1024*800,true,true}});
    if(!(GetWindowLongPtrW(GetDlgItem(editor,215),GWL_STYLE)&WS_VISIBLE))return 228;
    SendMessageW(editor,WM_COMMAND,220,0);
    wchar_t currentPath[32768]{};GetWindowTextW(GetDlgItem(editor,204),currentPath,32768);
    if(pathBefore!=currentPath)return 229;
    profile_editor::previewPrograms({{L"C:\\Games\\Game.exe",L"Example game",1024*1024*800,true,true}});
    SendMessageW(editor,WM_COMMAND,219,0);GetWindowTextW(GetDlgItem(editor,204),currentPath,32768);
    if(wcscmp(currentPath,L"C:\\Games\\Game.exe")!=0)return 230;
    HRGN editShape=CreateRectRgn(0,0,0,0);const bool roundedEdit=GetWindowRgn(GetDlgItem(editor,204),editShape)==COMPLEXREGION&&!PtInRegion(editShape,0,0);DeleteObject(editShape);
    if(!roundedEdit)return 214;
    GetClientRect(editor,&client);GetWindowRect(editor,&outer);
    if(client.right!=px(420)||client.bottom!=px(526))return 93;
    const auto profileBounds=outer;
    SetWindowPos(editor,nullptr,0,0,px(300),px(260),SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE);
    ui::revealControl(editor,GetDlgItem(editor,213));
    if(ui::viewportOffset(editor).y<=0)return 286;
    SetWindowPos(editor,nullptr,0,0,profileBounds.right-profileBounds.left,profileBounds.bottom-profileBounds.top,SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE);
    if(ui::viewportOffset(editor).x||ui::viewportOffset(editor).y)return 287;
    if(SendMessageW(editor,WM_NCHITTEST,0,MAKELPARAM(outer.right-px(65),outer.top+px(18)))!=HTCAPTION)return 94;
    POINT close{outer.right-px(20),outer.top+px(18)};ScreenToClient(editor,&close);
    SendMessageW(editor,WM_NCLBUTTONDOWN,HTCLOSE,0);
    SendMessageW(editor,WM_LBUTTONUP,0,MAKELPARAM(close.x,close.y));
    MSG message{};while(PeekMessageW(&message,editor,WM_SYSCOMMAND,WM_SYSCOMMAND,PM_REMOVE))DispatchMessageW(&message);
    if(profile_editor::window())return 95;
    const std::wstring previousFamily=ui::fontFamily();
    SendMessageW(fontChoice,CB_SETCURSEL,1,0);SendMessageW(window,WM_COMMAND,MAKEWPARAM(FontChoice,CBN_SELCHANGE),0);
    for(HWND control:{monitorMode,monitorProfile}){
        LOGFONTW style{};auto handle=reinterpret_cast<HFONT>(SendMessageW(control,WM_GETFONT,0,0));
        if(handle!=font||!GetObjectW(handle,sizeof(style),&style)||wcscmp(style.lfFaceName,L"Segoe UI"))return 218;
    }
    ui::setFontFamily(previousFamily.c_str());layout();
    return liveSession.active()||correctionsRequested?96:0;
}
void cleanPresetUiTest() {
    // Remove only the exact fixtures created by preparePresetUiTest; no recursive deletion.
    try {
        for (const auto& path : {presetFolder(0) / L"A.lutpreset", presetFolder(0) / L"B.lutpreset", presetFolder(0) / L"Copy.lutpreset", presetFolder(0) / L"Farver-\u00e6\u00f8\u00e5.lutpreset", presetFolder(0) / L"Off.lutpreset", presetFolder(1) / L"Off.lutpreset", presetFolder(1) / L"C.lutpreset", presetFolder(1) / L"Added from folder.lutpreset", std::filesystem::path(testSettingsPath)})
            std::filesystem::remove(path);
        std::filesystem::remove(presetFolder(0)); std::filesystem::remove(presetFolder(1));
        std::filesystem::remove(presetFolder(0).parent_path()); std::filesystem::remove(std::filesystem::path(testSettingsPath).parent_path());
    } catch (...) {}
}
}
int WINAPI wWinMain(HINSTANCE app, HINSTANCE, PWSTR, int show) {
    instance = app; SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    int argumentCount = 0;
    LPWSTR* arguments = CommandLineToArgvW(GetCommandLineW(), &argumentCount);
    if(arguments&&argumentCount==3&&std::wstring(arguments[1])==L"--benchmark-matrix"){const int result=matrix_effect::benchmark(arguments[2]);LocalFree(arguments);return result;}
    if(arguments&&argumentCount==2&&std::wstring(arguments[1])==L"--self-test-matrix-effect"){LocalFree(arguments);return matrix_effect::selfTest();}
    if(arguments&&argumentCount==4&&(std::wstring(arguments[1])==L"--matrix-effect-worker"||std::wstring(arguments[1])==L"--matrix-effect-probe")){
        wchar_t* end=nullptr;const auto pid=wcstoul(arguments[2],&end,10);const int result=pid&&end&&!*end?matrix_effect::worker(pid,arguments[3],std::wstring(arguments[1])==L"--matrix-effect-probe"):2;LocalFree(arguments);return result;}
    if(arguments&&argumentCount==2&&std::wstring(arguments[1])==L"--matrix-preview"){LocalFree(arguments);return matrix_preview::run(app);}
    if(arguments&&argumentCount==2&&std::wstring(arguments[1])==L"--self-test-matrix-preview"){const int result=matrix_preview::selfTest(arguments[0]);LocalFree(arguments);return result;}
    if(arguments&&argumentCount==2&&std::wstring(arguments[1])==L"--matrix-preview-owner"){LocalFree(arguments);Sleep(INFINITE);return 0;}
    if(arguments&&argumentCount==4&&(std::wstring(arguments[1])==L"--matrix-worker"||std::wstring(arguments[1])==L"--matrix-worker-probe")){
        wchar_t* end=nullptr;const auto pid=wcstoul(arguments[2],&end,10);const int result=pid&&end&&!*end?matrix_preview::worker(pid,arguments[3],std::wstring(arguments[1])==L"--matrix-worker-probe"):2;LocalFree(arguments);return result;}
    if (arguments && argumentCount == 2 && std::wstring(arguments[1]) == L"--self-test-icons") {
        LocalFree(arguments);
        if (app_icons::choose(false, false, true) != app_icons::State::Disabled ||
            app_icons::choose(false, true, false) != app_icons::State::DisabledHdr ||
            app_icons::choose(false, true, true) != app_icons::State::DisabledHdr ||
            app_icons::choose(true, false, false) != app_icons::State::Active ||
            app_icons::choose(true, true, false) != app_icons::State::Hdr ||
            app_icons::choose(true, false, true) != app_icons::State::Application ||
            app_icons::choose(true, true, true) != app_icons::State::ApplicationHdr) return 1;
        for (const auto state : {app_icons::State::Disabled, app_icons::State::DisabledHdr, app_icons::State::Active, app_icons::State::Hdr, app_icons::State::Application, app_icons::State::ApplicationHdr}) {
            for (int size : {16, 20, 24, 32, 40, 48, 64, 96, 128, 256}) {
                HICON icon = iconCache.get(instance, state, size); if (!icon) return 2;
                if (iconCache.get(instance, state, size) != icon) return 3;
                ICONINFO info{}; if (!GetIconInfo(icon, &info)) return 4;
                BITMAP bitmap{}; const bool correct = info.hbmColor && GetObjectW(info.hbmColor, sizeof(bitmap), &bitmap) && bitmap.bmWidth == size && bitmap.bmHeight == size;
                if (info.hbmColor) DeleteObject(info.hbmColor); if (info.hbmMask) DeleteObject(info.hbmMask);
                if (!correct) return 5;
            }
        }
        return 0;
    }
    if (arguments && argumentCount == 2 && std::wstring(arguments[1]) == L"--self-test-hotkeys") {
        LocalFree(arguments); return shortcutSelfTest();
    }
    if (arguments && argumentCount == 2 && std::wstring(arguments[1]) == L"--live-probe-owner") {
        LocalFree(arguments); Sleep(INFINITE); return 0;
    }
    if (arguments && argumentCount == 7 && (std::wstring(arguments[1]) == L"--live-guard" || std::wstring(arguments[1]) == L"--live-guard-probe")) {
        wchar_t* end = nullptr; const unsigned long parent = wcstoul(arguments[3], &end, 10);
        const int result = parent && end && !*end ? live::guardian(arguments[2], parent, arguments[4], arguments[5], arguments[6], std::wstring(arguments[1]) == L"--live-guard-probe") : 1;
        LocalFree(arguments); return result;
    }
    if (arguments && argumentCount == 2 && (std::wstring(arguments[1]) == L"--self-test-live" || std::wstring(arguments[1]) == L"--self-test-live-crash")) {
        const int result = live::recoverySelfTest(arguments[0], std::wstring(arguments[1]) == L"--self-test-live-crash"); LocalFree(arguments); return result;
    }
    if (arguments && argumentCount == 3 && std::wstring(arguments[1]) == L"--recover-live") {
        const int result = live::recover(arguments[2]); LocalFree(arguments); return result;
    }
    const bool render = (arguments&&argumentCount==3&&(std::wstring(arguments[1])==L"--render-small"||std::wstring(arguments[1])==L"--render-small-profiles"||std::wstring(arguments[1])==L"--render-mixer")) || arguments && argumentCount == 3 &&
        (std::wstring(arguments[1]) == L"--render-saturation" || std::wstring(arguments[1]) == L"--render-matrix-preview" || std::wstring(arguments[1]) == L"--render-preview" || std::wstring(arguments[1]) == L"--render-hdr" || std::wstring(arguments[1]) == L"--render-profiles" || std::wstring(arguments[1]) == L"--render-running-programs" || std::wstring(arguments[1]) == L"--render-application" || std::wstring(arguments[1]) == L"--render-tray" || std::wstring(arguments[1]) == L"--render-balance" || std::wstring(arguments[1]) == L"--render-curves" || std::wstring(arguments[1]) == L"--render-settings" || std::wstring(arguments[1]) == L"--render-color-filters" || std::wstring(arguments[1]) == L"--render-night-light" || std::wstring(arguments[1]) == L"--render-font-segoe" || std::wstring(arguments[1]) == L"--render-unsaved" || std::wstring(arguments[1]) == L"--render-error" || std::wstring(arguments[1]) == L"--render-carryover" || std::wstring(arguments[1]) == L"--render-ready" || std::wstring(arguments[1]) == L"--render-title-menu");
    const std::wstring renderFile = render ? arguments[2] : L"";
    const std::wstring renderMode=render?arguments[1]:L"";
    const bool renderTray = render && std::wstring(arguments[1]) == L"--render-tray";
    const bool renderHdr = render && std::wstring(arguments[1]) == L"--render-hdr";
    const bool renderProfiles = render && (std::wstring(arguments[1]) == L"--render-profiles"||std::wstring(arguments[1])==L"--render-small-profiles");
    const bool renderApplication = render && std::wstring(arguments[1]) == L"--render-application";
    const bool trayExitTest=arguments && argumentCount==2 && std::wstring(arguments[1])==L"--self-test-tray-exit";
    const bool trayStartupTest=trayExitTest || arguments && argumentCount==2 && std::wstring(arguments[1])==L"--self-test-tray-startup";
    const bool trayBenchmark=arguments && argumentCount==3 && std::wstring(arguments[1])==L"--benchmark-tray-startup";
    const std::filesystem::path trayBenchmarkReport=trayBenchmark?arguments[2]:L"";
    if(trayBenchmark && std::filesystem::exists(trayBenchmarkReport)){LocalFree(arguments);return 1;}
    const bool benchmark=arguments && argumentCount==3 && std::wstring(arguments[1])==L"--benchmark-ui";
    const std::filesystem::path benchmarkReport=benchmark?arguments[2]:L"";
    if(benchmark && std::filesystem::exists(benchmarkReport)){LocalFree(arguments);return 1;}
    profileUiTest = arguments && argumentCount == 2 && std::wstring(arguments[1]) == L"--self-test-profile-ui";
    effectUiTest = arguments && argumentCount == 2 && std::wstring(arguments[1]) == L"--self-test-effect-ui";
    const bool surfaces96=arguments&&argumentCount==2&&std::wstring(arguments[1])==L"--self-test-surfaces-96";
    if(surfaces96&&!SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_UNAWARE)){LocalFree(arguments);return 1;}
    surfaceUiTest = surfaces96||(arguments && argumentCount == 2 && std::wstring(arguments[1]) == L"--self-test-surfaces");
    presetUiTest = trayStartupTest || trayBenchmark || benchmark || surfaceUiTest || effectUiTest || profileUiTest || (arguments && argumentCount == 2 && std::wstring(arguments[1]) == L"--self-test-preset-ui");
    if (arguments && argumentCount == 3 && std::wstring(arguments[1]) == L"--idle-probe") idleReport = arguments[2];
    if (!idleReport.empty() && GetFileAttributesW(idleReport.c_str()) != INVALID_FILE_ATTRIBUTES) { LocalFree(arguments); return 1; }
    const bool previewOnly = arguments && argumentCount == 2 && std::wstring(arguments[1]) == L"--preview-ui";
    renderOnly = previewOnly || render || !idleReport.empty() || presetUiTest;
    if (arguments) LocalFree(arguments);
    if (presetUiTest && !preparePresetUiTest()) return 1;
    const auto mutexName = renderOnly ? L"Local\\LutWorkbenchRender-" + std::to_wstring(GetCurrentProcessId()) : std::wstring(L"Local\\LutWorkbenchPreview");
    HANDLE mutex = CreateMutexW(nullptr, FALSE, mutexName.c_str());
    if (!mutex) return 1;
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        HWND existing = FindWindowW(L"LutWorkbenchPreview", nullptr);
        if (existing) { ShowWindow(existing, SW_SHOW); SetForegroundWindow(existing); }
        CloseHandle(mutex); return 0;
    }
    deferEditor=trayStartupTest || trayBenchmark || !renderOnly && GetPrivateProfileIntW(L"Startup",L"Minimized",0,shortcutPath().c_str())==1;
    const DWORD setupFaultStart=benchmark?processFaults():0;
    const auto setupStart=std::chrono::steady_clock::now();
    explorerRestart = RegisterWindowMessageW(L"TaskbarCreated");
    WNDCLASSW preview{}; preview.hInstance = instance; preview.lpfnWndProc = curveProc;preview.style=CS_DBLCLKS;
    preview.lpszClassName = L"LutCurvePreview"; preview.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    WNDCLASSW main{}; main.hInstance = instance; main.lpfnWndProc = windowProc;
    main.lpszClassName = L"LutWorkbenchPreview"; main.hCursor = preview.hCursor;
    main.hIcon = LoadIconW(instance, MAKEINTRESOURCEW(IDI_LUT_ACTIVE)); main.hbrBackground = ui::background();
    if (!RegisterClassW(&preview) || !RegisterClassW(&main)) { CloseHandle(mutex); return 1; }
    POINT cursor{}; GetCursorPos(&cursor); HMONITOR monitor = MonitorFromPoint(cursor, MONITOR_DEFAULTTONEAREST);
    MONITORINFO monitorInfo{}; monitorInfo.cbSize = sizeof(monitorInfo); GetMonitorInfoW(monitor, &monitorInfo);
    HWND probe = CreateWindowExW(0, L"STATIC", L"", WS_POPUP, cursor.x, cursor.y, 1, 1, nullptr, nullptr, instance, nullptr);
    if (probe) { dpi = static_cast<int>(GetDpiForWindow(probe)); DestroyWindow(probe); }
    RECT rect{0, 0, px(ClientWidth), px(ClientHeight)}; constexpr DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_CLIPCHILDREN;
    ui::adjustWindowRect(rect, static_cast<UINT>(dpi));
    const int width = rect.right - rect.left, height = rect.bottom - rect.top;
    const int x = monitorInfo.rcWork.left + std::max(0L, (monitorInfo.rcWork.right - monitorInfo.rcWork.left - width) / 2);
    const int y = monitorInfo.rcWork.top + std::max(0L, (monitorInfo.rcWork.bottom - monitorInfo.rcWork.top - height) / 2);
    window = CreateWindowExW(0, main.lpszClassName, L"MonitorMage - live contrast and HDR correction", style, x, y, width, height, nullptr, nullptr, instance, nullptr);
    if (!window) { CloseHandle(mutex); return 1; }
    if (!renderOnly || !idleReport.empty()) {
        foregroundHook = SetWinEventHook(EVENT_SYSTEM_FOREGROUND, EVENT_SYSTEM_FOREGROUND, nullptr, foregroundEvent, 0, 0, WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS);
        if (!foregroundHook) { appConfig.enabled = false; profileMessage = L"Foreground notifications unavailable. Application profiles are disabled."; }
        inspectForeground(); if(appFocus.executable().empty())applyApplicationProfile(true); labels();
    }
    if(!renderOnly)startMaster();
    if(trayStartupTest || trayBenchmark){
        const auto milliseconds=[](auto a,auto b){return std::chrono::duration<double,std::milli>(b-a).count();};
        const double setupMs=milliseconds(setupStart,std::chrono::steady_clock::now());
        PROCESS_MEMORY_COUNTERS_EX usage{};usage.cb=sizeof(usage);GetProcessMemoryInfo(GetCurrentProcess(),reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&usage),sizeof(usage));
        int result=0;
        RECT client{};GetClientRect(window,&client);
        if(client.right!=px(ClientWidth) || client.bottom!=px(ClientHeight))result=11;
        if(editorReady || GetWindow(window,GW_CHILD) || font || hintFont || curveScratch.bytes() || presetLibrary[0].size()!=2)result=1;
        const auto original=currentPreset(0).current;const auto originalIndex=presetIndex[0];
        // A profile change must load its look and update processing state with
        // no controls. All hardware/session operations are disabled here.
        appConfig.enabled=true;appConfig.rules={{L"C:\\Tests\\TrayGame.exe",presetFolder(0)/L"B.lutpreset",applications::Action::Look}};
        correctionsRequested=true;acceptForeground(L"C:\\Tests\\TrayGame.exe");
        if(!applicationContextActive || contrastPosition!=-10 || editorReady || GetWindow(window,GW_CHILD))result=2;
        toggleBypass();if(!lookBypassed() || editorReady)result=3;toggleBypass();
        acceptForeground(L"C:\\Tests\\Desktop.exe");
        if(applicationContextActive || presetIndex[0]!=originalIndex || currentPreset(0).current!=original || contrastPosition!=original.contrast)result=4;
        toggleCorrections();if(correctionsRequested || editorReady)result=8;
        toggleCorrections();if(!correctionsRequested || editorReady)result=9;
        if(trayExitTest){DestroyWindow(window);CloseHandle(mutex);cleanPresetUiTest();return result;}
        showProfiles(true);
        if(!profile_editor::window() || editorReady || GetWindow(window,GW_CHILD))result=10;
        if(profile_editor::window())DestroyWindow(profile_editor::window());
        const auto openStart=std::chrono::steady_clock::now();ensureEditor();const double openMs=milliseconds(openStart,std::chrono::steady_clock::now());
        const HWND firstCanvas=canvas;ensureEditor();
        if(!editorReady || !canvas || canvas!=firstCanvas || !IsWindow(contrastSlider) || currentPreset(0).current!=original || GetWindowTextLengthW(monitorProfile)==0)result=5;
        if(SendMessageW(contrastSlider,TBM_GETPOS,0,0)!=original.contrast || selectedSourceIndex()!=selectedSource)result=6;
        if(trayBenchmark){
            std::ofstream out(trayBenchmarkReport);out.imbue(std::locale::classic());
            out<<"{\"tray_setup_ms\":"<<setupMs<<",\"first_editor_setup_ms\":"<<openMs<<",\"tray_private_mib\":"<<usage.PrivateUsage/1048576.0<<",\"tray_working_set_mib\":"<<usage.WorkingSetSize/1048576.0<<",\"result\":"<<result<<",\"mode\":\"hidden fixture; no tray registration, hotkey registration or display changes; excludes OS loader and live helper startup\"}\n";
            if(!out)result=7;
        }
        DestroyWindow(window);CloseHandle(mutex);cleanPresetUiTest();return result;
    }
    if(benchmark){
        const auto now=[](){return std::chrono::steady_clock::now();};
        const auto ms=[](auto a,auto b){return std::chrono::duration<double,std::milli>(b-a).count();};
        const double setupMs=ms(setupStart,now());const DWORD setupFaults=processFaults()-setupFaultStart;
        RECT bounds{};GetWindowRect(window,&bounds);HDC screen=GetDC(nullptr),memory=CreateCompatibleDC(screen);
        HBITMAP image=CreateCompatibleBitmap(screen,bounds.right-bounds.left,bounds.bottom-bounds.top);ReleaseDC(nullptr,screen);
        if(!memory||!image){if(image)DeleteObject(image);if(memory)DeleteDC(memory);DestroyWindow(window);CloseHandle(mutex);cleanPresetUiTest();return 1;}
        const auto old=SelectObject(memory,image);
        const auto paint=[&](){SendMessageW(window,WM_PRINT,reinterpret_cast<WPARAM>(memory),PRF_NONCLIENT|PRF_CLIENT|PRF_CHILDREN|PRF_ERASEBKGND);GdiFlush();};
        const DWORD firstFaultStart=processFaults();auto start=now();paint();const double firstMs=ms(start,now());const DWORD firstFaults=processFaults()-firstFaultStart;
        const DWORD repaintFaultStart=processFaults();start=now();for(int i=0;i<10;++i)paint();const double repaintMs=ms(start,now())/10;const DWORD repaintFaults=processFaults()-repaintFaultStart;
        const auto fullBufferBytes=lastGraphPaintBytes;const auto dirty=graphUpdateBounds(canvas);const int dirtySaved=SaveDC(memory);IntersectClipRect(memory,dirty.left,dirty.top,dirty.right,dirty.bottom);
        const DWORD dirtyFaultStart=processFaults();start=now();for(int i=0;i<10;++i){SendMessageW(canvas,WM_PRINTCLIENT,reinterpret_cast<WPARAM>(memory),PRF_CLIENT);GdiFlush();}
        const double dirtyMs=ms(start,now())/10;const DWORD dirtyFaults=processFaults()-dirtyFaultStart;const auto dirtyBufferBytes=lastGraphPaintBytes;RestoreDC(memory,dirtySaved);
        PROCESS_MEMORY_COUNTERS_EX usage{};usage.cb=sizeof(usage);GetProcessMemoryInfo(GetCurrentProcess(),reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&usage),sizeof(usage));
        SelectObject(memory,old);DeleteObject(image);DeleteDC(memory);
        const auto tileBytes=curveScratch.bytes();SendMessageW(window,WM_SHOWWINDOW,FALSE,0);
        PROCESS_MEMORY_COUNTERS_EX hidden{};hidden.cb=sizeof(hidden);GetProcessMemoryInfo(GetCurrentProcess(),reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&hidden),sizeof(hidden));
        start=now();DestroyWindow(window);const double shutdownMs=ms(start,now());
        start=now();const int helper=live::recoverySelfTest(app_settings::executable().c_str());const double helperMs=ms(start,now());
        CloseHandle(mutex);cleanPresetUiTest();
        std::ofstream report(benchmarkReport);report.imbue(std::locale::classic());
        report<<"{\"editor_setup_ms\":"<<setupMs<<",\"first_paint_ms\":"<<firstMs<<",\"mean_repaint_ms\":"<<repaintMs
            <<",\"setup_page_faults\":"<<setupFaults<<",\"first_paint_page_faults\":"<<firstFaults<<",\"repeated_paint_page_faults\":"<<repaintFaults<<",\"paint_count\":10,\"fault_counter\":\"process total; does not distinguish hard from soft faults\""
            <<",\"full_graph_buffer_mib\":"<<fullBufferBytes/1048576.0<<",\"plot_update_buffer_mib\":"<<dirtyBufferBytes/1048576.0<<",\"plot_update_mean_ms\":"<<dirtyMs<<",\"plot_update_page_faults\":"<<dirtyFaults
            <<",\"graph_surface_bits\":32"
            <<",\"editor_shutdown_ms\":"<<shutdownMs<<",\"probe_helper_roundtrip_ms\":"<<helperMs<<",\"helper_ok\":"<<(helper==0?"true":"false")
            <<",\"peak_working_set_mib\":"<<usage.PeakWorkingSetSize/1048576.0<<",\"private_commit_mib\":"<<usage.PrivateUsage/1048576.0
            <<",\"curve_tile_mib\":"<<tileBytes/1048576.0<<",\"after_hide_private_commit_mib\":"<<hidden.PrivateUsage/1048576.0
            <<",\"dpi\":"<<dpi<<",\"mode\":\"hidden fixture editor; helper probe never reads or writes a display LUT; excludes OS loader, real user library and actual LUT restoration\"}\n";
        return report&&helper==0?0:1;
    }
    if (presetUiTest) {
        const int result = surfaceUiTest ? runSurfaceUiTest() : effectUiTest ? runEffectUiTest() : profileUiTest ? runProfileUiTest() : runPresetUiTest(); DestroyWindow(window); CloseHandle(mutex); cleanPresetUiTest(); return result;
    }
    if (render) {
        if(renderMode==L"--render-title-menu"){
            const bool rendered=ui::previewCaptionMenu(window,[&](HWND target){return renderPreview(renderFile.c_str(),target);});
            DestroyWindow(window);CloseHandle(mutex);return rendered?0:1;
        }
        if(renderTray) {
            const bool rendered=ui::previewPopup(window,stateIcon(SM_CXICON),trayItems(),[&](HWND target){return renderPreview(renderFile.c_str(),target);});
            DestroyWindow(window);CloseHandle(mutex);return rendered?0:1;
        }
        HWND target = window;
        if(renderMode==L"--render-saturation"){selected=10;currentPreset(0).current.saturationEnabled=true;currentPreset(0).current.saturation=50;correctionsRequested=true;labels();}
        if(renderMode==L"--render-mixer"){selected=11;currentPreset(0).current.monochromeEnabled=true;currentPreset(0).current.monochrome={100,20,0};correctionsRequested=true;labels();}
        if(renderMode==L"--render-matrix-preview"){matrix_preview::show(window,true);target=matrix_preview::window();if(!target){DestroyWindow(window);CloseHandle(mutex);return 1;}}
        if (renderApplication) {
            presets::Data data; data.contrast = 15;
            presetLibrary[0].push_back({L"C:\\Examples\\Gentle contrast.lutpreset", data, data});
            presetIndex[0] = 1; applicationContextActive = true; appConfig.enabled = true;
            appFocus.changed(L"C:\\Games\\Example game.exe"); profileAction = applications::Action::PauseHdr; loadPresetControls();
        }
        if (renderProfiles || renderMode==L"--render-running-programs") {
            const auto data = presetLibrary[0][0].current;
            presetLibrary[0].push_back({L"C:\\Examples\\Neutral.lutpreset", data, data});
            presetLibrary[0].push_back({L"C:\\Examples\\Gentle contrast.lutpreset", data, data});
            appConfig.rules = {{L"C:\\Games\\Example game.exe", presetLibrary[0][2].path, applications::Action::Look},
                {L"C:\\Video\\HDR player.exe", {}, applications::Action::PauseHdr}};
            showProfiles(true); target = profile_editor::window();
            if (!target) { DestroyWindow(window); CloseHandle(mutex); return 1; }
            SendMessageW(GetDlgItem(target, 203), LB_SETCURSEL, 1, 0);
            SendMessageW(target, WM_COMMAND, MAKEWPARAM(203, LBN_SELCHANGE), 0);
        }
        if(renderMode==L"--render-running-programs")profile_editor::previewPrograms({{L"C:\\Games\\Example game.exe",L"Example game",SIZE_T{2400}*1048576,true,true},{L"C:\\Video\\vlc.exe",L"VLC media player",SIZE_T{420}*1048576,true,true},{L"C:\\Browser\\browser.exe",L"Web browser",SIZE_T{1300}*1048576,true,true}});
        if (renderHdr || renderApplication) { selected = 1; refreshPresetChoice(); labels(); }
        if(renderMode==L"--render-balance"){selected=2;currentPreset(0).current.adjustments.balance={100,100,100,20,0};labels();}
        if(renderMode==L"--render-curves"){selected=6;auto& curves=currentPreset(0).current.adjustments.curves;curves[0]={0,.09,.2,.33,.5,.67,.8,.91,1};curves[3]={0,.06,.15,.26,.4,.56,.72,.87,1};labels();}
        if(renderMode==L"--render-error"){liveMessage=ui::messages::error("Driver rejected the correction");liveError=true;labels();}
        if(renderMode==L"--render-carryover"){liveMessage=L"The previous correction carried into this display mode. Restore Windows calibration before re-enabling";liveError=true;labels();}
        if(renderMode==L"--render-ready"){startMaster();}
        if(renderMode==L"--render-settings"){settingsPage=true;layout();}
        if(renderMode==L"--render-color-filters"){settingsPage=false;selected=8;layout();}
        if(renderMode==L"--render-night-light"){settingsPage=false;selected=9;nightConfig.enabled=true;nightClock();layout();}
        if(renderMode==L"--render-font-segoe"){ui::setFontFamily(L"Segoe UI");layout();}
        if(renderMode==L"--render-unsaved") {
            target=unsaved::preview(window);
            if(!target){DestroyWindow(window);CloseHandle(mutex);return 1;}
        }
        if(renderMode==L"--render-small"){
            selected=2;labels();SetWindowPos(window,nullptr,0,0,px(480),px(360),SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE);ui::revealControl(window,adjustmentSliders[4]);
        }
        if(renderMode==L"--render-small-profiles"){
            SetWindowPos(target,nullptr,0,0,px(300),px(340),SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE);ui::revealControl(target,GetDlgItem(target,213));
        }
        if(target==window){ui::captionSettings(window,SettingsPage,settingsPage);ui::iconButton(powerButton,ui::Glyph::Power,correctionsRequested,true);}
        const bool rendered = renderPreview(renderFile.c_str(), target);
        DestroyWindow(window); CloseHandle(mutex); return rendered ? 0 : 1;
    }
    if (!idleReport.empty()) {
        idleCpuStart = processCpu(); idleStart = GetTickCount64();idleFaultStart=processFaults();
        if (!SetTimer(window, IdleTimer, 3000, nullptr)) { DestroyWindow(window); CloseHandle(mutex); return 1; }
    } else if(renderOnly||!startMinimized||!trayInstalled) { ShowWindow(window, show); UpdateWindow(window); }
    MSG message{}; int result = 0;
    while ((result = matrixSession.getMessage(message)) > 0) {
        if(matrix_preview::dialogMessage(message))continue;
        if(navigateTab(message))continue;
        if(GetFocus()!=shortcutControl && message.message==WM_SYSKEYDOWN && (message.wParam==L'P'||message.wParam==L'S') && (message.lParam&(1L<<29)) && GetAncestor(message.hwnd,GA_ROOT)==window) {
            if(!(message.lParam&(1L<<30)))SendMessageW(window,WM_COMMAND,message.wParam==L'P'?TestContrast:SettingsPage,0);continue;
        }
        if (profile_editor::window() && IsDialogMessageW(profile_editor::window(), &message)){ui::revealControl(profile_editor::window(),GetFocus());continue;}
        if (!IsDialogMessageW(window, &message)) { TranslateMessage(&message); DispatchMessageW(&message); }
    }
    CloseHandle(mutex); return result < 0 ? 1 : static_cast<int>(message.wParam);
}
