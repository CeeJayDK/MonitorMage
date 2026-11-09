#pragma once
// Everything the parts of the main program share: control ids, the program's state, and the
// functions one file calls in another. Each .cpp file named below holds one topic.
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

namespace app
{
// ---- Constants, types and program state ----

constexpr int SIDEBAR_WIDTH = 194;
constexpr int PANE_OFFSET = SIDEBAR_WIDTH - 186;
constexpr int CLIENT_WIDTH = 768 + PANE_OFFSET;
constexpr int CLIENT_HEIGHT = 552;
constexpr UINT MESSAGE_TRAY = WM_APP + 1;
constexpr UINT MESSAGE_FOREGROUND = WM_APP + 2;
constexpr int ID_EFFECT_LIST = 101, ID_CONTRAST = 102, ID_PIVOT = 103, ID_WHITE = 104, ID_GAMMA = 105;
constexpr int ID_RESET = 106, ID_EXPORT = 107, ID_OPEN = 108, ID_EXIT = 109;
constexpr int ID_GAMMA_OTHER = 110;
constexpr int ID_SOURCE_CHOICE = 111, ID_TEST_CONTRAST = 112;
constexpr int ID_BYPASS = 113, ID_HDR_FIX = 114;
constexpr int ID_SHORTCUT = 115, ID_SET_SHORTCUT = 116;
constexpr int ID_PRESET_CHOICE = 117, ID_SAVE_PRESET = 118, ID_SAVE_PRESET_AS = 119, ID_OPEN_PRESET = 120,
			  ID_AUTOSAVE = 121;
constexpr int ID_REVERT_PRESET = 122;
constexpr int ID_APPLICATION_PROFILES = 123;
constexpr int ID_ACTIVE_LOOK = 124;
constexpr int ID_SETTINGS_PAGE = 125, ID_FONT_CHOICE = 126, ID_CURVE_CHANNEL = 127, ID_IMPORT_1D = 128,
			  ID_EXPORT_1D = 129, ID_ADJUSTMENT_BASE = 200;
constexpr int ID_TOGGLE_WINDOWS_HDR = 130;
constexpr int ID_MONITOR_STATUS = 131;
constexpr int ID_OUTPUT_INFO = 139;
constexpr int ID_COLOR_FILTER_CHOICE = 142, ID_COLOR_FILTER_SETTINGS = 143;
constexpr UINT_PTR TIMER_COLOR_FILTER = 907;
constexpr UINT_PTR TIMER_NIGHT = 908;
constexpr UINT_PTR TIMER_MATRIX_RECOVERY = 909;
inline matrix_effect::Recovery matrix_recovery;
constexpr int ID_NIGHT_SCHEDULE = 148, ID_NIGHT_START = 149, ID_NIGHT_END = 150, ID_NIGHT_INCLUDE = 151,
			  ID_NIGHT_DISPLAY = 152;
constexpr int ID_MONITOR_MODE = 133;
constexpr int ID_MONITOR_PROFILE = 138;
constexpr int ID_WEBSITE_LINK = 153;
constexpr int ID_START_WITH_WINDOWS = 134;
constexpr int ID_START_MINIMIZED = 154;
constexpr int ID_SAVE_DIAGNOSTICS = 155;
constexpr int ID_CORRECTION_LAYERS = 135;
constexpr int ID_ORDER_HINT = 136;
constexpr int ID_SELECTED_EFFECT_TOGGLE = 137;
constexpr int ID_WINDOWS_CORRECTION_LAYERS = 146, ID_SATURATION_REPORT = 147;
constexpr UINT_PTR TIMER_APPLY = 901, TIMER_DISPLAY = 902;
constexpr UINT_PTR TIMER_IDLE = 903;
constexpr UINT_PTR TIMER_PRESET = 904;
constexpr UINT_PTR TIMER_FOREGROUND = 905;
constexpr UINT_PTR TIMER_HDR = 906;
inline hdr::State hdr_state;
inline hdr::State hdr_transition;
inline bool hdr_pending = false, hdr_desired = false, hdr_resume = false;
inline unsigned hdr_attempts = 0;
inline std::wstring hdr_source;
inline HINSTANCE app_instance;
inline app_icons::Cache icon_cache;
inline HWND main_window, effect_list, contrast_slider, pivot_slider, white_slider, gamma_choice, gamma_other,
	curve_canvas;
inline HWND correction_list, windows_effect_list, effects_label;
inline HWND selected_effect_toggle, saturation_report_button;
inline HWND effect_title, effect_description, amount_label, pivot_label, white_label, gamma_label, status_label,
	reset_button, export_button, corrections_heading;
inline HWND source_choice, test_note;
inline HWND monitor_mode, monitor_profile, website_link;
inline HWND bypass_button, hdr_fix_control;
inline HWND shortcut_control, shortcut_button, shortcut_label;
inline HWND preset_label, preset_choice, save_preset_button, save_as_button, open_preset_button, autosave_control;
inline HWND revert_preset_button;
inline HWND profiles_button;
inline HWND active_look_label, order_hint;
inline HWND settings_button, font_choice, font_label, curve_channel, import_button, export_1d_button;
inline HWND hdr_button, power_button, monitor_tips, info_button;
inline HWND monitor_settings_button;
inline HWND version_label, startup_control, start_minimized_control, diagnostics_control;
inline HWND filter_choice, filter_settings_button, correction_note;
inline windows_controls::State filter_state;
inline night::Settings night_config;
inline night::Outputs night_outputs;
inline double night_level = 0;
inline std::wstring night_message;
inline HWND night_schedule, night_start, night_end, night_include, night_start_label, night_end_label, night_display;
inline windows_controls::Controller filter_controller;
inline bool filter_error = false;
inline std::wstring filter_message;
inline bool startup_enabled = false;
inline bool start_minimized = false;
#ifdef MONITORMAGE_DEVELOPER_BUILD
inline bool save_diagnostics = true;
#else
inline bool save_diagnostics = false;
#endif
inline bool editor_ready = false, runtime_ready = false, defer_editor = false;
inline unsigned editor_sync_depth = 0;
struct EditorSync
{
	EditorSync() { ++editor_sync_depth; }
	~EditorSync() { --editor_sync_depth; }
};
inline int selected_source = 0;
inline bool is_closing = false;
inline std::array<HWND, 5> adjustment_sliders {}, adjustment_labels {}, adjustment_values {};
inline HWND amount_value, pivot_value, white_value;
inline bool settings_page = false;
inline int adjustment_page = -1, editing_channel = 0, dragged_point = -1, keyboard_point = 4;
inline std::vector<int> effect_order { effect::ContrastCurves, effect::ColorBalance,      effect::MidtoneGamma,
	                                   effect::Levels,         effect::ShadowsHighlights, effect::CustomCurves,
	                                   effect::Imported1dLut,  effect::HdrGamma };
inline applications::Configuration profile_configuration;
inline applications::Focus foreground_program;
inline applications::Action profile_action = applications::Action::Look;
inline HWINEVENTHOOK foreground_hook = nullptr;
inline HWND pending_foreground = nullptr;
inline bool corrections_requested = false;
inline bool power_suspended = false;
inline std::wstring profile_paused_identity, profile_message;
inline std::array<std::vector<presets::Draft>, 2> preset_library;
inline std::array<size_t, 2> preset_index {};
inline size_t regular_look_index = 0;
inline bool application_context_active = false;
inline bool autosave_enabled = false;
inline bool slider_dragging = false, look_dirty_before_drag = false;
inline bool preset_ui_test = false;
inline bool profile_ui_test = false, effect_ui_test = false, surface_ui_test = false;
inline std::wstring test_settings_path;
inline std::wstring preset_message;
inline WORD active_shortcut = MAKEWORD(VK_F8, HOTKEYF_CONTROL | HOTKEYF_ALT);
inline WORD preferred_shortcut = active_shortcut;
inline int active_hotkey_id = 1;
inline DWORD hotkey_error = 0;
inline std::wstring hotkey_message;
inline HANDLE active_test = nullptr;
using Source = live::Output;
inline std::vector<Source> display_sources;
inline live::Session live_session;
inline matrix_effect::Controller matrix_session;
inline std::wstring matrix_message;
inline live::Output live_output;
inline bool contrast_enabled = true, correction_first = false;
inline bool bypassed = false, hdr_fix_enabled = true, hotkey_registered = false, resume_after_wake = false;
inline std::wstring live_message = L"Off. Turn on MonitorMage to apply your adjustments.";
inline bool live_error = false, status_error = false;
inline bool graph_status_visible = false;
inline bool display_retry_pending = false, mode_reset_pending = false;
inline int display_attempts = 0;
inline std::wstring last_preset_error;
inline HFONT ui_font, hint_font;
inline plot::Scratch curve_scratch;
inline size_t last_graph_paint_bytes = 0;
inline unsigned graph_layout_version = 0;
inline std::array<RECT, 3> graph_audit_damage {};
inline unsigned graph_audit_count = 0;
inline NOTIFYICONDATAW tray_icon {};
inline UINT taskbar_created_message;
inline bool tray_installed = false;
inline bool render_only = false;
inline std::wstring idle_report;
inline ULONGLONG idle_cpu_start = 0, idle_start = 0;
inline DWORD idle_fault_start = 0;
inline int dpi = 96, selected_effect = effect::ContrastCurves, contrast_position = 0, pivot_position = 50,
		   white_position = 200, gamma_position = 0;
// One discovery snapshot per synchronous operation. Hardware reads/writes in
// live::Session still verify the current mode and ramp independently.
struct SourceQueryBatch
{
	static inline unsigned depth = 0;
	static inline bool valid = false;
	static inline std::vector<Source> snapshot;
	SourceQueryBatch()
	{
		if (depth++ == 0)
			valid = false;
	}
	~SourceQueryBatch()
	{
		if (--depth == 0)
		{
			valid = false;
			snapshot.clear();
		}
	}
};
struct Parameter
{
	const wchar_t* label;
	int minimum, maximum;
	int* value;
	int default_value = 0;
};
inline bool effect_layout_valid = false;
struct MonitorGeometry
{
	RECT body, screen, plot, foot;
	int left, right, top, bottom, size;
};

// ---- app_state.cpp ----

void EnsureCommonControls();
int SelectedSourceIndex();
void SelectSource(int index);
int SelectedPresetRole();
int ProcessingOrderIndex(int effect);
std::vector<int> CreativeOrder();
HWND EffectListForRole(int effect);
int EffectListRow(int effect);
int ScaleForDpi(int value);
std::wstring SettingsFilePath();
void SetTextIfChanged(HWND control, const wchar_t* text);
void SetControlEnabled(HWND control, bool enabled);
void SetControlVisible(HWND control, bool visible);
DWORD ProcessPageFaults();
ULONGLONG ProcessCpuTime();
double ContrastAmount();
double ContrastPivot();
double HdrGamma();
void PlaceControl(HWND control, int x, int y, int width, int height, bool body = false);
std::wstring FormatDecimal(double value, int precision = 2);

// ---- app_presets.cpp ----

presets::Kind PresetKindForRole(int preset_role);
presets::Draft& CurrentPreset(int preset_role);
std::filesystem::path PresetFolder(int preset_role);
void ShowPresetError(const std::exception& error);
bool PersistPresetChoices();
size_t FindPreset(int preset_role, const std::filesystem::path& path);
size_t ReadPreset(int preset_role, const std::filesystem::path& path);
void DiscoverPresets(int preset_role);
void LoadPresetControls();
void InitializePresets();
void RefreshPresetChoice();
bool SaveExistingPreset(int preset_role);
void SavePresetAsPath(int preset_role, const std::filesystem::path& path, bool replace);
void FlushPresetAutosave();
void OnPresetEdited(int preset_role);
void PresetFileDialog(bool save_as);
bool ConfirmExit();

// ---- app_hotkey.cpp ----

UINT HotkeyModifiers(WORD shortcut);
std::wstring HotkeyName(WORD shortcut);
bool BindHotkey(WORD shortcut);
void BindPreferredHotkey(WORD preferred);
void LoadAndBindHotkey();

// ---- app_corrections.cpp ----

void RefreshColorFilters();
void ChangeWindowsFilter(bool on, DWORD filter, bool selecting_filter = false);
Source OutputForPreview();
void RefreshSources();
bool IsLookBypassed();
bool IsHdrFixActive();
bool IsColorMatrixNeeded();
void SyncColorMatrix(bool refresh = false);
void ScheduleColorMatrixRecovery();
bool ProcessingActive();
live::Settings CurrentSettings();
void UpdateNightLightSchedule();
int DisableLiveCorrections(bool retain_request = false);
bool WaitForDisplay();
void ApplyLiveCorrections();
void EnableLiveCorrections();
void ToggleBypass();
void ReconcileLiveCorrections();
void ToggleCorrections();
void TurnOnCorrections();
void ToggleWindowsHdrMode();

// ---- app_profiles.cpp ----

void ManualLookOverride();
void ApplyApplicationProfile(bool force = false);
void OnForegroundProgramChanged(const std::filesystem::path& path);
void CheckForegroundProgram(HWND recent = nullptr);
void CALLBACK ForegroundChangedCallback(HWINEVENTHOOK, DWORD, HWND foreground, LONG, LONG, DWORD, DWORD);
bool ApplyProfileConfiguration(const applications::Configuration& candidate);
void ShowProfiles(bool hidden = false);

// ---- app_effects.cpp ----

bool& AdjustmentEnabled(int effect);
void ToggleEffect(int effect);
void MoveEffect(int effect, int target);
void MoveCreative(int effect, int target);
std::vector<Parameter> SliderParameters();

// ---- app_tray.cpp ----

app_icons::State CurrentIconState(bool active);
app_icons::State CurrentIconState();
HICON IconForState(int metric, app_icons::State state);
HICON IconForState(int metric);
void UpdateTray();
bool InstallTray();
std::vector<ui::MenuItem> TrayMenuItems();
void ShowTrayMenu();

// ---- app_interface.cpp ----

std::vector<HWND> TabStopControls();
HWND NextTabStop(HWND from, bool reverse);
bool NavigateTab(MSG& message);
void RefreshInterface();
void LayoutEditorControls();
void ShowEditor();
void LayoutMonitorControls();

// ---- app_curve_view.cpp ----

void ExportCurve();
void ImportOrExportLut(bool importing);
MonitorGeometry CalculateMonitorGeometry(HWND hwnd);
RECT CurvePlotRect(HWND hwnd);
RECT GraphUpdateBounds(HWND hwnd);
void InvalidateGraph();
LRESULT CALLBACK CurveWindowProc(HWND hwnd, UINT message, WPARAM w_param, LPARAM l_param);

// ---- app_main_window.cpp ----

bool RenderWindowToFile(const wchar_t* path, HWND target = nullptr);
void CreateEditorIfNeeded();
LRESULT CALLBACK MainWindowProc(HWND hwnd, UINT message, WPARAM w_param, LPARAM l_param);

// ---- app_self_tests.cpp ----

int RunHotkeySelfTest();
bool PreparePresetUiTest();
int RunPresetUiTest();
int RunProfileUiTest();
int RunEffectUiTest();
int RunSurfaceUiTest();
void CleanPresetUiTest();

}
