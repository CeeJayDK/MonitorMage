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
#pragma comment(                                                                                                       \
	linker,                                                                                                            \
	"\"/manifestdependency:type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")
#endif

namespace
{
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
matrix_effect::Recovery matrix_recovery;
constexpr int ID_NIGHT_SCHEDULE = 148, ID_NIGHT_START = 149, ID_NIGHT_END = 150, ID_NIGHT_INCLUDE = 151,
			  ID_NIGHT_DISPLAY = 152;
constexpr int ID_MONITOR_MODE = 133;
constexpr int ID_MONITOR_PROFILE = 138;
constexpr int ID_WEBSITE_LINK = 153;
constexpr int ID_START_WITH_WINDOWS = 134;
constexpr int ID_START_MINIMIZED = 154;
constexpr int ID_CORRECTION_LAYERS = 135;
constexpr int ID_ORDER_HINT = 136;
constexpr int ID_SELECTED_EFFECT_TOGGLE = 137;
constexpr int ID_WINDOWS_CORRECTION_LAYERS = 146, ID_SATURATION_REPORT = 147;
constexpr UINT_PTR TIMER_APPLY = 901, TIMER_DISPLAY = 902;
constexpr UINT_PTR TIMER_IDLE = 903;
constexpr UINT_PTR TIMER_PRESET = 904;
constexpr UINT_PTR TIMER_FOREGROUND = 905;
constexpr UINT_PTR TIMER_HDR = 906;
hdr::State hdr_state;
hdr::State hdr_transition;
bool hdr_pending = false, hdr_desired = false, hdr_resume = false;
unsigned hdr_attempts = 0;
std::wstring hdr_source;
HINSTANCE app_instance;
app_icons::Cache icon_cache;
HWND main_window, effect_list, contrast_slider, pivot_slider, white_slider, gamma_choice, gamma_other, curve_canvas;
HWND correction_list, windows_effect_list, effects_label;
HWND selected_effect_toggle, saturation_report_button;
HWND effect_title, effect_description, amount_label, pivot_label, white_label, gamma_label, status_label, reset_button,
	export_button, corrections_heading;
HWND source_choice, test_note;
HWND monitor_mode, monitor_profile, website_link;
HWND bypass_button, hdr_fix_control;
HWND shortcut_control, shortcut_button, shortcut_label;
HWND preset_label, preset_choice, save_preset_button, save_as_button, open_preset_button, autosave_control;
HWND revert_preset_button;
HWND profiles_button;
HWND active_look_label, order_hint;
HWND settings_button, font_choice, font_label, curve_channel, import_button, export_1d_button;
HWND hdr_button, power_button, monitor_tips, info_button;
HWND monitor_settings_button;
HWND version_label, startup_control, start_minimized_control;
HWND filter_choice, filter_settings_button, correction_note;
windows_controls::State filter_state;
night::Settings night_config;
night::Outputs night_outputs;
double night_level = 0;
std::wstring night_message;
HWND night_schedule, night_start, night_end, night_include, night_start_label, night_end_label, night_display;
windows_controls::Controller filter_controller;
bool filter_error = false;
std::wstring filter_message;
bool startup_enabled = false;
bool start_minimized = false;
bool editor_ready = false, runtime_ready = false, defer_editor = false;
unsigned editor_sync_depth = 0;
struct EditorSync
{
	EditorSync() { ++editor_sync_depth; }
	~EditorSync() { --editor_sync_depth; }
};

int selected_source = 0;
void CreateEditorIfNeeded();
void EnsureCommonControls()
{
	static const bool initialized = []()
	{
		INITCOMMONCONTROLSEX common { sizeof(common), ICC_BAR_CLASSES | ICC_HOTKEY_CLASS };
		return InitCommonControlsEx(&common) != FALSE;
	}();
	(void)initialized;
}

void UpdateTray();
int SelectedSourceIndex()
{
	return source_choice ? static_cast<int>(SendMessageW(source_choice, CB_GETCURSEL, 0, 0)) : selected_source;
}

void SelectSource(int index)
{
	selected_source = index;
	if (source_choice)
		SendMessageW(source_choice, CB_SETCURSEL, index, 0);
}

bool is_closing = false;
void LayoutMonitorControls();
std::array<HWND, 5> adjustment_sliders {}, adjustment_labels {}, adjustment_values {};
HWND amount_value, pivot_value, white_value;
bool settings_page = false;
int adjustment_page = -1, editing_channel = 0, dragged_point = -1, keyboard_point = 4;
std::vector<int> effect_order {
	effect::ContrastCurves,    effect::ColorBalance, effect::MidtoneGamma,  effect::Levels,
	effect::ShadowsHighlights, effect::CustomCurves, effect::Imported1dLut, effect::HdrGamma
};
int SelectedPresetRole();
applications::Configuration profile_configuration;
applications::Focus foreground_program;
applications::Action profile_action = applications::Action::Look;
HWINEVENTHOOK foreground_hook = nullptr;
HWND pending_foreground = nullptr;
bool corrections_requested = false;
bool power_suspended = false;
std::wstring profile_paused_identity, profile_message;
std::array<std::vector<presets::Draft>, 2> preset_library;
std::array<size_t, 2> preset_index {};
size_t regular_look_index = 0;
bool application_context_active = false;
bool autosave_enabled = false;
bool slider_dragging = false, look_dirty_before_drag = false;
bool preset_ui_test = false;
bool profile_ui_test = false, effect_ui_test = false, surface_ui_test = false;
std::wstring test_settings_path;
std::wstring preset_message;
WORD active_shortcut = MAKEWORD(VK_F8, HOTKEYF_CONTROL | HOTKEYF_ALT);
WORD preferred_shortcut = active_shortcut;
int active_hotkey_id = 1;
DWORD hotkey_error = 0;
std::wstring hotkey_message;
HANDLE active_test = nullptr;
using Source = live::Output;
std::vector<Source> display_sources;
live::Session live_session;
matrix_effect::Controller matrix_session;
std::wstring matrix_message;
live::Output live_output;
bool contrast_enabled = true, correction_first = false;
bool bypassed = false, hdr_fix_enabled = true, hotkey_registered = false, resume_after_wake = false;
std::wstring live_message = L"Off. Turn on MonitorMage to apply your adjustments.";
bool live_error = false, status_error = false;
bool graph_status_visible = false;
bool display_retry_pending = false, mode_reset_pending = false;
int display_attempts = 0;
std::wstring last_preset_error;
HFONT ui_font, hint_font;
plot::Scratch curve_scratch;
size_t last_graph_paint_bytes = 0;
unsigned graph_layout_version = 0;
std::array<RECT, 3> graph_audit_damage {};
unsigned graph_audit_count = 0;
void InvalidateGraph();
NOTIFYICONDATAW tray_icon {};
UINT taskbar_created_message;
bool tray_installed = false;
bool render_only = false;
std::wstring idle_report;
ULONGLONG idle_cpu_start = 0, idle_start = 0;
DWORD idle_fault_start = 0;
int dpi = 96, selected_effect = effect::ContrastCurves, contrast_position = 0, pivot_position = 50,
	white_position = 200, gamma_position = 0;

int SelectedPresetRole()
{
	return selected_effect == effect::HdrGamma ? preset_role::Correction : preset_role::Look;
}

int ProcessingOrderIndex(int effect)
{
	return static_cast<int>(std::find(effect_order.begin(), effect_order.end(), effect) - effect_order.begin());
}

std::vector<int> CreativeOrder()
{
	auto result = effect_order;
	result.erase(std::remove(result.begin(), result.end(), 1), result.end());
	result.push_back(10);
	result.push_back(11);
	return result;
}

HWND EffectListForRole(int effect)
{
	return (effect == effect::ColorFilters || effect == effect::NightLight) ? windows_effect_list
	       : effect == effect::HdrGamma                                     ? correction_list
	                                                                        : effect_list;
}

int EffectListRow(int effect)
{
	if (effect == effect::ColorFilters || effect == effect::NightLight)
		return effect - effect::FirstWindows;
	if (effect == effect::HdrGamma)
		return 0;
	const auto order = CreativeOrder();
	return static_cast<int>(std::find(order.begin(), order.end(), effect) - order.begin());
}

int ScaleForDpi(int value)
{
	return MulDiv(value, dpi, 96);
}

std::wstring SettingsFilePath()
{
	if (!test_settings_path.empty())
		return test_settings_path;
	wchar_t path[32768] {};
	const DWORD length = GetModuleFileNameW(nullptr, path, static_cast<DWORD>(std::size(path)));
	if (!length || length >= std::size(path))
		return L"";
	std::wstring result(path);
	const auto slash = result.find_last_of(L"\\/");
	return slash == std::wstring::npos ? L"" : result.substr(0, slash + 1) + L"MonitorMage.ini";
}

void RefreshInterface();
void RefreshColorFilters()
{
	filter_state = windows_controls::ReadColorFilterState();
}

void ChangeWindowsFilter(bool on, DWORD filter, bool selecting_filter = false)
{
	if (render_only || filter_controller.IsBusy())
		return;
	try
	{
		filter_controller.Set(filter_state, on, filter, selecting_filter);
		filter_error = false;
		filter_message = L"Windows is applying the color filter...";
		if (!SetTimer(main_window, TIMER_COLOR_FILTER, 150, nullptr))
		{
			filter_controller.StopWaiting();
			throw std::runtime_error("The Windows request was sent but could not be monitored; check Windows settings");
		}
	}
	catch (const std::exception& error)
	{
		filter_message = ui::messages::FriendlyError(error.what());
		filter_error = true;
	}
	RefreshColorFilters();
	RefreshInterface();
}

void ApplyLiveCorrections();
void RefreshPresetChoice();
void ReconcileLiveCorrections();
void ApplyApplicationProfile(bool force = false);
void ManualLookOverride();
bool IsLookBypassed();
bool IsHdrFixActive();
presets::Kind PresetKindForRole(int preset_role)
{
	return preset_role == preset_role::Look ? presets::Kind::Look : presets::Kind::Correction;
}

presets::Draft& CurrentPreset(int preset_role)
{
	return preset_library[preset_role][preset_index[preset_role]];
}

std::filesystem::path PresetFolder(int preset_role)
{
	return std::filesystem::path(SettingsFilePath()).parent_path() / L"presets" /
	       (preset_role == preset_role::Look ? L"looks" : L"corrections");
}

void ShowPresetError(const std::exception& error)
{
	preset_message = ui::messages::FriendlyError(error.what());
	last_preset_error = preset_message;
}

bool PersistPresetChoices()
{
	if (render_only && !preset_ui_test)
		return false;
	const auto path = SettingsFilePath();
	std::wstring text = L"[Hotkey]\r\nBypass=" + std::to_wstring(preferred_shortcut) + L"\r\n\r\n[Presets]\r\n";
	for (int preset_role = preset_role::Look; preset_role < preset_role::Count; ++preset_role)
	{
		auto stored = preset_role == preset_role::Look ? preset_library[preset_role::Look][regular_look_index].path
		                                               : CurrentPreset(preset_role).path;
		if (!stored.empty())
		{
			const auto relative = stored.lexically_relative(std::filesystem::path(path).parent_path());
			if (!relative.empty() && !relative.is_absolute() && *relative.begin() != L"..")
				stored = relative;
		}
		const auto value = stored.wstring();
		text += (preset_role == preset_role::Look ? L"Look=" : L"Correction=") + value + L"\r\n";
	}
	text += autosave_enabled ? L"Autosave=1\r\n" : L"Autosave=0\r\n";
	text += L"\r\n[Startup]\r\nMinimized=" + std::to_wstring(start_minimized) + L"\r\n";
	text += L"\r\n[NightLight]\r\nEnabled=" + std::to_wstring(night_config.Enabled) + L"\r\nScheduled=" +
	        std::to_wstring(night_config.Scheduled) + L"\r\nStrength=" + std::to_wstring(night_config.Strength) +
	        L"\r\nStart=" + std::to_wstring(night_config.Start) + L"\r\nEnd=" + std::to_wstring(night_config.End) +
	        L"\r\nFade=" + std::to_wstring(night_config.Fade) + L"\r\nExcluded=";
	for (const auto& key : night_config.Excluded)
		text += key + L",";
	text += L"\r\n";
	text += L"\r\n[Effects]\r\nCorrectionFirst=" + std::wstring(correction_first ? L"1" : L"0") + L"\r\n";
	text += L"Order=";
	for (size_t i = 0; i < effect_order.size(); ++i)
	{
		if (i)
			text += L",";
		text += std::to_wstring(effect_order[i]);
	}
	text += L"\r\n\r\n[Appearance]\r\nFont=" + std::wstring(ui::FontFamily()) + L"\r\n";
	try
	{
		text += applications::Encode(profile_configuration, path);
	}
	catch (const std::exception& error)
	{
		ShowPresetError(error);
		return false;
	}
	// Win32 INI readers require a UTF-16 BOM to retain arbitrary Unicode paths.
	std::string bytes("\xff\xfe", 2);
	for (wchar_t unit : text)
	{
		bytes.push_back(static_cast<char>(unit & 255));
		bytes.push_back(static_cast<char>(unit >> 8));
	}
	try
	{
		if (path.empty())
			throw std::runtime_error("Settings path unavailable");
		presets::WriteBytes(path, bytes, true);
		WritePrivateProfileStringW(nullptr, nullptr, nullptr, path.c_str());
		return true;
	}
	catch (const std::exception& error)
	{
		ShowPresetError(error);
		return false;
	}
}

size_t FindPreset(int preset_role, const std::filesystem::path& path)
{
	const auto full = std::filesystem::absolute(path).lexically_normal().wstring();
	auto& entries = preset_library[preset_role];
	for (size_t i = 1; i < entries.size(); ++i)
	{
		const auto candidate = std::filesystem::absolute(entries[i].path).lexically_normal().wstring();
		if (CompareStringOrdinal(full.c_str(), -1, candidate.c_str(), -1, TRUE) == CSTR_EQUAL)
			return i;
	}
	return entries.size();
}

size_t ReadPreset(int preset_role, const std::filesystem::path& path)
{
	const size_t index = FindPreset(preset_role, path);
	if (index < preset_library[preset_role].size() && preset_library[preset_role][index].loaded)
		return index;
	const auto data = presets::Read(path);
	if (data.kind != PresetKindForRole(preset_role))
		throw std::runtime_error(preset_role == preset_role::Look
		                             ? "This is a correction preset. Select HDR gamma correction to open it."
		                             : "This is a look preset. Select Contrast to open it.");
	if (index == preset_library[preset_role].size())
		preset_library[preset_role].push_back({ std::filesystem::absolute(path), data, data });
	else
		preset_library[preset_role][index] = { std::filesystem::absolute(path), data, data };
	// An already-open preset keeps its unsaved session draft.
	return index;
}

void AddPresetToLibrary(int preset_role, const std::filesystem::path& path)
{
	if (FindPreset(preset_role, path) != preset_library[preset_role].size())
		return;
	const auto inactive = presets::InactivePreset(PresetKindForRole(preset_role));
	preset_library[preset_role].push_back({ std::filesystem::absolute(path), inactive, inactive, false });
}

void DiscoverPresets(int preset_role)
{
	try
	{
		const auto folder = PresetFolder(preset_role);
		if (!std::filesystem::exists(folder))
			return;
		std::vector<std::filesystem::path> paths;
		for (const auto& entry : std::filesystem::directory_iterator(folder))
			if (entry.is_regular_file() && _wcsicmp(entry.path().extension().c_str(), L".lutpreset") == 0)
				paths.push_back(entry.path());
		std::sort(paths.begin(), paths.end());
		for (const auto& path : paths)
			AddPresetToLibrary(preset_role, path);
	}
	catch (const std::exception& error)
	{
		ShowPresetError(error);
	}
}

void LoadPresetControls()
{
	const auto& look = CurrentPreset(preset_role::Look).current;
	const auto& correction = CurrentPreset(preset_role::Correction).current;
	contrast_position = look.Contrast;
	pivot_position = look.Pivot;
	contrast_enabled = look.ContrastEnabled;
	hdr_fix_enabled = correction.HdrFix;
	gamma_position = correction.Gamma == 22 ? 0 : 1;
	if (!editor_ready)
		return;
	SendMessageW(contrast_slider, TBM_SETPOS, TRUE, contrast_position);
	SendMessageW(pivot_slider, TBM_SETPOS, TRUE, pivot_position);
	SendMessageW(gamma_choice, BM_SETCHECK, gamma_position == 0 ? BST_CHECKED : BST_UNCHECKED, 0);
	SendMessageW(gamma_other, BM_SETCHECK, gamma_position == 1 ? BST_CHECKED : BST_UNCHECKED, 0);
	SendMessageW(hdr_fix_control, BM_SETCHECK, correction.ApplyInSdr ? BST_CHECKED : BST_UNCHECKED, 0);
}

void InitializePresets()
{
	if (!render_only)
	{
		const auto path = SettingsFilePath();
		night_config.Enabled = GetPrivateProfileIntW(L"NightLight", L"Enabled", 0, path.c_str()) == 1;
		night_config.Scheduled = GetPrivateProfileIntW(L"NightLight", L"Scheduled", 0, path.c_str()) == 1;
		night_config.Strength =
			std::clamp(static_cast<int>(GetPrivateProfileIntW(L"NightLight", L"Strength", 35, path.c_str())), 0, 100);
		night_config.Start =
			std::clamp(static_cast<int>(GetPrivateProfileIntW(L"NightLight", L"Start", 1260, path.c_str())), 0, 1439);
		night_config.End =
			std::clamp(static_cast<int>(GetPrivateProfileIntW(L"NightLight", L"End", 420, path.c_str())), 0, 1439);
		if (night_config.End == night_config.Start)
			night_config.End = (night_config.Start + 600) % 1440;
		night_config.Fade =
			std::clamp(static_cast<int>(GetPrivateProfileIntW(L"NightLight", L"Fade", 10, path.c_str())), 0, 60);
		wchar_t excluded[8192] {};
		GetPrivateProfileStringW(L"NightLight", L"Excluded", L"", excluded, 8192, path.c_str());
		std::wistringstream keys(excluded);
		std::wstring key;
		while (std::getline(keys, key, L','))
			if (key.size() == 16 && key.find_first_not_of(L"0123456789abcdef") == std::wstring::npos)
				night_config.Excluded.insert(key);
	}
	for (int preset_role = preset_role::Look; preset_role < preset_role::Count; ++preset_role)
	{
		const auto data = presets::InactivePreset(PresetKindForRole(preset_role));
		preset_library[preset_role].push_back({ {}, data, data });
		if (render_only && !preset_ui_test)
			continue;
		try
		{
			const auto folder = PresetFolder(preset_role);
			if ((!defer_editor || editor_ready) && std::filesystem::exists(folder))
			{
				std::vector<std::filesystem::path> paths;
				for (const auto& entry : std::filesystem::directory_iterator(folder))
					if (entry.is_regular_file() && _wcsicmp(entry.path().extension().c_str(), L".lutpreset") == 0)
						paths.push_back(entry.path());
				std::sort(paths.begin(), paths.end());
				for (const auto& path : paths)
				{
					AddPresetToLibrary(preset_role, path);
				}
			}
			wchar_t path[32768] {};
			GetPrivateProfileStringW(L"Presets", preset_role == preset_role::Look ? L"Look" : L"Correction", L"", path,
			                         static_cast<DWORD>(std::size(path)), SettingsFilePath().c_str());
			if (*path)
			{
				std::filesystem::path saved_path(path);
				if (saved_path.is_relative())
					saved_path = std::filesystem::path(SettingsFilePath()).parent_path() / saved_path;
				preset_index[preset_role] = ReadPreset(preset_role, saved_path);
			}
			else
			{
				const auto initial = folder / L"Off.lutpreset";
				if (std::filesystem::exists(initial))
					preset_index[preset_role] = ReadPreset(preset_role, initial);
			}
		}
		catch (const std::exception& error)
		{
			ShowPresetError(error);
		}
	}
	if (!render_only || preset_ui_test)
		autosave_enabled = GetPrivateProfileIntW(L"Presets", L"Autosave", 0, SettingsFilePath().c_str()) == 1;
	if (!render_only || preset_ui_test)
		start_minimized = GetPrivateProfileIntW(L"Startup", L"Minimized", 0, SettingsFilePath().c_str()) == 1;
	if (!render_only || preset_ui_test)
		correction_first = GetPrivateProfileIntW(L"Effects", L"CorrectionFirst", 0, SettingsFilePath().c_str()) == 1;
	if (!render_only || preset_ui_test)
	{
		wchar_t order[128] {};
		GetPrivateProfileStringW(L"Effects", L"Order", L"", order, 128, SettingsFilePath().c_str());
		if (*order)
		{
			std::wistringstream in(order);
			std::wstring part;
			std::vector<int> candidate;
			std::array<bool, 8> seen {};
			bool valid = true;
			while (std::getline(in, part, L','))
			{
				try
				{
					size_t consumed = 0;
					int value = std::stoi(part, &consumed);
					if (consumed != part.size() || value < 0 || value > 7 || seen[value])
					{
						valid = false;
						break;
					}
					seen[value] = true;
					candidate.push_back(value);
				}
				catch (...)
				{
					valid = false;
					break;
				}
			}
			if (valid && candidate.size() == 8)
				effect_order = candidate;
		}
		else if (correction_first)
			effect_order = { effect::HdrGamma, effect::ContrastCurves,    effect::ColorBalance, effect::MidtoneGamma,
				             effect::Levels,   effect::ShadowsHighlights, effect::CustomCurves, effect::Imported1dLut };
		wchar_t family[80] {};
		GetPrivateProfileStringW(L"Appearance", L"Font", L"Bahnschrift", family, 80, SettingsFilePath().c_str());
		for (const wchar_t* candidate : { L"Bahnschrift", L"Segoe UI", L"Segoe UI Semilight" })
			if (wcscmp(family, candidate) == 0)
				ui::SetFontFamily(candidate);
	}
	correction_first = ProcessingOrderIndex(effect::HdrGamma) < ProcessingOrderIndex(effect::ContrastCurves);
	regular_look_index = preset_index[preset_role::Look];
	SendMessageW(autosave_control, BM_SETCHECK, autosave_enabled ? BST_CHECKED : BST_UNCHECKED, 0);
	LoadPresetControls();
}

// Avoid repainting unchanged controls during continuous slider input.
void SetTextIfChanged(HWND control, const wchar_t* text)
{
	std::wstring previous(GetWindowTextLengthW(control) + 1, L'\0');
	GetWindowTextW(control, previous.data(), static_cast<int>(previous.size()));
	previous.resize(wcslen(previous.c_str()));
	if (previous != text)
		SetWindowTextW(control, text);
}

void SetControlEnabled(HWND control, bool enabled)
{
	if ((IsWindowEnabled(control) != FALSE) != enabled)
		EnableWindow(control, enabled);
}

void SetControlVisible(HWND control, bool visible)
{
	if (((GetWindowLongPtrW(control, GWL_STYLE) & WS_VISIBLE) != 0) != visible)
		ShowWindow(control, visible ? SW_SHOW : SW_HIDE);
}

void RefreshPresetChoice()
{
	if (!editor_ready)
		return;
	if (slider_dragging)
		return;
	if (preset_library[SelectedPresetRole()].empty())
		return;
	SetTextIfChanged(preset_label, selected_effect != effect::HdrGamma ? L"Look preset" : L"Correction preset");
	std::vector<std::wstring> names;
	for (const auto& entry : preset_library[SelectedPresetRole()])
		names.push_back((entry.path.empty() ? std::wstring(L"Unsaved") : entry.path.stem().wstring()) +
		                (entry.IsDirty() ? L" *" : L""));
	bool changed = SendMessageW(preset_choice, CB_GETCOUNT, 0, 0) != static_cast<LRESULT>(names.size());
	for (size_t i = 0; !changed && i < names.size(); ++i)
	{
		const auto length = SendMessageW(preset_choice, CB_GETLBTEXTLEN, i, 0);
		std::wstring previous(static_cast<size_t>(length) + 1, L'\0');
		SendMessageW(preset_choice, CB_GETLBTEXT, i, reinterpret_cast<LPARAM>(previous.data()));
		previous.resize(static_cast<size_t>(length));
		changed = previous != names[i];
	}
	if (changed)
	{
		SendMessageW(preset_choice, CB_RESETCONTENT, 0, 0);
		for (const auto& name : names)
			SendMessageW(preset_choice, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(name.c_str()));
	}
	if (SendMessageW(preset_choice, CB_GETCURSEL, 0, 0) != static_cast<LRESULT>(preset_index[SelectedPresetRole()]))
		SendMessageW(preset_choice, CB_SETCURSEL, preset_index[SelectedPresetRole()], 0);
	const auto& entry = CurrentPreset(SelectedPresetRole());
	SetControlEnabled(revert_preset_button, entry.IsDirty());
}

bool SaveExistingPreset(int preset_role)
{
	auto& entry = CurrentPreset(preset_role);
	if (entry.path.empty())
		return false;
	try
	{
		if (presets::Read(entry.path) != entry.saved)
			throw std::runtime_error("This preset was changed outside MonitorMage. Use Save as to keep both versions.");
		presets::Write(entry.path, entry.current, true);
		entry.MarkSaved();
		preset_message.clear();
		return true;
	}
	catch (const std::exception& error)
	{
		ShowPresetError(error);
		return false;
	}
}

void SavePresetAsPath(int preset_role, const std::filesystem::path& path, bool replace)
{
	const size_t source_index = preset_index[preset_role];
	const auto data = CurrentPreset(preset_role).current;
	const size_t index = FindPreset(preset_role, path);
	if (index != preset_library[preset_role].size() && index != source_index &&
	    preset_library[preset_role][index].IsDirty())
		throw std::runtime_error("You have unsaved changes to that preset. Choose another filename.");
	presets::Write(path, data, replace);
	if (index == preset_library[preset_role].size())
		preset_library[preset_role].push_back({ std::filesystem::absolute(path), data, data });
	else
		preset_library[preset_role][index] = { std::filesystem::absolute(path), data, data };
	if (index != source_index)
		preset_library[preset_role][source_index].current = preset_library[preset_role][source_index].saved;
	preset_index[preset_role] = index;
	if (preset_role == preset_role::Look && !application_context_active)
		regular_look_index = index;
}

void FlushPresetAutosave()
{
	KillTimer(main_window, TIMER_PRESET);
	if (slider_dragging || (render_only && !preset_ui_test))
		return;
	for (int preset_role = preset_role::Look; preset_role < preset_role::Count; ++preset_role)
		if (CurrentPreset(preset_role).CanAutosave(autosave_enabled))
			SaveExistingPreset(preset_role);
}

void OnPresetEdited(int preset_role)
{
	if (render_only && !preset_ui_test)
		return;
	auto& data = CurrentPreset(preset_role).current;
	if (preset_role == preset_role::Look)
	{
		data.Contrast = contrast_position;
		data.Pivot = pivot_position;
		data.ContrastEnabled = contrast_enabled;
	}
	else
	{
		data.Gamma = gamma_position == 0 ? 22 : 24;
		data.HdrFix = hdr_fix_enabled;
	}
	preset_message.clear();
	RefreshPresetChoice();
	if (!slider_dragging && CurrentPreset(preset_role).CanAutosave(autosave_enabled))
		SetTimer(main_window, TIMER_PRESET, 350, nullptr);
}

void PresetFileDialog(bool save_as)
{
	if (render_only)
		return;
	FlushPresetAutosave();
	try
	{
		const auto folder = PresetFolder(SelectedPresetRole());
		std::filesystem::create_directories(folder);
		wchar_t file[32768] {};
		if (save_as)
			wcscpy_s(file, selected_effect != effect::HdrGamma ? L"My look.lutpreset" : L"My correction.lutpreset");
		OPENFILENAMEW dialog {};
		dialog.lStructSize = sizeof(dialog);
		dialog.hwndOwner = main_window;
		dialog.lpstrFilter = L"MonitorMage preset (*.lutpreset)\0*.lutpreset\0\0";
		dialog.lpstrFile = file;
		dialog.nMaxFile = static_cast<DWORD>(std::size(file));
		dialog.lpstrDefExt = L"lutpreset";
		dialog.lpstrInitialDir = folder.c_str();
		dialog.lpstrTitle = save_as ? L"Save preset as" : L"Open preset";
		dialog.Flags = OFN_NOCHANGEDIR | OFN_PATHMUSTEXIST | (save_as ? OFN_OVERWRITEPROMPT : OFN_FILEMUSTEXIST);
		if (!(save_as ? GetSaveFileNameW(&dialog) : GetOpenFileNameW(&dialog)))
			return;
		if (save_as)
		{
			const bool exists = GetFileAttributesW(file) != INVALID_FILE_ATTRIBUTES;
			SavePresetAsPath(SelectedPresetRole(), file, exists);
		}
		else
			preset_index[SelectedPresetRole()] = ReadPreset(SelectedPresetRole(), file);
		preset_message.clear();
		if (selected_effect != effect::HdrGamma)
			ManualLookOverride();
		PersistPresetChoices();
		LoadPresetControls();
		RefreshPresetChoice();
		ReconcileLiveCorrections();
	}
	catch (const std::exception& error)
	{
		ShowPresetError(error);
	}
	RefreshInterface();
}

bool ConfirmExit()
{
	FlushPresetAutosave();
	bool dirty = false;
	for (const auto& entries : preset_library)
		for (const auto& entry : entries)
			dirty = dirty || entry.IsDirty();
	if (!dirty || render_only)
		return true;
	return unsaved::Confirm(main_window);
}

UINT HotkeyModifiers(WORD shortcut)
{
	const BYTE flags = HIBYTE(shortcut);
	return MOD_NOREPEAT | ((flags & HOTKEYF_CONTROL) ? MOD_CONTROL : 0) | ((flags & HOTKEYF_ALT) ? MOD_ALT : 0) |
	       ((flags & HOTKEYF_SHIFT) ? MOD_SHIFT : 0);
}

bool IsValidHotkey(WORD shortcut)
{
	const BYTE key = LOBYTE(shortcut), flags = HIBYTE(shortcut);
	return key && key != VK_F12 && key != VK_CONTROL && key != VK_MENU && key != VK_SHIFT &&
	       (flags & (HOTKEYF_CONTROL | HOTKEYF_ALT | HOTKEYF_SHIFT)) &&
	       !(flags & ~(HOTKEYF_CONTROL | HOTKEYF_ALT | HOTKEYF_SHIFT | HOTKEYF_EXT));
}

std::wstring HotkeyName(WORD shortcut)
{
	std::wstring name;
	if (HIBYTE(shortcut) & HOTKEYF_CONTROL)
		name += L"Ctrl+";
	if (HIBYTE(shortcut) & HOTKEYF_ALT)
		name += L"Alt+";
	if (HIBYTE(shortcut) & HOTKEYF_SHIFT)
		name += L"Shift+";
	wchar_t key_name[80] {};
	LONG scan = static_cast<LONG>(MapVirtualKeyW(LOBYTE(shortcut), MAPVK_VK_TO_VSC) << 16);
	if (HIBYTE(shortcut) & HOTKEYF_EXT)
		scan |= 1 << 24;
	if (GetKeyNameTextW(scan, key_name, static_cast<int>(std::size(key_name))))
		name += key_name;
	else
		name += L"key " + std::to_wstring(LOBYTE(shortcut));
	return name;
}

bool BindHotkey(WORD shortcut)
{
	if (!IsValidHotkey(shortcut))
	{
		hotkey_error = ERROR_INVALID_PARAMETER;
		return false;
	}
	if (hotkey_registered && shortcut == active_shortcut)
		return true;
	const int next_id = hotkey_registered ? (active_hotkey_id == 1 ? 2 : 1) : 1;
	if (!RegisterHotKey(main_window, next_id, HotkeyModifiers(shortcut), LOBYTE(shortcut)))
	{
		hotkey_error = GetLastError();
		return false;
	}
	if (hotkey_registered)
		UnregisterHotKey(main_window, active_hotkey_id);
	active_hotkey_id = next_id;
	active_shortcut = shortcut;
	hotkey_registered = true;
	hotkey_error = 0;
	return true;
}

void BindPreferredHotkey(WORD preferred)
{
	const WORD candidates[] = { preferred, MAKEWORD(VK_F8, HOTKEYF_CONTROL | HOTKEYF_ALT),
		                        MAKEWORD(VK_F8, HOTKEYF_CONTROL | HOTKEYF_SHIFT),
		                        MAKEWORD(VK_F9, HOTKEYF_CONTROL | HOTKEYF_ALT),
		                        MAKEWORD(VK_F9, HOTKEYF_CONTROL | HOTKEYF_SHIFT) };
	for (const WORD candidate : candidates)
	{
		if (BindHotkey(candidate))
		{
			if (candidate != preferred)
				hotkey_message = L"Your shortcut is unavailable. Using " + HotkeyName(candidate) + L".";
			return;
		}
	}
	hotkey_message = L"The shortcut could not be set. Choose another key combination in Settings.";
}

void LoadAndBindHotkey()
{
	const auto path = SettingsFilePath();
	preferred_shortcut = static_cast<WORD>(GetPrivateProfileIntW(L"Hotkey", L"Bypass", active_shortcut, path.c_str()));
	BindPreferredHotkey(preferred_shortcut);
}

int RunHotkeySelfTest()
{
	// Real Windows registration collisions, with no key injection or display changes.
	main_window = CreateWindowExW(0, L"STATIC", L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, app_instance, nullptr);
	HWND other = CreateWindowExW(0, L"STATIC", L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, app_instance, nullptr);
	if (!main_window || !other)
		return 1;
	WORD blocked = 0;
	for (int key = VK_F8; key <= VK_F11; ++key)
	{
		const WORD candidate = MAKEWORD(key, HOTKEYF_CONTROL | HOTKEYF_ALT | HOTKEYF_SHIFT);
		if (RegisterHotKey(other, 99, HotkeyModifiers(candidate), LOBYTE(candidate)))
		{
			blocked = candidate;
			break;
		}
	}
	int result = 2;
	if (blocked)
	{
		BindPreferredHotkey(blocked);
		if (hotkey_registered && active_shortcut != blocked && !hotkey_message.empty())
		{
			const WORD previous = active_shortcut;
			const bool rejected = !BindHotkey(blocked) && hotkey_error == ERROR_HOTKEY_ALREADY_REGISTERED;
			const bool retained = hotkey_registered && active_shortcut == previous;
			const bool invalid_rejected = !BindHotkey(MAKEWORD(VK_F12, HOTKEYF_CONTROL)) && active_shortcut == previous;
			UnregisterHotKey(other, 99);
			const bool replaced = BindHotkey(blocked);
			const bool old_released = RegisterHotKey(other, 99, HotkeyModifiers(previous), LOBYTE(previous)) != FALSE;
			result = rejected && retained && invalid_rejected && replaced && old_released ? 0 : 3;
		}
	}
	UnregisterHotKey(other, 99);
	if (result == 0)
	{
		bool shift_bound = false;
		for (int key = VK_F20; key <= VK_F24; ++key)
			if (BindHotkey(MAKEWORD(key, HOTKEYF_SHIFT)))
			{
				shift_bound =
					HIBYTE(active_shortcut) == HOTKEYF_SHIFT && (HotkeyModifiers(active_shortcut) & MOD_SHIFT);
				break;
			}
		if (!shift_bound)
			result = 4;
	}
	if (hotkey_registered)
		UnregisterHotKey(main_window, active_hotkey_id);
	hotkey_registered = false;
	DestroyWindow(other);
	DestroyWindow(main_window);
	main_window = nullptr;
	return result;
}

DWORD ProcessPageFaults()
{
	PROCESS_MEMORY_COUNTERS counters {};
	counters.cb = sizeof(counters);
	GetProcessMemoryInfo(GetCurrentProcess(), &counters, sizeof(counters));
	return counters.PageFaultCount;
}

ULONGLONG ProcessCpuTime()
{
	FILETIME created {}, ended {}, kernel {}, user {};
	if (!GetProcessTimes(GetCurrentProcess(), &created, &ended, &kernel, &user))
		return 0;
	ULARGE_INTEGER kernel_ticks {}, user_ticks {};
	kernel_ticks.LowPart = kernel.dwLowDateTime;
	kernel_ticks.HighPart = kernel.dwHighDateTime;
	user_ticks.LowPart = user.dwLowDateTime;
	user_ticks.HighPart = user.dwHighDateTime;
	return kernel_ticks.QuadPart + user_ticks.QuadPart;
}

double ContrastAmount()
{
	return contrast_position / 100.0;
}

double ContrastPivot()
{
	return pivot_position / 100.0;
}

double HdrGamma()
{
	return gamma_position == 0 ? 2.2 : 2.4;
}

live::Settings CurrentSettings();
Source OutputForPreview()
{
	if (live_session.IsActive())
		return live_output;
	Source output;
	const auto source_index = SelectedSourceIndex();
	if (source_index >= 0 && static_cast<size_t>(source_index) < display_sources.size())
		output = display_sources[source_index];
	if (selected_effect == effect::HdrGamma && !CurrentPreset(preset_role::Correction).current.ApplyInSdr)
		output.hdr = true;
	output.white = white_position;
	return output;
}

void PlaceControl(HWND control, int x, int y, int width, int height, bool body = false)
{
	if (x >= 198)
		x += PANE_OFFSET;
	const auto offset = ui::ViewportOffset(main_window);
	if (control == curve_canvas)
		SetWindowPos(control, nullptr, ScaleForDpi(x) - offset.x, ScaleForDpi(y + (body ? 56 : 0)) - offset.y,
		             ScaleForDpi(width), ScaleForDpi(height), SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOCOPYBITS);
	else
		MoveWindow(control, ScaleForDpi(x) - offset.x, ScaleForDpi(y + (body ? 56 : 0)) - offset.y, ScaleForDpi(width),
		           ScaleForDpi(height), TRUE);
}

std::wstring FormatDecimal(double value, int precision = 2)
{
	std::wostringstream text;
	text.imbue(std::locale::classic());
	text << std::fixed << std::setprecision(precision) << value;
	return text.str();
}

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

std::vector<Source> ActiveSources(bool fresh = false)
{
	try
	{
		if (SourceQueryBatch::depth)
		{
			if (fresh || !SourceQueryBatch::valid)
			{
				SourceQueryBatch::snapshot = live::ActiveOutputs();
				SourceQueryBatch::valid = true;
			}
			return SourceQueryBatch::snapshot;
		}
		return live::ActiveOutputs();
	}
	catch (const std::exception& error)
	{
		live_message = ui::messages::FriendlyError(error.what());
		live_error = true;
		return {};
	}
}

void RefreshSources()
{
	SourceQueryBatch query_batch;
	if (night_display)
		SendMessageW(night_display, CB_RESETCONTENT, 0, 0);
	const auto old_index = SelectedSourceIndex();
	const std::wstring previous = old_index >= 0 && static_cast<size_t>(old_index) < display_sources.size()
	                                  ? display_sources[old_index].name
	                                  : L"";
	display_sources = ActiveSources(true);
	SendMessageW(source_choice, CB_RESETCONTENT, 0, 0);
	int select = 0;
	for (size_t i = 0; i < display_sources.size(); ++i)
	{
		SendMessageW(source_choice, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(display_sources[i].label.c_str()));
		if ((live_session.IsActive() && display_sources[i].identity == live_output.identity) ||
		    display_sources[i].name == previous)
			select = static_cast<int>(i);
	}
	SelectSource(static_cast<int>(select));
	hdr_state = display_sources.empty() ? hdr::State {} : hdr::ReadHdrState(display_sources[select].name);
}

bool IsLookBypassed()
{
	return bypassed || profile_action == applications::Action::Bypass;
}

bool IsHdrFixActive()
{
	return hdr_fix_enabled && profile_action != applications::Action::PauseHdr;
}

bool LutProcessingActive()
{
	static live::ProcessingState cache;
	if (!live_session.IsActive())
		return cache.IsProcessing(false, live_output, {});
	return cache.IsProcessing(true, live_output, CurrentSettings());
}

bool IsColorMatrixNeeded()
{
	const auto& look = CurrentPreset(preset_role::Look).current;
	return corrections_requested && !power_suspended && !hdr_pending &&
	       profile_action != applications::Action::Disable && !IsLookBypassed() &&
	       ((look.SaturationEnabled && look.Saturation != 100) || look.MonochromeEnabled);
}

void SyncColorMatrix(bool refresh = false)
{
	if (render_only)
		return;
	if (!IsColorMatrixNeeded())
		KillTimer(main_window, TIMER_MATRIX_RECOVERY);
	try
	{
		const auto& look = CurrentPreset(preset_role::Look).current;
		const bool wanted = IsColorMatrixNeeded();
		matrix_session.Update(main_window, wanted ? (look.SaturationEnabled ? look.Saturation : 100) : -1, refresh,
		                      false, wanted && look.MonochromeEnabled ? &look.Monochrome : nullptr);
		matrix_message.clear();
	}
	catch (const std::exception&)
	{
		matrix_message = L"The color matrix could not start. Your color curves can still work.";
	}
}

void ScheduleColorMatrixRecovery()
{
	KillTimer(main_window, TIMER_MATRIX_RECOVERY);
	if (render_only || !IsColorMatrixNeeded())
		return;
	matrix_recovery.Restart();
	SetTimer(main_window, TIMER_MATRIX_RECOVERY, matrix_recovery.NextDelay(), nullptr);
}

bool ProcessingActive()
{
	return LutProcessingActive() || night_outputs.IsActive() ||
	       (!render_only && IsColorMatrixNeeded() && matrix_session.CurrentState() == matrix_effect::State::Active);
}

app_icons::State CurrentIconState(bool active)
{
	const auto source_index = SelectedSourceIndex();
	const bool hdr = source_index >= 0 && static_cast<size_t>(source_index) < display_sources.size() &&
	                 display_sources[source_index].hdr;
	return app_icons::ChooseState(active, hdr, application_context_active && !foreground_program.IsManualOverride());
}

app_icons::State CurrentIconState()
{
	return CurrentIconState(ProcessingActive());
}

HICON IconForState(int metric, app_icons::State state)
{
	const UINT window_dpi = GetDpiForWindow(main_window);
	return icon_cache.GetIcon(app_instance, state, GetSystemMetricsForDpi(metric, window_dpi ? window_dpi : 96));
}

HICON IconForState(int metric)
{
	return IconForState(metric, CurrentIconState());
}

live::Settings CurrentSettings()
{
	live::Settings settings { contrast_enabled ? ContrastAmount() : 0,
		                      ContrastPivot(),
		                      HdrGamma(),
		                      IsHdrFixActive(),
		                      IsLookBypassed(),
		                      correction_first };
	settings.Adjustments = CurrentPreset(preset_role::Look).current.Adjustments;
	settings.Order = effect_order;
	settings.ApplyInSdr = CurrentPreset(preset_role::Correction).current.ApplyInSdr;
	const auto source_index = SelectedSourceIndex();
	const auto output = live_session.IsActive() ? live_output
	                    : source_index >= 0 && static_cast<size_t>(source_index) < display_sources.size()
	                        ? display_sources[source_index]
	                        : Source {};
	if (night::IsDisplayIncluded(night_config, output.identity))
		settings.NightWarmth = night_level;
	return settings;
}

void RefreshInterface();
void UpdateNightLightSchedule()
{
	KillTimer(main_window, TIMER_NIGHT);
	SYSTEMTIME now {};
	GetLocalTime(&now);
	const auto phase = night::CurrentPhase(night_config, now.wHour * 3600. + now.wMinute * 60. + now.wSecond);
	night_level = phase.level;
	if (!render_only && !is_closing && corrections_requested && !power_suspended && !hdr_pending &&
	    profile_action != applications::Action::Disable && phase.next_ms)
		SetTimer(main_window, TIMER_NIGHT, phase.next_ms, nullptr);
}

void SyncNightOutputs()
{
	if (render_only)
		return;
	try
	{
		if (is_closing || !corrections_requested || power_suspended || hdr_pending ||
		    profile_action == applications::Action::Disable || night_level <= 0)
		{
			if (night_outputs.Stop() != 0)
				throw std::runtime_error("Night light could not restore all displays");
		}
		else
		{
			const auto source_index = SelectedSourceIndex();
			const auto primary = live_session.IsActive() ? live_output
			                     : source_index >= 0 && static_cast<size_t>(source_index) < display_sources.size()
			                         ? display_sources[source_index]
			                         : Source {};
			night_outputs.Update(ActiveSources(), primary, night_config, night_level);
		}
		night_message.clear();
	}
	catch (const std::exception& error)
	{
		night_message = ui::messages::FriendlyError(error.what());
	}
}

struct NightSync
{
	static int& Depth()
	{
		static int count = 0;
		return count;
	}
	NightSync() { ++Depth(); }
	~NightSync()
	{
		if (--Depth() == 0 && !is_closing)
		{
			SyncNightOutputs();
			RefreshInterface();
		}
	}
};

int DisableLiveCorrections(bool retain_request = false)
{
	KillTimer(main_window, TIMER_APPLY);
	if (!retain_request)
		KillTimer(main_window, TIMER_NIGHT);
	const int night_stopped = night_outputs.Stop();
	if (!render_only && (!retain_request || !IsColorMatrixNeeded()))
		matrix_session.Stop();
	const int primary_stopped = live_session.Stop();
	const int result = night_stopped ? night_stopped : primary_stopped;
	live_error = result != 0 && result != 10 && result != 11;
	if (!retain_request || (result != 0 && result != 10 && result != 11))
	{
		corrections_requested = false;
		profile_paused_identity.clear();
		display_retry_pending = false;
		mode_reset_pending = false;
		display_attempts = 0;
	}
	live_message =
		result == 0    ? L"Off. Your previous display colors have been restored."
		: result == 10 ? L"Off. Color changes made by another app have been kept."
		: result == 11
			? L"Paused after a display change. Check the picture, then turn MonitorMage on again."
			: L"MonitorMage could not confirm that your previous display colors were restored. Recovery details are in the temporary folder.";
	return result;
}

void EnableLiveCorrections();
bool WaitForDisplay()
{
	if (!corrections_requested || ++display_attempts > 10)
		return false;
	display_retry_pending = true;
	live_error = false;
	live_message = L"Updating your adjustments after a display change...";
	if (!render_only)
		SetTimer(main_window, TIMER_DISPLAY, 500, nullptr);
	RefreshInterface();
	return true;
}

void ApplyLiveCorrections()
{
	SourceQueryBatch query_batch;
	UpdateNightLightSchedule();
	NightSync night_sync;
	SyncColorMatrix();
	if (!live_session.IsActive())
	{
		if (corrections_requested && !render_only && !power_suspended && !hdr_pending &&
		    profile_action != applications::Action::Disable)
			EnableLiveCorrections();
		return;
	}
	try
	{
		const auto current = ActiveSources();
		const auto at = std::find_if(current.begin(), current.end(),
		                             [&](const Source& output) { return output.identity == live_output.identity; });
		if (at == current.end())
		{
			const auto replacement =
				std::find_if(current.begin(), current.end(),
			                 [&](const Source& output) { return live::SameDisplays(output, live_output); });
			if (replacement == current.end() || !replacement->supported)
			{
				if (WaitForDisplay())
					return;
				throw std::runtime_error("The selected display did not become available after the mode change");
			}
			const auto next = *replacement;
			if (++display_attempts > 10)
				throw std::runtime_error("The display mode did not settle after repeated changes");
			mode_reset_pending = next.hdr != live_output.hdr;
			display_retry_pending = true;
			const int stopped = DisableLiveCorrections(true);
			if (stopped != 0 && stopped != 10 && stopped != 11)
				throw std::runtime_error("Display restoration could not be verified during the mode change");
			RefreshSources();
			for (size_t i = 0; i < display_sources.size(); ++i)
				if (display_sources[i].identity == next.identity)
					SelectSource(static_cast<int>(i));
			EnableLiveCorrections();
			return;
		}
		live_output = *at;
		live_session.Update(live_output, CurrentSettings());
		live_error = false;
		display_retry_pending = false;
		mode_reset_pending = false;
		display_attempts = 0;
		if (!LutProcessingActive())
		{
			const int stopped = live_session.Stop();
			if (stopped != 0 && stopped != 10 && stopped != 11)
				throw std::runtime_error("Your previous display colors could not be restored");
			live_message = L"Ready. Turn on an effect or correction to change the picture.";
			RefreshInterface();
			return;
		}
		live_message =
			live_output.hdr && profile_action == applications::Action::PauseHdr
				? L"This application profile pauses HDR gamma. Your other adjustments stay on."
			: IsLookBypassed() ? L"Effects paused. Monitor corrections can still be active."
			: live_output.hdr
				? L"Your adjustments are on. In HDR, they affect ordinary content while preserving bright HDR highlights."
			: CurrentSettings().HdrFix && CurrentSettings().ApplyInSdr
				? L"Your adjustments are on, including HDR gamma because Also apply in SDR mode is selected."
				: L"Your adjustments are on. HDR gamma is skipped while Windows HDR is off.";
	}
	catch (const std::exception& error)
	{
		// A second mode change can race the query/read/write sequence. Retry
		// only transition failures, with a bounded, event-driven timer.
		const auto latest = ActiveSources(true);
		const bool mode_gone =
			std::none_of(latest.begin(), latest.end(),
		                 [&](const Source& output) { return output.identity == live_output.identity; });
		if ((display_retry_pending || mode_gone) && WaitForDisplay())
			return;
		DisableLiveCorrections();
		live_message = ui::messages::FriendlyError(error.what());
		live_error = true;
	}
	RefreshInterface();
}

void EnableLiveCorrections()
{
	SourceQueryBatch query_batch;
	UpdateNightLightSchedule();
	NightSync night_sync;
	SyncColorMatrix();
	if (render_only || live_session.IsActive())
		return;
	try
	{
		const auto source_index = SelectedSourceIndex();
		if (source_index < 0 || static_cast<size_t>(source_index) >= display_sources.size())
			throw std::runtime_error("Select an active display group");
		if (active_test && WaitForSingleObject(active_test, 0) == WAIT_TIMEOUT)
			throw std::runtime_error("Close the timed diagnostic before enabling live correction");
		live_output = display_sources[source_index];
		static live::ProcessingState pending;
		const auto settings = CurrentSettings();
		if (!mode_reset_pending && !pending.IsProcessing(true, live_output, settings))
		{
			display_retry_pending = false;
			display_attempts = 0;
			live_error = false;
			live_message = L"Ready. Turn on an effect or correction to change the picture.";
			RefreshInterface();
			return;
		}
		night_outputs.Release(live_output, &live_session);
		live_session.Start(live_output, settings, mode_reset_pending);
		mode_reset_pending = false;
		ApplyLiveCorrections();
	}
	catch (const std::exception& error)
	{
		if (display_retry_pending)
		{
			DisableLiveCorrections(true);
			if (WaitForDisplay())
				return;
		}
		DisableLiveCorrections();
		live_message = ui::messages::FriendlyError(error.what());
		live_error = true;
	}
	RefreshInterface();
}

void ToggleBypass()
{
	const bool previous = IsLookBypassed();
	if (profile_action == applications::Action::Bypass)
	{
		foreground_program.SetManualOverride();
		profile_action = applications::Action::Look;
	}
	bypassed = !previous;
	ApplyLiveCorrections();
	RefreshInterface();
}

void ReconcileLiveCorrections()
{
	SourceQueryBatch query_batch;
	SyncColorMatrix();
	if (power_suspended)
	{
		RefreshInterface();
		return;
	}
	if (profile_action == applications::Action::Disable)
	{
		if (live_session.IsActive() || night_outputs.IsActive())
		{
			profile_paused_identity = live_output.identity;
			DisableLiveCorrections(true);
		}
		if (corrections_requested)
		{
			const auto source_index = SelectedSourceIndex();
			if (profile_paused_identity.empty() && source_index >= 0 &&
			    static_cast<size_t>(source_index) < display_sources.size())
				profile_paused_identity = display_sources[source_index].identity;
			live_message =
				L"This application profile pauses MonitorMage. Your adjustments resume when you switch to another program.";
		}
	}
	else if (corrections_requested)
	{
		if (live_session.IsActive())
			ApplyLiveCorrections();
		else
		{
			profile_paused_identity.clear();
			EnableLiveCorrections();
		}
	}
	RefreshInterface();
}

void ToggleCorrections()
{
	if (hdr_pending || (render_only && !preset_ui_test))
		return;
	corrections_requested = !corrections_requested;
	if (!corrections_requested)
		DisableLiveCorrections();
	else
		ReconcileLiveCorrections();
	RefreshInterface();
}

void TurnOnCorrections()
{
	corrections_requested = true;
	live_error = false;
	live_message = L"Ready. Turn on an effect or correction to change the picture.";
	ReconcileLiveCorrections();
}

void ToggleWindowsHdrMode()
{
	if (render_only || hdr_pending)
		return;
	try
	{
		const auto source_index = SelectedSourceIndex();
		if (source_index < 0 || static_cast<size_t>(source_index) >= display_sources.size())
			throw std::runtime_error("Select a display before switching HDR");
		const auto state = hdr::ReadHdrState(display_sources[source_index].name);
		if (!state.available)
		{
			live_message = state.reason;
			RefreshInterface();
			return;
		}
		hdr_resume = corrections_requested;
		hdr_source = state.source;
		hdr_desired = !state.on;
		hdr_transition = state;
		if (DisableLiveCorrections() != 0)
			throw std::runtime_error("Restore the display baseline before changing HDR mode");
		mode_reset_pending = hdr_resume;
		hdr_pending = true;
		hdr_attempts = 0;
		hdr::SetHdrEnabled(state, hdr_desired);
		live_message = L"Switching Windows HDR...";
		if (!SetTimer(main_window, TIMER_HDR, 600, nullptr))
			throw std::runtime_error(
				"HDR was requested but its completion timer could not start; corrections remain off");
	}
	catch (const std::exception& error)
	{
		hdr_pending = false;
		hdr_resume = false;
		mode_reset_pending = false;
		live_message = ui::messages::FriendlyError(error.what());
		live_error = true;
	}
	RefreshInterface();
}

void ManualLookOverride()
{
	if (profile_configuration.Enabled && application_context_active)
		foreground_program.SetManualOverride();
	else
	{
		regular_look_index = preset_index[preset_role::Look];
		foreground_program.ClearManualOverride();
	}
	profile_action = applications::Action::Look;
	profile_message.clear();
}

void ApplyApplicationProfile(bool force)
{
	if (profile_configuration.Enabled && foreground_program.IsManualOverride() && !force)
		return;
	if (force)
		foreground_program.ClearManualOverride();
	auto decision = applications::ResolveForProgram(profile_configuration, foreground_program.FocusedProgram());
	// Switching between programs without profiles does not change the look.
	// Avoid re-querying displays and reapplying curves on every focus change.
	if (!force && !decision.matched && !application_context_active)
		return;
	FlushPresetAutosave();
	profile_message.clear();
	if (!decision.matched)
	{
		if (application_context_active)
			preset_index[preset_role::Look] = regular_look_index;
		application_context_active = false;
	}
	else
	{
		// Snapshot once across an entire chain of profiled applications, including manual overrides.
		if (!application_context_active)
			regular_look_index = preset_index[preset_role::Look];
		application_context_active = true;
	}
	if (decision.matched && decision.action == applications::Action::Look)
	{
		try
		{
			preset_index[preset_role::Look] = ReadPreset(preset_role::Look, decision.look);
		}
		catch (const std::exception&)
		{
			preset_index[preset_role::Look] = regular_look_index;
			profile_message = L"The preset for this program could not be loaded. Your previous look has been restored.";
		}
	}
	profile_action = decision.action;
	LoadPresetControls();
	RefreshPresetChoice();
	ReconcileLiveCorrections();
	RefreshInterface();
}

void OnForegroundProgramChanged(const std::filesystem::path& path)
{
	if (foreground_program.UpdateFocusedProgram(path))
	{
		ApplyApplicationProfile();
		RefreshInterface();
	}
}

void CheckForegroundProgram(HWND recent = nullptr)
{
	HWND foreground = GetForegroundWindow();
	if (!foreground)
		return;
	DWORD process = 0;
	GetWindowThreadProcessId(foreground, &process);
	if (process == GetCurrentProcessId() && recent && IsWindow(recent))
	{
		foreground = recent;
		GetWindowThreadProcessId(foreground, &process);
	}
	if (!process || process == GetCurrentProcessId())
		return;
	wchar_t class_name[128] {};
	GetClassNameW(foreground, class_name, static_cast<int>(std::size(class_name)));
	if (wcscmp(class_name, L"Shell_TrayWnd") == 0 || wcscmp(class_name, L"Shell_SecondaryTrayWnd") == 0 ||
	    wcscmp(class_name, L"MultitaskingViewFrame") == 0)
		return;
	HANDLE handle = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, process);
	wchar_t path[32768] {};
	DWORD length = static_cast<DWORD>(std::size(path));
	const bool known = handle && QueryFullProcessImageNameW(handle, 0, path, &length);
	if (handle)
		CloseHandle(handle);
	// An inaccessible process ends the temporary application override and restores the regular look.
	OnForegroundProgramChanged(known ? std::filesystem::path(path)
	                                 : std::filesystem::path(L"Unknown process " + std::to_wstring(process)));
}

void CALLBACK ForegroundChangedCallback(HWINEVENTHOOK, DWORD, HWND foreground, LONG, LONG, DWORD, DWORD)
{
	if (main_window)
		PostMessageW(main_window, MESSAGE_FOREGROUND, 0, reinterpret_cast<LPARAM>(foreground));
}

bool ApplyProfileConfiguration(const applications::Configuration& candidate)
{
	try
	{
		applications::Validate(candidate);
		if (candidate.Enabled && !foreground_hook && !render_only)
			throw std::runtime_error(
				"Windows foreground notifications are unavailable; automatic profiles cannot be enabled");
		for (const auto& rule : candidate.Rules)
			if (rule.Action == applications::Action::Look)
				ReadPreset(preset_role::Look, rule.Look);
		const auto previous = profile_configuration;
		profile_configuration = candidate;
		if (!PersistPresetChoices())
		{
			profile_configuration = previous;
			RefreshInterface();
			return false;
		}
		foreground_program.ClearManualOverride();
		profile_message.clear();
		ApplyApplicationProfile(true);
		RefreshInterface();
		return true;
	}
	catch (const std::exception& error)
	{
		ShowPresetError(error);
		RefreshInterface();
		return false;
	}
}

void ShowProfiles(bool hidden = false)
{
	EnsureCommonControls();
	DiscoverPresets(preset_role::Look);
	std::vector<profile_editor::Look> looks;
	for (const auto& entry : preset_library[preset_role::Look])
		if (!entry.path.empty())
			looks.push_back({ entry.path.stem().wstring(), entry.path });
	const auto last = foreground_program.FocusedProgram().is_absolute() ? foreground_program.FocusedProgram()
	                                                                    : std::filesystem::path {};
	profile_editor::ShowProfileEditor(main_window, app_instance, profile_configuration, looks, last,
	                                  ApplyProfileConfiguration, hidden);
}

bool& AdjustmentEnabled(int effect)
{
	auto& adjustments = CurrentPreset(preset_role::Look).current.Adjustments;
	switch (effect)
	{
		case 2:
			return adjustments.BalanceEnabled;
		case 3:
			return adjustments.GammaEnabled;
		case 4:
			return adjustments.LevelsEnabled;
		case 5:
			return adjustments.ToneEnabled;
		case 6:
			return adjustments.CurvesEnabled;
		default:
			return adjustments.ImportEnabled;
	}
}

void ToggleEffect(int effect)
{
	if (effect == effect::ColorFilters)
	{
		ChangeWindowsFilter(!filter_state.on, filter_state.filter);
		return;
	}
	if (effect == effect::NightLight)
	{
		night_config.Enabled = !night_config.Enabled;
		PersistPresetChoices();
		ApplyLiveCorrections();
		RefreshInterface();
		return;
	}
	if (effect >= effect::Saturation)
	{
		auto& look = CurrentPreset(preset_role::Look).current;
		auto& enabled = effect == effect::Saturation ? look.SaturationEnabled : look.MonochromeEnabled;
		enabled = !enabled;
		OnPresetEdited(preset_role::Look);
		SyncColorMatrix();
		RefreshInterface();
		return;
	}
	if (effect == effect::ContrastCurves)
		contrast_enabled = !contrast_enabled;
	else if (effect == effect::HdrGamma)
		hdr_fix_enabled = !hdr_fix_enabled;
	else
		AdjustmentEnabled(effect) = !AdjustmentEnabled(effect);
	OnPresetEdited(effect == effect::HdrGamma ? 1 : 0);
	ApplyLiveCorrections();
	RefreshInterface();
}

void MoveEffect(int effect, int target)
{
	if (target < 0 || target >= static_cast<int>(effect_order.size()))
		return;
	const auto before = effect_order;
	const auto position = std::find(effect_order.begin(), effect_order.end(), effect);
	effect_order.erase(position);
	effect_order.insert(effect_order.begin() + target, effect);
	correction_first =
		std::find(effect_order.begin(), effect_order.end(), 1) < std::find(effect_order.begin(), effect_order.end(), 0);
	if (!render_only && !PersistPresetChoices())
	{
		effect_order = before;
		correction_first = std::find(before.begin(), before.end(), 1) < std::find(before.begin(), before.end(), 0);
		RefreshInterface();
		return;
	}
	ApplyLiveCorrections();
	RefreshInterface();
}

void MoveCreative(int effect, int target)
{
	auto order = CreativeOrder();
	if (effect == effect::HdrGamma || effect >= effect::Saturation || target < 0 ||
	    target >= static_cast<int>(order.size()) - 2)
		return;
	const auto before = effect_order;
	const auto found = std::find(order.begin(), order.end(), effect);
	if (found == order.end())
		return;
	order.erase(found);
	order.insert(order.begin() + target, effect);
	size_t index = 0;
	for (auto& slot : effect_order)
		if (slot != effect::HdrGamma)
			slot = order[index++];
	correction_first = ProcessingOrderIndex(effect::HdrGamma) < ProcessingOrderIndex(effect::ContrastCurves);
	if (!render_only && !PersistPresetChoices())
	{
		effect_order = before;
		correction_first = ProcessingOrderIndex(effect::HdrGamma) < ProcessingOrderIndex(effect::ContrastCurves);
		RefreshInterface();
		return;
	}
	ApplyLiveCorrections();
	RefreshInterface();
}

struct Parameter
{
	const wchar_t* label;
	int minimum, maximum;
	int* value;
	int default_value = 0;
};

std::vector<Parameter> SliderParameters()
{
	auto& adjustments = CurrentPreset(preset_role::Look).current.Adjustments;
	if (selected_effect == effect::ColorBalance)
		return { { L"Red", 50, 150, &adjustments.Balance[color::BALANCE_RED], 100 },
			     { L"Green", 50, 150, &adjustments.Balance[color::BALANCE_GREEN], 100 },
			     { L"Blue", 50, 150, &adjustments.Balance[color::BALANCE_BLUE], 100 },
			     { L"Warmth", -50, 50, &adjustments.Balance[color::BALANCE_WARMTH] },
			     { L"Tint", -50, 50, &adjustments.Balance[color::BALANCE_TINT] } };
	if (selected_effect == effect::MidtoneGamma)
		return { { L"Midtone gamma", 50, 200, &adjustments.Midtone, 100 } };
	if (selected_effect == effect::Levels)
		return { { L"Input black", 0, 95, &adjustments.Levels[color::INPUT_BLACK] },
			     { L"Input white", 5, 100, &adjustments.Levels[color::INPUT_WHITE], 100 },
			     { L"Output black", 0, 95, &adjustments.Levels[color::OUTPUT_BLACK] },
			     { L"Output white", 5, 100, &adjustments.Levels[color::OUTPUT_WHITE], 100 } };
	if (selected_effect == effect::ShadowsHighlights)
		return { { L"Shadows", -50, 50, &adjustments.Shadows }, { L"Highlights", -50, 50, &adjustments.Highlights } };
	if (selected_effect == effect::Saturation)
		return { { L"Saturation", 0, color::SATURATION_MAXIMUM, &CurrentPreset(preset_role::Look).current.Saturation,
			       100 } };
	if (selected_effect == effect::ChannelMixer)
	{
		auto& mix = CurrentPreset(preset_role::Look).current.Monochrome;
		return { { L"Red", 0, 200, &mix[0], 21 }, { L"Green", 0, 200, &mix[1], 72 }, { L"Blue", 0, 200, &mix[2], 7 } };
	}
	if (selected_effect == effect::NightLight)
		return { { L"Warmth", 0, 100, &night_config.Strength, 35 },
			     { L"Fade minutes", 0, 60, &night_config.Fade, 10 } };
	return {};
}

std::vector<HWND> TabStopControls()
{
	// Sidebar, then the right pane in reading order. The graph is an input
	// only for curve editing, between the monitor's upper and lower buttons.
	std::vector<HWND> order { correction_list, windows_effect_list, effect_list,
		                      preset_choice,   autosave_control,    save_preset_button,
		                      save_as_button,  export_button,       revert_preset_button };
	if (settings_page)
		order.insert(order.end(), { font_choice, shortcut_control, shortcut_button, startup_control, bypass_button,
		                            start_minimized_control, open_preset_button, saturation_report_button });
	else
	{
		order.insert(order.end(), { selected_effect_toggle, reset_button });
		order.insert(order.end(), { contrast_slider, pivot_slider, white_slider,
		                            gamma_position == 0 ? gamma_choice : gamma_other, hdr_fix_control });
		order.insert(order.end(), adjustment_sliders.begin(), adjustment_sliders.end());
		order.insert(order.end(), { curve_channel, import_button, export_1d_button });
		if (selected_effect == effect::ColorFilters)
			order.insert(order.end(), { filter_choice, filter_settings_button });
		if (selected_effect == effect::NightLight)
			order.insert(order.end(), { night_schedule, night_start, night_end, night_display, night_include });
	}
	order.insert(order.end(), { hdr_button, info_button, power_button });
	if (!settings_page && selected_effect == effect::CustomCurves)
		order.push_back(curve_canvas);
	order.insert(order.end(),
	             { monitor_settings_button, profiles_button, source_choice, website_link, monitor_profile });
	order.erase(std::remove_if(order.begin(), order.end(),
	                           [](HWND control)
	                           {
								   if (!control)
									   return true;
								   for (auto at = control; at && at != main_window; at = GetParent(at))
									   if (!(GetWindowLongPtrW(at, GWL_STYLE) & WS_VISIBLE) || !IsWindowEnabled(at))
										   return true;
								   return false;
							   }),
	            order.end());
	return order;
}

HWND NextTabStop(HWND from, bool reverse)
{
	const auto order = TabStopControls();
	if (order.empty())
		return nullptr;
	const auto found = std::find(order.begin(), order.end(), from);
	if (found == order.end())
		return reverse ? order.back() : order.front();
	const auto index = static_cast<size_t>(found - order.begin());
	return order[(index + order.size() + (reverse ? -1 : 1)) % order.size()];
}

bool NavigateTab(MSG& message)
{
	if (message.message != WM_KEYDOWN || message.wParam != VK_TAB ||
	    GetAncestor(message.hwnd, GA_ROOT) != main_window || (GetKeyState(VK_CONTROL) & 0x8000) ||
	    (GetKeyState(VK_MENU) & 0x8000))
		return false;
	const auto from = GetFocus(), next = NextTabStop(from, (GetKeyState(VK_SHIFT) & 0x8000) != 0);
	if (!next)
		return false;
	wchar_t class_name[32] {};
	if (from)
		GetClassNameW(from, class_name, 32);
	if (wcscmp(class_name, L"ComboBox") == 0)
		SendMessageW(from, CB_SHOWDROPDOWN, FALSE, 0);
	SendMessageW(main_window, WM_UPDATEUISTATE, MAKEWPARAM(UIS_CLEAR, UISF_HIDEFOCUS), 0);
	SetFocus(next);
	ui::RevealControl(main_window, next);
	return true;
}

bool effect_layout_valid = false;
void LayoutEffectControls()
{
	if (!ui_font || !effect_description)
		return;
	static int previous_page = -1;
	static bool previous_settings = false;
	static std::wstring previous_text;
	wchar_t text[1024] {};
	GetWindowTextW(effect_description, text, 1024);
	if (effect_layout_valid && previous_page == selected_effect && previous_settings == settings_page &&
	    previous_text == text)
		return;
	effect_layout_valid = true;
	previous_page = selected_effect;
	previous_settings = settings_page;
	previous_text = text;
	if (settings_page)
	{
		PlaceControl(curve_canvas, 198, 262, 558, 268);
		LayoutMonitorControls();
		InvalidateRect(curve_canvas, nullptr, FALSE);
		return;
	}
	HDC dc = GetDC(effect_description);
	const auto old = SelectObject(dc, ui_font);
	TEXTMETRICW metrics {};
	GetTextMetricsW(dc, &metrics);
	RECT measure { 0, 0, ScaleForDpi(558), 0 };
	DrawTextW(dc, text, -1, &measure, DT_CALCRECT | DT_WORDBREAK | DT_NOPREFIX);
	SelectObject(dc, old);
	ReleaseDC(effect_description, dc);
	// Reserve at least two font lines, then an explicit eight-DIP gap. The
	// native static's actual line metrics vary with font and display scaling.
	const int text_height = std::max(measure.bottom, 2 * metrics.tmHeight) + ScaleForDpi(2);
	const int height = (text_height * 96 + dpi - 1) / dpi, first = 108 + height + 8;
	PlaceControl(effect_description, 198, 108, 558, height);
	const auto row = [&](HWND label, HWND value, HWND slider, int index)
	{
		const int top = first + index * 24;
		PlaceControl(label, 198, top, 106, 22);
		PlaceControl(value, 306, top, 56, 22);
		PlaceControl(slider, 368, top, 388, 22);
	};
	row(amount_label, amount_value, contrast_slider, 0);
	row(pivot_label, pivot_value, pivot_slider, 1);
	row(white_label, white_value, white_slider, 0);
	for (size_t i = 0; i < adjustment_sliders.size(); ++i)
		row(adjustment_labels[i], adjustment_values[i], adjustment_sliders[i], static_cast<int>(i));
	const auto width = [&](HWND control, int minimum = 64, int maximum = 240)
	{
		return ui::ContentWidth(control, ui_font, minimum, maximum);
	};
	const int gamma_width = width(gamma_choice, 56, 100);
	PlaceControl(gamma_label, 198, first + 24, 130, 22);
	PlaceControl(gamma_choice, 331, first + 24, gamma_width, 22);
	PlaceControl(gamma_other, 331 + gamma_width + 12, first + 24, width(gamma_other, 56, 100), 22);
	PlaceControl(hdr_fix_control, 198, first + 48, 550, 22);
	PlaceControl(curve_channel, 198, first, width(curve_channel, 84, 200), 180);
	const int import_width = width(import_button);
	PlaceControl(import_button, 198, first, import_width, 28);
	PlaceControl(export_1d_button, 198 + import_width + 12, first, width(export_1d_button), 28);
	if (selected_effect == effect::ColorFilters)
	{
		const int choice_width = width(filter_choice, 160, 350);
		PlaceControl(filter_choice, 198, first, choice_width, 180);
		PlaceControl(filter_settings_button, 198 + choice_width + 12, first, width(filter_settings_button), 28);
		PlaceControl(correction_note, 198, first + 40, 558, 42);
	}
	if (selected_effect == effect::NightLight)
	{
		PlaceControl(night_schedule, 198, first + 51, 166, 22);
		PlaceControl(night_start_label, 376, first + 51, 40, 22);
		PlaceControl(night_start, 418, first + 50, 72, 24);
		PlaceControl(night_end_label, 514, first + 51, 40, 22);
		PlaceControl(night_end, 556, first + 50, 72, 24);
		PlaceControl(night_display, 198, first + 82, 256, 180);
		PlaceControl(night_include, 466, first + 82, 290, 22);
		PlaceControl(correction_note, 198, first + 108, 558, 22);
	}
	const auto align_track = [&](HWND slider)
	{
		RECT channel {}, bounds {};
		SendMessageW(slider, TBM_GETCHANNELRECT, 0, reinterpret_cast<LPARAM>(&channel));
		GetWindowRect(slider, &bounds);
		MapWindowPoints(nullptr, main_window, reinterpret_cast<POINT*>(&bounds), 2);
		const int right = bounds.right - bounds.left - channel.right, h = bounds.bottom - bounds.top;
		MoveWindow(slider, ScaleForDpi(376 + PANE_OFFSET) - ui::ViewportOffset(main_window).x - channel.left,
		           bounds.top + h / 2 - (channel.top + channel.bottom) / 2, ScaleForDpi(380) + channel.left + right, h,
		           TRUE);
	};
	for (HWND slider : { contrast_slider, pivot_slider, white_slider })
		align_track(slider);
	for (HWND slider : adjustment_sliders)
		align_track(slider);
	const int rows = selected_effect == effect::NightLight     ? 6
	                 : selected_effect == effect::ColorBalance ? 5
	                 : selected_effect == effect::Levels       ? 4
	                 : selected_effect == effect::HdrGamma     ? 3
	                                                           : 2;
	const int bottom = first + (rows - 1) * 24 + 22;
	const int graph_top = std::max(262, bottom + 8);
	PlaceControl(curve_canvas, 198, graph_top, 558, 530 - graph_top);
	LayoutMonitorControls();
	InvalidateRect(curve_canvas, nullptr, FALSE);
}

void UpdateTray()
{
	if (!tray_installed)
		return;
	const auto& look = CurrentPreset(preset_role::Look);
	const auto origin =
		application_context_active
			? (foreground_program.IsManualOverride() ? L"Manual override: " : L"Application profile: ") +
				  programs::DisplayName(foreground_program.FocusedProgram())
			: std::wstring(L"Manual look");
	std::wstring look_state = (look.path.empty() ? std::wstring(L"Unsaved look") : look.path.stem().wstring()) +
	                          (look.IsDirty() ? L" *" : L"");
	if (profile_action == applications::Action::Disable)
		look_state += L" (all adjustments paused)";
	else
	{
		if (profile_action == applications::Action::PauseHdr)
			look_state += L" (HDR gamma paused)";
		if (IsLookBypassed())
			look_state += L" (look bypassed)";
	}
	const HICON small_icon = IconForState(SM_CXSMICON);
	if (tray_installed)
	{
		const std::wstring tip = origin + L" | " + look_state + (!corrections_requested ? L" (off)" : L"");
		wchar_t next_tip[std::size(tray_icon.szTip)] {};
		wcsncpy_s(next_tip, tip.c_str(), _TRUNCATE);
		tray_icon.uFlags = wcscmp(tray_icon.szTip, next_tip) ? NIF_TIP : 0;
		wcscpy_s(tray_icon.szTip, next_tip);
		if (small_icon && tray_icon.hIcon != small_icon)
		{
			tray_icon.hIcon = small_icon;
			tray_icon.uFlags |= NIF_ICON;
		}
		if (tray_icon.uFlags)
			Shell_NotifyIconW(NIM_MODIFY, &tray_icon);
	}
}

void RefreshInterface()
{
	if (!editor_ready || (!render_only && !editor_sync_depth && !IsWindowVisible(main_window)))
	{
		if (runtime_ready)
			UpdateTray();
		return;
	}
	const auto source_index = SelectedSourceIndex();
	const bool valid = source_index >= 0 && static_cast<size_t>(source_index) < display_sources.size();
	const bool correction_powered =
		corrections_requested && !power_suspended && !hdr_pending && profile_action != applications::Action::Disable &&
		profile_action != applications::Action::PauseHdr && valid && display_sources[source_index].supported &&
		(display_sources[source_index].hdr || CurrentPreset(preset_role::Correction).current.ApplyInSdr);
	std::array<bool, effect::Count> enabled { contrast_enabled, hdr_fix_enabled };
	for (int i = 2; i < 8; ++i)
		enabled[i] = AdjustmentEnabled(i);
	enabled[effect::Saturation] = CurrentPreset(preset_role::Look).current.SaturationEnabled;
	enabled[effect::ChannelMixer] = CurrentPreset(preset_role::Look).current.MonochromeEnabled;
	effects::Update(effect_list, CreativeOrder(), enabled, false, selected_effect, corrections_requested);
	effects::Update(correction_list, { 1 }, enabled, profile_action == applications::Action::PauseHdr, selected_effect,
	                correction_powered);
	const bool night_powered =
		corrections_requested && night_level > 0 && !power_suspended && !hdr_pending &&
		profile_action != applications::Action::Disable &&
		std::any_of(display_sources.begin(), display_sources.end(),
	                [](const Source& output)
	                { return output.supported && night::IsDisplayIncluded(night_config, output.identity); });
	enabled[effect::ColorFilters] = filter_state.on;
	enabled[effect::NightLight] = night_config.Enabled;
	effects::Update(windows_effect_list, { 8, 9 }, enabled, false, selected_effect, night_powered);
	SetTextIfChanged(effect_title, effects::EFFECT_NAMES[selected_effect]);
	SetTextIfChanged(selected_effect_toggle,
	                 (std::wstring(effects::EFFECT_NAMES[selected_effect]) + L" on/off").c_str());
	ui::ToggleButton(selected_effect_toggle,
	                 selected_effect == effect::ColorFilters ? filter_state.on : enabled[selected_effect],
	                 selected_effect == effect::ColorFilters ? true
	                 : selected_effect == effect::NightLight ? night_powered
	                 : selected_effect == effect::HdrGamma   ? correction_powered
	                                                         : corrections_requested);
	SetControlVisible(selected_effect_toggle, !settings_page);
	SetControlVisible(reset_button, !settings_page && (selected_effect < effect::FirstWindows ||
	                                                   selected_effect == effect::NightLight ||
	                                                   selected_effect >= effect::Saturation));
	SetControlEnabled(selected_effect_toggle,
	                  selected_effect != effect::ColorFilters ||
	                      (filter_state.available && !filter_controller.IsBusy() && !render_only));
	if (selected_effect < effect::FirstCreative)
		SetTextIfChanged(
			effect_description,
			selected_effect == effect::ContrastCurves
				? L"Make dark and bright tones stand apart. Pivot sets the tone that stays unchanged. Zero has no effect."
				: L"Adjust shadows in ordinary content when Windows HDR is on. Gamma changes the shadow brightness.");
	const wchar_t* explanations[] {
		L"",
		L"",
		L"Balance red, green and blue, or make the picture warmer, cooler or tinted. Brightness is kept within range.",
		L"Brighten or darken the middle tones while keeping black and white unchanged. 1.00 has no effect.",
		L"Input sets which tones become black or white. Output sets how dark or bright they can be.",
		L"Adjust dark and bright areas separately while keeping pure black and white unchanged.",
		L"Drag points to change the curve. Double-click resets a point; right-click resets the whole curve.",
		L"Load color curves from a file. Supports 1D cube, spi1d, CAL, CSV and PNG/TIFF ramps, but not 3D LUTs."
	};
	if (selected_effect >= effect::FirstCreative && selected_effect < effect::FirstWindows)
		SetTextIfChanged(effect_description, explanations[selected_effect]);
	if (selected_effect == effect::ColorFilters)
		SetTextIfChanged(
			effect_description,
			L"Windows color filters apply to all displays for your account. They stay on when MonitorMage is off or closed.");
	if (selected_effect == effect::Saturation)
		SetTextIfChanged(effect_description, L"All displays: 0% grayscale, 100% unchanged. Strong boosts may clip.");
	if (selected_effect == effect::ChannelMixer)
		SetTextIfChanged(
			effect_description,
			L"Make a black-and-white look by mixing red, green and blue. White stays white. All zeros use the standard mix. Applies to all displays.");
	if (selected_effect == effect::NightLight)
		SetTextIfChanged(
			effect_description,
			L"Warmer colors in the evening. Applies to all included displays, independently of your look. Linked displays change together.");
	const auto controls = SliderParameters();
	const bool changing_adjustment_page = adjustment_page != (settings_page ? -1 : selected_effect);
	for (size_t i = 0; i < adjustment_sliders.size(); ++i)
	{
		const bool visible = i < controls.size() && !settings_page;
		SetControlVisible(adjustment_sliders[i], visible && !changing_adjustment_page);
		SetControlVisible(adjustment_labels[i], visible);
		SetControlVisible(adjustment_values[i], visible);
		if (visible)
		{
			const auto& p = controls[i];
			COLORREF colour = ui::THEME_ACCENT;
			if (selected_effect == effect::NightLight && i == 0)
				colour = RGB(255, 165, 82);
			if (selected_effect == effect::ChannelMixer)
			{
				constexpr COLORREF CHANNEL_COLORS[] { RGB(245, 95, 104), RGB(92, 210, 126), RGB(90, 155, 248) };
				colour = CHANNEL_COLORS[i];
			}
			if (selected_effect == effect::ColorBalance)
			{
				constexpr COLORREF CHANNEL_COLORS[] { RGB(245, 95, 104), RGB(92, 210, 126), RGB(90, 155, 248) };
				if (i < 3)
					colour = CHANNEL_COLORS[i];
				else if (*p.value)
				{
					const auto target = i == 3 ? (*p.value > 0 ? RGB(255, 165, 82) : RGB(113, 173, 255))
					                           : (*p.value > 0 ? RGB(228, 112, 220) : RGB(104, 215, 139));
					const double mix = .45 + .55 * std::abs(*p.value) / 50.0;
					const auto blend = [&](int a, int b)
					{
						return static_cast<BYTE>(std::lround(a + (b - a) * mix));
					};
					colour = RGB(blend(GetRValue(ui::THEME_MUTED), GetRValue(target)),
					             blend(((ui::THEME_MUTED >> 8) & 255), GetGValue(target)),
					             blend(GetBValue(ui::THEME_MUTED), GetBValue(target)));
				}
				else
					colour = ui::THEME_MUTED;
			}
			ui::SliderColour(adjustment_sliders[i], colour);
			ui::ConfigureSlider(adjustment_sliders[i], p.default_value,
			                    (selected_effect == effect::ColorBalance || selected_effect == effect::ChannelMixer) &&
			                            i < 3
			                        ? L"RGB"[i]
			                        : 0);
			if (adjustment_page != selected_effect)
				SendMessageW(adjustment_sliders[i], TBM_SETRANGE, TRUE, MAKELPARAM(p.minimum, p.maximum));
			if (SendMessageW(adjustment_sliders[i], TBM_GETPOS, 0, 0) != *p.value)
				SendMessageW(adjustment_sliders[i], TBM_SETPOS, TRUE, *p.value);
			SetTextIfChanged(adjustment_labels[i], p.label);
			SetTextIfChanged(adjustment_values[i],
			                 (selected_effect >= effect::Saturation || (selected_effect == effect::NightLight && i == 0)
			                      ? std::to_wstring(*p.value) + L"%"
			                  : selected_effect == effect::NightLight ? std::to_wstring(*p.value)
			                                                          : FormatDecimal(*p.value / 100.0))
			                     .c_str());
		}
	}
	static constexpr const wchar_t* PARAMETER_HELP[][5] {
		{ L"Red balance", L"Green balance", L"Blue balance", L"Warmth", L"Tint" },
		{ L"Midtone brightness" },
		{ L"Input black level", L"Input white level", L"Output black level", L"Output white level" },
		{ L"Shadow brightness", L"Highlight brightness" }
	};
	if (selected_effect >= effect::FirstCreative && selected_effect <= effect::ShadowsHighlights)
		for (size_t i = 0; i < controls.size(); ++i)
			ui::SetHelpText(adjustment_sliders[i], PARAMETER_HELP[selected_effect - effect::FirstCreative][i]);
	if (selected_effect == effect::Saturation)
		ui::SetHelpText(adjustment_sliders[0], L"Color strength");
	if (selected_effect == effect::ChannelMixer)
		for (int i = 0; i < 3; ++i)
		{
			const wchar_t* help[] { L"Red contribution", L"Green contribution", L"Blue contribution" };
			ui::SetHelpText(adjustment_sliders[i], help[i]);
		}
	if (selected_effect == effect::NightLight)
	{
		ui::SetHelpText(adjustment_sliders[0], L"Night light warmth");
		ui::SetHelpText(adjustment_sliders[1], L"Fade duration");
	}
	adjustment_page = settings_page ? -1 : selected_effect;
	SetControlVisible(saturation_report_button, settings_page);
	SetControlVisible(curve_channel, selected_effect == effect::CustomCurves && !settings_page);
	SetControlVisible(import_button, selected_effect == effect::Imported1dLut && !settings_page);
	SetControlVisible(export_1d_button, selected_effect == effect::Imported1dLut && !settings_page);
	for (HWND control :
	     { effect_list, correction_list, windows_effect_list, corrections_heading, effects_label, order_hint })
		SetControlVisible(control, true);
	SetControlVisible(preset_label, false);
	SetControlVisible(effect_title, !settings_page);
	SetControlVisible(effect_description, !settings_page);
	SetControlVisible(active_look_label, false);
	SetControlVisible(open_preset_button, settings_page);
	SetControlVisible(settings_button, false);
	for (HWND control : { shortcut_label, shortcut_control, shortcut_button, font_choice, font_label, bypass_button })
		SetControlVisible(control, settings_page);
	// Personal corrections have their own settings, outside LUT presets.
	for (HWND control :
	     { preset_choice, autosave_control, save_preset_button, save_as_button, export_button, revert_preset_button })
		SetControlVisible(control, settings_page || selected_effect < effect::FirstWindows ||
		                               selected_effect >= effect::Saturation);
	SetControlVisible(version_label, settings_page);
	SetControlVisible(startup_control, settings_page);
	SetControlVisible(start_minimized_control, settings_page);
	SetControlVisible(open_preset_button, settings_page);
	for (HWND control : { filter_choice, filter_settings_button, correction_note })
		SetControlVisible(control, false);
	for (HWND control :
	     { night_schedule, night_start, night_end, night_start_label, night_end_label, night_include, night_display })
		SetControlVisible(control, !settings_page && selected_effect == effect::NightLight);
	if (!settings_page && selected_effect == effect::NightLight)
	{
		if (SendMessageW(night_display, CB_GETCOUNT, 0, 0) != static_cast<LRESULT>(display_sources.size()))
		{
			SendMessageW(night_display, CB_RESETCONTENT, 0, 0);
			for (const auto& output : display_sources)
				SendMessageW(night_display, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(output.label.c_str()));
			SendMessageW(night_display, CB_SETCURSEL, valid ? source_index : 0, 0);
		}
		const auto night_index = SendMessageW(night_display, CB_GETCURSEL, 0, 0);
		const bool night_valid = night_index >= 0 && static_cast<size_t>(night_index) < display_sources.size();
		SetControlVisible(correction_note, true);
		SetTextIfChanged(correction_note, night_message.empty() ? (night_config.Scheduled
		                                                               ? (night_level > 0 ? L"Schedule active"
		                                                                                  : L"Waiting for the schedule")
		                                                               : L"Manual mode")
		                                                        : night_message.c_str());
		for (const auto& entry :
		     { std::pair<HWND, bool> { night_schedule, night_config.Scheduled },
		       { night_include,
		         night_valid && night::IsDisplayIncluded(night_config, display_sources[night_index].identity) } })
			if (SendMessageW(entry.first, BM_GETCHECK, 0, 0) != (entry.second ? BST_CHECKED : BST_UNCHECKED))
				SendMessageW(entry.first, BM_SETCHECK, entry.second ? BST_CHECKED : BST_UNCHECKED, 0);
		if (GetFocus() != night_start)
			SetTextIfChanged(night_start, night::FormatTime(night_config.Start).c_str());
		if (GetFocus() != night_end)
			SetTextIfChanged(night_end, night::FormatTime(night_config.End).c_str());
		SetControlEnabled(night_start, night_config.Scheduled);
		SetControlEnabled(night_end, night_config.Scheduled);
		SetControlEnabled(night_include, night_valid && display_sources[night_index].supported);
	}
	if (!settings_page && selected_effect == effect::ColorFilters)
	{
		SetControlVisible(filter_choice, true);
		SetControlVisible(filter_settings_button, true);
		SetControlVisible(correction_note, !filter_message.empty() || !filter_state.available);
	}
	SetTextIfChanged(version_label, L"MonitorMage " MONITORMAGE_VERSION_W);
	SetControlEnabled(filter_choice, filter_state.available && !filter_controller.IsBusy() && !render_only);
	if (SendMessageW(filter_choice, CB_GETCURSEL, 0, 0) != filter_state.filter)
		SendMessageW(filter_choice, CB_SETCURSEL, filter_state.filter, 0);
	if (selected_effect != effect::NightLight || settings_page)
		SetTextIfChanged(correction_note, (!filter_message.empty() ? filter_message
		                                   : !filter_state.available
		                                       ? filter_state.reason
		                                       : L"Windows color filters stay on when MonitorMage is off or closed.")
		                                      .c_str());
	const auto startup_check = startup_enabled ? BST_CHECKED : BST_UNCHECKED;
	if (SendMessageW(startup_control, BM_GETCHECK, 0, 0) != startup_check)
		SendMessageW(startup_control, BM_SETCHECK, startup_check, 0);
	SetControlEnabled(startup_control, !render_only);
	const auto minimized_check = start_minimized ? BST_CHECKED : BST_UNCHECKED;
	if (SendMessageW(start_minimized_control, BM_GETCHECK, 0, 0) != minimized_check)
		SendMessageW(start_minimized_control, BM_SETCHECK, minimized_check, 0);
	SetTextIfChanged(settings_button, settings_page ? L"Back to effects" : ui::wording::LABEL_SETTINGS);
	SetTextIfChanged(amount_label, L"Contrast");
	SetTextIfChanged(amount_value, FormatDecimal(ContrastAmount()).c_str());
	SetTextIfChanged(pivot_label, L"Pivot");
	SetTextIfChanged(pivot_value, FormatDecimal(ContrastPivot()).c_str());
	std::wstring status_text = live_message;
	status_error = live_error;
	const bool applying = ProcessingActive(),
			   profile_active = application_context_active && !foreground_program.IsManualOverride();
	if (!live_error && live_session.IsActive() && !applying)
		status_text = L"Ready. No adjustments are changing the picture.";
	if (!render_only && !hotkey_message.empty())
		status_text += L" " + hotkey_message;
	if (!live_error && !preset_message.empty())
	{
		status_text = preset_message;
		status_error = preset_message == last_preset_error;
	}
	else if (!live_error && !profile_message.empty())
	{
		status_text = profile_message;
		status_error = false;
	}
	if (IsColorMatrixNeeded())
	{
		const auto state = render_only ? matrix_effect::State::Active : matrix_session.CurrentState();
		if (!matrix_message.empty())
		{
			status_text = matrix_message;
			status_error = true;
		}
		else if (state == matrix_effect::State::OtherTool || state == matrix_effect::State::ExternalChange)
		{
			status_text =
				L"Color effects are paused because another color tool is active. Your color curves can still work.";
			status_error = true;
		}
		else if (state == matrix_effect::State::Failed)
		{
			status_text = L"Color effects could not be applied. Your color curves can still work.";
			status_error = true;
		}
		else if (state == matrix_effect::State::Active && !live_error)
		{
			status_text =
				L"Your color effects are on. They apply to all displays; some fullscreen games may skip them. The graph shows color curves only.";
			status_error = false;
		}
	}
	if (!render_only && !IsColorMatrixNeeded() && matrix_session.CurrentState() == matrix_effect::State::Failed)
	{
		status_text = L"Your previous colors could not be confirmed. See the color effect report in Settings.";
		status_error = true;
	}
	if (!matrix_message.empty())
	{
		status_text = matrix_message;
		status_error = true;
	}
	if (filter_error)
	{
		status_text = filter_message;
		status_error = true;
	}
	if (!night_message.empty() && !render_only)
	{
		status_text = night_message;
		status_error = true;
	}
	const auto& look = CurrentPreset(preset_role::Look);
	std::wstring profile_text = L"Desktop profile";
	if (profile_active)
	{
		profile_text = L"Profile: " + programs::DisplayName(foreground_program.FocusedProgram()) + L" | ";
		profile_text += profile_action == applications::Action::PauseHdr  ? L"HDR gamma paused"
		                : profile_action == applications::Action::Disable ? L"All adjustments paused"
		                : profile_action == applications::Action::Bypass  ? L"Look bypassed"
		                : look.path.empty()                               ? L"Unsaved look"
		                                                                  : look.path.stem().wstring();
	}
	SetTextIfChanged(monitor_profile, profile_text.c_str());
	const auto look_name = (look.path.empty() ? std::wstring(L"Unsaved look") : look.path.stem().wstring()) +
	                       ((slider_dragging ? look_dirty_before_drag : look.IsDirty()) ? L" *" : L"");
	std::wstring origin =
		application_context_active
			? (foreground_program.IsManualOverride() ? L"Manual override: " : L"Application profile: ") +
				  programs::DisplayName(foreground_program.FocusedProgram())
			: L"Manual look";
	std::wstring look_state = look_name;
	if (profile_action == applications::Action::Disable)
		look_state += L" (all adjustments paused)";
	else
	{
		if (profile_action == applications::Action::PauseHdr)
			look_state += L" (HDR gamma paused)";
		if (IsLookBypassed())
			look_state += L" (look bypassed)";
	}
	SetTextIfChanged(hdr_fix_control, L"Also apply when Windows HDR is off (SDR)");
	const auto active_text = origin + L" | " + look_state;
	SetTextIfChanged(active_look_label, active_text.c_str());
	const auto caption = L"MonitorMage - " + active_text;
	SetTextIfChanged(main_window, caption.c_str());
	const auto state = CurrentIconState(applying);
	const HICON small_icon = IconForState(SM_CXSMICON, state), large_icon = IconForState(SM_CXICON, state);
	if (small_icon && reinterpret_cast<HICON>(SendMessageW(main_window, WM_GETICON, ICON_SMALL, 0)) != small_icon)
		SendMessageW(main_window, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(small_icon));
	if (large_icon && reinterpret_cast<HICON>(SendMessageW(main_window, WM_GETICON, ICON_BIG, 0)) != large_icon)
		SendMessageW(main_window, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(large_icon));
	const auto shortcut_text =
		hotkey_registered ? L"Bypass: " + HotkeyName(active_shortcut) : std::wstring(L"Bypass shortcut");
	SetTextIfChanged(shortcut_label, shortcut_text.c_str());
	SetTextIfChanged(status_label, status_text.c_str());
	graph_status_visible = status_error || hdr_pending || display_retry_pending;
	SetControlVisible(info_button, !graph_status_visible);
	SetTextIfChanged(info_button, status_text.c_str());
	// Expose the painted status as the keyboard-focusable graph's accessible name.
	SetTextIfChanged(curve_canvas, status_text.c_str());
	for (HWND control : { amount_label, amount_value, contrast_slider, pivot_label, pivot_value, pivot_slider })
		SetControlVisible(control, selected_effect == effect::ContrastCurves && !settings_page);
	for (HWND control :
	     { white_label, white_value, white_slider, gamma_label, gamma_choice, gamma_other, hdr_fix_control })
		SetControlVisible(control, selected_effect == effect::HdrGamma && !settings_page);
	ui::SetCaptionSettingsButton(main_window, ID_SETTINGS_PAGE, settings_page);
	const bool can_power =
		!hdr_pending && !render_only && (corrections_requested || (valid && display_sources[source_index].supported));
	ui::SetIconButton(power_button, ui::Glyph::Power, corrections_requested, can_power);
	ui::SetIconButton(hdr_button, ui::Glyph::Sun, valid && display_sources[source_index].hdr,
	                  !render_only && !hdr_pending && hdr_state.available);
	ui::SetIconButton(profiles_button, ui::Glyph::Diamond, profile_active, true);
	ui::SetIconButton(monitor_settings_button, ui::Glyph::Settings, settings_page, true);
	ui::SetIconButton(info_button, ui::Glyph::Info, false, true);
	ui::CaptionActions(main_window, { { { ID_TOGGLE_WINDOWS_HDR, valid && display_sources[source_index].hdr,
	                                      !render_only && !hdr_pending && hdr_state.available },
	                                    { ID_APPLICATION_PROFILES, profile_active, true },
	                                    { ID_TEST_CONTRAST, corrections_requested, can_power } } });
	SetControlEnabled(source_choice, !live_session.IsActive() && !hdr_pending);
	SetControlEnabled(white_slider, !(valid && display_sources[source_index].hdr));
	SetTextIfChanged(bypass_button, IsLookBypassed() ? L"Resume look" : L"Bypass look");
	std::wstring note = L"No active display group available.";
	if (valid)
	{
		const auto& source = display_sources[source_index];
		note = source.label + L"\n";
		if (settings_page && std::count(source.identity.begin(), source.identity.end(), L'\n') > 1)
			note += L"Linked displays change together.\n";
		note += source.hdr ? L"HDR: SDR white follows Windows." : L"SDR: hardware colour curves.";
		if (!source.supported)
			note += L" Mode not supported.";
	}
	SetTextIfChanged(test_note, note.c_str());
	SetControlVisible(test_note, false);
	SetControlVisible(source_choice, true);
	std::wstring mode = valid ? (display_sources[source_index].hdr ? L"HDR" : L"SDR") : L"";
	SetTextIfChanged(monitor_mode, mode.c_str());
	if (valid && display_sources[source_index].hdr)
	{
		white_position = static_cast<int>(std::lround(display_sources[source_index].white));
		if (SendMessageW(white_slider, TBM_GETPOS, 0, 0) != white_position)
			SendMessageW(white_slider, TBM_SETPOS, TRUE, white_position);
		const auto detected = FormatDecimal(display_sources[source_index].white, 0) + L" nits";
		SetTextIfChanged(white_value, detected.c_str());
	}
	else
	{
		const auto text = std::to_wstring(white_position) + L" nits";
		SetTextIfChanged(white_value, text.c_str());
	}
	UpdateTray();
	LayoutEffectControls();
	if (changing_adjustment_page)
		for (size_t i = 0; i < adjustment_sliders.size(); ++i)
			SetControlVisible(adjustment_sliders[i], i < controls.size() && !settings_page);
	InvalidateGraph();
}

void LayoutEditorControls()
{
	if (!editor_ready)
		return;
	++graph_layout_version;
	effect_layout_valid = false;
	if (ui_font)
		DeleteObject(ui_font);
	ui_font = ui::CreateUiFont(static_cast<UINT>(dpi));
	if (hint_font)
		DeleteObject(hint_font);
	LOGFONTW hint_style {};
	GetObjectW(ui_font, sizeof(hint_style), &hint_style);
	hint_style.lfHeight = -ScaleForDpi(ui::style::SECONDARY_TEXT_SIZE);
	hint_font = CreateFontIndirectW(&hint_style);
	SendMessageW(selected_effect_toggle, WM_SETFONT, reinterpret_cast<WPARAM>(ui_font), TRUE);
	for (HWND slider : adjustment_sliders)
		SendMessageW(slider, WM_SETFONT, reinterpret_cast<WPARAM>(ui_font), TRUE);
	for (HWND slider : { contrast_slider, pivot_slider, white_slider })
		SendMessageW(slider, WM_SETFONT, reinterpret_cast<WPARAM>(ui_font), TRUE);
	ui::StyleTooltip(monitor_tips);
	for (HWND control :
	     { effect_list,       effect_title,       effect_description, amount_label,         pivot_label,
	       white_label,       gamma_label,        gamma_choice,       gamma_other,          status_label,
	       monitor_mode,      monitor_profile,    reset_button,       export_button,        corrections_heading,
	       source_choice,     test_note,          bypass_button,      hdr_fix_control,      shortcut_control,
	       shortcut_button,   shortcut_label,     preset_label,       preset_choice,        save_preset_button,
	       save_as_button,    open_preset_button, autosave_control,   revert_preset_button, profiles_button,
	       active_look_label, order_hint })
		SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(ui_font), TRUE);
	for (HWND control : { correction_list, windows_effect_list, effects_label })
		SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(ui_font), TRUE);
	// Presets and their actions belong to the right pane; the sidebar is the stack.
	PlaceControl(preset_choice, 198, 21, 244, 240);
	PlaceControl(save_preset_button, 558, 16, 32, 42);
	PlaceControl(save_as_button, 598, 16, 32, 42);
	PlaceControl(autosave_control, 454, 16, 92, 28);
	PlaceControl(export_button, 644, 16, 32, 42);
	PlaceControl(active_look_label, 20, 48, 444, 24);
	PlaceControl(revert_preset_button, 724, 16, 32, 42);
	PlaceControl(test_note, 20, 480, 180, 82);
	PlaceControl(effect_title, 198, 64, 470, 24);
	PlaceControl(selected_effect_toggle, 676, 62, 40, 28);
	PlaceControl(reset_button, 724, 62, 32, 42);
	SendMessageW(saturation_report_button, WM_SETFONT, reinterpret_cast<WPARAM>(ui_font), TRUE);
	for (HWND control : { amount_value, pivot_value, white_value })
		SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(ui_font), TRUE);
	PlaceControl(corrections_heading, 13, 16, 160, 28);
	PlaceControl(correction_list, 0, 48, SIDEBAR_WIDTH, 36);
	PlaceControl(windows_effect_list, 0, 84, SIDEBAR_WIDTH, 72);
	const int effect_rows = std::min(static_cast<int>(CreativeOrder().size()), 10),
			  effect_height = effect_rows * effects::ROW_HEIGHT;
	PlaceControl(effects_label, 13, 168, 160, 24);
	PlaceControl(effect_list, 0, 196, SIDEBAR_WIDTH, effect_height);
	PlaceControl(order_hint, 0, 196 + effect_height + 6, SIDEBAR_WIDTH, 18);
	PlaceControl(test_note, 20, settings_page ? 480 : 500, 180, settings_page ? 82 : 62);
	const auto width = [&](HWND control, int minimum = 64, int maximum = 240)
	{
		return ui::ContentWidth(control, ui_font, minimum, maximum);
	};
	PlaceControl(font_label, 198, 98, 220, 20);
	PlaceControl(font_choice, 198, 125, width(font_choice, 128, 220), 180);
	PlaceControl(open_preset_button, 198, 228, width(open_preset_button), 28);
	PlaceControl(bypass_button, 456, 164, width(bypass_button, 100), 28);
	PlaceControl(saturation_report_button, 456, 228, width(saturation_report_button), 28);
	PlaceControl(version_label, 198, 64, 220, 24);
	PlaceControl(startup_control, 198, 164, 220, 26);
	PlaceControl(start_minimized_control, 198, 196, 220, 26);
	PlaceControl(shortcut_label, 456, 98, 300, 20);
	PlaceControl(shortcut_control, 456, 120, 200, 28);
	PlaceControl(shortcut_button, 664, 120, 92, 28);

	for (size_t i = 0; i < adjustment_sliders.size(); ++i)
	{
		SendMessageW(adjustment_labels[i], WM_SETFONT, reinterpret_cast<WPARAM>(ui_font), TRUE);
		SendMessageW(adjustment_values[i], WM_SETFONT, reinterpret_cast<WPARAM>(ui_font), TRUE);
	}
	for (HWND control : { settings_button, font_choice, font_label, curve_channel, import_button, export_1d_button })
		SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(ui_font), TRUE);
	for (HWND control : { version_label, startup_control, start_minimized_control })
		SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(ui_font), TRUE);
	for (HWND control : { filter_choice, filter_settings_button, correction_note })
		SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(ui_font), TRUE);
	for (HWND control :
	     { night_schedule, night_start, night_end, night_start_label, night_end_label, night_include, night_display })
		SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(ui_font), TRUE);
	SendMessageW(order_hint, WM_SETFONT, reinterpret_cast<WPARAM>(hint_font ? hint_font : ui_font), TRUE);
	ui::RefreshCaptionFont(main_window);
	RefreshPresetChoice();
	RefreshInterface();
	for (HWND control : { monitor_mode, monitor_profile })
		InvalidateRect(control, nullptr, FALSE);
	InvalidateRect(main_window, nullptr, TRUE);
}

void ShowEditor()
{
	SourceQueryBatch query_batch;
	EditorSync sync;
	CreateEditorIfNeeded();
	RefreshColorFilters();
	RefreshSources();
	ApplyLiveCorrections();
	RefreshInterface();
	ShowWindow(main_window, SW_SHOW);
	ShowWindow(main_window, SW_RESTORE);
	SetForegroundWindow(main_window);
}

bool InstallTray()
{
	tray_icon.cbSize = sizeof(tray_icon);
	tray_icon.hWnd = main_window;
	tray_icon.uID = 1;
	tray_icon.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP | NIF_SHOWTIP;
	tray_icon.uCallbackMessage = MESSAGE_TRAY;
	tray_icon.hIcon = IconForState(SM_CXSMICON);
	if (!tray_icon.hIcon)
		tray_icon.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
	wcscpy_s(tray_icon.szTip, L"MonitorMage - disabled");
	tray_installed = Shell_NotifyIconW(NIM_ADD, &tray_icon) != FALSE;
	if (tray_installed)
	{
		tray_icon.uVersion = NOTIFYICON_VERSION_4;
		Shell_NotifyIconW(NIM_SETVERSION, &tray_icon);
	}
	return tray_installed;
}

std::vector<ui::MenuItem> TrayMenuItems()
{
	return { { ID_OPEN, L"Open editor", L"", false, false, true, ui::Glyph::Editor },
		     { ID_APPLICATION_PROFILES, ui::wording::LABEL_APPLICATION_PROFILES, L"",
		       application_context_active && !foreground_program.IsManualOverride(), false, true, ui::Glyph::Diamond },
		     { ID_TOGGLE_WINDOWS_HDR, L"Windows HDR", hdr_pending ? L"SWITCHING" : L"", hdr_state.on, false,
		       hdr_state.available && !hdr_pending, ui::Glyph::Sun },
		     { ID_BYPASS, IsLookBypassed() ? L"Resume look" : L"Bypass look",
		       hotkey_registered ? HotkeyName(active_shortcut) : L"", IsLookBypassed(), false, true,
		       ui::Glyph::Bypass },
		     { ID_TEST_CONTRAST, L"Master power", !ProcessingActive() && corrections_requested ? L"READY" : L"",
		       ProcessingActive(), false, true, ui::Glyph::Power },
		     { ID_EXIT, L"Exit", L"", false, true, true, ui::Glyph::Close } };
}

void ShowTrayMenu()
{
	if (!render_only)
	{
		RefreshSources();
		RefreshColorFilters();
		RefreshInterface();
	}
	POINT point {};
	GetCursorPos(&point);
	const UINT command = ui::ShowTrayPopup(main_window, IconForState(SM_CXSMICON), TrayMenuItems(), point);
	if (command == ID_OPEN)
		ShowEditor();
	else if (command == ID_EXIT && ConfirmExit())
		DestroyWindow(main_window);
	else if (command == ID_APPLICATION_PROFILES)
		ShowProfiles();
	else if (command == ID_TEST_CONTRAST)
		ToggleCorrections();
	else if (command == ID_BYPASS)
		ToggleBypass();
	else if (command == ID_TOGGLE_WINDOWS_HDR)
		ToggleWindowsHdrMode();
}

void ExportCurve()
{
	wchar_t file[32768] {};
	wcscpy_s(file, selected_effect == effect::ContrastCurves ? L"creative-curves.csv" : L"compiled-curves.csv");
	OPENFILENAMEW dialog {};
	dialog.lStructSize = sizeof(dialog);
	dialog.hwndOwner = main_window;
	dialog.lpstrFilter = L"Curve CSV (*.csv)\0*.csv\0\0";
	dialog.lpstrFile = file;
	dialog.nMaxFile = static_cast<DWORD>(std::size(file));
	dialog.lpstrDefExt = L"csv";
	dialog.Flags = OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
	if (!GetSaveFileNameW(&dialog))
		return;
	HANDLE handle = CreateFileW(file, GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
	if (handle == INVALID_HANDLE_VALUE)
	{
		MessageBoxW(main_window, L"Choose a different filename to keep the existing file.", L"Export curve",
		            MB_OK | MB_ICONINFORMATION);
		return;
	}
	std::ostringstream csv;
	csv.imbue(std::locale::classic());
	csv << std::setprecision(17);
	csv << "input,output_red,output_green,output_blue,model,contrast,pivot,sdr_white_nits,gamma,hdr_fix,bypass,contrast_enabled,correction_first\n";
	const auto output = OutputForPreview();
	const auto settings = CurrentSettings();
	const bool hdr_model = output.hdr;
	for (int i = 0; i < 1024; ++i)
	{
		const double x = i / 1023.0;
		csv << x << ',' << live::EvaluateOutputCode(x, 0, output, settings) << ','
			<< live::EvaluateOutputCode(x, 1, output, settings) << ','
			<< live::EvaluateOutputCode(x, 2, output, settings) << ','
			<< (hdr_model ? (live_session.IsActive() ? "combined_hdr_pq" : "hdr_pq") : "creative_1d") << ',';
		csv << ContrastAmount() << ',' << ContrastPivot() << ',';
		if (hdr_model)
			csv << (live_session.IsActive() ? live_output.white : white_position) << ',' << HdrGamma();
		else
			csv << ',';
		csv << ',' << IsHdrFixActive() << ',' << IsLookBypassed() << ',' << contrast_enabled << ',' << correction_first
			<< '\n';
	}
	const auto data = csv.str();
	DWORD written = 0;
	const bool ok =
		WriteFile(handle, data.data(), static_cast<DWORD>(data.size()), &written, nullptr) && written == data.size();
	CloseHandle(handle);
	MessageBoxW(main_window,
	            ok ? L"Curve exported." : L"The curve could not be saved completely. Check free space and try again.",
	            L"Export curve", MB_OK | (ok ? MB_ICONINFORMATION : MB_ICONERROR));
}

void ImportOrExportLut(bool importing)
{
	if (render_only)
		return;
	wchar_t file[32768] {};
	if (!importing)
		wcscpy_s(file, L"MonitorMage-look");
	OPENFILENAMEW dialog { sizeof(dialog) };
	dialog.hwndOwner = main_window;
	dialog.lpstrFile = file;
	dialog.nMaxFile = static_cast<DWORD>(std::size(file));
	dialog.lpstrFilter =
		importing
			? L"All supported LUTs\0*.cube;*.spi1d;*.cal;*.csv;*.png;*.tif;*.tiff\0Cube 1D (*.cube)\0*.cube\0OpenColorIO (*.spi1d)\0*.spi1d\0Argyll display curves (*.cal)\0*.cal\0RGB CSV (*.csv)\0*.csv\0PNG LUT image (*.png)\0*.png\0TIFF LUT image (*.tif;*.tiff)\0*.tif;*.tiff\0\0"
			: L"Cube 1D (*.cube)\0*.cube\0OpenColorIO (*.spi1d)\0*.spi1d\0Argyll curves (*.cal)\0*.cal\0RGB CSV (*.csv)\0*.csv\0PNG 16-bit ramp (*.png)\0*.png\0TIFF 16-bit ramp (*.tif)\0*.tif\0\0";
	dialog.nFilterIndex = 1;
	dialog.Flags = OFN_EXPLORER | OFN_NOCHANGEDIR | (importing ? OFN_FILEMUSTEXIST : OFN_OVERWRITEPROMPT);
	if (!(importing ? GetOpenFileNameW(&dialog) : GetSaveFileNameW(&dialog)))
		return;
	try
	{
		std::filesystem::path path(file);
		if (importing)
		{
			auto loaded = lut1d::Read(path);
			CurrentPreset(preset_role::Look).current.Adjustments.Imported = std::move(loaded.channels);
			OnPresetEdited(preset_role::Look);
			preset_message = L"Color curves imported.";
			ApplyLiveCorrections();
		}
		else
		{
			if (!path.has_extension())
			{
				const wchar_t* extensions[] { L".cube", L".spi1d", L".cal", L".csv", L".png", L".tif" };
				path += extensions[std::clamp(static_cast<int>(dialog.nFilterIndex) - 1, 0, 5)];
				if (std::filesystem::exists(path) &&
				    MessageBoxW(main_window, L"Replace the existing LUT file?", L"Export look LUT",
				                MB_YESNO | MB_ICONQUESTION) != IDYES)
					return;
			}
			auto settings = CurrentSettings();
			settings.HdrFix = false;
			settings.Bypass = false;
			settings.NightWarmth = 0;
			Source output;
			lut1d::Channels channels;
			for (int i = 0; i < 1024; ++i)
				for (int c = 0; c < 3; ++c)
					channels[c].push_back(live::EvaluateOutputCode(i / 1023.0, c, output, settings));
			lut1d::Write(path, channels);
			preset_message =
				L"Color curves exported. Saturation, Channel mixer and monitor corrections are not included.";
		}
	}
	catch (const std::exception& error)
	{
		ShowPresetError(error);
	}
	RefreshInterface();
}

struct MonitorGeometry
{
	RECT body, screen, plot, foot;
	int left, right, top, bottom, size;
};

MonitorGeometry CalculateMonitorGeometry(HWND hwnd)
{
	RECT client {};
	GetClientRect(hwnd, &client);
	MonitorGeometry geometry {};
	geometry.body = { 0, 0, client.right, client.bottom - ScaleForDpi(30) };
	geometry.screen = geometry.body;
	InflateRect(&geometry.screen, -ScaleForDpi(6), -ScaleForDpi(6));
	// Six DIP of internal padding plus a two-DIP inset gives an eight-DIP
	// visible corner gap (previously sixteen), retaining rounded screen corners.
	geometry.size = ScaleForDpi(24);
	const int inset = ScaleForDpi(2);
	geometry.left = geometry.screen.left + inset;
	geometry.right = geometry.screen.right - inset - geometry.size;
	geometry.top = geometry.screen.top + inset;
	geometry.bottom = geometry.screen.bottom - inset - geometry.size;
	// Reserve a small axis-label strip above the separate profile row.
	geometry.plot = { geometry.left + geometry.size / 2 + ScaleForDpi(20),
		              geometry.top + geometry.size / 2 + ScaleForDpi(20),
		              geometry.right + geometry.size / 2 - ScaleForDpi(20),
		              geometry.bottom + geometry.size / 2 - ScaleForDpi(32) };
	const int center = client.right / 2;
	geometry.foot = { center - ScaleForDpi(57), client.bottom - ScaleForDpi(15), center + ScaleForDpi(57),
		              client.bottom - ScaleForDpi(3) };
	return geometry;
}

RECT CurvePlotRect(HWND hwnd)
{
	return CalculateMonitorGeometry(hwnd).plot;
}

RECT GraphUpdateBounds(HWND hwnd)
{
	auto area = CurvePlotRect(hwnd);
	InflateRect(&area, ScaleForDpi(8), ScaleForDpi(8));
	RECT client {}, visible {};
	GetClientRect(hwnd, &client);
	IntersectRect(&visible, &area, &client);
	return visible;
}

RECT GraphWarningBounds(HWND hwnd, const wchar_t* text)
{
	const auto g = CalculateMonitorGeometry(hwnd);
	RECT area { g.left + g.size + ScaleForDpi(12), g.top, g.right - ScaleForDpi(12), g.bottom }, measured = area;
	HDC dc = GetDC(hwnd);
	const auto old = SelectObject(dc, ui_font);
	DrawTextW(dc, text, -1, &measured, DT_CENTER | DT_WORDBREAK | DT_CALCRECT | DT_NOPREFIX);
	SelectObject(dc, old);
	ReleaseDC(hwnd, dc);
	area.top += std::max(0, (g.size - static_cast<int>(measured.bottom - measured.top)) / 2);
	area.bottom = std::min<LONG>(g.bottom, area.top + measured.bottom - measured.top);
	return area;
}

void InvalidateGraph()
{
	if (!curve_canvas || !ui_font)
		return;
	if (render_only)
		graph_audit_count = 0;
	const auto queue = [&](const RECT& area)
	{
		if (render_only && graph_audit_count < graph_audit_damage.size())
			graph_audit_damage[graph_audit_count++] = area;
		InvalidateRect(curve_canvas, &area, FALSE);
	};
	const RECT plot = GraphUpdateBounds(curve_canvas);
	queue(plot);
	static std::wstring previous;
	static RECT old_bounds {};
	static bool had_warning = false;
	static unsigned previous_layout = 0;
	wchar_t text[2048] {};
	if (graph_status_visible)
		GetWindowTextW(status_label, text, 2048);
	if (had_warning != graph_status_visible || previous != text || previous_layout != graph_layout_version)
	{
		if (had_warning)
			queue(old_bounds);
		if (graph_status_visible)
		{
			old_bounds = GraphWarningBounds(curve_canvas, text);
			queue(old_bounds);
		}
		previous = text;
		had_warning = graph_status_visible;
		previous_layout = graph_layout_version;
	}
}

void LayoutMonitorControls()
{
	if (!curve_canvas)
		return;
	RECT client {};
	GetClientRect(curve_canvas, &client);
	const auto g = CalculateMonitorGeometry(curve_canvas);
	const auto label = [&](HWND control, int x, int y, int width, int height)
	{
		SetWindowPos(control, nullptr, x, y, width, height, SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOCOPYBITS);
		InvalidateRect(control, nullptr, FALSE);
	};
	label(website_link, g.foot.left + ScaleForDpi(2), g.foot.top + ScaleForDpi(1),
	      g.foot.right - g.foot.left - ScaleForDpi(4), g.foot.bottom - g.foot.top - ScaleForDpi(2));
	MoveWindow(hdr_button, g.left, g.top, g.size, g.size, TRUE);
	MoveWindow(power_button, g.right, g.top, g.size, g.size, TRUE);
	MoveWindow(info_button, (g.screen.left + g.screen.right - g.size) / 2, g.top, g.size, g.size, TRUE);
	MoveWindow(profiles_button, g.right, g.bottom, g.size, g.size, TRUE);
	MoveWindow(monitor_settings_button, g.left, g.bottom, g.size, g.size, TRUE);
	MoveWindow(status_label, g.left + g.size + ScaleForDpi(12), g.top, g.right - g.left - g.size - ScaleForDpi(24),
	           g.size, TRUE);
	label(monitor_mode, g.left + g.size + ScaleForDpi(32), g.bottom, g.right - g.left - g.size - ScaleForDpi(64),
	      g.size);
	const int center = client.right / 2;
	RECT choice {};
	GetWindowRect(source_choice, &choice);
	const int height = choice.bottom - choice.top;
	const int row_top = (g.body.bottom + g.foot.bottom - height) / 2;
	MoveWindow(source_choice, 0, row_top, center - ScaleForDpi(68), ScaleForDpi(180), TRUE);
	label(monitor_profile, center + ScaleForDpi(68), row_top, client.right - center - ScaleForDpi(68), height);
}

void ChangeCurvePoint(HWND hwnd, LPARAM point)
{
	if (selected_effect != effect::CustomCurves)
		return;
	const RECT plot = CurvePlotRect(hwnd);
	auto& curve = CurrentPreset(preset_role::Look).current.Adjustments.Curves[editing_channel];
	if (curve.empty())
	{
		for (int i = 0; i < 9; ++i)
			curve.push_back(i / 8.0);
	}
	if (dragged_point < 0)
		dragged_point = std::clamp(
			static_cast<int>(std::lround((GET_X_LPARAM(point) - plot.left) * 8.0 / (plot.right - plot.left))), 0, 8);
	keyboard_point = dragged_point;
	const auto index = static_cast<size_t>(dragged_point);
	const double low = index ? curve[index - 1] : 0, high = index + 1 < curve.size() ? curve[index + 1] : 1;
	curve[index] =
		std::clamp((plot.bottom - GET_Y_LPARAM(point)) / static_cast<double>(plot.bottom - plot.top), low, high);
	OnPresetEdited(preset_role::Look);
	RefreshInterface();
	if (corrections_requested)
		SetTimer(main_window, TIMER_APPLY, 100, nullptr);
}

LRESULT CALLBACK ProfileLinkProc(HWND hwnd, UINT message, WPARAM w_param, LPARAM l_param, UINT_PTR, DWORD_PTR)
{
	if (message == WM_SETCURSOR)
	{
		SetCursor(LoadCursorW(nullptr, IDC_HAND));
		return TRUE;
	}
	if (message == WM_GETDLGCODE && l_param)
	{
		const auto* key = reinterpret_cast<MSG*>(l_param);
		if (key->wParam == VK_RETURN || key->wParam == VK_SPACE)
			return DLGC_WANTMESSAGE;
	}
	if (message == WM_KEYDOWN && (w_param == VK_RETURN || w_param == VK_SPACE))
	{
		SendMessageW(GetParent(hwnd), WM_COMMAND, MAKEWPARAM(GetDlgCtrlID(hwnd), STN_CLICKED),
		             reinterpret_cast<LPARAM>(hwnd));
		return 0;
	}
	if (message == WM_MOUSEMOVE && !GetPropW(hwnd, L"LutProfileHover"))
	{
		SetPropW(hwnd, L"LutProfileHover", reinterpret_cast<HANDLE>(1));
		TRACKMOUSEEVENT track { sizeof(track), TME_LEAVE, hwnd, 0 };
		TrackMouseEvent(&track);
		InvalidateRect(hwnd, nullptr, FALSE);
	}
	if (message == WM_MOUSELEAVE)
	{
		RemovePropW(hwnd, L"LutProfileHover");
		InvalidateRect(hwnd, nullptr, FALSE);
	}
	if (message == WM_SETFOCUS || message == WM_KILLFOCUS)
		InvalidateRect(hwnd, nullptr, FALSE);
	if (message == WM_NCDESTROY)
	{
		RemovePropW(hwnd, L"LutProfileHover");
		RemoveWindowSubclass(hwnd, ProfileLinkProc, 80);
	}
	return DefSubclassProc(hwnd, message, w_param, l_param);
}

LRESULT CALLBACK MonitorLabelPaintProc(HWND hwnd, UINT message, WPARAM w_param, LPARAM l_param, UINT_PTR, DWORD_PTR)
{
	if (message == WM_ERASEBKGND)
		return 1;
	if (message == WM_PAINT || message == WM_PRINTCLIENT || message == WM_PRINT)
	{
		PAINTSTRUCT paint {};
		HDC target = message == WM_PAINT ? BeginPaint(hwnd, &paint) : reinterpret_cast<HDC>(w_param);
		RECT bounds {};
		GetClientRect(hwnd, &bounds);
		// Native static controls can preserve/copy parent pixels while the
		// monitor is resized. Paint these labels only in their own local DC.
		paint::Buffer frame(target, bounds);
		if (frame && frame.IsBuffered())
		{
			DRAWITEMSTRUCT item {};
			item.CtlType = ODT_STATIC;
			item.CtlID = GetDlgCtrlID(hwnd);
			item.itemAction = ODA_DRAWENTIRE;
			item.hwndItem = hwnd;
			item.hDC = frame.DeviceContext();
			item.rcItem = bounds;
			SendMessageW(GetParent(hwnd), WM_DRAWITEM, item.CtlID, reinterpret_cast<LPARAM>(&item));
		}
		frame.Finish();
		if (message == WM_PAINT)
			EndPaint(hwnd, &paint);
		return 0;
	}
	if (message == WM_NCDESTROY)
		RemoveWindowSubclass(hwnd, MonitorLabelPaintProc, 81);
	return DefSubclassProc(hwnd, message, w_param, l_param);
}

LRESULT CALLBACK CurveWindowProc(HWND hwnd, UINT message, WPARAM w_param, LPARAM l_param)
{
	if (message == WM_DRAWITEM && reinterpret_cast<DRAWITEMSTRUCT*>(l_param)->hwndItem == website_link)
	{
		const auto& item = *reinterpret_cast<DRAWITEMSTRUCT*>(l_param);
		const int saved = SaveDC(item.hDC);
		IntersectClipRect(item.hDC, item.rcItem.left, item.rcItem.top, item.rcItem.right, item.rcItem.bottom);
		HBRUSH back = CreateSolidBrush(RGB(80, 87, 96));
		FillRect(item.hDC, &item.rcItem, back);
		DeleteObject(back);
		LOGFONTW style {};
		GetObjectW(ui_font, sizeof(style), &style);
		style.lfHeight = -ScaleForDpi(8);
		style.lfUnderline = GetPropW(website_link, L"LutProfileHover") != nullptr;
		HFONT small = CreateFontIndirectW(&style);
		const auto old = SelectObject(item.hDC, small ? small : ui_font);
		SetBkMode(item.hDC, TRANSPARENT);
		SetTextColor(item.hDC, RGB(0, 0, 0));
		RECT area = item.rcItem;
		DrawTextW(item.hDC, L"CeeJay.dk", -1, &area, DT_SINGLELINE | DT_CENTER | DT_VCENTER | DT_NOPREFIX);
		if (GetFocus() == website_link)
			DrawFocusRect(item.hDC, &area);
		SelectObject(item.hDC, old);
		if (small)
			DeleteObject(small);
		if (saved)
			RestoreDC(item.hDC, saved);
		return TRUE;
	}
	if (message == WM_SETCURSOR && reinterpret_cast<HWND>(w_param) == hwnd && LOWORD(l_param) == HTCLIENT &&
	    selected_effect == effect::CustomCurves && !settings_page)
	{
		POINT point {};
		GetCursorPos(&point);
		ScreenToClient(hwnd, &point);
		const auto plot = CurvePlotRect(hwnd);
		if (PtInRect(&plot, point) || dragged_point >= 0)
		{
			SetCursor(LoadCursorW(nullptr, IDC_CROSS));
			return TRUE;
		}
	}
	if (message == WM_DRAWITEM && (reinterpret_cast<DRAWITEMSTRUCT*>(l_param)->hwndItem == monitor_mode ||
	                               reinterpret_cast<DRAWITEMSTRUCT*>(l_param)->hwndItem == monitor_profile))
	{
		const auto& item = *reinterpret_cast<DRAWITEMSTRUCT*>(l_param);
		const bool profile = item.hwndItem == monitor_profile;
		const int saved = SaveDC(item.hDC);
		IntersectClipRect(item.hDC, item.rcItem.left, item.rcItem.top, item.rcItem.right, item.rcItem.bottom);
		HBRUSH back = profile ? ui::BackgroundBrush() : CreateSolidBrush(RGB(7, 9, 12));
		FillRect(item.hDC, &item.rcItem, back);
		if (!profile)
			DeleteObject(back);
		const auto old = SelectObject(item.hDC, reinterpret_cast<HFONT>(SendMessageW(item.hwndItem, WM_GETFONT, 0, 0)));
		SetBkMode(item.hDC, TRANSPARENT);
		SetTextColor(item.hDC, ui::THEME_MUTED);
		wchar_t text[2048] {};
		GetWindowTextW(item.hwndItem, text, 2048);
		RECT area = item.rcItem;
		if (profile)
		{
			const bool hover = GetPropW(monitor_profile, L"LutProfileHover") != nullptr;
			SetTextColor(item.hDC, hover || GetFocus() == monitor_profile ? ui::THEME_TEXT : ui::THEME_MUTED);
			RECT measured {};
			DrawTextW(item.hDC, text, -1, &measured, DT_SINGLELINE | DT_CALCRECT | DT_NOPREFIX);
			const int icon = ScaleForDpi(12), gap = ScaleForDpi(5),
					  width = std::min(static_cast<int>(measured.right - measured.left) + ScaleForDpi(4),
			                           static_cast<int>(area.right - area.left) - icon - gap);
			const int start = area.right - width - icon - gap, mid = (area.top + area.bottom) / 2;
			ui::PaintMenuGlyph(monitor_profile, item.hDC, { start, mid - icon / 2, start + icon, mid + (icon + 1) / 2 },
			                   ui::Glyph::Diamond, application_context_active && !foreground_program.IsManualOverride(),
			                   true, ui::THEME_BACKGROUND);
			area.left = start + icon + gap;
			area.right = area.left + width;
			DrawTextW(item.hDC, text, -1, &area, DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX);
			if (hover)
			{
				HPEN pen = CreatePen(PS_SOLID, 1, ui::THEME_FOCUS_BORDER);
				const auto previous = SelectObject(item.hDC, pen);
				const int y = mid + ScaleForDpi(8);
				MoveToEx(item.hDC, area.left, y, nullptr);
				LineTo(item.hDC, area.right, y);
				SelectObject(item.hDC, previous);
				DeleteObject(pen);
			}
			if (GetFocus() == monitor_profile)
				DrawFocusRect(item.hDC, &area);
		}
		else
			DrawTextW(item.hDC, text, -1, &area,
			          DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX | DT_CENTER);
		SelectObject(item.hDC, old);
		if (saved)
			RestoreDC(item.hDC, saved);
		return TRUE;
	}
	LRESULT themed = 0;
	if ((message == WM_NOTIFY || message == WM_DRAWITEM || message == WM_MEASUREITEM ||
	     (message >= WM_CTLCOLORMSGBOX && message <= WM_CTLCOLORSTATIC)) &&
	    ui::HandleThemeMessage(hwnd, message, w_param, l_param, themed))
		return themed;
	if (message == WM_COMMAND)
		return SendMessageW(GetParent(hwnd), message, w_param, l_param);
	if (message == WM_NOTIFY)
		return SendMessageW(GetParent(hwnd), message, w_param, l_param);
	if (message == WM_SIZE)
	{
		LayoutMonitorControls();
		InvalidateRect(hwnd, nullptr, FALSE);
		return 0;
	}
	if (message == WM_SETFOCUS || message == WM_KILLFOCUS)
	{
		InvalidateGraph();
		return 0;
	}
	if (selected_effect == effect::CustomCurves)
	{
		if (message == WM_GETDLGCODE)
			return DLGC_WANTARROWS;
		if (message == WM_LBUTTONDBLCLK)
		{
			const auto plot = CurvePlotRect(hwnd);
			POINT point { GET_X_LPARAM(l_param), GET_Y_LPARAM(l_param) };
			auto hit = plot;
			InflateRect(&hit, ScaleForDpi(6), ScaleForDpi(6));
			if (PtInRect(&hit, point))
			{
				auto& curve = CurrentPreset(preset_role::Look).current.Adjustments.Curves[editing_channel];
				if (!curve.empty())
				{
					const auto i = static_cast<size_t>(std::clamp(
						static_cast<int>(std::lround((point.x - plot.left) * 8.0 / (plot.right - plot.left))), 0, 8));
					curve[i] = std::clamp(i / 8.0, i ? curve[i - 1] : 0, i + 1 < curve.size() ? curve[i + 1] : 1);
					keyboard_point = static_cast<int>(i);
					dragged_point = -1;
					slider_dragging = false;
					if (GetCapture() == hwnd)
						ReleaseCapture();
					OnPresetEdited(preset_role::Look);
					ApplyLiveCorrections();
					RefreshInterface();
				}
			}
			return 0;
		}
		if (message == WM_LBUTTONDOWN)
		{
			const auto plot = CurvePlotRect(hwnd);
			POINT point { GET_X_LPARAM(l_param), GET_Y_LPARAM(l_param) };
			if (PtInRect(&plot, point))
			{
				SetFocus(hwnd);
				look_dirty_before_drag = CurrentPreset(preset_role::Look).IsDirty();
				slider_dragging = true;
				KillTimer(main_window, TIMER_PRESET);
				dragged_point = -1;
				SetCapture(hwnd);
				ChangeCurvePoint(hwnd, l_param);
			}
			return 0;
		}
		if (message == WM_MOUSEMOVE && GetCapture() == hwnd)
		{
			ChangeCurvePoint(hwnd, l_param);
			return 0;
		}
		if (message == WM_LBUTTONUP || message == WM_CAPTURECHANGED || message == WM_CANCELMODE)
		{
			if (dragged_point >= 0)
			{
				dragged_point = -1;
				slider_dragging = false;
				if (GetCapture() == hwnd)
					ReleaseCapture();
				OnPresetEdited(preset_role::Look);
				RefreshInterface();
			}
			return 0;
		}
		if (message == WM_RBUTTONUP)
		{
			CurrentPreset(preset_role::Look).current.Adjustments.Curves[editing_channel].clear();
			OnPresetEdited(preset_role::Look);
			ApplyLiveCorrections();
			RefreshInterface();
			return 0;
		}
		if (message == WM_KEYDOWN &&
		    (w_param == VK_UP || w_param == VK_DOWN || w_param == VK_LEFT || w_param == VK_RIGHT))
		{
			auto& curve = CurrentPreset(preset_role::Look).current.Adjustments.Curves[editing_channel];
			if (curve.empty())
				for (int i = 0; i < 9; ++i)
					curve.push_back(i / 8.0);
			if (w_param == VK_LEFT || w_param == VK_RIGHT)
				keyboard_point = std::clamp(keyboard_point + (w_param == VK_RIGHT ? 1 : -1), 0, 8);
			else
			{
				const size_t i = static_cast<size_t>(keyboard_point);
				curve[i] = std::clamp(curve[i] + (w_param == VK_UP ? .01 : -.01), i ? curve[i - 1] : 0,
				                      i + 1 < curve.size() ? curve[i + 1] : 1);
				OnPresetEdited(preset_role::Look);
				if (corrections_requested)
					SetTimer(main_window, TIMER_APPLY, 100, nullptr);
			}
			RefreshInterface();
			return 0;
		}
	}
	if (message == WM_ERASEBKGND)
		return 1;
	if (message != WM_PAINT && message != WM_PRINTCLIENT)
		return DefWindowProcW(hwnd, message, w_param, l_param);
	PAINTSTRUCT paint {};
	HDC dc = message == WM_PAINT ? BeginPaint(hwnd, &paint) : reinterpret_cast<HDC>(w_param);
	RECT client {};
	GetClientRect(hwnd, &client);
	// Buffer only while painting; no persistent frame loop or render resources.
	paint::Buffer frame(dc, client);
	// Keep the previous image if off-screen allocation fails; never fall
	// back to drawing the graph directly into the visible window.
	if (!frame || !frame.IsBuffered())
	{
		frame.Finish();
		if (message == WM_PAINT)
			EndPaint(hwnd, &paint);
		return 0;
	}
	if (render_only)
		last_graph_paint_bytes = frame.ByteCount();
	HDC buffer = frame.DeviceContext();
	FillRect(buffer, &client, ui::BackgroundBrush());
	const auto geometry = CalculateMonitorGeometry(hwnd);
	const RECT body = geometry.body;
	auto rounded = [&](RECT rect, COLORREF fill, COLORREF outline, int radius)
	{
		HBRUSH brush = CreateSolidBrush(fill);
		HPEN pen = CreatePen(PS_SOLID, 1, outline);
		auto old_brush = SelectObject(buffer, brush), old_pen = SelectObject(buffer, pen);
		RoundRect(buffer, rect.left, rect.top, rect.right, rect.bottom, ScaleForDpi(radius * 2),
		          ScaleForDpi(radius * 2));
		SelectObject(buffer, old_brush);
		SelectObject(buffer, old_pen);
		DeleteObject(brush);
		DeleteObject(pen);
	};
	const int center = client.right / 2;
	RECT neck { center - ScaleForDpi(17), body.bottom - ScaleForDpi(2), center + ScaleForDpi(17),
		        client.bottom - ScaleForDpi(7) };
	const RECT foot = geometry.foot;
	rounded(neck, RGB(65, 70, 77), RGB(86, 92, 100), 2);
	rounded(foot, RGB(80, 87, 96), RGB(112, 120, 130), 3);
	rounded(body, RGB(82, 89, 98), RGB(120, 128, 138), 7);
	const RECT screen = geometry.screen;
	rounded(screen, RGB(7, 9, 12), RGB(32, 36, 42), 3);
	const auto plot_rect = CurvePlotRect(hwnd);
	const int left = plot_rect.left, right = plot_rect.right, top = plot_rect.top, bottom = plot_rect.bottom;
	HPEN grid = CreatePen(PS_SOLID, 1, RGB(58, 62, 69));
	HGDIOBJ old_pen = SelectObject(buffer, grid);
	for (int i = 0; i <= 4; ++i)
	{
		const int x = left + (right - left) * i / 4, y = bottom - (bottom - top) * i / 4;
		MoveToEx(buffer, x, top, nullptr);
		LineTo(buffer, x, bottom);
		MoveToEx(buffer, left, y, nullptr);
		LineTo(buffer, right, y);
	}
	HPEN identity = CreatePen(PS_DOT, 1, ui::THEME_BORDER);
	SelectObject(buffer, identity);
	MoveToEx(buffer, left, bottom, nullptr);
	LineTo(buffer, right, top);
	const auto settings = CurrentSettings();
	const auto output = OutputForPreview();
	const COLORREF inks[] { RGB(255, 0, 0), RGB(0, 255, 0), RGB(0, 0, 255) };
	plot::Paths paths;
	std::vector<POINT> editable_path;
	bool needs_guide = false;
	for (auto& path : paths)
		path.reserve(513);
	if (selected_effect == effect::CustomCurves)
		editable_path.reserve(513);
	const auto position = [&](double x, double y)
	{
		return POINT { left + static_cast<int>(std::lround((right - left) * x)),
			           bottom - static_cast<int>(std::lround((bottom - top) * y)) };
	};
	for (int i = 0; i <= 512; ++i)
	{
		const double x = i / 512.0;
		POINT editable {};
		if (selected_effect == effect::CustomCurves)
		{
			editable = position(x, color::SampleSpline(settings.Adjustments.Curves[editing_channel], x));
			editable_path.push_back(editable);
		}
		for (int c = 0; c < 3; ++c)
		{
			// In the editor show the Master + RGB transform even when its
			// switch is off, so editing remains possible before enabling it.
			const double y = selected_effect == effect::CustomCurves
			                     ? color::SampleSpline(settings.Adjustments.Curves[c + 1],
			                                           color::SampleSpline(settings.Adjustments.Curves[0], x))
			                     : live::EvaluateOutputCode(x, c, output, settings);
			paths[c].push_back(position(x, y));
			if (selected_effect == effect::CustomCurves && (editing_channel == 0 || editing_channel == c + 1) &&
			    std::abs(paths[c].back().y - editable.y) > 1)
				needs_guide = true;
		}
	}
	if (selected_effect == effect::CustomCurves && needs_guide)
	{
		HPEN guide = CreatePen(PS_DOT, 1, ui::THEME_MUTED);
		SelectObject(buffer, guide);
		Polyline(buffer, editable_path.data(), static_cast<int>(editable_path.size()));
		SelectObject(buffer, old_pen);
		DeleteObject(guide);
	}
	plot::DrawRgbCurves(buffer, client.right, client.bottom, paths, ScaleForDpi(2), &curve_scratch);
	if (selected_effect == effect::CustomCurves)
	{
		const auto& editable = settings.Adjustments.Curves[editing_channel];
		HPEN handles =
			CreatePen(PS_SOLID, ScaleForDpi(2), editing_channel ? inks[editing_channel - 1] : RGB(255, 255, 255));
		SelectObject(buffer, handles);
		const auto previous_brush = SelectObject(buffer, GetStockObject(BLACK_BRUSH));
		for (int i = 0; i < 9; ++i)
		{
			const auto at = position(i / 8.0, color::SampleSpline(editable, i / 8.0));
			const int x = at.x, y = at.y;
			Ellipse(buffer, x - ScaleForDpi(3), y - ScaleForDpi(3), x + ScaleForDpi(4), y + ScaleForDpi(4));
			if (GetFocus() == hwnd && i == keyboard_point)
			{
				RECT focus { x - ScaleForDpi(6), y - ScaleForDpi(6), x + ScaleForDpi(7), y + ScaleForDpi(7) };
				DrawFocusRect(buffer, &focus);
			}
		}
		SelectObject(buffer, previous_brush);
		SelectObject(buffer, old_pen);
		DeleteObject(handles);
	}
	SelectObject(buffer, old_pen);
	DeleteObject(grid);
	DeleteObject(identity);
	LOGFONTW axis_style {};
	GetObjectW(ui_font, sizeof(axis_style), &axis_style);
	axis_style.lfHeight = -ScaleForDpi(ui::style::SECONDARY_TEXT_SIZE);
	HFONT axis_font = CreateFontIndirectW(&axis_style);
	HGDIOBJ old_font = SelectObject(buffer, axis_font ? axis_font : ui_font);
	SetBkMode(buffer, TRANSPARENT);
	SetTextColor(buffer, ui::THEME_TEXT);
	const auto axis = [&](const wchar_t* value, int cx, int cy)
	{
		RECT area { cx - ScaleForDpi(8), cy - ScaleForDpi(8), cx + ScaleForDpi(8), cy + ScaleForDpi(8) };
		DrawTextW(buffer, value, 1, &area, DT_SINGLELINE | DT_CENTER | DT_VCENTER | DT_NOPREFIX);
	};
	const int lower_center = geometry.bottom + geometry.size / 2, side_center = geometry.left + geometry.size / 2;
	axis(L"0", left, lower_center);
	axis(L"1", right, lower_center);
	axis(L"1", side_center, top);
	axis(L"0", side_center, bottom);
	axis_style.lfHeight = -ScaleForDpi(ui::style::CAPTION_TEXT_SIZE);
	HFONT tone_font = CreateFontIndirectW(&axis_style);
	SelectObject(buffer, tone_font ? tone_font : ui_font);
	SetTextColor(buffer, ui::THEME_MUTED);
	const wchar_t* tones[] { L"Shadows", L"Midtones", L"Highlights" };
	for (int i = 0; i < 3; ++i)
	{
		RECT label { left + (right - left) * i / 3, bottom + ScaleForDpi(2), left + (right - left) * (i + 1) / 3,
			         bottom + ScaleForDpi(14) };
		DrawTextW(buffer, tones[i], -1, &label,
		          DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX |
		              (i == 0   ? DT_LEFT
		               : i == 1 ? DT_CENTER
		                        : DT_RIGHT));
	}
	SelectObject(buffer, old_font);
	if (tone_font)
		DeleteObject(tone_font);
	if (axis_font)
		DeleteObject(axis_font);
	// Paint last, over the graph itself: long warnings may wrap below the
	// header row without a child window clipping their second line.
	wchar_t status_text[2048] {};
	GetWindowTextW(status_label, status_text, 2048);
	RECT status_area { geometry.left + geometry.size + ScaleForDpi(12), geometry.top, geometry.right - ScaleForDpi(12),
		               geometry.bottom };
	RECT measured = status_area;
	const auto status_font = SelectObject(buffer, ui_font);
	SetBkMode(buffer, TRANSPARENT);
	SetTextColor(buffer, status_error ? ui::THEME_ERROR : ui::THEME_MUTED);
	DrawTextW(buffer, status_text, -1, &measured, DT_CENTER | DT_WORDBREAK | DT_CALCRECT | DT_NOPREFIX);
	status_area.top += std::max(0, (geometry.size - static_cast<int>(measured.bottom - measured.top)) / 2);
	if (graph_status_visible)
		DrawTextW(buffer, status_text, -1, &status_area, DT_CENTER | DT_WORDBREAK | DT_NOPREFIX);
	SelectObject(buffer, status_font);
	frame.Finish();
	if (message == WM_PAINT)
		EndPaint(hwnd, &paint);
	return 0;
}

bool RenderWindowToFile(const wchar_t* path, HWND target = nullptr)
{
	if (!target)
		target = main_window;
	RECT outer {};
	GetWindowRect(target, &outer);
	RECT rect { 0, 0, outer.right - outer.left, outer.bottom - outer.top };
	HDC screen = GetDC(nullptr);
	HDC dc = CreateCompatibleDC(screen);
	HBITMAP bitmap = CreateCompatibleBitmap(screen, rect.right, rect.bottom);
	ReleaseDC(nullptr, screen);
	HGDIOBJ previous = SelectObject(dc, bitmap);
	FillRect(dc, &rect, ui::BackgroundBrush());
	SendMessageW(target, WM_PRINT, reinterpret_cast<WPARAM>(dc),
	             PRF_NONCLIENT | PRF_CLIENT | PRF_CHILDREN | PRF_ERASEBKGND);
	SelectObject(dc, previous);
	BITMAPINFO info {};
	info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
	info.bmiHeader.biWidth = rect.right;
	info.bmiHeader.biHeight = rect.bottom;
	info.bmiHeader.biPlanes = 1;
	info.bmiHeader.biBitCount = 32;
	info.bmiHeader.biCompression = BI_RGB;
	std::vector<unsigned char> pixels(static_cast<std::size_t>(rect.right) * rect.bottom * 4);
	const bool captured =
		GetDIBits(dc, bitmap, 0, static_cast<UINT>(rect.bottom), pixels.data(), &info, DIB_RGB_COLORS) != 0;
	DeleteObject(bitmap);
	DeleteDC(dc);
	if (!captured)
		return false;
	BITMAPFILEHEADER header {};
	header.bfType = 0x4d42;
	header.bfOffBits = sizeof(header) + sizeof(BITMAPINFOHEADER);
	header.bfSize = header.bfOffBits + static_cast<DWORD>(pixels.size());
	HANDLE file = CreateFileW(path, GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
	if (file == INVALID_HANDLE_VALUE)
		return false;
	DWORD count = 0;
	const bool written = WriteFile(file, &header, sizeof(header), &count, nullptr) && count == sizeof(header) &&
	                     WriteFile(file, &info.bmiHeader, sizeof(BITMAPINFOHEADER), &count, nullptr) &&
	                     count == sizeof(BITMAPINFOHEADER) &&
	                     WriteFile(file, pixels.data(), static_cast<DWORD>(pixels.size()), &count, nullptr) &&
	                     count == pixels.size();
	CloseHandle(file);
	return written;
}

HWND CreateChildControl(const wchar_t* class_name, const wchar_t* text, DWORD style, int id, HWND parent = nullptr)
{
	if (wcscmp(class_name, L"COMBOBOX") == 0)
		style |= CBS_OWNERDRAWFIXED | CBS_HASSTRINGS;
	HWND child =
		CreateWindowExW(0, class_name, text, WS_CHILD | WS_VISIBLE | style, 0, 0, 1, 1, parent ? parent : main_window,
	                    reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), app_instance, nullptr);
	ui::ApplyControlTheme(child);
	return child;
}

void CreateEditorIfNeeded()
{
	if (editor_ready)
		return;
	EditorSync sync;
	EnsureCommonControls();
	editor_ready = true;
	HWND hwnd = main_window;
	ui::FixedWindowSize(hwnd, CLIENT_WIDTH, CLIENT_HEIGHT);
	ui::EnableViewport(hwnd, CLIENT_WIDTH, CLIENT_HEIGHT);
	preset_label = CreateChildControl(L"STATIC", L"Look preset", 0, 0);
	preset_choice = CreateChildControl(L"COMBOBOX", L"", WS_TABSTOP | CBS_DROPDOWNLIST | WS_VSCROLL, ID_PRESET_CHOICE);
	save_preset_button = CreateChildControl(L"BUTTON", L"Save", WS_TABSTOP | BS_PUSHBUTTON, ID_SAVE_PRESET);
	save_as_button = CreateChildControl(L"BUTTON", L"Save as...", WS_TABSTOP | BS_PUSHBUTTON, ID_SAVE_PRESET_AS);
	open_preset_button = CreateChildControl(L"BUTTON", L"Preset folder...", WS_TABSTOP | BS_PUSHBUTTON, ID_OPEN_PRESET);
	autosave_control = CreateChildControl(L"BUTTON", L"Autosave", WS_TABSTOP | BS_AUTOCHECKBOX, ID_AUTOSAVE);
	revert_preset_button = CreateChildControl(L"BUTTON", L"Revert edits", WS_TABSTOP | BS_PUSHBUTTON, ID_REVERT_PRESET);
	active_look_label = CreateChildControl(L"STATIC", L"", SS_ENDELLIPSIS | SS_NOPREFIX, ID_ACTIVE_LOOK);
	corrections_heading = CreateChildControl(L"STATIC", L"Corrections", SS_CENTERIMAGE, 0);
	effects_label = CreateChildControl(L"STATIC", L"Effects", SS_CENTERIMAGE, 0);
	effect_list = CreateChildControl(L"LISTBOX", L"",
	                                 WS_TABSTOP | LBS_NOTIFY | LBS_OWNERDRAWFIXED | LBS_HASSTRINGS |
	                                     LBS_NOINTEGRALHEIGHT | WS_VSCROLL,
	                                 ID_EFFECT_LIST);
	effects::Attach(effect_list, ToggleEffect, MoveCreative);
	correction_list = CreateChildControl(
		L"LISTBOX", L"Corrections",
		WS_TABSTOP | LBS_NOTIFY | LBS_OWNERDRAWFIXED | LBS_HASSTRINGS | LBS_NOINTEGRALHEIGHT, ID_CORRECTION_LAYERS);
	effects::Attach(correction_list, ToggleEffect, [](int, int) {});
	windows_effect_list =
		CreateChildControl(L"LISTBOX", L"Windows account corrections",
	                       WS_TABSTOP | LBS_NOTIFY | LBS_OWNERDRAWFIXED | LBS_HASSTRINGS | LBS_NOINTEGRALHEIGHT,
	                       ID_WINDOWS_CORRECTION_LAYERS);
	effects::Attach(windows_effect_list, ToggleEffect, [](int, int) {});
	order_hint = CreateChildControl(L"STATIC", L"Drag / Alt+arrows reorder", SS_OWNERDRAW, ID_ORDER_HINT);
	effect_title = CreateChildControl(L"STATIC", L"", 0, 0);
	effect_description = CreateChildControl(L"STATIC", L"", 0, 0);
	amount_label = CreateChildControl(L"STATIC", L"Contrast", SS_CENTERIMAGE, 0);
	pivot_label = CreateChildControl(L"STATIC", L"Pivot", SS_CENTERIMAGE, 0);
	white_label = CreateChildControl(L"STATIC", L"SDR white", SS_CENTERIMAGE, 0);
	gamma_label = CreateChildControl(L"STATIC", L"Target gamma", SS_CENTERIMAGE, 0);
	amount_value = CreateChildControl(L"STATIC", L"", SS_RIGHT | SS_CENTERIMAGE, 0);
	pivot_value = CreateChildControl(L"STATIC", L"", SS_RIGHT | SS_CENTERIMAGE, 0);
	white_value = CreateChildControl(L"STATIC", L"", SS_RIGHT | SS_CENTERIMAGE, 0);
	contrast_slider = CreateChildControl(TRACKBAR_CLASSW, L"", WS_TABSTOP | TBS_NOTICKS, ID_CONTRAST);
	pivot_slider = CreateChildControl(TRACKBAR_CLASSW, L"", WS_TABSTOP | TBS_NOTICKS, ID_PIVOT);
	white_slider = CreateChildControl(TRACKBAR_CLASSW, L"", WS_TABSTOP | TBS_NOTICKS, ID_WHITE);
	SendMessageW(contrast_slider, TBM_SETRANGE, TRUE, MAKELPARAM(-25, 25));
	SendMessageW(pivot_slider, TBM_SETRANGE, TRUE, MAKELPARAM(20, 80));
	SendMessageW(pivot_slider, TBM_SETPOS, TRUE, pivot_position);
	SendMessageW(white_slider, TBM_SETRANGE, TRUE, MAKELPARAM(80, 480));
	SendMessageW(white_slider, TBM_SETPOS, TRUE, white_position);
	ui::ConfigureSlider(contrast_slider, 0);
	ui::ConfigureSlider(pivot_slider, 50);
	ui::ConfigureSlider(white_slider, 200);
	gamma_choice = CreateChildControl(L"BUTTON", L"2.2", WS_GROUP | WS_TABSTOP | BS_AUTORADIOBUTTON, ID_GAMMA);
	gamma_other = CreateChildControl(L"BUTTON", L"2.4", BS_AUTORADIOBUTTON, ID_GAMMA_OTHER);
	SendMessageW(gamma_choice, BM_SETCHECK, BST_CHECKED, 0);
	curve_canvas = CreateChildControl(L"LutCurvePreview", L"", WS_TABSTOP | WS_GROUP | WS_CLIPCHILDREN, 0);
	status_label = CreateChildControl(L"STATIC", L"", SS_OWNERDRAW, ID_MONITOR_STATUS, curve_canvas);
	// Retain the status text handle for state queries; the canvas paints it
	// and exposes it to accessibility without a separately clipped surface.
	ShowWindow(status_label, SW_HIDE);

	monitor_mode = CreateChildControl(L"STATIC", L"", SS_OWNERDRAW, ID_MONITOR_MODE, curve_canvas);
	monitor_profile = CreateChildControl(L"STATIC", L"Desktop profile", SS_OWNERDRAW | SS_NOTIFY | WS_TABSTOP,
	                                     ID_MONITOR_PROFILE, curve_canvas);
	SetWindowSubclass(monitor_profile, ProfileLinkProc, 80, 0);
	website_link = CreateChildControl(L"STATIC", L"CeeJay.dk", SS_OWNERDRAW | SS_NOTIFY | WS_TABSTOP, ID_WEBSITE_LINK,
	                                  curve_canvas);
	SetWindowSubclass(website_link, ProfileLinkProc, 80, 0);
	for (HWND label : { monitor_mode, monitor_profile, website_link })
	{
		SetWindowLongPtrW(label, GWL_STYLE, GetWindowLongPtrW(label, GWL_STYLE) | WS_CLIPSIBLINGS);
		SetWindowSubclass(label, MonitorLabelPaintProc, 81, 0);
	}
	SetWindowLongPtrW(curve_canvas, GWL_EXSTYLE, GetWindowLongPtrW(curve_canvas, GWL_EXSTYLE) | WS_EX_CONTROLPARENT);
	hdr_button = CreateChildControl(L"BUTTON", L"Toggle Windows HDR on the selected display",
	                                WS_TABSTOP | BS_PUSHBUTTON, ID_TOGGLE_WINDOWS_HDR, curve_canvas);
	power_button = CreateChildControl(L"BUTTON", L"Master power on/off (Alt+P)", WS_TABSTOP | BS_PUSHBUTTON,
	                                  ID_TEST_CONTRAST, curve_canvas);
	profiles_button = CreateChildControl(L"BUTTON", ui::wording::LABEL_APPLICATION_PROFILES, WS_TABSTOP | BS_PUSHBUTTON,
	                                     ID_APPLICATION_PROFILES, curve_canvas);
	info_button =
		CreateChildControl(L"BUTTON", L"Picture status", WS_TABSTOP | BS_PUSHBUTTON, ID_OUTPUT_INFO, curve_canvas);
	monitor_settings_button = CreateChildControl(L"BUTTON", ui::wording::LABEL_SETTINGS, WS_TABSTOP | BS_PUSHBUTTON,
	                                             ID_SETTINGS_PAGE, curve_canvas);
	monitor_tips =
		CreateWindowExW(WS_EX_TOPMOST, TOOLTIPS_CLASSW, nullptr, WS_POPUP | TTS_ALWAYSTIP | TTS_NOPREFIX, CW_USEDEFAULT,
	                    CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, main_window, nullptr, app_instance, nullptr);
	ui::StyleTooltip(monitor_tips);
	SendMessageW(monitor_tips, TTM_SETMAXTIPWIDTH, 0, ScaleForDpi(360));
	for (HWND button : { hdr_button, power_button, profiles_button, monitor_settings_button, info_button,
	                     monitor_profile, website_link })
	{
		TOOLINFOW tip { sizeof(tip) };
		tip.uFlags = TTF_IDISHWND | TTF_SUBCLASS;
		tip.hwnd = curve_canvas;
		tip.uId = reinterpret_cast<UINT_PTR>(button);
		tip.lpszText = LPSTR_TEXTCALLBACKW;
		SendMessageW(monitor_tips, TTM_ADDTOOLW, 0, reinterpret_cast<LPARAM>(&tip));
	}
	reset_button = CreateChildControl(L"BUTTON", L"Reset adjustment", WS_TABSTOP | BS_PUSHBUTTON, ID_RESET);
	selected_effect_toggle = CreateChildControl(L"BUTTON", L"Selected effect on/off", WS_TABSTOP | BS_AUTOCHECKBOX,
	                                            ID_SELECTED_EFFECT_TOGGLE);
	ui::ToggleButton(selected_effect_toggle, false);
	export_button = CreateChildControl(L"BUTTON", L"Export curve...", WS_TABSTOP | BS_PUSHBUTTON, ID_EXPORT);
	{
		const std::pair<HWND, ui::Glyph> toolbar[] { { save_preset_button, ui::Glyph::Save },
			                                         { save_as_button, ui::Glyph::SaveAs },
			                                         { export_button, ui::Glyph::Export },
			                                         { revert_preset_button, ui::Glyph::Revert },
			                                         { reset_button, ui::Glyph::Reset } };
		for (const auto& entry : toolbar)
		{
			ui::SetIconButton(entry.first, entry.second, false, IsWindowEnabled(entry.first) != FALSE);
		}
		const std::pair<HWND, const wchar_t*> hints[] {
			{ save_preset_button, L"Save preset" },     { save_as_button, L"Save preset as..." },
			{ export_button, L"Export curve..." },      { revert_preset_button, L"Revert unsaved changes" },
			{ reset_button, L"Reset this adjustment" }, { selected_effect_toggle, L"Turn this adjustment on or off" }
		};
		for (const auto& entry : hints)
		{
			TOOLINFOW tip { sizeof(tip) };
			tip.uFlags = TTF_IDISHWND | TTF_SUBCLASS;
			tip.hwnd = main_window;
			tip.uId = reinterpret_cast<UINT_PTR>(entry.first);
			tip.lpszText = const_cast<wchar_t*>(entry.second);
			SendMessageW(monitor_tips, TTM_ADDTOOLW, 0, reinterpret_cast<LPARAM>(&tip));
		}
	}

	source_choice = CreateChildControl(L"COMBOBOX", L"", WS_TABSTOP | CBS_DROPDOWNLIST | WS_VSCROLL, ID_SOURCE_CHOICE,
	                                   curve_canvas);
	test_note = CreateChildControl(L"STATIC", L"", 0, 0);
	bypass_button = CreateChildControl(L"BUTTON", L"Bypass look", WS_TABSTOP | BS_PUSHBUTTON, ID_BYPASS);
	shortcut_label = CreateChildControl(L"STATIC", L"Bypass shortcut", 0, 0);
	shortcut_control = CreateChildControl(HOTKEY_CLASSW, L"", WS_TABSTOP, ID_SHORTCUT);
	SendMessageW(shortcut_control, HKM_SETRULES, HKCOMB_NONE, HOTKEYF_CONTROL);
	shortcut_button = CreateChildControl(L"BUTTON", L"Set shortcut", WS_TABSTOP | BS_PUSHBUTTON, ID_SET_SHORTCUT);
	hdr_fix_control = CreateChildControl(L"BUTTON", L"Also apply when Windows HDR is off (SDR)",
	                                     WS_TABSTOP | BS_AUTOCHECKBOX, ID_HDR_FIX);
	settings_button =
		CreateChildControl(L"BUTTON", ui::wording::LABEL_SETTINGS, WS_TABSTOP | BS_PUSHBUTTON, ID_SETTINGS_PAGE);
	version_label = CreateChildControl(L"STATIC", L"MonitorMage " MONITORMAGE_VERSION_W, 0, 0);
	startup_control =
		CreateChildControl(L"BUTTON", L"Start with Windows", WS_TABSTOP | BS_AUTOCHECKBOX, ID_START_WITH_WINDOWS);
	start_minimized_control =
		CreateChildControl(L"BUTTON", L"Start minimized", WS_TABSTOP | BS_AUTOCHECKBOX, ID_START_MINIMIZED);
	saturation_report_button =
		CreateChildControl(L"BUTTON", L"Color effect report...", WS_TABSTOP | BS_PUSHBUTTON, ID_SATURATION_REPORT);
	ui::SetHelpText(saturation_report_button, L"Color effect report");
	filter_choice = CreateChildControl(L"COMBOBOX", L"", WS_TABSTOP | CBS_DROPDOWNLIST, ID_COLOR_FILTER_CHOICE);
	for (const auto* name : windows_controls::FILTER_NAMES)
		SendMessageW(filter_choice, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(name));
	filter_settings_button =
		CreateChildControl(L"BUTTON", L"Windows settings...", WS_TABSTOP | BS_PUSHBUTTON, ID_COLOR_FILTER_SETTINGS);
	night_schedule = CreateChildControl(L"BUTTON", L"Use schedule", WS_TABSTOP | BS_AUTOCHECKBOX, ID_NIGHT_SCHEDULE);
	night_start_label = CreateChildControl(L"STATIC", L"From", SS_CENTERIMAGE, 0);
	night_end_label = CreateChildControl(L"STATIC", L"Until", SS_CENTERIMAGE, 0);
	night_start = CreateChildControl(L"EDIT", L"21:00", WS_TABSTOP | ES_AUTOHSCROLL, ID_NIGHT_START);
	night_end = CreateChildControl(L"EDIT", L"07:00", WS_TABSTOP | ES_AUTOHSCROLL, ID_NIGHT_END);
	SendMessageW(night_start, EM_SETLIMITTEXT, 5, 0);
	SendMessageW(night_end, EM_SETLIMITTEXT, 5, 0);
	night_display = CreateChildControl(L"COMBOBOX", L"", WS_TABSTOP | CBS_DROPDOWNLIST, ID_NIGHT_DISPLAY);
	night_include =
		CreateChildControl(L"BUTTON", L"Use on this display", WS_TABSTOP | BS_AUTOCHECKBOX, ID_NIGHT_INCLUDE);
	ui::SetHelpText(night_display, L"Night light display");
	ui::SetHelpText(night_schedule, L"Use daily schedule");
	ui::SetHelpText(night_start, L"Start time (24-hour)");
	ui::SetHelpText(night_end, L"End time (24-hour)");
	ui::SetHelpText(night_include, L"Include this display");
	correction_note = CreateChildControl(L"STATIC", L"", 0, 0);
	if (!render_only)
	{
		try
		{
			startup_enabled = app_settings::StartsWithWindows(app_settings::ExecutablePath());
		}
		catch (const std::exception& error)
		{
			ShowPresetError(error);
		}
	}
	font_label = CreateChildControl(L"STATIC", L"Interface font", 0, 0);
	font_choice = CreateChildControl(L"COMBOBOX", L"", WS_TABSTOP | CBS_DROPDOWNLIST, ID_FONT_CHOICE);
	for (const wchar_t* name : { L"Bahnschrift", L"Segoe UI", L"Segoe UI Semilight" })
		SendMessageW(font_choice, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(name));
	curve_channel = CreateChildControl(L"COMBOBOX", L"", WS_TABSTOP | CBS_DROPDOWNLIST, ID_CURVE_CHANNEL);
	for (const wchar_t* name : { L"Master", L"Red", L"Green", L"Blue" })
		SendMessageW(curve_channel, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(name));
	SendMessageW(curve_channel, CB_SETCURSEL, 0, 0);
	import_button = CreateChildControl(L"BUTTON", L"Import 1D LUT...", WS_TABSTOP | BS_PUSHBUTTON, ID_IMPORT_1D);
	export_1d_button = CreateChildControl(L"BUTTON", L"Export look LUT...", WS_TABSTOP | BS_PUSHBUTTON, ID_EXPORT_1D);
	for (size_t i = 0; i < adjustment_sliders.size(); ++i)
	{
		adjustment_labels[i] = CreateChildControl(L"STATIC", L"", SS_CENTERIMAGE, 0);
		adjustment_values[i] = CreateChildControl(L"STATIC", L"", SS_RIGHT | SS_CENTERIMAGE, 0);
		adjustment_sliders[i] = CreateChildControl(TRACKBAR_CLASSW, L"", WS_TABSTOP | TBS_NOTICKS,
		                                           ID_ADJUSTMENT_BASE + static_cast<int>(i));
	}
	{
		const std::pair<HWND, const wchar_t*> help[] { { preset_choice, L"Choose preset" },
			                                           { autosave_control, L"Autosave preset changes" },
			                                           { source_choice, L"Choose display" },
			                                           { font_choice, L"Interface font" },
			                                           { shortcut_control, L"Pause effects shortcut" },
			                                           { shortcut_button, L"Set shortcut" },
			                                           { startup_control, L"Start with Windows" },
			                                           { start_minimized_control, L"Start in the tray" },
			                                           { open_preset_button, L"Open preset folder" },
			                                           { bypass_button, L"Pause / resume effects" },
			                                           { filter_choice, L"Choose color filter" },
			                                           { filter_settings_button, L"Windows color filter settings" },
			                                           { contrast_slider, L"Contrast" },
			                                           { pivot_slider, L"Contrast pivot" },
			                                           { white_slider, L"SDR white brightness" },
			                                           { gamma_choice, L"Gamma 2.2" },
			                                           { gamma_other, L"Gamma 2.4" },
			                                           { hdr_fix_control, L"Also apply in SDR" },
			                                           { curve_channel, L"Choose curve channel" },
			                                           { import_button, L"Import 1D LUT" },
			                                           { export_1d_button, L"Export effects as 1D LUT" } };
		for (const auto& item : help)
			ui::SetHelpText(item.first, item.second);
	}
	DiscoverPresets(preset_role::Look);
	DiscoverPresets(preset_role::Correction);
	LoadPresetControls();
	SendMessageW(autosave_control, BM_SETCHECK, autosave_enabled ? BST_CHECKED : BST_UNCHECKED, 0);
	{
		const int family = wcscmp(ui::FontFamily(), L"Bahnschrift") == 0 ? 0
		                   : wcscmp(ui::FontFamily(), L"Segoe UI") == 0  ? 1
		                                                                 : 2;
		SendMessageW(font_choice, CB_SETCURSEL, family, 0);
	}
	RefreshColorFilters();
	for (const auto& source : display_sources)
		SendMessageW(source_choice, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(source.label.c_str()));
	SelectSource(selected_source);
	SendMessageW(shortcut_control, HKM_SETHOTKEY, active_shortcut, 0);
	LayoutEditorControls();
	RefreshInterface();
}

LRESULT CALLBACK MainWindowProc(HWND hwnd, UINT message, WPARAM w_param, LPARAM l_param)
{
	static const HBRUSH sidebar = CreateSolidBrush(ui::THEME_SIDEBAR);
	const auto paint_background = [&](HDC dc)
	{
		RECT rect {};
		GetClientRect(hwnd, &rect);
		FillRect(dc, &rect, ui::BackgroundBrush());
		rect.right = std::max(0L, ScaleForDpi(SIDEBAR_WIDTH) - ui::ViewportOffset(hwnd).x);
		FillRect(dc, &rect, sidebar);
	};
	if (message == WM_ERASEBKGND || message == WM_PRINTCLIENT)
	{
		paint_background(reinterpret_cast<HDC>(w_param));
		return 1;
	}
	if (message == WM_PAINT)
	{
		PAINTSTRUCT paint {};
		const auto dc = BeginPaint(hwnd, &paint);
		paint_background(dc);
		EndPaint(hwnd, &paint);
		return 0;
	}
	if ((message == WM_CTLCOLORSTATIC &&
	     (reinterpret_cast<HWND>(l_param) == corrections_heading || reinterpret_cast<HWND>(l_param) == effects_label ||
	      reinterpret_cast<HWND>(l_param) == order_hint)) ||
	    (message == WM_CTLCOLORLISTBOX &&
	     (reinterpret_cast<HWND>(l_param) == effect_list || reinterpret_cast<HWND>(l_param) == correction_list ||
	      reinterpret_cast<HWND>(l_param) == windows_effect_list)))
	{
		const auto dc = reinterpret_cast<HDC>(w_param);
		SetBkColor(dc, ui::THEME_SIDEBAR);
		SetTextColor(dc, ui::THEME_TEXT);
		return reinterpret_cast<LRESULT>(sidebar);
	}
	LRESULT theme_result = 0;
	// Client geometry must already match the eventual custom title bar, even
	// while its painting, fonts and interactive controls are deferred.
	if ((editor_ready || message == WM_NCCALCSIZE) &&
	    ui::HandleThemeMessage(hwnd, message, w_param, l_param, theme_result))
		return theme_result;
	if (taskbar_created_message && message == taskbar_created_message)
	{
		InstallTray();
		return 0;
	}
	switch (message)
	{
		case WM_CREATE:
			main_window = hwnd;
			InitializePresets();
			runtime_ready = true;
			if (!render_only)
			{
				try
				{
					profile_configuration = applications::Read(SettingsFilePath());
				}
				catch (const std::exception& error)
				{
					ShowPresetError(error);
				}
			}
			RefreshSources();
			if (!render_only)
				LoadAndBindHotkey();
			if (!defer_editor)
				CreateEditorIfNeeded();
			if (!render_only && !InstallTray())
				CreateEditorIfNeeded();
			return 0;
		case WM_MEASUREITEM:
			if (reinterpret_cast<MEASUREITEMSTRUCT*>(l_param)->CtlID == ID_EFFECT_LIST ||
			    reinterpret_cast<MEASUREITEMSTRUCT*>(l_param)->CtlID == ID_CORRECTION_LAYERS)
			{
				reinterpret_cast<MEASUREITEMSTRUCT*>(l_param)->itemHeight = ScaleForDpi(effects::ROW_HEIGHT);
				return TRUE;
			}
			break;
		case WM_DRAWITEM:
			if (reinterpret_cast<DRAWITEMSTRUCT*>(l_param)->hwndItem == order_hint)
			{
				const auto& item = *reinterpret_cast<DRAWITEMSTRUCT*>(l_param);
				HBRUSH brush = CreateSolidBrush(ui::THEME_SIDEBAR);
				FillRect(item.hDC, &item.rcItem, brush);
				DeleteObject(brush);
				const auto previous =
					SelectObject(item.hDC, reinterpret_cast<HFONT>(SendMessageW(order_hint, WM_GETFONT, 0, 0)));
				SetBkMode(item.hDC, TRANSPARENT);
				SetTextColor(item.hDC, ui::THEME_MUTED);
				wchar_t text[128] {};
				GetWindowTextW(order_hint, text, 128);
				RECT area = item.rcItem;
				DrawTextW(item.hDC, text, -1, &area,
				          DT_SINGLELINE | DT_CENTER | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX);
				SelectObject(item.hDC, previous);
				return TRUE;
			}
			if (effects::Draw(*reinterpret_cast<DRAWITEMSTRUCT*>(l_param)))
				return TRUE;
			break;
		case WM_NOTIFY:
			if (reinterpret_cast<NMHDR*>(l_param)->hwndFrom == monitor_tips &&
			    reinterpret_cast<NMHDR*>(l_param)->code == TTN_GETDISPINFOW)
			{
				auto tip = reinterpret_cast<NMTTDISPINFOW*>(l_param);
				const HWND button = reinterpret_cast<HWND>(tip->hdr.idFrom);
				static std::wstring text;
				if (button == info_button)
				{
					wchar_t details[2048] {};
					GetWindowTextW(status_label, details, 2048);
					text = details;
				}
				else if (button == website_link)
					text = L"Visit CeeJay.dk";
				else
					text = button == hdr_button ? std::wstring(ui::HdrTooltip(hdr_state.on, hdr_state.available))
					       : button == power_button
					           ? std::wstring(corrections_requested ? L"Turn off (Alt+P)" : L"Turn on (Alt+P)")
					       : button == monitor_settings_button ? std::wstring(ui::wording::LABEL_SETTINGS)
					                                           : std::wstring(ui::wording::LABEL_APPLICATION_PROFILES);
				tip->lpszText = text.data();
				return 0;
			}
			break;
		case WM_HSCROLL:
			if (LOWORD(w_param) == TB_THUMBTRACK && !slider_dragging)
			{
				look_dirty_before_drag = CurrentPreset(preset_role::Look).IsDirty();
				slider_dragging = true;
				if (selected_effect != effect::NightLight)
					KillTimer(hwnd, TIMER_PRESET);
			}
			if (LOWORD(w_param) == TB_THUMBPOSITION || LOWORD(w_param) == TB_ENDTRACK)
				slider_dragging = false;
			{
				const int index = GetDlgCtrlID(reinterpret_cast<HWND>(l_param)) - ID_ADJUSTMENT_BASE;
				const auto fields = SliderParameters();
				if (index >= 0 && static_cast<size_t>(index) < fields.size())
				{
					int value = static_cast<int>(SendMessageW(adjustment_sliders[index], TBM_GETPOS, 0, 0));
					if (selected_effect == effect::Levels)
					{
						if (index % 2 == 0)
							value = std::min(value, *fields[index + 1].value - 5);
						else
							value = std::max(value, *fields[index - 1].value + 5);
					}
					*fields[index].value = value;
					if (selected_effect == effect::NightLight)
					{
						if (!render_only && LOWORD(w_param) != TB_THUMBTRACK)
							PersistPresetChoices();
						UpdateNightLightSchedule();
					}
					else
						OnPresetEdited(preset_role::Look);
				}
			}
			contrast_position = static_cast<int>(SendMessageW(contrast_slider, TBM_GETPOS, 0, 0));
			pivot_position = static_cast<int>(SendMessageW(pivot_slider, TBM_GETPOS, 0, 0));
			white_position = static_cast<int>(SendMessageW(white_slider, TBM_GETPOS, 0, 0));
			if (reinterpret_cast<HWND>(l_param) == contrast_slider || reinterpret_cast<HWND>(l_param) == pivot_slider)
				OnPresetEdited(preset_role::Look);
			RefreshInterface();
			if (corrections_requested)
				SetTimer(hwnd, TIMER_APPLY, 100, nullptr);
			return 0;
		case WM_COMMAND:
			if (LOWORD(w_param) == ID_NIGHT_DISPLAY && HIWORD(w_param) == CBN_SELCHANGE)
			{
				RefreshInterface();
				return 0;
			}
			if (LOWORD(w_param) == ID_NIGHT_SCHEDULE || LOWORD(w_param) == ID_NIGHT_INCLUDE)
			{
				if (LOWORD(w_param) == ID_NIGHT_SCHEDULE)
					night_config.Scheduled = SendMessageW(night_schedule, BM_GETCHECK, 0, 0) == BST_CHECKED;
				else
				{
					const auto source_index = SendMessageW(night_display, CB_GETCURSEL, 0, 0);
					if (source_index >= 0 && static_cast<size_t>(source_index) < display_sources.size())
						night::SetDisplayIncluded(night_config, display_sources[source_index].identity,
						                          SendMessageW(night_include, BM_GETCHECK, 0, 0) == BST_CHECKED);
				}
				if (!render_only)
					PersistPresetChoices();
				ApplyLiveCorrections();
				RefreshInterface();
				return 0;
			}
			if ((LOWORD(w_param) == ID_NIGHT_START || LOWORD(w_param) == ID_NIGHT_END) &&
			    HIWORD(w_param) == EN_KILLFOCUS)
			{
				if (is_closing)
					return 0;
				wchar_t text[16] {};
				GetWindowTextW(reinterpret_cast<HWND>(l_param), text, 16);
				const int value = night::ParseTime(text);
				const bool start = LOWORD(w_param) == ID_NIGHT_START;
				if (value < 0 || value == (start ? night_config.End : night_config.Start))
				{
					night_message = L"Use HH:MM, with different start and end times.";
					RefreshInterface();
					return 0;
				}
				if (value == (start ? night_config.Start : night_config.End))
					return 0;
				(start ? night_config.Start : night_config.End) = value;
				night_message.clear();
				if (!render_only)
					PersistPresetChoices();
				ApplyLiveCorrections();
				RefreshInterface();
				return 0;
			}
			if (LOWORD(w_param) == ID_SATURATION_REPORT)
			{
				if (!render_only)
					try
					{
						const auto report = matrix_session.WriteReport();
						ShellExecuteW(main_window, L"open", report.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
					}
					catch (const std::exception&)
					{
						matrix_message = L"The saturation report could not be saved.";
						RefreshInterface();
					}
				return 0;
			}
			if (LOWORD(w_param) == ID_OUTPUT_INFO)
			{
				SendMessageW(monitor_tips, TTM_POPUP, 0, 0);
				return 0;
			}
			if (LOWORD(w_param) == ID_SELECTED_EFFECT_TOGGLE && HIWORD(w_param) == BN_CLICKED)
			{
				ToggleEffect(selected_effect);
				return 0;
			}
			if (LOWORD(w_param) == ID_TOGGLE_WINDOWS_HDR)
			{
				ToggleWindowsHdrMode();
				return 0;
			}
			if (LOWORD(w_param) == ID_PRESET_CHOICE && HIWORD(w_param) == CBN_DROPDOWN)
			{
				DiscoverPresets(SelectedPresetRole());
				RefreshPresetChoice();
				RefreshInterface();
				return 0;
			}
			if (LOWORD(w_param) == ID_IMPORT_1D || LOWORD(w_param) == ID_EXPORT_1D)
			{
				ImportOrExportLut(LOWORD(w_param) == ID_IMPORT_1D);
				return 0;
			}
			if (LOWORD(w_param) == ID_SETTINGS_PAGE)
			{
				settings_page = !settings_page;
				RefreshColorFilters();
				LayoutEditorControls();
				SetFocus(settings_page ? font_choice : EffectListForRole(selected_effect));
				return 0;
			}
			if (LOWORD(w_param) == ID_COLOR_FILTER_CHOICE && HIWORD(w_param) == CBN_SELCHANGE)
			{
				const auto choice = SendMessageW(filter_choice, CB_GETCURSEL, 0, 0);
				if (choice >= 0)
					ChangeWindowsFilter(filter_state.on, static_cast<DWORD>(choice), true);
				return 0;
			}
			if (LOWORD(w_param) == ID_COLOR_FILTER_SETTINGS)
			{
				if (!render_only)
					try
					{
						windows_controls::OpenColorFilterSettings(main_window);
					}
					catch (const std::exception& error)
					{
						filter_message = ui::messages::FriendlyError(error.what());
						filter_error = true;
						RefreshInterface();
					}
				return 0;
			}
			if (LOWORD(w_param) == ID_START_WITH_WINDOWS)
			{
				if (!render_only)
				{
					try
					{
						const bool enabled = SendMessageW(startup_control, BM_GETCHECK, 0, 0) == BST_CHECKED;
						app_settings::StartWithWindows(app_settings::ExecutablePath(), enabled);
						startup_enabled = enabled;
						preset_message.clear();
					}
					catch (const std::exception& error)
					{
						ShowPresetError(error);
					}
				}
				RefreshInterface();
				return 0;
			}
			if (LOWORD(w_param) == ID_START_MINIMIZED && HIWORD(w_param) == BN_CLICKED)
			{
				const bool previous = start_minimized;
				start_minimized = SendMessageW(start_minimized_control, BM_GETCHECK, 0, 0) == BST_CHECKED;
				if (!PersistPresetChoices())
					start_minimized = previous;
				RefreshInterface();
				return 0;
			}
			if (LOWORD(w_param) == ID_FONT_CHOICE && HIWORD(w_param) == CBN_SELCHANGE)
			{
				wchar_t name[80] {};
				GetWindowTextW(font_choice, name, 80);
				ui::SetFontFamily(name);
				PersistPresetChoices();
				LayoutEditorControls();
				profile_editor::RefreshAppearance();
				return 0;
			}
			if (LOWORD(w_param) == ID_CURVE_CHANNEL && HIWORD(w_param) == CBN_SELCHANGE)
			{
				editing_channel = static_cast<int>(SendMessageW(curve_channel, CB_GETCURSEL, 0, 0));
				InvalidateGraph();
				return 0;
			}
			if (LOWORD(w_param) == ID_APPLICATION_PROFILES)
			{
				ShowProfiles();
				return 0;
			}
			if (LOWORD(w_param) == ID_MONITOR_PROFILE && HIWORD(w_param) == STN_CLICKED)
			{
				ShowProfiles(render_only);
				return 0;
			}
			if (LOWORD(w_param) == ID_WEBSITE_LINK && HIWORD(w_param) == STN_CLICKED)
			{
				if (!render_only)
					ShellExecuteW(hwnd, L"open", L"https://CeeJay.dk", nullptr, nullptr, SW_SHOWNORMAL);
				return 0;
			}
			if (LOWORD(w_param) == ID_REVERT_PRESET)
			{
				auto& entry = CurrentPreset(SelectedPresetRole());
				entry.current = entry.saved;
				preset_message.clear();
				if (selected_effect != effect::HdrGamma)
					ManualLookOverride();
				LoadPresetControls();
				RefreshPresetChoice();
				ReconcileLiveCorrections();
				RefreshInterface();
				return 0;
			}
			if (LOWORD(w_param) == ID_PRESET_CHOICE && HIWORD(w_param) == CBN_SELCHANGE)
			{
				const auto index = SendMessageW(preset_choice, CB_GETCURSEL, 0, 0);
				if (index >= 0 && static_cast<size_t>(index) < preset_library[SelectedPresetRole()].size())
				{
					const int preset_role = SelectedPresetRole();
					try
					{
						if (index > 0)
							ReadPreset(preset_role, preset_library[preset_role][index].path);
					}
					catch (const std::exception& error)
					{
						ShowPresetError(error);
						RefreshPresetChoice();
						RefreshInterface();
						return 0;
					}
					FlushPresetAutosave();
					preset_index[preset_role] = static_cast<size_t>(index);
					if (selected_effect != effect::HdrGamma)
						ManualLookOverride();
					PersistPresetChoices();
					LoadPresetControls();
					RefreshPresetChoice();
					ReconcileLiveCorrections();
					RefreshInterface();
				}
				return 0;
			}
			if (LOWORD(w_param) == ID_SAVE_PRESET)
			{
				if (CurrentPreset(SelectedPresetRole()).path.empty())
					PresetFileDialog(true);
				else
				{
					SaveExistingPreset(SelectedPresetRole());
					RefreshPresetChoice();
					RefreshInterface();
				}
				return 0;
			}
			if (LOWORD(w_param) == ID_SAVE_PRESET_AS)
			{
				PresetFileDialog(true);
				return 0;
			}
			if (LOWORD(w_param) == ID_OPEN_PRESET)
			{
				const auto folder = PresetFolder(preset_role::Look).parent_path();
				try
				{
					std::filesystem::create_directories(folder);
					ShellExecuteW(main_window, L"explore", folder.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
				}
				catch (const std::exception& error)
				{
					ShowPresetError(error);
					RefreshInterface();
				}
				return 0;
			}
			if (LOWORD(w_param) == ID_AUTOSAVE && HIWORD(w_param) == BN_CLICKED)
			{
				autosave_enabled = SendMessageW(autosave_control, BM_GETCHECK, 0, 0) == BST_CHECKED;
				FlushPresetAutosave();
				PersistPresetChoices();
				RefreshPresetChoice();
				RefreshInterface();
				return 0;
			}
			if (LOWORD(w_param) == ID_SET_SHORTCUT && HIWORD(w_param) == BN_CLICKED)
			{
				const WORD requested = static_cast<WORD>(SendMessageW(shortcut_control, HKM_GETHOTKEY, 0, 0));
				if (render_only)
					return 0;
				if (BindHotkey(requested))
				{
					preferred_shortcut = requested;
					hotkey_message = PersistPresetChoices()
					                     ? L""
					                     : L"This shortcut works now, but could not be saved for next time.";
				}
				else
				{
					hotkey_message = hotkey_error == ERROR_INVALID_PARAMETER
					                     ? L"Choose a key with Ctrl, Alt or Shift. Windows reserves F12."
					                     : L"That shortcut is unavailable. " +
					                           std::wstring(hotkey_registered ? L"Your previous shortcut still works."
					                                                          : L"Choose another combination.");
				}
				RefreshInterface();
				return 0;
			}
			if (LOWORD(w_param) == ID_EXIT)
			{
				if (ConfirmExit())
					DestroyWindow(hwnd);
				return 0;
			}
			if (LOWORD(w_param) == ID_SOURCE_CHOICE && HIWORD(w_param) == CBN_SELCHANGE)
			{
				const auto source_index = SelectedSourceIndex();
				hdr_state = source_index >= 0 && static_cast<size_t>(source_index) < display_sources.size()
				                ? hdr::ReadHdrState(display_sources[source_index].name)
				                : hdr::State {};
				ReconcileLiveCorrections();
			}
			if (LOWORD(w_param) == ID_TEST_CONTRAST && HIWORD(w_param) == BN_CLICKED)
			{
				ToggleCorrections();
			}
			if (LOWORD(w_param) == ID_BYPASS && HIWORD(w_param) == BN_CLICKED)
				ToggleBypass();
			if (LOWORD(w_param) == ID_HDR_FIX && HIWORD(w_param) == BN_CLICKED)
			{
				CurrentPreset(preset_role::Correction).current.ApplyInSdr =
					SendMessageW(hdr_fix_control, BM_GETCHECK, 0, 0) == BST_CHECKED;
				OnPresetEdited(preset_role::Correction);
				ApplyLiveCorrections();
				RefreshInterface();
			}
			if ((LOWORD(w_param) == ID_EFFECT_LIST || LOWORD(w_param) == ID_CORRECTION_LAYERS ||
			     LOWORD(w_param) == ID_WINDOWS_CORRECTION_LAYERS) &&
			    HIWORD(w_param) == LBN_SELCHANGE)
			{
				const auto list = LOWORD(w_param) == ID_EFFECT_LIST                 ? effect_list
				                  : LOWORD(w_param) == ID_WINDOWS_CORRECTION_LAYERS ? windows_effect_list
				                                                                    : correction_list;
				const auto row = SendMessageW(list, LB_GETCURSEL, 0, 0);
				if (row >= 0)
					selected_effect = static_cast<int>(SendMessageW(list, LB_GETITEMDATA, row, 0));
				RefreshColorFilters();
				if (settings_page)
				{
					settings_page = false;
					LayoutEditorControls();
				}
				else
				{
					RefreshPresetChoice();
					RefreshInterface();
				}
			}
			if ((LOWORD(w_param) == ID_GAMMA || LOWORD(w_param) == ID_GAMMA_OTHER) && HIWORD(w_param) == BN_CLICKED)
			{
				gamma_position = LOWORD(w_param) == ID_GAMMA ? 0 : 1;
				OnPresetEdited(preset_role::Correction);
				ApplyLiveCorrections();
				RefreshInterface();
			}
			if (LOWORD(w_param) == ID_EXPORT)
				ExportCurve();
			if (LOWORD(w_param) == ID_RESET &&
			    (selected_effect < effect::FirstWindows || selected_effect == effect::NightLight ||
			     selected_effect >= effect::Saturation))
			{
				if (selected_effect == effect::NightLight)
				{
					night_config.Strength = 35;
					night_config.Fade = 10;
					if (!render_only)
						PersistPresetChoices();
					ApplyLiveCorrections();
					RefreshInterface();
					return 0;
				}
				if (selected_effect == effect::ChannelMixer)
					CurrentPreset(preset_role::Look).current.Monochrome = { 21, 72, 7 };
				else if (selected_effect == effect::Saturation)
					CurrentPreset(preset_role::Look).current.Saturation = 100;
				else if (selected_effect == effect::ContrastCurves)
				{
					contrast_position = 0;
					pivot_position = 50;
					SendMessageW(contrast_slider, TBM_SETPOS, TRUE, 0);
					SendMessageW(pivot_slider, TBM_SETPOS, TRUE, 50);
				}
				else if (selected_effect >= effect::FirstCreative)
				{
					auto& adjustments = CurrentPreset(preset_role::Look).current.Adjustments;
					const color::Adjustments defaults;
					if (selected_effect == effect::ColorBalance)
						adjustments.Balance = defaults.Balance;
					else if (selected_effect == effect::MidtoneGamma)
						adjustments.Midtone = 100;
					else if (selected_effect == effect::Levels)
						adjustments.Levels = defaults.Levels;
					else if (selected_effect == effect::ShadowsHighlights)
					{
						adjustments.Shadows = 0;
						adjustments.Highlights = 0;
					}
					else if (selected_effect == effect::CustomCurves)
						adjustments.Curves = {};
					else
						adjustments.Imported = {};
				}
				else
				{
					white_position = 200;
					gamma_position = 0;
					SendMessageW(white_slider, TBM_SETPOS, TRUE, 200);
					SendMessageW(gamma_choice, BM_SETCHECK, BST_CHECKED, 0);
					SendMessageW(gamma_other, BM_SETCHECK, BST_UNCHECKED, 0);
				}
				OnPresetEdited(SelectedPresetRole());
				ApplyLiveCorrections();
				RefreshInterface();
			}
			return 0;
		case WM_TIMECHANGE:
			if (!render_only)
			{
				UpdateNightLightSchedule();
				ApplyLiveCorrections();
			}
			return 0;
		case WM_DISPLAYCHANGE:
			curve_scratch.Reset();
			[[fallthrough]];
		case WM_SETTINGCHANGE:
		case WM_DEVICECHANGE:
			if (!render_only)
			{
				display_attempts = 0;
				SetTimer(hwnd, TIMER_DISPLAY, 500, nullptr);
			}
			return 0;
		case WM_TIMER:
			if (w_param == TIMER_NIGHT)
			{
				KillTimer(hwnd, TIMER_NIGHT);
				ApplyLiveCorrections();
				return 0;
			}
			if (w_param == TIMER_COLOR_FILTER)
			{
				try
				{
					if (!filter_controller.Poll())
						return 0;
					filter_message = filter_controller.Notice();
					filter_error = false;
				}
				catch (const std::exception& error)
				{
					filter_message = ui::messages::FriendlyError(error.what());
					filter_error = true;
				}
				KillTimer(hwnd, TIMER_COLOR_FILTER);
				RefreshColorFilters();
				SyncColorMatrix(true);
				RefreshInterface();
				return 0;
			}
			if (w_param == TIMER_HDR)
			{
				KillTimer(hwnd, TIMER_HDR);
				const auto state = hdr::ReadHdrState(hdr_source);
				if (state.available && state.on == hdr_desired && state.target == hdr_transition.target &&
				    state.adapter.HighPart == hdr_transition.adapter.HighPart &&
				    state.adapter.LowPart == hdr_transition.adapter.LowPart)
				{
					hdr_pending = false;
					RefreshSources();
					const auto source_index = SelectedSourceIndex();
					if (hdr_resume && source_index >= 0 && static_cast<size_t>(source_index) < display_sources.size() &&
					    display_sources[source_index].name == hdr_source)
					{
						corrections_requested = true;
						ReconcileLiveCorrections();
					}
					else
						live_message = hdr_desired ? L"Windows HDR is on." : L"Windows HDR is off.";
					hdr_resume = false;
					RefreshInterface();
				}
				else if (++hdr_attempts < 6)
					SetTimer(hwnd, TIMER_HDR, 600, nullptr);
				else
				{
					hdr_pending = false;
					hdr_resume = false;
					mode_reset_pending = false;
					RefreshSources();
					live_message =
						L"MonitorMage could not confirm the HDR change. Check Windows display settings, then turn MonitorMage on again.";
					live_error = true;
					RefreshInterface();
				}
				return 0;
			}
			if (w_param == TIMER_FOREGROUND)
			{
				KillTimer(hwnd, TIMER_FOREGROUND);
				CheckForegroundProgram(pending_foreground);
				SyncColorMatrix(true);
				ScheduleColorMatrixRecovery();
				pending_foreground = nullptr;
				return 0;
			}
			if (w_param == TIMER_MATRIX_RECOVERY)
			{
				KillTimer(hwnd, TIMER_MATRIX_RECOVERY);
				const auto state = matrix_session.CurrentState();
				if (!IsColorMatrixNeeded() || state == matrix_effect::State::OtherTool ||
				    state == matrix_effect::State::ExternalChange)
					return 0;
				SyncColorMatrix(true);
				if (const auto delay = matrix_recovery.NextDelay())
					SetTimer(hwnd, TIMER_MATRIX_RECOVERY, delay, nullptr);
				return 0;
			}
			if (w_param == TIMER_PRESET)
			{
				FlushPresetAutosave();
				RefreshPresetChoice();
				RefreshInterface();
				return 0;
			}
			if (w_param == TIMER_IDLE)
			{
				KillTimer(hwnd, TIMER_IDLE);
				const double cpu_ms = (ProcessCpuTime() - idle_cpu_start) / 10000.0;
				PROCESS_MEMORY_COUNTERS_EX memory {};
				memory.cb = sizeof(memory);
				const bool measured =
					GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&memory),
				                         sizeof(memory)) != FALSE;
				{
					std::ofstream report(idle_report);
					report.imbue(std::locale::classic());
					report << "{\"elapsed_ms\":" << GetTickCount64() - idle_start << ",\"cpu_ms\":" << cpu_ms
						   << ",\"working_set_mib\":" << memory.WorkingSetSize / 1048576.0
						   << ",\"private_commit_mib\":" << memory.PrivateUsage / 1048576.0
						   << ",\"page_faults\":" << memory.PageFaultCount - idle_fault_start
						   << ",\"memory_query_ok\":" << (measured ? "true" : "false")
						   << ",\"foreground_hook_ok\":" << (foreground_hook ? "true" : "false")
						   << ",\"mode\":\"hidden editor, corrections disabled, no recovery helper\"}\n";
				}
				DestroyWindow(hwnd);
				return 0;
			}
			if (w_param == TIMER_APPLY)
			{
				KillTimer(hwnd, TIMER_APPLY);
				ApplyLiveCorrections();
			}
			if (w_param == TIMER_DISPLAY)
			{
				KillTimer(hwnd, TIMER_DISPLAY);
				RefreshSources();
				SyncColorMatrix(true);
				ScheduleColorMatrixRecovery();
				RefreshColorFilters();
				if (resume_after_wake)
					resume_after_wake = false;
				ReconcileLiveCorrections();
				RefreshInterface();
			}
			return 0;
		case matrix_effect::MESSAGE_MATRIX_CHANGED:
			if (matrix_session.CurrentState() == matrix_effect::State::OtherTool ||
			    matrix_session.CurrentState() == matrix_effect::State::ExternalChange)
				KillTimer(hwnd, TIMER_MATRIX_RECOVERY);
			RefreshInterface();
			return 0;
		case WM_ACTIVATE:
			if (LOWORD(w_param) != WA_INACTIVE && filter_choice)
			{
				RefreshColorFilters();
				SyncColorMatrix(true);
				RefreshInterface();
			}
			break;
		case WM_HOTKEY:
			if (hotkey_registered && w_param == static_cast<WPARAM>(active_hotkey_id))
				ToggleBypass();
			return 0;
		case MESSAGE_FOREGROUND:
			if (!render_only || !idle_report.empty())
			{
				pending_foreground = reinterpret_cast<HWND>(l_param);
				SetTimer(hwnd, TIMER_FOREGROUND, 100, nullptr);
			}
			return 0;
		case WM_POWERBROADCAST:
			if (w_param == PBT_APMSUSPEND)
			{
				power_suspended = true;
				resume_after_wake = corrections_requested;
				if (live_session.IsActive())
					profile_paused_identity = live_output.identity;
				DisableLiveCorrections(true);
				RefreshInterface();
			}
			if (w_param == PBT_APMRESUMEAUTOMATIC || w_param == PBT_APMRESUMESUSPEND)
			{
				power_suspended = false;
				if (resume_after_wake)
					SetTimer(hwnd, TIMER_DISPLAY, 1500, nullptr);
			}
			return TRUE;
		case WM_CONTEXTMENU:
			if (render_only && reinterpret_cast<HWND>(w_param) == hwnd)
			{
				ShowTrayMenu();
				return 0;
			}
			break;
		case WM_QUERYENDSESSION:
			FlushPresetAutosave();
			DisableLiveCorrections();
			return TRUE;
		case MESSAGE_TRAY:
			if (LOWORD(l_param) == NIN_SELECT || LOWORD(l_param) == NIN_KEYSELECT)
				ShowEditor();
			else if (LOWORD(l_param) == WM_CONTEXTMENU)
				ShowTrayMenu();
			return 0;
		case WM_DPICHANGED:
		{
			dpi = HIWORD(w_param);
			const auto rect = reinterpret_cast<RECT*>(l_param);
			SetWindowPos(hwnd, nullptr, rect->left, rect->top, rect->right - rect->left, rect->bottom - rect->top,
			             SWP_NOZORDER | SWP_NOACTIVATE);
			LayoutEditorControls();
			if (editor_ready)
				ui::ApplyWindowFrameTheme(hwnd);
			return 0;
		}
		case WM_SHOWWINDOW:
			if (w_param)
			{
				EditorSync sync;
				CreateEditorIfNeeded();
				RefreshInterface();
			}
			else
				curve_scratch.Reset();
			break;
		case WM_SIZE:
			if (w_param == SIZE_MINIMIZED)
				curve_scratch.Reset();
			if (editor_ready)
				ui::ApplyWindowFrameTheme(hwnd);
			return 0;
		case WM_CLOSE:
			if (tray_installed)
				ShowWindow(hwnd, SW_HIDE);
			else if (ConfirmExit())
				DestroyWindow(hwnd);
			return 0;
		case WM_DESTROY:
			is_closing = true;
			KillTimer(hwnd, TIMER_MATRIX_RECOVERY);
			// Request matrix restoration before waiting on independent LUT helpers.
			if (!render_only)
				matrix_session.Stop();
			KillTimer(hwnd, TIMER_NIGHT);
			night_outputs.Stop();
			curve_scratch.Reset();
			KillTimer(hwnd, TIMER_COLOR_FILTER);
			if (foreground_hook)
			{
				UnhookWinEvent(foreground_hook);
				foreground_hook = nullptr;
			}
			if (profile_editor::ProfileEditorHandle())
				DestroyWindow(profile_editor::ProfileEditorHandle());
			KillTimer(hwnd, TIMER_FOREGROUND);
			KillTimer(hwnd, TIMER_APPLY);
			KillTimer(hwnd, TIMER_DISPLAY);
			KillTimer(hwnd, TIMER_IDLE);
			KillTimer(hwnd, TIMER_PRESET);
			live_session.Stop();
			if (hotkey_registered)
			{
				UnregisterHotKey(hwnd, active_hotkey_id);
				hotkey_registered = false;
			}
			if (active_test)
			{
				CloseHandle(active_test);
				active_test = nullptr;
			}
			if (tray_installed)
				Shell_NotifyIconW(NIM_DELETE, &tray_icon);
			if (hint_font)
			{
				DeleteObject(hint_font);
				hint_font = nullptr;
			}
			if (ui_font)
			{
				DeleteObject(ui_font);
				ui_font = nullptr;
			}
			PostQuitMessage(0);
			return 0;
	}
	return DefWindowProcW(hwnd, message, w_param, l_param);
}

bool PreparePresetUiTest()
{
	try
	{
		wchar_t temporary[32768] {};
		if (!GetTempPathW(static_cast<DWORD>(std::size(temporary)), temporary))
			return false;
		const auto folder =
			std::filesystem::path(temporary) /
			(L"LutPresetUi-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64()));
		if (!std::filesystem::create_directory(folder))
			return false;
		test_settings_path = (folder / L"MonitorMage.ini").wstring();
		std::filesystem::create_directories(PresetFolder(preset_role::Look));
		std::filesystem::create_directories(PresetFolder(preset_role::Correction));
		presets::Data a, b, c;
		a.Contrast = 10;
		b.Contrast = -10;
		c.kind = presets::Kind::Correction;
		c.Gamma = 24;
		c.HdrFix = false;
		const auto look = PresetFolder(preset_role::Look) / L"A.lutpreset",
				   correction = PresetFolder(preset_role::Correction) / L"C.lutpreset";
		presets::Write(look, a, false);
		presets::Write(PresetFolder(preset_role::Look) / L"B.lutpreset", b, false);
		presets::Write(correction, c, false);
		return WritePrivateProfileStringW(L"Presets", L"Look", look.c_str(), test_settings_path.c_str()) &&
		       WritePrivateProfileStringW(L"Presets", L"Correction", correction.c_str(), test_settings_path.c_str());
	}
	catch (...)
	{
		return false;
	}
}

int RunPresetUiTest()
{
	// Drive our own hidden controls. render_only prevents every live LUT operation.
	auto command = [&](int id, int notification = BN_CLICKED)
	{
		SendMessageW(main_window, WM_COMMAND, MAKEWPARAM(id, notification), 0);
	};
	auto switch_look = [&](const wchar_t* name)
	{
		SendMessageW(preset_choice, CB_SETCURSEL, FindPreset(preset_role::Look, PresetFolder(preset_role::Look) / name),
		             0);
		command(ID_PRESET_CHOICE, CBN_SELCHANGE);
	};
	auto edit_contrast = [&](int value)
	{
		SendMessageW(contrast_slider, TBM_SETPOS, TRUE, value);
		SendMessageW(main_window, WM_HSCROLL, TB_THUMBPOSITION, reinterpret_cast<LPARAM>(contrast_slider));
	};
	const auto look = PresetFolder(preset_role::Look) / L"A.lutpreset",
			   correction = PresetFolder(preset_role::Correction) / L"C.lutpreset";
	try
	{
		if (contrast_position != 10 || gamma_position != 1 || hdr_fix_enabled || autosave_enabled)
			return 10;
		for (bool minimized : { true, false })
		{
			SendMessageW(start_minimized_control, BM_SETCHECK, minimized ? BST_CHECKED : BST_UNCHECKED, 0);
			command(ID_START_MINIMIZED);
			if (start_minimized != minimized ||
			    GetPrivateProfileIntW(L"Startup", L"Minimized", 2, SettingsFilePath().c_str()) !=
			        static_cast<UINT>(minimized))
				return 282;
			// Saving another preference must retain the startup choice.
			if (!PersistPresetChoices() ||
			    GetPrivateProfileIntW(L"Startup", L"Minimized", 2, SettingsFilePath().c_str()) !=
			        static_cast<UINT>(minimized))
				return 283;
		}
		const auto unopened = FindPreset(preset_role::Look, PresetFolder(preset_role::Look) / L"B.lutpreset");
		if (unopened == preset_library[preset_role::Look].size() ||
		    preset_library[preset_role::Look][unopened].loaded || preset_library[preset_role::Look][unopened].IsDirty())
			return 191;
		// An invalid unopened file must neither slow startup with parsing nor
		// replace the active draft when the user selects it later.
		const auto invalid = PresetFolder(preset_role::Look) / L"Invalid.lutpreset";
		presets::WriteBytes(invalid, "not a MonitorMage preset", false);
		DiscoverPresets(preset_role::Look);
		RefreshPresetChoice();
		const auto bad_index = FindPreset(preset_role::Look, invalid), original_index = preset_index[preset_role::Look];
		const auto original_data = CurrentPreset(preset_role::Look).current;
		SendMessageW(preset_choice, CB_SETCURSEL, bad_index, 0);
		command(ID_PRESET_CHOICE, CBN_SELCHANGE);
		if (preset_index[preset_role::Look] != original_index ||
		    CurrentPreset(preset_role::Look).current != original_data ||
		    preset_library[preset_role::Look][bad_index].loaded)
			return 192;
		std::filesystem::remove(invalid);
		preset_library[preset_role::Look].erase(preset_library[preset_role::Look].begin() +
		                                        static_cast<std::ptrdiff_t>(bad_index));
		preset_message.clear();
		RefreshPresetChoice();
		edit_contrast(20);
		if (!CurrentPreset(preset_role::Look).IsDirty() || presets::Read(look).Contrast != 10)
			return 11;
		switch_look(L"B.lutpreset");
		if (contrast_position != -10 || !CurrentPreset(preset_role::Look).loaded)
			return 12;
		switch_look(L"A.lutpreset");
		if (contrast_position != 20 || !CurrentPreset(preset_role::Look).IsDirty())
			return 13;
		command(ID_REVERT_PRESET);
		if (contrast_position != 10 || CurrentPreset(preset_role::Look).IsDirty())
			return 14;
		edit_contrast(23);
		SavePresetAsPath(preset_role::Look, PresetFolder(preset_role::Look) / L"Copy.lutpreset", false);
		if (CurrentPreset(preset_role::Look).IsDirty() ||
		    presets::Read(CurrentPreset(preset_role::Look).path).Contrast != 23)
			return 23;
		switch_look(L"A.lutpreset");
		if (contrast_position != 10 || CurrentPreset(preset_role::Look).IsDirty() || presets::Read(look).Contrast != 10)
			return 24;
		edit_contrast(20);
		command(ID_SAVE_PRESET);
		if (presets::Read(look).Contrast != 20 || CurrentPreset(preset_role::Look).IsDirty())
			return 15;
		SendMessageW(autosave_control, BM_SETCHECK, BST_CHECKED, 0);
		command(ID_AUTOSAVE);
		edit_contrast(15);
		SendMessageW(main_window, WM_TIMER, TIMER_PRESET, 0);
		if (presets::Read(look).Contrast != 15 || CurrentPreset(preset_role::Look).IsDirty())
			return 16;
		SendMessageW(correction_list, LB_SETCURSEL, EffectListRow(effect::HdrGamma), 0);
		command(ID_CORRECTION_LAYERS, LBN_SELCHANGE);
		command(ID_GAMMA);
		SendMessageW(main_window, WM_TIMER, TIMER_PRESET, 0);
		if (presets::Read(correction).Gamma != 22 || presets::Read(look).Contrast != 15)
			return 17;
		SendMessageW(autosave_control, BM_SETCHECK, BST_UNCHECKED, 0);
		command(ID_AUTOSAVE);
		ToggleEffect(effect::HdrGamma);
		if (!CurrentPreset(preset_role::Correction).IsDirty() || presets::Read(correction).HdrFix)
			return 18;
		bool rejected = false;
		try
		{
			ReadPreset(preset_role::Look, correction);
		}
		catch (...)
		{
			rejected = true;
		}
		if (!rejected || contrast_position != 15)
			return 19;
		// Simulate restoring settings in a new editor session using the saved configuration.
		preset_library = {};
		preset_index = {};
		InitializePresets();
		if (contrast_position != 15 || gamma_position != 0 || hdr_fix_enabled || autosave_enabled)
			return 20;
		const auto unicode_path = PresetFolder(preset_role::Look) / L"Farver-\u00e6\u00f8\u00e5.lutpreset";
		SavePresetAsPath(preset_role::Look, unicode_path, false);
		preferred_shortcut = MAKEWORD(VK_F10, HOTKEYF_CONTROL | HOTKEYF_SHIFT);
		if (!PersistPresetChoices())
			return 25;
		preset_library = {};
		preset_index = {};
		InitializePresets();
		if (CurrentPreset(preset_role::Look).path != unicode_path || contrast_position != 15 ||
		    GetPrivateProfileIntW(L"Hotkey", L"Bypass", 0, test_settings_path.c_str()) != preferred_shortcut)
			return 26;
		wchar_t stored[32768] {};
		GetPrivateProfileStringW(L"Presets", L"Look", L"", stored, static_cast<DWORD>(std::size(stored)),
		                         test_settings_path.c_str());
		if (std::filesystem::path(stored).is_absolute())
			return 27;
		// A fresh installation selects the bundled all-off presets. Saved
		// effect switches, including a mixture of on/off, win on later loads.
		for (int preset_role = preset_role::Look; preset_role < preset_role::Count; ++preset_role)
			presets::Write(PresetFolder(preset_role) / L"Off.lutpreset",
			               presets::InactivePreset(PresetKindForRole(preset_role)), false);
		WritePrivateProfileStringW(L"Presets", L"Look", nullptr, test_settings_path.c_str());
		WritePrivateProfileStringW(L"Presets", L"Correction", nullptr, test_settings_path.c_str());
		preset_library = {};
		preset_index = {};
		InitializePresets();
		for (int preset_role = preset_role::Look; preset_role < preset_role::Count; ++preset_role)
			if (CurrentPreset(preset_role).current != presets::InactivePreset(PresetKindForRole(preset_role)) ||
			    CurrentPreset(preset_role).path.filename() != L"Off.lutpreset")
				return 150;
		if (contrast_enabled || hdr_fix_enabled)
			return 151;
		auto& mixed = CurrentPreset(preset_role::Look).current;
		mixed.ContrastEnabled = true;
		mixed.Contrast = 12;
		mixed.Adjustments.BalanceEnabled = true;
		mixed.Adjustments.Balance[color::BALANCE_WARMTH] = 15;
		CurrentPreset(preset_role::Correction).current.HdrFix = true;
		const auto saved_mixed = mixed;
		if (!SaveExistingPreset(preset_role::Look) || !SaveExistingPreset(preset_role::Correction) ||
		    !PersistPresetChoices())
			return 152;
		preset_library = {};
		preset_index = {};
		InitializePresets();
		if (CurrentPreset(preset_role::Look).current != saved_mixed || !contrast_enabled || !hdr_fix_enabled)
			return 153;
		SendMessageW(hdr_fix_control, BM_SETCHECK, BST_CHECKED, 0);
		command(ID_HDR_FIX);
		if (!hdr_fix_enabled || !CurrentPreset(preset_role::Correction).current.ApplyInSdr ||
		    !CurrentSettings().ApplyInSdr || !CurrentPreset(preset_role::Correction).IsDirty())
			return 163;
		if (!SaveExistingPreset(preset_role::Correction))
			return 164;
		preset_library = {};
		preset_index = {};
		InitializePresets();
		if (!hdr_fix_enabled || !CurrentPreset(preset_role::Correction).current.ApplyInSdr ||
		    SendMessageW(hdr_fix_control, BM_GETCHECK, 0, 0) != BST_CHECKED)
			return 165;
		const auto before_folder_import = CurrentPreset(preset_role::Correction).current;
		presets::Write(PresetFolder(preset_role::Correction) / L"Added from folder.lutpreset",
		               presets::InactivePreset(presets::Kind::Correction), false);
		SendMessageW(main_window, WM_COMMAND, MAKEWPARAM(ID_PRESET_CHOICE, CBN_DROPDOWN), 0);
		if (FindPreset(preset_role::Correction,
		               PresetFolder(preset_role::Correction) / L"Added from folder.lutpreset") ==
		        preset_library[preset_role::Correction].size() ||
		    CurrentPreset(preset_role::Correction).current != before_folder_import)
			return 168;
		if (live_session.IsActive())
			return 21;
		return 0;
	}
	catch (...)
	{
		return 22;
	}
}

int RunProfileUiTest()
{
	using applications::Action;
	auto command = [&](int id, int event = BN_CLICKED)
	{
		SendMessageW(main_window, WM_COMMAND, MAKEWPARAM(id, event), 0);
	};
	auto choose_look = [&](const wchar_t* name)
	{
		SendMessageW(preset_choice, CB_SETCURSEL, FindPreset(preset_role::Look, PresetFolder(preset_role::Look) / name),
		             0);
		command(ID_PRESET_CHOICE, CBN_SELCHANGE);
	};
	auto indicator = [&]
	{
		wchar_t text[1024] {};
		GetWindowTextW(active_look_label, text, static_cast<int>(std::size(text)));
		return std::wstring(text);
	};
	auto monitor_indicator = [&]
	{
		wchar_t text[1024] {};
		GetWindowTextW(monitor_profile, text, static_cast<int>(std::size(text)));
		return std::wstring(text);
	};
	try
	{
		// The regular look may be an unsaved experiment; an app override must leave it intact.
		SendMessageW(contrast_slider, TBM_SETPOS, TRUE, 20);
		SendMessageW(main_window, WM_HSCROLL, TB_THUMBPOSITION, reinterpret_cast<LPARAM>(contrast_slider));
		presets::Data manual;
		manual.Contrast = 23;
		presets::Write(PresetFolder(preset_role::Look) / L"Copy.lutpreset", manual, false);
		ReadPreset(preset_role::Look, PresetFolder(preset_role::Look) / L"Copy.lutpreset");
		applications::Configuration config;
		config.Enabled = true;
		config.Rules = { { L"C:\\Tests\\Game.exe", PresetFolder(preset_role::Look) / L"B.lutpreset", Action::Look },
			             { L"C:\\Tests\\Movie.exe", {}, Action::Bypass },
			             { L"C:\\Tests\\HDR.exe", {}, Action::Disable },
			             { L"C:\\Tests\\Second.exe", PresetFolder(preset_role::Look) / L"A.lutpreset", Action::Look } };
		if (!ApplyProfileConfiguration(config))
			return 30;
		OnForegroundProgramChanged(L"C:\\Tests\\Game.exe");
		if (contrast_position != -10 || gamma_position != 1 || hdr_fix_enabled)
			return 31;
		if (indicator().find(L"Application profile: Game.exe") == std::wstring::npos ||
		    indicator().find(L" | B") == std::wstring::npos)
			return 48;
		if (monitor_indicator() != L"Profile: Game.exe | B")
			return 215;
		if (!PersistPresetChoices())
			return 49;
		wchar_t stored_look[32768] {};
		GetPrivateProfileStringW(L"Presets", L"Look", L"", stored_look, static_cast<DWORD>(std::size(stored_look)),
		                         SettingsFilePath().c_str());
		if (std::filesystem::path(stored_look).filename() != L"A.lutpreset")
			return 50;
		choose_look(L"Copy.lutpreset");
		OnForegroundProgramChanged(L"c:\\tests\\GAME.EXE");
		if (contrast_position != 23 || !foreground_program.IsManualOverride())
			return 32;
		if (monitor_indicator() != L"Desktop profile")
			return 216;
		if (indicator().find(L"Manual override: Game.exe") == std::wstring::npos ||
		    indicator().find(L"Copy") == std::wstring::npos)
			return 51;
		SendMessageW(correction_list, LB_SETCURSEL, EffectListRow(effect::HdrGamma), 0);
		command(ID_CORRECTION_LAYERS, LBN_SELCHANGE);
		if (indicator().find(L"Manual override: Game.exe") == std::wstring::npos ||
		    indicator().find(L"Copy") == std::wstring::npos)
			return 52;
		SendMessageW(effect_list, LB_SETCURSEL, 0, 0);
		command(ID_EFFECT_LIST, LBN_SELCHANGE);
		OnForegroundProgramChanged(L"C:\\Tests\\Other.exe");
		if (contrast_position != 20 || foreground_program.IsManualOverride() || application_context_active ||
		    !CurrentPreset(preset_role::Look).IsDirty())
			return 33;
		if (monitor_indicator() != L"Desktop profile")
			return 217;
		if (indicator().find(L"Manual look | A *") == std::wstring::npos ||
		    presets::Read(PresetFolder(preset_role::Look) / L"A.lutpreset").Contrast != 10)
			return 53;
		// Consecutive profiled programs must not replace the original return look.
		OnForegroundProgramChanged(L"C:\\Tests\\Game.exe");
		OnForegroundProgramChanged(L"C:\\Tests\\Second.exe");
		if (contrast_position != 20 || !application_context_active)
			return 54;
		OnForegroundProgramChanged(L"C:\\Tests\\Game.exe");
		OnForegroundProgramChanged(L"C:\\Tests\\Movie.exe");
		if (!CurrentSettings().Bypass || profile_action != Action::Bypass || hdr_fix_enabled || gamma_position != 1)
			return 34;
		OnForegroundProgramChanged(L"C:\\Tests\\Other.exe");
		if (contrast_position != 20 || CurrentSettings().Bypass || application_context_active)
			return 55;
		OnForegroundProgramChanged(L"C:\\Tests\\Movie.exe");
		choose_look(L"A.lutpreset");
		if (CurrentSettings().Bypass || !foreground_program.IsManualOverride())
			return 35;
		// Synthetic output only: render_only still forbids opening a LUT session.
		display_sources.clear();
		live::Output output;
		output.identity = L"test identity";
		output.name = L"test source";
		output.supported = true;
		display_sources.push_back(output);
		SendMessageW(source_choice, CB_RESETCONTENT, 0, 0);
		SendMessageW(source_choice, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Test display"));
		SendMessageW(source_choice, CB_SETCURSEL, 0, 0);
		display_sources[0].hdr = true;
		if (CurrentIconState() != app_icons::State::DisabledHdr)
			return 57;
		display_sources[0].hdr = false;
		if (CurrentIconState() != app_icons::State::Disabled)
			return 58;
		OnForegroundProgramChanged(L"C:\\Tests\\HDR.exe");
		ToggleCorrections();
		if (!corrections_requested || profile_action != Action::Disable || live_session.IsActive() ||
		    profile_paused_identity != output.identity)
			return 36;
		OnForegroundProgramChanged(L"C:\\Tests\\Game.exe");
		if (!corrections_requested || profile_action != Action::Look || !profile_paused_identity.empty())
			return 37;
		OnForegroundProgramChanged(L"C:\\Tests\\HDR.exe");
		display_sources[0].identity = L"changed mode";
		ReconcileLiveCorrections();
		if (!corrections_requested || live_session.IsActive())
			return 38;
		OnForegroundProgramChanged(L"C:\\Tests\\Game.exe");
		if (!corrections_requested)
			return 39; // Mode changes retain the user's master-power request.
		power_suspended = true;
		corrections_requested = true;
		profile_paused_identity = L"pre-suspend identity";
		ReconcileLiveCorrections();
		if (!corrections_requested || profile_paused_identity != L"pre-suspend identity")
			return 47;
		power_suspended = false;
		DisableLiveCorrections();
		const auto restored = applications::Read(SettingsFilePath());
		if (!restored.Enabled || restored.Rules.size() != 4)
			return 40;
		ShowProfiles(true);
		HWND editor = profile_editor::ProfileEditorHandle();
		if (!editor)
			return 41;
		SetTextIfChanged(GetDlgItem(editor, 204), L"C:\\Tests\\New.exe");
		SendMessageW(GetDlgItem(editor, 207), CB_SETCURSEL, 1, 0); // Bypass action.
		SendMessageW(editor, WM_COMMAND, MAKEWPARAM(209, BN_CLICKED), 0);
		if (SendMessageW(GetDlgItem(editor, 203), LB_GETCOUNT, 0, 0) != 5)
			return 42;
		SendMessageW(editor, WM_COMMAND, MAKEWPARAM(212, BN_CLICKED), 0);
		if (profile_configuration.Rules.size() != 5 || profile_configuration.Rules.back().Action != Action::Bypass)
			return 43;
		SendMessageW(editor, WM_COMMAND, MAKEWPARAM(210, BN_CLICKED), 0);
		SendMessageW(editor, WM_COMMAND, MAKEWPARAM(212, BN_CLICKED), 0);
		if (profile_configuration.Rules.size() != 4)
			return 44;
		SendMessageW(editor, WM_COMMAND, MAKEWPARAM(213, BN_CLICKED), 0);
		if (profile_editor::ProfileEditorHandle() || live_session.IsActive() || corrections_requested)
			return 45;
		// HDR-only pausing must leave the preset and creative look intact, including drafts.
		config.Rules.push_back({ L"C:\\Tests\\NativeHDR.exe", {}, Action::PauseHdr });
		if (!ApplyProfileConfiguration(config))
			return 59;
		ToggleEffect(effect::HdrGamma);
		const auto correction_before_pause = CurrentPreset(preset_role::Correction).current;
		const auto contrast_before_pause = contrast_position;
		OnForegroundProgramChanged(L"C:\\Tests\\NativeHDR.exe");
		if (!hdr_fix_enabled || CurrentSettings().HdrFix || CurrentSettings().Bypass ||
		    contrast_position != contrast_before_pause ||
		    CurrentPreset(preset_role::Correction).current != correction_before_pause ||
		    indicator().find(L"HDR gamma paused") == std::wstring::npos)
			return 60;
		ToggleBypass();
		if (!CurrentSettings().Bypass || CurrentSettings().HdrFix)
			return 61;
		ToggleBypass();
		if (CurrentSettings().Bypass || CurrentSettings().HdrFix)
			return 62;
		OnForegroundProgramChanged(L"C:\\Tests\\Other.exe");
		if (!CurrentSettings().HdrFix || contrast_position != 20 || application_context_active)
			return 63;
		OnForegroundProgramChanged(L"C:\\Tests\\NativeHDR.exe");
		choose_look(L"Copy.lutpreset");
		if (!CurrentSettings().HdrFix || !foreground_program.IsManualOverride() || contrast_position != 23)
			return 64;
		OnForegroundProgramChanged(L"C:\\Tests\\Other.exe");
		// Exercise the actual editor and persisted action, not just policy resolution.
		ShowProfiles(true);
		editor = profile_editor::ProfileEditorHandle();
		if (!editor)
			return 65;
		SetTextIfChanged(GetDlgItem(editor, 204), L"C:\\Tests\\EditorHDR.exe");
		SendMessageW(GetDlgItem(editor, 207), CB_SETCURSEL, 3, 0);
		SendMessageW(editor, WM_COMMAND, MAKEWPARAM(207, CBN_SELCHANGE), 0);
		if (IsWindowEnabled(GetDlgItem(editor, 208)))
			return 66;
		SendMessageW(editor, WM_COMMAND, MAKEWPARAM(209, BN_CLICKED), 0);
		SendMessageW(editor, WM_COMMAND, MAKEWPARAM(212, BN_CLICKED), 0);
		const auto hdr_rules = applications::Read(SettingsFilePath());
		if (hdr_rules.Rules.back().Action != Action::PauseHdr)
			return 67;
		SendMessageW(editor, WM_COMMAND, MAKEWPARAM(213, BN_CLICKED), 0);
		// Turning off auto-selection releases the temporary application look too.
		auto disabled = config;
		disabled.Enabled = false;
		if (!ApplyProfileConfiguration(disabled) || contrast_position != 20 || application_context_active ||
		    IsLookBypassed())
			return 56;
		return 0;
	}
	catch (...)
	{
		return 46;
	}
}

int RunEffectUiTest()
{
	const auto choose = [&](int effect)
	{
		auto list = EffectListForRole(effect);
		SendMessageW(list, LB_SETCURSEL, EffectListRow(effect), 0);
		SendMessageW(main_window, WM_COMMAND, MAKEWPARAM(GetDlgCtrlID(list), LBN_SELCHANGE), 0);
	};
	const auto click = [&](int effect)
	{
		auto list = EffectListForRole(effect);
		const int row = EffectListRow(effect);
		SendMessageW(list, LB_SETTOPINDEX, row, 0);
		RECT r {};
		SendMessageW(list, LB_GETITEMRECT, row, reinterpret_cast<LPARAM>(&r));
		RECT client {};
		GetClientRect(list, &client);
		SendMessageW(list, WM_LBUTTONDOWN, MK_LBUTTON,
		             MAKELPARAM(client.right - ScaleForDpi(18), (r.top + r.bottom) / 2));
		SendMessageW(list, WM_LBUTTONUP, 0, MAKELPARAM(client.right - ScaleForDpi(18), (r.top + r.bottom) / 2));
	};
	const auto edit = [&](size_t index, int value)
	{
		SendMessageW(adjustment_sliders[index], TBM_SETPOS, TRUE, value);
		SendMessageW(main_window, WM_HSCROLL, TB_THUMBPOSITION, reinterpret_cast<LPARAM>(adjustment_sliders[index]));
	};
	try
	{
		if (correction_first || !contrast_enabled || selected_effect != effect::ContrastCurves ||
		    effect_order.size() != 8 || effect_order.back() != 1)
			return 70;
		// Real rapid input is DOWN, UP, DBLCLK, UP, not two DOWN messages.
		// Verify every press on every switch, including off-screen rows after
		// scrolling, and that each toggle selects its effect's controls.
		for (int effect = effect::ContrastCurves; effect < effect::FirstWindows; ++effect)
		{
			const auto enabled = [&]()
			{
				return effect == effect::ContrastCurves ? contrast_enabled
				       : effect == effect::HdrGamma     ? hdr_fix_enabled
				                                        : AdjustmentEnabled(effect);
			};
			const bool initial = enabled();
			auto list = EffectListForRole(effect);
			const int row = EffectListRow(effect);
			SendMessageW(list, LB_SETTOPINDEX, row, 0);
			RECT bounds {}, client {};
			SendMessageW(list, LB_GETITEMRECT, row, reinterpret_cast<LPARAM>(&bounds));
			GetClientRect(list, &client);
			const LPARAM at = MAKELPARAM(client.right - ScaleForDpi(18), (bounds.top + bounds.bottom) / 2);
			for (int press = 0; press < 12; ++press)
			{
				SendMessageW(list, press % 2 ? WM_LBUTTONDBLCLK : WM_LBUTTONDOWN, MK_LBUTTON, at);
				SendMessageW(list, WM_LBUTTONUP, 0, at);
				if (enabled() != (press % 2 == 0 ? !initial : initial) || selected_effect != effect)
					return 161;
			}
			if ((SendMessageW(selected_effect_toggle, BM_GETCHECK, 0, 0) == BST_CHECKED) != initial)
				return 201;
			// The heading switch uses the same stage state as the sidebar,
			// retaining every parameter and leaving the other preset alone.
			const auto look = CurrentPreset(preset_role::Look).current,
					   correction = CurrentPreset(preset_role::Correction).current;
			SendMessageW(selected_effect_toggle, BM_CLICK, 0, 0);
			if (enabled() == initial || selected_effect != effect ||
			    (SendMessageW(selected_effect_toggle, BM_GETCHECK, 0, 0) == BST_CHECKED) != enabled())
				return 202;
			if (effect == effect::HdrGamma ? CurrentPreset(preset_role::Look).current != look
			                               : CurrentPreset(preset_role::Correction).current != correction)
				return 203;
			SendMessageW(selected_effect_toggle, BM_CLICK, 0, 0);
			if (enabled() != initial || CurrentPreset(preset_role::Look).current != look ||
			    CurrentPreset(preset_role::Correction).current != correction)
				return 204;
		}
		const auto before = CurrentPreset(preset_role::Look).current;
		choose(effect::HdrGamma);
		click(effect::ContrastCurves);
		if (selected_effect != effect::ContrastCurves || contrast_enabled || CurrentSettings().Contrast != 0 ||
		    !CurrentPreset(preset_role::Look).IsDirty() ||
		    !(GetWindowLongPtrW(contrast_slider, GWL_STYLE) & WS_VISIBLE))
			return 71;
		click(effect::ContrastCurves);
		if (!contrast_enabled || selected_effect != effect::ContrastCurves ||
		    CurrentPreset(preset_role::Look).IsDirty())
			return 73;
		choose(effect::ContrastCurves);
		SendMessageW(effect_list, WM_KEYDOWN, VK_SPACE, 0);
		if (contrast_enabled || !SaveExistingPreset(preset_role::Look) ||
		    presets::Read(CurrentPreset(preset_role::Look).path).ContrastEnabled)
			return 74;
		SendMessageW(contrast_slider, TBM_SETPOS, TRUE, 17);
		SendMessageW(main_window, WM_HSCROLL, TB_THUMBPOSITION, reinterpret_cast<LPARAM>(contrast_slider));
		if (contrast_enabled || CurrentPreset(preset_role::Look).current.Contrast != 17 ||
		    CurrentSettings().Contrast != 0)
			return 75;
		SendMessageW(effect_list, LB_SETTOPINDEX, 0, 0);
		SendMessageW(effect_list, WM_LBUTTONDOWN, MK_LBUTTON,
		             MAKELPARAM(ScaleForDpi(12), ScaleForDpi(effects::ROW_HEIGHT / 2)));
		SendMessageW(effect_list, WM_MOUSEMOVE, MK_LBUTTON,
		             MAKELPARAM(ScaleForDpi(12), ScaleForDpi(effects::ROW_HEIGHT * 3 / 2)));
		SendMessageW(effect_list, WM_LBUTTONUP, 0,
		             MAKELPARAM(ScaleForDpi(12), ScaleForDpi(effects::ROW_HEIGHT * 3 / 2)));
		if (EffectListRow(effect::ContrastCurves) != 1 || effect_order.front() != 2 ||
		    selected_effect != effect::ContrastCurves)
			return 76;
		MoveEffect(effect::HdrGamma, 0);
		if (!correction_first || effect_order.front() != 1 || selected_effect != effect::ContrastCurves)
			return 77;
		choose(effect::HdrGamma);
		const bool hdr = hdr_fix_enabled;
		click(effect::HdrGamma);
		if (selected_effect != effect::HdrGamma || hdr_fix_enabled == hdr ||
		    CurrentPreset(preset_role::Correction).current.HdrFix != hdr_fix_enabled)
			return 78;
		for (int i = 0; i < 7; ++i)
			SendMessageW(correction_list, WM_SYSKEYDOWN, VK_DOWN, 0);
		if (!correction_first || effect_order.front() != 1 || selected_effect != effect::HdrGamma)
			return 79;
		// New effects retain disabled parameters, own their role, and survive save/load.
		choose(effect::ColorBalance);
		click(effect::ColorBalance);
		edit(0, 85);
		if (selected_effect != effect::ColorBalance || SelectedPresetRole() != 0 ||
		    CurrentPreset(preset_role::Look).current.Adjustments.BalanceEnabled ||
		    CurrentPreset(preset_role::Look).current.Adjustments.Balance[color::BALANCE_RED] != 85)
			return 84;
		choose(effect::Levels);
		edit(0, 95);
		edit(1, 5);
		if (CurrentPreset(preset_role::Look).current.Adjustments.Levels[color::INPUT_WHITE] -
		        CurrentPreset(preset_role::Look).current.Adjustments.Levels[color::INPUT_BLACK] <
		    5)
			return 85;
		SendMessageW(main_window, WM_COMMAND, ID_RESET, 0);
		choose(effect::CustomCurves);
		editing_channel = 1;
		const auto plot = CurvePlotRect(curve_canvas);
		const LPARAM at =
			MAKELPARAM((plot.left + plot.right) / 2, plot.bottom - static_cast<int>((plot.bottom - plot.top) * .57));
		SendMessageW(curve_canvas, WM_LBUTTONDOWN, MK_LBUTTON, at);
		SendMessageW(curve_canvas, WM_LBUTTONUP, 0, at);
		const auto& points = CurrentPreset(preset_role::Look).current.Adjustments.Curves[1];
		if (points.size() != 9 || points[4] <= .5 || slider_dragging || dragged_point != -1)
			return 86;
		const auto other_points = points;
		SendMessageW(curve_canvas, WM_LBUTTONDBLCLK, MK_LBUTTON, at);
		SendMessageW(curve_canvas, WM_LBUTTONUP, 0, at);
		const auto& reset_points = CurrentPreset(preset_role::Look).current.Adjustments.Curves[1];
		if (reset_points[4] != .5 || slider_dragging || dragged_point != -1)
			return 275;
		for (size_t i = 0; i < reset_points.size(); ++i)
			if (i != 4 && reset_points[i] != other_points[i])
				return 276;
		CurrentPreset(preset_role::Look).current.Adjustments.Imported =
			lut1d::DecodeCube("LUT_1D_SIZE 2\n0 0 0\n1 .9 .8\n");
		if (!SaveExistingPreset(preset_role::Look))
			return 87;
		const auto saved = CurrentPreset(preset_role::Look).current;
		const auto saved_order = effect_order;
		if (presets::Read(CurrentPreset(preset_role::Look).path) != saved || !PersistPresetChoices())
			return 88;
		preset_library = {};
		preset_index = {};
		effect_order = { effect::ContrastCurves,    effect::ColorBalance, effect::MidtoneGamma,  effect::Levels,
			             effect::ShadowsHighlights, effect::CustomCurves, effect::Imported1dLut, effect::HdrGamma };
		InitializePresets();
		RefreshInterface();
		if (CurrentPreset(preset_role::Look).current != saved || effect_order != saved_order || contrast_enabled)
			return 81;
		SendMessageW(main_window, WM_COMMAND, ID_SETTINGS_PAGE, 0);
		if (!settings_page || !(GetWindowLongPtrW(effect_list, GWL_STYLE) & WS_VISIBLE) ||
		    (GetWindowLongPtrW(effect_title, GWL_STYLE) & WS_VISIBLE))
			return 89;
		SendMessageW(font_choice, CB_SETCURSEL, 1, 0);
		SendMessageW(main_window, WM_COMMAND, MAKEWPARAM(ID_FONT_CHOICE, CBN_SELCHANGE), 0);
		if (wcscmp(ui::FontFamily(), L"Segoe UI"))
			return 146;
		SendMessageW(main_window, WM_COMMAND, ID_SETTINGS_PAGE, 0);
		if (settings_page)
			return 147;
		for (int effect :
		     { effect::ContrastCurves, effect::HdrGamma, effect::ColorBalance, effect::MidtoneGamma, effect::Levels,
		       effect::ShadowsHighlights, effect::NightLight, effect::Saturation, effect::ChannelMixer })
		{
			selected_effect = effect;
			RefreshInterface();
			const auto fields = SliderParameters();
			std::vector<HWND> sliders;
			if (effect == effect::ContrastCurves)
				sliders = { contrast_slider, pivot_slider };
			else if (effect == effect::HdrGamma)
				sliders = { white_slider };
			else
				for (size_t i = 0; i < fields.size(); ++i)
					sliders.push_back(adjustment_sliders[i]);
			for (HWND slider : sliders)
			{
				std::vector<int> previous_values;
				for (const auto& field : fields)
					previous_values.push_back(*field.value);
				const int baseline =
					static_cast<int>(reinterpret_cast<INT_PTR>(GetPropW(slider, L"LutSliderDefault")) - 32769);
				const int maximum = static_cast<int>(SendMessageW(slider, TBM_GETRANGEMAX, 0, 0));
				SendMessageW(slider, TBM_SETPOS, TRUE, baseline == maximum ? baseline - 1 : baseline + 1);
				SendMessageW(slider, WM_RBUTTONDOWN, MK_RBUTTON, 0);
				SendMessageW(slider, WM_RBUTTONUP, 0, 0);
				if (SendMessageW(slider, TBM_GETPOS, 0, 0) != baseline)
					return 265;
				for (size_t i = 0; i < fields.size(); ++i)
					if (*fields[i].value != (slider == adjustment_sliders[i] ? baseline : previous_values[i]))
						return 267;
				if (effect == effect::ContrastCurves &&
				    (slider == contrast_slider ? contrast_position : pivot_position) != baseline)
					return 268;
				if (effect == effect::HdrGamma && white_position != baseline)
					return 269;
				SendMessageW(slider, TBM_SETPOS, TRUE, baseline == maximum ? baseline - 1 : baseline + 1);
				SendMessageW(slider, WM_LBUTTONDBLCLK, MK_LBUTTON, 0);
				SendMessageW(slider, WM_LBUTTONUP, 0, 0);
				if (SendMessageW(slider, TBM_GETPOS, 0, 0) != baseline)
					return 274;
				RECT thumb {};
				SendMessageW(slider, TBM_GETTHUMBRECT, 0, reinterpret_cast<LPARAM>(&thumb));
				const LPARAM slider_point = MAKELPARAM((thumb.left + thumb.right) / 2, (thumb.top + thumb.bottom) / 2);
				SendMessageW(slider, WM_LBUTTONDOWN, MK_LBUTTON, slider_point);
				SendMessageW(slider, WM_LBUTTONUP, 0, slider_point);
				SendMessageW(slider, TBM_SETPOS, TRUE, baseline == maximum ? baseline - 1 : baseline + 1);
				SendMessageW(slider, WM_LBUTTONDOWN, MK_LBUTTON, slider_point);
				SendMessageW(slider, WM_LBUTTONUP, 0, slider_point);
				if (SendMessageW(slider, TBM_GETPOS, 0, 0) != baseline)
					return 277;
			}
		}
		return live_session.IsActive() || corrections_requested ? 82 : 0;
	}
	catch (...)
	{
		return 83;
	}
}

LRESULT CALLBACK CountSurfaceChanges(HWND hwnd, UINT message, WPARAM w_param, LPARAM l_param, UINT_PTR, DWORD_PTR data)
{
	if (message == WM_SETTEXT || message == WM_ENABLE || message == BM_SETCHECK || message == CB_RESETCONTENT ||
	    message == LB_SETCURSEL || message == LB_SETITEMHEIGHT)
		++*reinterpret_cast<int*>(data);
	return DefSubclassProc(hwnd, message, w_param, l_param);
}

int RunTabUiTest()
{
	const auto saved_selected = selected_effect;
	const bool saved_page = settings_page;
	const auto saved_focus = GetFocus();
	BYTE saved_keys[256] {}, keys[256] {};
	GetKeyboardState(saved_keys);
	SetKeyboardState(keys);
	int result = 0;
	for (int page = 0; page < 11 && !result; ++page)
	{
		settings_page = page == 8;
		selected_effect = settings_page ? 0 : page >= 9 ? page + 1 : page;
		RefreshInterface();
		const auto original_look = CurrentPreset(preset_role::Look).current,
				   original_correction = CurrentPreset(preset_role::Correction).current;
		if (((GetWindowLongPtrW(reset_button, GWL_STYLE) & WS_VISIBLE) != 0) == settings_page ||
		    ((GetWindowLongPtrW(selected_effect_toggle, GWL_STYLE) & WS_VISIBLE) != 0) == settings_page)
		{
			result = 205;
			break;
		}
		if (!settings_page)
		{
			RECT heading {}, reset {}, toggle {};
			GetWindowRect(effect_title, &heading);
			GetWindowRect(reset_button, &reset);
			GetWindowRect(selected_effect_toggle, &toggle);
			if ((heading.top + heading.bottom) / 2 != reset.top + ScaleForDpi(14) ||
			    (toggle.top + toggle.bottom) / 2 != reset.top + ScaleForDpi(14))
			{
				result = 206;
				break;
			}
		}
		const auto order = TabStopControls();
		if (order.empty() || order.front() != correction_list || order.back() != monitor_profile ||
		    NextTabStop(effect_list, false) != preset_choice || NextTabStop(preset_choice, false) != autosave_control ||
		    NextTabStop(autosave_control, false) != save_preset_button ||
		    NextTabStop(save_preset_button, false) != save_as_button)
		{
			result = 180;
			break;
		}
		const auto first_input = settings_page                               ? font_choice
		                         : selected_effect == effect::ContrastCurves ? contrast_slider
		                         : selected_effect == effect::HdrGamma ? (IsWindowEnabled(white_slider) ? white_slider
		                                                                  : gamma_position == 0         ? gamma_choice
		                                                                                                : gamma_other)
		                         : (selected_effect < effect::CustomCurves || selected_effect >= effect::Saturation)
		                             ? adjustment_sliders[0]
		                         : selected_effect == effect::CustomCurves ? curve_channel
		                                                                   : import_button;
		const auto last_toolbar = IsWindowEnabled(revert_preset_button) ? revert_preset_button : export_button;
		if (settings_page ? NextTabStop(last_toolbar, false) != first_input
		                  : (NextTabStop(last_toolbar, false) != selected_effect_toggle ||
		                     NextTabStop(selected_effect_toggle, false) != reset_button ||
		                     NextTabStop(reset_button, false) != first_input))
		{
			result = 181;
			break;
		}
		for (size_t i = 0; i < order.size(); ++i)
		{
			if (NextTabStop(order[i], false) != order[(i + 1) % order.size()] ||
			    NextTabStop(order[i], true) != order[(i + order.size() - 1) % order.size()])
			{
				result = 182;
				break;
			}
			if (!settings_page &&
			    (order[i] == font_choice || order[i] == shortcut_control || order[i] == startup_control))
			{
				result = 183;
				break;
			}
			if (order[i] == curve_canvas && (settings_page || selected_effect != effect::CustomCurves))
			{
				result = 184;
				break;
			}
		}
		// Exercise the actual key route on hidden native controls. No global
		// keyboard input or display changes are sent by this test.
		if (!result)
		{
			SetFocus(order.front());
			MSG message {};
			message.message = WM_KEYDOWN;
			message.wParam = VK_TAB;
			for (size_t step = 1; step <= order.size(); ++step)
			{
				message.hwnd = GetFocus();
				if (!NavigateTab(message) || GetFocus() != order[step % order.size()])
				{
					result = 185;
					break;
				}
			}
			keys[VK_SHIFT] = keys[VK_LSHIFT] = 0x80;
			SetKeyboardState(keys);
			for (size_t step = 1; step <= order.size() && !result; ++step)
			{
				message.hwnd = GetFocus();
				if (!NavigateTab(message) || GetFocus() != order[(order.size() - step) % order.size()])
				{
					result = 186;
					break;
				}
			}
			keys[VK_SHIFT] = keys[VK_LSHIFT] = 0;
			SetKeyboardState(keys);
		}
		if (!result && (CurrentPreset(preset_role::Look).current != original_look ||
		                CurrentPreset(preset_role::Correction).current != original_correction))
			result = 187;
	}
	settings_page = saved_page;
	selected_effect = saved_selected;
	RefreshInterface();
	SetKeyboardState(saved_keys);
	if (saved_focus)
		SetFocus(saved_focus);
	return result;
}

bool PartialGraphMatchesFull()
{
	RECT bounds {};
	GetClientRect(curve_canvas, &bounds);
	BITMAPINFO info {};
	info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
	info.bmiHeader.biWidth = bounds.right;
	info.bmiHeader.biHeight = -bounds.bottom;
	info.bmiHeader.biPlanes = 1;
	info.bmiHeader.biBitCount = 32;
	info.bmiHeader.biCompression = BI_RGB;
	HDC dc = CreateCompatibleDC(nullptr);
	void* raw = nullptr;
	HBITMAP bitmap = CreateDIBSection(dc, &info, DIB_RGB_COLORS, &raw, nullptr, 0);
	if (!dc || !bitmap)
	{
		if (bitmap)
			DeleteObject(bitmap);
		if (dc)
			DeleteDC(dc);
		return false;
	}
	const auto old = SelectObject(dc, bitmap);
	auto pixels = static_cast<uint32_t*>(raw);
	const size_t count = static_cast<size_t>(bounds.right) * bounds.bottom;
	const auto old_look = CurrentPreset(preset_role::Look).current;
	const int old_position = contrast_position;
	const bool old_enabled = contrast_enabled, old_error = live_error;
	const auto old_message = live_message;
	SendMessageW(curve_canvas, WM_PRINTCLIENT, reinterpret_cast<WPARAM>(dc), PRF_CLIENT);
	GdiFlush();
	const auto full_bytes = last_graph_paint_bytes;
	bool okay = true;
	for (int stage = 0; stage < 3; ++stage)
	{
		ValidateRect(curve_canvas, nullptr);
		if (stage == 0)
		{
			contrast_position = 15;
			contrast_enabled = true;
			CurrentPreset(preset_role::Look).current.Contrast = 15;
			CurrentPreset(preset_role::Look).current.ContrastEnabled = true;
		}
		else
		{
			live_error = stage == 1;
			live_message =
				stage == 1
					? L"This is a deliberately long warning used to verify that wrapped graph messages are fully erased when the warning is cleared."
					: old_message;
		}
		RefreshInterface();
		HRGN dirty = CreateRectRgn(0, 0, 0, 0);
		// Hidden native windows may discard their update region. In that
		// fixture case, replay the exact rectangles requested by the painter.
		if (GetUpdateRgn(curve_canvas, dirty, FALSE) <= NULLREGION)
			for (unsigned i = 0; i < graph_audit_count; ++i)
			{
				HRGN part = CreateRectRgnIndirect(&graph_audit_damage[i]);
				CombineRgn(dirty, dirty, part, RGN_OR);
				DeleteObject(part);
			}
		const int saved = SaveDC(dc);
		SelectClipRgn(dc, dirty);
		SendMessageW(curve_canvas, WM_PRINTCLIENT, reinterpret_cast<WPARAM>(dc), PRF_CLIENT);
		GdiFlush();
		if (stage == 0 && last_graph_paint_bytes >= full_bytes)
			okay = false;
		RestoreDC(dc, saved);
		DeleteObject(dirty);
		std::vector<uint32_t> partial(pixels, pixels + count);
		SendMessageW(curve_canvas, WM_PRINTCLIENT, reinterpret_cast<WPARAM>(dc), PRF_CLIENT);
		GdiFlush();
		for (size_t i = 0; i < count; ++i)
			if ((partial[i] & 0xffffff) != (pixels[i] & 0xffffff))
				okay = false;
	}
	CurrentPreset(preset_role::Look).current = old_look;
	contrast_position = old_position;
	contrast_enabled = old_enabled;
	live_error = old_error;
	live_message = old_message;
	RefreshInterface();
	SelectObject(dc, old);
	DeleteObject(bitmap);
	DeleteDC(dc);
	return okay;
}

bool ClippedListMatchesFull()
{
	RECT bounds {}, dirty {};
	GetClientRect(effect_list, &bounds);
	if (SendMessageW(effect_list, LB_GETITEMRECT, 1, reinterpret_cast<LPARAM>(&dirty)) == LB_ERR)
		return false;
	InflateRect(&dirty, -ScaleForDpi(4), -ScaleForDpi(3));
	BITMAPINFO info {};
	info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
	info.bmiHeader.biWidth = bounds.right;
	info.bmiHeader.biHeight = -bounds.bottom;
	info.bmiHeader.biPlanes = 1;
	info.bmiHeader.biBitCount = 32;
	info.bmiHeader.biCompression = BI_RGB;
	HDC dc = CreateCompatibleDC(nullptr);
	void* pixels = nullptr;
	HBITMAP bitmap = CreateDIBSection(dc, &info, DIB_RGB_COLORS, &pixels, nullptr, 0);
	if (!dc || !bitmap)
	{
		if (bitmap)
			DeleteObject(bitmap);
		if (dc)
			DeleteDC(dc);
		return false;
	}
	const auto old = SelectObject(dc, bitmap);
	SendMessageW(effect_list, WM_PRINTCLIENT, reinterpret_cast<WPARAM>(dc), PRF_CLIENT);
	GdiFlush();
	const size_t count = static_cast<size_t>(bounds.right) * bounds.bottom;
	auto values = static_cast<uint32_t*>(pixels);
	std::vector<uint32_t> reference(values, values + count);
	std::fill(values, values + count, 0);
	IntersectClipRect(dc, dirty.left, dirty.top, dirty.right, dirty.bottom);
	SendMessageW(effect_list, WM_PRINTCLIENT, reinterpret_cast<WPARAM>(dc), PRF_CLIENT);
	GdiFlush();
	bool same = true;
	for (int y = 0; y < bounds.bottom; ++y)
		for (int x = 0; x < bounds.right; ++x)
		{
			const size_t at = static_cast<size_t>(y) * bounds.right + x;
			const bool inside = x >= dirty.left && x < dirty.right && y >= dirty.top && y < dirty.bottom;
			if ((values[at] & 0xffffff) != (inside ? reference[at] & 0xffffff : 0))
				same = false;
		}
	SelectObject(dc, old);
	DeleteObject(bitmap);
	DeleteDC(dc);
	return same;
}

int RunSurfaceUiTest()
{
	if (!ClippedListMatchesFull())
		return 255;
	if (!PartialGraphMatchesFull())
		return 260;
	for (HWND control : { preset_choice,    autosave_control, source_choice,          font_choice,
	                      shortcut_control, shortcut_button,  startup_control,        open_preset_button,
	                      bypass_button,    filter_choice,    filter_settings_button, saturation_report_button,
	                      contrast_slider,  pivot_slider,     white_slider,           gamma_choice,
	                      gamma_other,      hdr_fix_control,  curve_channel,          import_button,
	                      export_1d_button })
		if (!*ui::HelpTextForRegion(control) || wcslen(ui::HelpTextForRegion(control)) > 48)
			return 233;
	for (int effect = effect::ContrastCurves; effect < effect::Saturation; ++effect)
		if (!*ui::HelpTextForRegion(EffectListForRole(effect), effect + 1) ||
		    wcslen(ui::HelpTextForRegion(EffectListForRole(effect), effect + 1)) > 48 ||
		    !*ui::HelpTextForRegion(EffectListForRole(effect), 100 + effect))
			return 234;
	const int saved_selected = selected_effect;
	const auto saved_look = CurrentPreset(preset_role::Look).current,
			   saved_correction = CurrentPreset(preset_role::Correction).current;
	const auto saved_filter = filter_state;
	for (int effect : { effect::ColorFilters, effect::NightLight })
	{
		SendMessageW(windows_effect_list, LB_SETCURSEL, effect - effect::FirstWindows, 0);
		SendMessageW(main_window, WM_COMMAND, MAKEWPARAM(ID_WINDOWS_CORRECTION_LAYERS, LBN_SELCHANGE), 0);
		if (selected_effect != effect || settings_page || (GetWindowLongPtrW(preset_choice, GWL_STYLE) & WS_VISIBLE) ||
		    (effect == effect::ColorFilters && (GetWindowLongPtrW(reset_button, GWL_STYLE) & WS_VISIBLE)))
			return 235;
		const auto order = TabStopControls();
		if (std::find(order.begin(), order.end(), windows_effect_list) == order.end() ||
		    std::find(order.begin(), order.end(), preset_choice) != order.end())
			return 236;
		if (effect == effect::NightLight && !(GetWindowLongPtrW(selected_effect_toggle, GWL_STYLE) & WS_VISIBLE))
			return 237;
		if (effect == effect::ColorFilters && !(GetWindowLongPtrW(filter_choice, GWL_STYLE) & WS_VISIBLE))
			return 238;
	}
	if (CurrentPreset(preset_role::Look).current != saved_look ||
	    CurrentPreset(preset_role::Correction).current != saved_correction || filter_state.on != saved_filter.on ||
	    filter_state.filter != saved_filter.filter || filter_controller.IsBusy())
		return 239;
	const auto saved_night = night_config;
	const bool saved_bypass = bypassed;
	selected_effect = effect::NightLight;
	night_config.Enabled = false;
	night_config.Scheduled = false;
	RefreshInterface();
	for (const auto& pair :
	     { std::pair<HWND, HWND> { night_start, night_start_label }, { night_end, night_end_label } })
	{
		RECT outer {}, client {}, label {};
		GetWindowRect(pair.first, &outer);
		GetClientRect(pair.first, &client);
		GetWindowRect(pair.second, &label);
		POINT origin {};
		ClientToScreen(pair.first, &origin);
		const int top = origin.y - outer.top, bottom = outer.bottom - origin.y - client.bottom;
		if (top <= 0 || std::abs(top - bottom) > 1 ||
		    std::abs((outer.top + outer.bottom) - (label.top + label.bottom)) > 2)
			return 279;
		wchar_t original[16] {}, edited[16] {};
		GetWindowTextW(pair.first, original, 16);
		SetWindowTextW(pair.first, L"21:00");
		SendMessageW(pair.first, EM_SETSEL, 0, 2);
		SendMessageW(pair.first, EM_REPLACESEL, FALSE, reinterpret_cast<LPARAM>(L"22"));
		GetWindowTextW(pair.first, edited, 16);
		SetWindowTextW(pair.first, original);
		if (wcscmp(edited, L"22:00"))
			return 280;
	}
	SendMessageW(main_window, WM_COMMAND, ID_SELECTED_EFFECT_TOGGLE, 0);
	SendMessageW(adjustment_sliders[0], TBM_SETPOS, TRUE, 70);
	SendMessageW(main_window, WM_HSCROLL, TB_ENDTRACK, reinterpret_cast<LPARAM>(adjustment_sliders[0]));
	if (!night_config.Enabled || night_config.Strength != 70 ||
	    CurrentPreset(preset_role::Look).current != saved_look || CurrentSettings().NightWarmth != .7)
		return 270;
	bypassed = true;
	if (CurrentSettings().NightWarmth != .7)
		return 271;
	SendMessageW(adjustment_sliders[0], WM_RBUTTONUP, 0, 0);
	if (night_config.Strength != 35)
		return 272;
	SetWindowTextW(night_start, L"24:99");
	SendMessageW(main_window, WM_COMMAND, MAKEWPARAM(ID_NIGHT_START, EN_KILLFOCUS),
	             reinterpret_cast<LPARAM>(night_start));
	if (night_config.Start != saved_night.Start || night_message.empty())
		return 273;
	night_config = saved_night;
	bypassed = saved_bypass;
	night_message.clear();
	UpdateNightLightSchedule();
	RefreshInterface();
	selected_effect = saved_selected;
	RefreshInterface();
	// New global effect uses the same preset and keyboard/UI contracts, but
	// hidden UI tests must never start a hardware worker.
	selected_effect = effect::Saturation;
	settings_page = false;
	RefreshInterface();
	if (EffectListForRole(effect::Saturation) != effect_list || EffectListRow(effect::Saturation) != 7 ||
	    !*ui::HelpTextForRegion(effect_list, 11) || SliderParameters().size() != 1)
		return 247;
	if (!(GetWindowLongPtrW(preset_choice, GWL_STYLE) & WS_VISIBLE) ||
	    !(GetWindowLongPtrW(reset_button, GWL_STYLE) & WS_VISIBLE))
		return 248;
	const auto before_matrix = CurrentPreset(preset_role::Look).current;
	SendMessageW(main_window, WM_COMMAND, ID_SELECTED_EFFECT_TOGGLE, 0);
	if (CurrentPreset(preset_role::Look).current.SaturationEnabled == before_matrix.SaturationEnabled ||
	    matrix_session.CurrentState() != matrix_effect::State::Off)
		return 249;
	SendMessageW(adjustment_sliders[0], TBM_SETPOS, FALSE, 50);
	SendMessageW(main_window, WM_HSCROLL, TB_ENDTRACK, reinterpret_cast<LPARAM>(adjustment_sliders[0]));
	if (CurrentPreset(preset_role::Look).current.Saturation != 50 ||
	    matrix_session.CurrentState() != matrix_effect::State::Off)
		return 250;
	SendMessageW(adjustment_sliders[0], TBM_SETPOS, FALSE, color::SATURATION_MAXIMUM);
	SendMessageW(main_window, WM_HSCROLL, TB_ENDTRACK, reinterpret_cast<LPARAM>(adjustment_sliders[0]));
	if (CurrentPreset(preset_role::Look).current.Saturation != color::SATURATION_MAXIMUM)
		return 252;
	SendMessageW(main_window, WM_COMMAND, ID_RESET, 0);
	if (CurrentPreset(preset_role::Look).current.Saturation != 100)
		return 251;
	selected_effect = effect::ChannelMixer;
	RefreshInterface();
	if (SliderParameters().size() != 3 || EffectListRow(effect::ChannelMixer) != 8)
		return 288;
	SendMessageW(main_window, WM_COMMAND, ID_SELECTED_EFFECT_TOGGLE, 0);
	if (CurrentPreset(preset_role::Look).current.MonochromeEnabled == before_matrix.MonochromeEnabled ||
	    matrix_session.CurrentState() != matrix_effect::State::Off)
		return 289;
	SendMessageW(adjustment_sliders[0], TBM_SETPOS, FALSE, 90);
	SendMessageW(main_window, WM_HSCROLL, TB_ENDTRACK, reinterpret_cast<LPARAM>(adjustment_sliders[0]));
	if (CurrentPreset(preset_role::Look).current.Monochrome[0] != 90)
		return 290;
	SendMessageW(main_window, WM_COMMAND, ID_RESET, 0);
	if (CurrentPreset(preset_role::Look).current.Monochrome != std::array<int, 3> { 21, 72, 7 })
		return 291;
	CurrentPreset(preset_role::Look).current = before_matrix;
	selected_effect = saved_selected;
	RefreshInterface();
	ShowProfiles(true);
	const auto help_editor = profile_editor::ProfileEditorHandle();
	if (!help_editor)
		return 240;
	for (int id : { 201, 203, 204, 205, 206, 207, 208, 209, 210, 211, 212, 213, 214, 215, 216, 217, 218, 219, 220 })
		if (!*ui::HelpTextForRegion(GetDlgItem(help_editor, id)) ||
		    wcslen(ui::HelpTextForRegion(GetDlgItem(help_editor, id))) > 48)
			return 241;
	DestroyWindow(help_editor);
	// Simulate Windows shrinking our fixed layout without changing the real
	// display mode. A mode-change notification must restore the intended size.
	RECT before {};
	GetWindowRect(main_window, &before);
	SetWindowPos(main_window, nullptr, 0, 0, ScaleForDpi(400), ScaleForDpi(260),
	             SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
	if (!ui::WindowNeedsSizeRepair(main_window))
		return 242;
	ui::RevealControl(main_window, source_choice);
	const auto scroll = ui::ViewportOffset(main_window);
	RECT visible_control {}, viewport {};
	GetWindowRect(source_choice, &visible_control);
	MapWindowPoints(nullptr, main_window, reinterpret_cast<POINT*>(&visible_control), 2);
	GetClientRect(main_window, &viewport);
	if (scroll.y <= 0 || visible_control.top < 0 || visible_control.bottom > viewport.bottom ||
	    visible_control.left < 0 || visible_control.right > viewport.right)
		return 284;
	SendMessageW(main_window, WM_DISPLAYCHANGE, 32, MAKELPARAM(640, 480));
	const auto deadline = GetTickCount64() + 1500;
	while (ui::WindowNeedsSizeRepair(main_window) && GetTickCount64() < deadline)
	{
		MSG event {};
		while (PeekMessageW(&event, nullptr, 0, 0, PM_REMOVE))
		{
			TranslateMessage(&event);
			DispatchMessageW(&event);
		}
		Sleep(5);
	}
	RECT after {};
	GetWindowRect(main_window, &after);
	if (after.right - after.left != before.right - before.left ||
	    after.bottom - after.top != before.bottom - before.top)
		return 243;
	if (ui::ViewportOffset(main_window).x || ui::ViewportOffset(main_window).y)
		return 285;
	SetWindowPos(main_window, nullptr, 0, 0, ScaleForDpi(400), ScaleForDpi(260),
	             SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
	SendMessageW(main_window, WM_SYSCOMMAND, SC_RESTORE, 0);
	GetWindowRect(main_window, &after);
	if (after.right - after.left != before.right - before.left ||
	    after.bottom - after.top != before.bottom - before.top)
		return 244;
	const SIZE preferred { 1200, 900 };
	const RECT low { 0, 0, 640, 480 }, high { -1920, -100, 1920, 2060 };
	const auto small = ui::FitWindowRect({ 300, 200, 1500, 1100 }, preferred, low);
	if (small.left || small.top || small.right != 640 || small.bottom != 480)
		return 245;
	const auto normal_size = ui::FitWindowRect(small, preferred, high);
	if (normal_size.right - normal_size.left != 1200 || normal_size.bottom - normal_size.top != 900)
		return 246;
	// Verify real page/font clearance, including dropdown and button pages.
	for (int effect : { effect::ContrastCurves, effect::HdrGamma, effect::ColorBalance, effect::MidtoneGamma,
	                    effect::Levels, effect::ShadowsHighlights, effect::CustomCurves, effect::Imported1dLut,
	                    effect::ColorFilters, effect::NightLight, effect::Saturation, effect::ChannelMixer })
	{
		selected_effect = effect;
		settings_page = false;
		RefreshInterface();
		RECT text_bounds {};
		GetWindowRect(effect_description, &text_bounds);
		HWND first = effect == effect::ContrastCurves  ? contrast_slider
		             : effect == effect::HdrGamma      ? white_slider
		             : effect == effect::CustomCurves  ? curve_channel
		             : effect == effect::Imported1dLut ? import_button
		             : effect == effect::ColorFilters  ? filter_choice
		                                               : adjustment_sliders[0];
		RECT control_bounds {};
		GetWindowRect(first, &control_bounds);
		if (control_bounds.top - text_bounds.bottom < ScaleForDpi(6))
			return 253;
		wchar_t words[1024] {};
		GetWindowTextW(effect_description, words, 1024);
		RECT measured { 0, 0, text_bounds.right - text_bounds.left, 0 };
		HDC dc = GetDC(effect_description);
		const auto prior = SelectObject(dc, ui_font);
		DrawTextW(dc, words, -1, &measured, DT_CALCRECT | DT_WORDBREAK | DT_NOPREFIX);
		SelectObject(dc, prior);
		ReleaseDC(effect_description, dc);
		if (measured.bottom > text_bounds.bottom - text_bounds.top)
			return 254;
	}
	selected_effect = saved_selected;
	RefreshInterface();
	const auto tab_result = RunTabUiTest();
	if (tab_result)
		return tab_result;
	const bool prior_power = corrections_requested;
	selected_effect = effect::ColorFilters;
	RefreshInterface();
	if (!(GetWindowLongPtrW(filter_choice, GWL_STYLE) & WS_VISIBLE) || IsWindowEnabled(selected_effect_toggle) ||
	    IsWindowEnabled(filter_choice))
		return 224;
	SendMessageW(main_window, WM_COMMAND, ID_SELECTED_EFFECT_TOGGLE, 0);
	if (filter_controller.IsBusy() || corrections_requested != prior_power)
		return 225;
	SendMessageW(main_window, WM_COMMAND, ID_SETTINGS_PAGE, 0);
	if (!settings_page || (GetWindowLongPtrW(filter_choice, GWL_STYLE) & WS_VISIBLE) ||
	    !(GetWindowLongPtrW(font_choice, GWL_STYLE) & WS_VISIBLE))
		return 226;
	SendMessageW(main_window, WM_COMMAND, ID_SETTINGS_PAGE, 0);
	selected_effect = saved_selected;
	RefreshInterface();
	// Exercise master startup in the read-only harness, including both power
	// indicators. Test mode cannot begin a hardware session.
	TurnOnCorrections();
	if (!corrections_requested || live_session.IsActive() || !GetPropW(power_button, L"LutMonitorOn") ||
	    !IsWindowEnabled(source_choice))
		return 193;
	if (wcsstr(ui::CaptionTooltip(main_window, 102), L"off") == nullptr)
		return 194;
	ToggleCorrections();
	if (corrections_requested)
		return 195;
	// Display settling retries keep master power on, stop at a fixed limit,
	// and cancel cleanly when the user turns master power off.
	corrections_requested = true;
	mode_reset_pending = true;
	display_attempts = 0;
	for (int i = 0; i < 10; ++i)
		if (!WaitForDisplay() || !corrections_requested)
			return 208;
	if (WaitForDisplay() || display_attempts != 11)
		return 209;
	DisableLiveCorrections(true);
	if (!corrections_requested || !mode_reset_pending)
		return 210;
	DisableLiveCorrections();
	if (corrections_requested || display_retry_pending || mode_reset_pending || display_attempts)
		return 211;
	for (int hit : { 100, 101, 102, HTHELP, HTMINBUTTON, HTCLOSE, HTSYSMENU })
		if (!ui::CaptionTooltip(main_window, hit) || !*ui::CaptionTooltip(main_window, hit))
			return 196;
	if (wcscmp(ui::CaptionTooltip(main_window, HTHELP), ui::wording::LABEL_SETTINGS) ||
	    wcscmp(ui::CaptionTooltip(main_window, 101), ui::wording::LABEL_APPLICATION_PROFILES))
		return 231;
	for (HWND button : { monitor_settings_button, profiles_button, monitor_profile })
	{
		NMTTDISPINFOW tip {};
		tip.hdr.hwndFrom = monitor_tips;
		tip.hdr.idFrom = reinterpret_cast<UINT_PTR>(button);
		tip.hdr.code = TTN_GETDISPINFOW;
		SendMessageW(main_window, WM_NOTIFY, 0, reinterpret_cast<LPARAM>(&tip));
		if (!tip.lpszText ||
		    wcscmp(tip.lpszText, button == monitor_settings_button ? ui::wording::LABEL_SETTINGS
		                                                           : ui::wording::LABEL_APPLICATION_PROFILES))
			return 232;
	}
	SendMessageW(main_window, WM_NCMOUSEMOVE, 100, 0);
	const auto caption_tip = reinterpret_cast<HWND>(GetPropW(main_window, L"LutCaptionTooltip"));
	if (!caption_tip || SendMessageW(caption_tip, TTM_GETTOOLCOUNT, 0, 0) != 1)
		return 197;
	for (HWND tip : { caption_tip, monitor_tips })
	{
		LOGFONTW tip_font {};
		GetObjectW(reinterpret_cast<HFONT>(SendMessageW(tip, WM_GETFONT, 0, 0)), sizeof(tip_font), &tip_font);
		if (tip_font.lfHeight != -MulDiv(ui::style::SECONDARY_TEXT_SIZE, GetDpiForWindow(tip), 96))
			return 262;
		if (SendMessageW(tip, TTM_GETTIPBKCOLOR, 0, 0) != ui::THEME_SURFACE ||
		    SendMessageW(tip, TTM_GETTIPTEXTCOLOR, 0, 0) != ui::THEME_TEXT)
			return 263;
		SendMessageW(tip, TTM_SETTIPBKCOLOR, GetSysColor(COLOR_INFOBK), 0);
		SendMessageW(tip, WM_THEMECHANGED, 0, 0);
		if (SendMessageW(tip, TTM_GETTIPBKCOLOR, 0, 0) != ui::THEME_SURFACE)
			return 264;
		SetWindowPos(tip, nullptr, 0, 0, ScaleForDpi(120), ScaleForDpi(24), SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
		HRGN region = CreateRectRgn(0, 0, 0, 0);
		const bool rounded = GetWindowRgn(tip, region) != ERROR && !PtInRegion(region, 0, 0) &&
		                     PtInRegion(region, ScaleForDpi(60), ScaleForDpi(12));
		DeleteObject(region);
		if (!rounded)
			return 266;
		if (!ui::TooltipEdgeSelfTest(tip))
			return 278;
	}
	// Owner-drawn links and status labels may share a DC during native painting.
	// Neither their colors nor their pixels may escape the supplied item bounds.
	{
		HDC screen = GetDC(curve_canvas), dc = CreateCompatibleDC(screen);
		HBITMAP bitmap = CreateCompatibleBitmap(screen, 64, 32);
		const auto old = SelectObject(dc, bitmap);
		RECT all { 0, 0, 64, 32 };
		FillRect(dc, &all, reinterpret_cast<HBRUSH>(GetStockObject(WHITE_BRUSH)));
		const COLORREF sentinel = RGB(19, 71, 123);
		SetTextColor(dc, sentinel);
		SetBkMode(dc, OPAQUE);
		bool isolated = true;
		for (HWND control : { website_link, monitor_mode, monitor_profile })
		{
			DRAWITEMSTRUCT item {};
			item.CtlType = ODT_STATIC;
			item.hwndItem = control;
			item.hDC = dc;
			item.rcItem = { 8, 8, 56, 24 };
			SendMessageW(curve_canvas, WM_DRAWITEM, GetDlgCtrlID(control), reinterpret_cast<LPARAM>(&item));
			isolated = isolated && GetTextColor(dc) == sentinel && GetBkMode(dc) == OPAQUE &&
			           GetPixel(dc, 0, 0) == RGB(255, 255, 255);
		}
		SelectObject(dc, old);
		DeleteObject(bitmap);
		DeleteDC(dc);
		ReleaseDC(curve_canvas, screen);
		if (!isolated)
			return 281;
	}
	SendMessageW(main_window, WM_NCMOUSELEAVE, 0, 0);
	if (GetPropW(main_window, L"LutCaptionTipHit"))
		return 198;
	const auto previous_live = live_message, previous_preset_message = preset_message;
	live_message = ui::messages::FriendlyError("Driver rejected the correction");
	live_error = true;
	preset_message = L"Preset saved";
	RefreshInterface();
	if (!status_error)
		return 199;
	wchar_t failure_text[256] {};
	GetWindowTextW(status_label, failure_text, 256);
	if (std::wstring(failure_text) != ui::messages::FriendlyError("Driver rejected the correction"))
		return 203;
	// A wrapped warning must actually paint below the old one-row boundary.
	live_message =
		L"The previous correction carried into this display mode. Restore Windows calibration before re-enabling";
	RefreshInterface();
	{
		RECT bounds {};
		GetClientRect(curve_canvas, &bounds);
		const auto g = CalculateMonitorGeometry(curve_canvas);
		HDC screen = GetDC(curve_canvas), dc = CreateCompatibleDC(screen);
		HBITMAP image = CreateCompatibleBitmap(screen, bounds.right, bounds.bottom);
		const auto old = SelectObject(dc, image);
		SendMessageW(curve_canvas, WM_PRINTCLIENT, reinterpret_cast<WPARAM>(dc), PRF_CLIENT);
		bool second_line = false;
		for (int y = g.top + g.size; y < std::min(g.bottom, g.top + ScaleForDpi(64)); ++y)
			for (int x = g.left + g.size + ScaleForDpi(12); x < g.right - ScaleForDpi(12); ++x)
				if (GetPixel(dc, x, y) == ui::THEME_ERROR)
					second_line = true;
		SelectObject(dc, old);
		DeleteObject(image);
		DeleteDC(dc);
		ReleaseDC(curve_canvas, screen);
		wchar_t accessible[256] {};
		GetWindowTextW(curve_canvas, accessible, 256);
		if (!second_line || live_message != accessible)
			return 207;
	}
	live_message = previous_live;
	preset_message = previous_preset_message;
	live_error = false;
	RefreshInterface();
	if (status_error)
		return 200;
	RECT client {}, outer {};
	GetClientRect(main_window, &client);
	GetWindowRect(main_window, &outer);
	if (client.right != ScaleForDpi(CLIENT_WIDTH) || client.bottom != ScaleForDpi(CLIENT_HEIGHT))
		return 90;
	const auto hit = [&](int x, int y)
	{
		return SendMessageW(main_window, WM_NCHITTEST, 0, MAKELPARAM(x, y));
	};
	if (hit(outer.right - ScaleForDpi(20), outer.top + ScaleForDpi(18)) != HTCLOSE ||
	    hit(outer.right - ScaleForDpi(65), outer.top + ScaleForDpi(18)) != HTMINBUTTON ||
	    hit(outer.left + ScaleForDpi(150), outer.top + ScaleForDpi(18)) != HTCAPTION)
		return 91;
	if (hit(outer.right - ScaleForDpi(164), outer.top + ScaleForDpi(18)) != HTHELP ||
	    GetParent(power_button) != curve_canvas || GetParent(hdr_button) != curve_canvas ||
	    GetParent(profiles_button) != curve_canvas)
		return 154;
	POINT gear { outer.right - ScaleForDpi(164), outer.top + ScaleForDpi(18) };
	ScreenToClient(main_window, &gear);
	const auto command = [&]()
	{
		MSG event {};
		return PeekMessageW(&event, main_window, WM_COMMAND, WM_COMMAND, PM_REMOVE) &&
		       LOWORD(event.wParam) == ID_SETTINGS_PAGE;
	};
	SendMessageW(main_window, WM_NCLBUTTONDOWN, HTHELP, 0);
	SendMessageW(main_window, WM_LBUTTONUP, 0, MAKELPARAM(ScaleForDpi(250), ScaleForDpi(100)));
	if (command())
		return 155;
	SendMessageW(main_window, WM_NCLBUTTONDOWN, HTHELP, 0);
	SendMessageW(main_window, WM_LBUTTONUP, 0, MAKELPARAM(gear.x, gear.y));
	if (!command())
		return 156;
	SendMessageW(main_window, WM_SYSCHAR, L's', 0);
	if (!command())
		return 158;
	SendMessageW(main_window, WM_SYSCOMMAND, 0x1F00, 0);
	if (!command())
		return 159;
	const std::array<ui::CaptionAction, 3> caption_test { { { ID_TOGGLE_WINDOWS_HDR, false, true },
		                                                    { ID_APPLICATION_PROFILES, false, true },
		                                                    { ID_TEST_CONTRAST, false, true } } };
	if (GetParent(monitor_settings_button) != curve_canvas)
		return 172;
	SendMessageW(curve_canvas, WM_COMMAND, ID_SETTINGS_PAGE, 0);
	if (!settings_page)
		return 173;
	SendMessageW(curve_canvas, WM_COMMAND, ID_SETTINGS_PAGE, 0);
	if (settings_page)
		return 174;
	ui::CaptionActions(main_window, caption_test);
	for (int i = 0; i < 3; ++i)
	{
		POINT at { outer.right - ScaleForDpi(i == 2 ? 118 : 244 - i * 40), outer.top + ScaleForDpi(18) };
		if (hit(at.x, at.y) != 100 + i)
			return 170;
		ScreenToClient(main_window, &at);
		SendMessageW(main_window, WM_NCLBUTTONDOWN, 100 + i, 0);
		SendMessageW(main_window, WM_LBUTTONUP, 0, MAKELPARAM(at.x, at.y));
		MSG event {};
		if (!PeekMessageW(&event, main_window, WM_COMMAND, WM_COMMAND, PM_REMOVE) ||
		    LOWORD(event.wParam) != caption_test[i].command)
			return 171;
	}
	RefreshInterface();
	BYTE previous_keys[256] {}, keys[256] {};
	GetKeyboardState(previous_keys);
	keys[VK_SHIFT] = keys[VK_LSHIFT] = 0x80;
	SetKeyboardState(keys);
	SendMessageW(shortcut_control, WM_KEYDOWN, VK_SHIFT, 0);
	SendMessageW(shortcut_control, WM_KEYDOWN, VK_F20, 0);
	SendMessageW(shortcut_control, WM_KEYUP, VK_F20, 0);
	keys[VK_SHIFT] = keys[VK_LSHIFT] = 0;
	SetKeyboardState(keys);
	SendMessageW(shortcut_control, WM_KEYUP, VK_SHIFT, 0);
	const WORD captured = static_cast<WORD>(SendMessageW(shortcut_control, HKM_GETHOTKEY, 0, 0));
	SetKeyboardState(previous_keys);
	SendMessageW(shortcut_control, HKM_SETHOTKEY, active_shortcut, 0);
	if (captured != MAKEWORD(VK_F20, HOTKEYF_SHIFT))
		return 172;
	// Render-only input must never reach a real Windows HDR or LUT write.
	SendMessageW(curve_canvas, WM_COMMAND, ID_TOGGLE_WINDOWS_HDR, 0);
	if (hdr_pending || live_session.IsActive())
		return 160;
	const auto saved_hdr_state = hdr_state;
	const auto hdr_menu_item = []()
	{
		for (const auto& item : TrayMenuItems())
			if (item.id == ID_TOGGLE_WINDOWS_HDR)
				return item;
		return ui::MenuItem {};
	};
	hdr_state.available = true;
	hdr_state.on = false;
	if (!hdr_menu_item().shortcut.empty() || hdr_menu_item().checked || hdr_menu_item().glyph != ui::Glyph::Sun ||
	    !hdr_menu_item().enabled)
		return 218;
	hdr_state.on = true;
	if (!hdr_menu_item().shortcut.empty() || !hdr_menu_item().checked || !hdr_menu_item().enabled)
		return 219;
	hdr_pending = true;
	if (hdr_menu_item().shortcut != L"SWITCHING" || hdr_menu_item().enabled)
		return 220;
	hdr_pending = false;
	hdr_state.available = false;
	if (hdr_menu_item().enabled)
		return 221;
	hdr_state = saved_hdr_state;
	const bool editor_visible = IsWindowVisible(main_window) != FALSE;
	SendMessageW(main_window, MESSAGE_TRAY, 0, MAKELPARAM(NIN_SELECT, 1));
	if (hdr_pending || live_session.IsActive() || !IsWindowVisible(main_window))
		return 222;
	if (!editor_visible)
		ShowWindow(main_window, SW_HIDE);
	bool unavailable_rejected = false;
	try
	{
		hdr::SetHdrEnabled(hdr::State {}, true);
	}
	catch (const std::runtime_error&)
	{
		unavailable_rejected = true;
	}
	if (!unavailable_rejected)
		return 169;
	RECT preview {}, text {};
	GetWindowRect(curve_canvas, &preview);
	GetWindowRect(status_label, &text);
	RECT display {};
	GetWindowRect(source_choice, &display);
	RECT hint {}, mode {};
	GetWindowRect(order_hint, &hint);
	GetWindowRect(monitor_profile, &mode);
	if (mode.top != display.top || mode.bottom != display.bottom)
		return 190;
	RECT effect_bounds {};
	GetWindowRect(effect_list, &effect_bounds);
	if (hint.left != effect_bounds.left || hint.right != effect_bounds.right ||
	    hint.top != effect_bounds.bottom + ScaleForDpi(6))
		return 223;
	const auto geometry = CalculateMonitorGeometry(curve_canvas);
	if (std::abs((display.top + display.bottom) - (2 * preview.top + geometry.body.bottom + geometry.foot.bottom)) > 1)
		return 220;
	LOGFONTW hint_style {}, normal_style {};
	GetObjectW(reinterpret_cast<HFONT>(SendMessageW(order_hint, WM_GETFONT, 0, 0)), sizeof(hint_style), &hint_style);
	GetObjectW(ui_font, sizeof(normal_style), &normal_style);
	if (std::abs(hint_style.lfHeight) >= std::abs(normal_style.lfHeight))
		return 221;
	{
		RECT area {};
		GetClientRect(effect_list, &area);
		const int original_height = area.bottom;
		SetWindowPos(effect_list, nullptr, 0, 0, area.right, original_height + ScaleForDpi(effects::ROW_HEIGHT),
		             SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
		GetClientRect(effect_list, &area);
		HDC screen = GetDC(effect_list), buffer = CreateCompatibleDC(screen);
		HBITMAP image = CreateCompatibleBitmap(screen, area.right, area.bottom);
		auto old = SelectObject(buffer, image);
		SendMessageW(effect_list, WM_PRINTCLIENT, reinterpret_cast<WPARAM>(buffer), PRF_CLIENT);
		const bool dark_tail = GetPixel(buffer, area.right / 2, area.bottom - 2) == ui::THEME_SIDEBAR;
		SelectObject(buffer, old);
		DeleteObject(image);
		DeleteDC(buffer);
		ReleaseDC(effect_list, screen);
		SetWindowPos(effect_list, nullptr, 0, 0, area.right, original_height,
		             SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
		if (!dark_tail)
			return 222;
	}
	if (GetParent(source_choice) != curve_canvas || !(GetWindowLongPtrW(source_choice, GWL_STYLE) & WS_VISIBLE) ||
	    display.bottom > preview.bottom ||
	    display.right > preview.left + (preview.right - preview.left) / 2 - ScaleForDpi(57))
		return 175;
	if (GetParent(status_label) != curve_canvas || preview.right - preview.left != ScaleForDpi(558) ||
	    preview.bottom - preview.top != ScaleForDpi(268) || text.left <= preview.left || text.right >= preview.right ||
	    text.top <= preview.top || text.bottom >= preview.bottom)
		return 97;
	if ((GetWindowLongPtrW(active_look_label, GWL_STYLE) & WS_VISIBLE) ||
	    (GetWindowLongPtrW(open_preset_button, GWL_STYLE) & WS_VISIBLE) ||
	    (GetWindowLongPtrW(settings_button, GWL_STYLE) & WS_VISIBLE))
		return 167;
	// Updating or focusing any balance slider must not erase an adjacent
	// slider's thumb. Check actual HWND bounds, including each value label.
	const auto aligned_row = [&](HWND label, HWND value, HWND slider)
	{
		RECT name {}, number {}, channel {};
		GetWindowRect(label, &name);
		GetWindowRect(value, &number);
		SendMessageW(slider, TBM_GETCHANNELRECT, 0, reinterpret_cast<LPARAM>(&channel));
		MapWindowPoints(slider, nullptr, reinterpret_cast<POINT*>(&channel), 2);
		return name.top == number.top && name.bottom == number.bottom &&
		       std::abs((name.top + name.bottom) / 2 - (channel.top + channel.bottom) / 2) <= 1;
	};
	if (!aligned_row(amount_label, amount_value, contrast_slider) ||
	    !aligned_row(pivot_label, pivot_value, pivot_slider) || !aligned_row(white_label, white_value, white_slider))
		return 188;
	for (size_t i = 0; i < adjustment_sliders.size(); ++i)
	{
		if (!aligned_row(adjustment_labels[i], adjustment_values[i], adjustment_sliders[i]))
			return 189;
		RECT slider {}, label {}, overlap {};
		GetWindowRect(adjustment_sliders[i], &slider);
		for (size_t j = 0; j < adjustment_sliders.size(); ++j)
		{
			GetWindowRect(adjustment_labels[j], &label);
			if (IntersectRect(&overlap, &slider, &label))
				return 148;
			if (i != j)
			{
				RECT other {};
				GetWindowRect(adjustment_sliders[j], &other);
				if (IntersectRect(&overlap, &slider, &other))
					return 149;
			}
		}
	}
	// First input changes the clean/dirty indicator. Subsequent slider input must
	// not reset unrelated controls or rebuild the preset dropdown.
	selected_effect = effect::ContrastCurves;
	SendMessageW(contrast_slider, TBM_SETPOS, TRUE, -1);
	SendMessageW(main_window, WM_HSCROLL, TB_THUMBPOSITION, reinterpret_cast<LPARAM>(contrast_slider));
	int mutations = 0;
	const HWND watched[] { save_preset_button, save_as_button, open_preset_button, revert_preset_button,
		                   profiles_button,    bypass_button,  hdr_fix_control,    reset_button,
		                   export_button,      preset_choice,  effect_list,        main_window };
	for (HWND control : watched)
		SetWindowSubclass(control, CountSurfaceChanges, 73, reinterpret_cast<DWORD_PTR>(&mutations));
	for (int i = -2; i >= -25; --i)
	{
		SendMessageW(contrast_slider, TBM_SETPOS, TRUE, i);
		SendMessageW(main_window, WM_HSCROLL, TB_THUMBPOSITION, reinterpret_cast<LPARAM>(contrast_slider));
	}
	for (HWND control : watched)
		RemoveWindowSubclass(control, CountSurfaceChanges, 73);
	if (mutations)
		return 98;
	// Drag across the saved value repeatedly: data/preview stay live, but
	// the dropdown, caption marker and effect selection stay unchanged.
	autosave_enabled = true;
	mutations = 0;
	for (HWND control : watched)
		SetWindowSubclass(control, CountSurfaceChanges, 73, reinterpret_cast<DWORD_PTR>(&mutations));
	for (int position : { 10, 12, 10, 12 })
	{
		SendMessageW(contrast_slider, TBM_SETPOS, TRUE, position);
		SendMessageW(main_window, WM_HSCROLL, TB_THUMBTRACK, reinterpret_cast<LPARAM>(contrast_slider));
		if (CurrentPreset(preset_role::Look).current.Contrast != position ||
		    CurrentPreset(preset_role::Look).IsDirty() != (position != 10))
			return 140;
		FlushPresetAutosave();
		if (presets::Read(CurrentPreset(preset_role::Look).path).Contrast != 10)
			return 141;
	}
	for (HWND control : watched)
		RemoveWindowSubclass(control, CountSurfaceChanges, 73);
	if (mutations || !slider_dragging)
		return 142;
	SendMessageW(contrast_slider, TBM_SETPOS, TRUE, 10);
	SendMessageW(main_window, WM_HSCROLL, TB_ENDTRACK, reinterpret_cast<LPARAM>(contrast_slider));
	wchar_t marker[1024] {};
	GetWindowTextW(preset_choice, marker, 1024);
	if (slider_dragging || CurrentPreset(preset_role::Look).IsDirty() || wcschr(marker, L'*'))
		return 143;
	SendMessageW(contrast_slider, TBM_SETPOS, TRUE, 12);
	SendMessageW(main_window, WM_HSCROLL, TB_THUMBTRACK, reinterpret_cast<LPARAM>(contrast_slider));
	GetWindowTextW(preset_choice, marker, 1024);
	if (wcschr(marker, L'*'))
		return 144;
	SendMessageW(contrast_slider, TBM_SETPOS, TRUE, 13);
	SendMessageW(main_window, WM_HSCROLL, TB_THUMBPOSITION, reinterpret_cast<LPARAM>(contrast_slider));
	GetWindowTextW(preset_choice, marker, 1024);
	if (slider_dragging || !CurrentPreset(preset_role::Look).IsDirty() || !wcschr(marker, L'*'))
		return 145;
	autosave_enabled = false;
	KillTimer(main_window, TIMER_PRESET);
	// Exercise the synchronous native-button custom-draw route as well as WM_PAINT.
	HDC screen = GetDC(main_window), buffer = CreateCompatibleDC(screen);
	HBITMAP bitmap = CreateCompatibleBitmap(screen, ScaleForDpi(125), ScaleForDpi(30));
	const auto old = SelectObject(buffer, bitmap);
	NMCUSTOMDRAW draw {};
	draw.hdr.hwndFrom = reset_button;
	draw.hdr.code = NM_CUSTOMDRAW;
	draw.dwDrawStage = CDDS_PREPAINT;
	draw.hdc = buffer;
	LRESULT result = 0;
	const bool handled = ui::HandleThemeMessage(main_window, WM_NOTIFY, 0, reinterpret_cast<LPARAM>(&draw), result);
	const auto pixel = GetPixel(buffer, ScaleForDpi(3), ScaleForDpi(14));
	DRAWITEMSTRUCT item {};
	item.CtlType = ODT_COMBOBOX;
	item.hwndItem = curve_channel;
	item.itemID = 0;
	item.itemState = ODS_SELECTED;
	item.hDC = buffer;
	item.rcItem = { 0, 0, ScaleForDpi(125), ScaleForDpi(30) };
	LRESULT combo_result = 0;
	if (!ui::HandleThemeMessage(main_window, WM_DRAWITEM, 0, reinterpret_cast<LPARAM>(&item), combo_result) ||
	    GetPixel(buffer, ScaleForDpi(124), ScaleForDpi(14)) != RGB(66, 71, 79))
		return 201;
	COMBOBOXINFO combo_info { sizeof(combo_info) };
	if (!GetComboBoxInfo(curve_channel, &combo_info))
		return 202;
	SetWindowPos(combo_info.hwndList, nullptr, 0, 0, ScaleForDpi(125), ScaleForDpi(30), SWP_NOACTIVATE | SWP_NOZORDER);
	SendMessageW(combo_info.hwndList, WM_PRINT, reinterpret_cast<WPARAM>(buffer), PRF_NONCLIENT | PRF_CLIENT);
	if (GetPixel(buffer, 0, ScaleForDpi(14)) != ui::THEME_FOCUS_BORDER)
		return 204;
	HRGN popup_shape = CreateRectRgn(0, 0, 0, 0);
	const bool rounded_popup = GetWindowRgn(combo_info.hwndList, popup_shape) == COMPLEXREGION &&
	                           !PtInRegion(popup_shape, 0, 0) &&
	                           PtInRegion(popup_shape, ScaleForDpi(10), ScaleForDpi(10));
	RECT popup_bounds {};
	GetWindowRect(combo_info.hwndList, &popup_bounds);
	const int popup_width = popup_bounds.right - popup_bounds.left,
			  popup_height = popup_bounds.bottom - popup_bounds.top;
	HDC corner_dc = CreateCompatibleDC(screen);
	HBITMAP corner_bitmap = CreateCompatibleBitmap(screen, popup_width, popup_height);
	const auto corner_old = SelectObject(corner_dc, corner_bitmap);
	SendMessageW(combo_info.hwndList, WM_PRINT, reinterpret_cast<WPARAM>(corner_dc), PRF_NONCLIENT | PRF_CLIENT);
	bool same_corner_ink = true;
	for (int inset = 0; inset <= ScaleForDpi(ui::style::CORNER_RADIUS) + 1; ++inset)
		for (int y : { inset, popup_height - 1 - inset })
		{
			int left = 0, right = popup_width - 1;
			while (left < popup_width && !PtInRegion(popup_shape, left, y))
				++left;
			while (right >= 0 && !PtInRegion(popup_shape, right, y))
				--right;
			if (left > right || GetPixel(corner_dc, left, y) != ui::THEME_FOCUS_BORDER ||
			    GetPixel(corner_dc, right, y) != ui::THEME_FOCUS_BORDER)
				same_corner_ink = false;
		}
	SelectObject(corner_dc, corner_old);
	DeleteObject(corner_bitmap);
	DeleteDC(corner_dc);
	DeleteObject(popup_shape);
	if (!same_corner_ink)
		return 224;
	if (!rounded_popup)
		return 210;
	// Rendering power state must not alter the stored enabled flag.
	const bool checked = SendMessageW(selected_effect_toggle, BM_GETCHECK, 0, 0) == BST_CHECKED;
	const bool powered = GetPropW(selected_effect_toggle, L"LutToggleUnpowered") == nullptr;
	ui::ToggleButton(selected_effect_toggle, true, false);
	SendMessageW(selected_effect_toggle, WM_PRINTCLIENT, reinterpret_cast<WPARAM>(buffer), PRF_CLIENT);
	const bool gray_on = GetPixel(buffer, ScaleForDpi(12), ScaleForDpi(14)) == ui::THEME_BORDER &&
	                     SendMessageW(selected_effect_toggle, BM_GETCHECK, 0, 0) == BST_CHECKED;
	ui::ToggleButton(selected_effect_toggle, true, true);
	SendMessageW(selected_effect_toggle, WM_PRINTCLIENT, reinterpret_cast<WPARAM>(buffer), PRF_CLIENT);
	const bool blue_on = GetPixel(buffer, ScaleForDpi(12), ScaleForDpi(14)) == ui::THEME_ACCENT;
	ui::ToggleButton(selected_effect_toggle, checked, powered);
	if (!gray_on || !blue_on)
		return 211;
	SendMessageW(shortcut_button, WM_PRINTCLIENT, reinterpret_cast<WPARAM>(buffer), PRF_CLIENT);
	const auto normal_fill = GetPixel(buffer, ScaleForDpi(10), ScaleForDpi(5));
	SendMessageW(shortcut_button, WM_MOUSEMOVE, 0, MAKELPARAM(ScaleForDpi(10), ScaleForDpi(5)));
	SendMessageW(shortcut_button, WM_PRINTCLIENT, reinterpret_cast<WPARAM>(buffer), PRF_CLIENT);
	const bool same_hover_fill = GetPixel(buffer, ScaleForDpi(10), ScaleForDpi(5)) == normal_fill;
	SendMessageW(shortcut_button, WM_MOUSELEAVE, 0, 0);
	if (!same_hover_fill)
		return 212;
	HRGN input_shape = CreateRectRgn(0, 0, 0, 0);
	const bool rounded_shortcut =
		GetWindowRgn(shortcut_control, input_shape) == COMPLEXREGION && !PtInRegion(input_shape, 0, 0);
	DeleteObject(input_shape);
	if (!rounded_shortcut)
		return 213;
	bool rgb_ok = true;
	for (int mask = 1; mask < 8; ++mask)
	{
		RECT area { 0, 0, ScaleForDpi(125), ScaleForDpi(30) };
		FillRect(buffer, &area, ui::BackgroundBrush());
		plot::Paths paths;
		for (int c = 0; c < 3; ++c)
			if (mask & (1 << c))
				paths[c] = { { ScaleForDpi(4), ScaleForDpi(8) }, { ScaleForDpi(24), ScaleForDpi(8) } };
		const COLORREF expected = RGB(mask & 1 ? 255 : 0, mask & 2 ? 255 : 0, mask & 4 ? 255 : 0);
		if (!plot::DrawRgbCurves(buffer, ScaleForDpi(125), ScaleForDpi(30), paths, ScaleForDpi(2)) ||
		    GetPixel(buffer, ScaleForDpi(12), ScaleForDpi(8)) != expected ||
		    GetPixel(buffer, ScaleForDpi(12), ScaleForDpi(20)) != ui::THEME_BACKGROUND)
			rgb_ok = false;
	}
	RECT area { 0, 0, ScaleForDpi(125), ScaleForDpi(30) };
	FillRect(buffer, &area, ui::BackgroundBrush());
	plot::Paths diagonal;
	diagonal[0] = { { ScaleForDpi(4), ScaleForDpi(4) }, { ScaleForDpi(24), ScaleForDpi(15) } };
	if (!plot::DrawRgbCurves(buffer, ScaleForDpi(125), ScaleForDpi(30), diagonal, ScaleForDpi(2)))
		rgb_ok = false;
	bool partial = false;
	for (int y = ScaleForDpi(4); y < ScaleForDpi(20); ++y)
		for (int x = ScaleForDpi(4); x < ScaleForDpi(24); ++x)
		{
			const auto value = GetPixel(buffer, x, y);
			if (GetRValue(value) > GetRValue(ui::THEME_BACKGROUND) + 20 && GetRValue(value) < 240)
				partial = true;
		}
	rgb_ok = rgb_ok && partial;
	if (!plot::DrawRgbCurves(buffer, ScaleForDpi(125), ScaleForDpi(30), diagonal, ScaleForDpi(2), &curve_scratch) ||
	    !curve_scratch.ByteCount() || curve_scratch.ByteCount() > 1048576)
		return 256;
	SendMessageW(main_window, WM_SHOWWINDOW, FALSE, 0);
	if (curve_scratch.ByteCount())
		return 257;
	if (!plot::DrawRgbCurves(buffer, ScaleForDpi(125), ScaleForDpi(30), diagonal, ScaleForDpi(2), &curve_scratch))
		return 258;
	SendMessageW(main_window, WM_SIZE, SIZE_MINIMIZED, 0);
	if (curve_scratch.ByteCount())
		return 259;
	SelectObject(buffer, old);
	DeleteObject(bitmap);
	DeleteDC(buffer);
	ReleaseDC(main_window, screen);
	if (!handled || result != CDRF_SKIPDEFAULT || pixel != ui::THEME_SURFACE)
		return 99;
	if (!rgb_ok)
		return 162;
	const int popup = ui::PopupSelfTest(main_window);
	if (popup)
		return 100 + popup;
	const bool caption_menu_okay = ui::PreviewCaptionMenu(main_window,
	                                                      [&](HWND menu)
	                                                      {
															  const HWND list = GetDlgItem(menu, 10);
															  const auto count = SendMessageW(list, LB_GETCOUNT, 0, 0);
															  bool close = false, power = false, maximize = false;
															  for (int i = 0; i < count; ++i)
															  {
																  const auto id =
																	  SendMessageW(list, LB_GETITEMDATA, i, 0);
																  close |= id == SC_CLOSE;
																  power |= id == 0x1F30;
																  if (id == SC_MAXIMIZE)
																  {
																	  maximize = true;
																	  SendMessageW(list, LB_SETCURSEL, i, 0);
																	  SendMessageW(list, WM_KEYDOWN, VK_RETURN, 0);
																	  if (!IsWindow(menu))
																		  return false;
																  }
															  }
															  return close && power && maximize;
														  });
	if (!caption_menu_okay)
		return 219;
	const int unsaved_result = unsaved::SelfTest(main_window);
	if (unsaved_result)
		return unsaved_result;
	SendMessageW(curve_canvas, WM_COMMAND, MAKEWPARAM(ID_MONITOR_PROFILE, STN_CLICKED),
	             reinterpret_cast<LPARAM>(monitor_profile));
	HWND editor = profile_editor::ProfileEditorHandle();
	if (!editor)
		return 92;
	if (!SendMessageW(editor, WM_GETICON, ICON_SMALL, 0))
		return 227;
	const auto path_before = []
	{
		wchar_t text[32768] {};
		GetWindowTextW(GetDlgItem(profile_editor::ProfileEditorHandle(), 204), text, 32768);
		return std::wstring(text);
	}();
	profile_editor::PreviewPrograms({ { L"C:\\Video\\Player.exe", L"Video player", 1024 * 1024 * 400, true, true },
	                                  { L"C:\\Games\\Game.exe", L"Example game", 1024 * 1024 * 800, true, true } });
	if (!(GetWindowLongPtrW(GetDlgItem(editor, 215), GWL_STYLE) & WS_VISIBLE))
		return 228;
	SendMessageW(editor, WM_COMMAND, 220, 0);
	wchar_t current_path[32768] {};
	GetWindowTextW(GetDlgItem(editor, 204), current_path, 32768);
	if (path_before != current_path)
		return 229;
	profile_editor::PreviewPrograms({ { L"C:\\Games\\Game.exe", L"Example game", 1024 * 1024 * 800, true, true } });
	SendMessageW(editor, WM_COMMAND, 219, 0);
	GetWindowTextW(GetDlgItem(editor, 204), current_path, 32768);
	if (wcscmp(current_path, L"C:\\Games\\Game.exe") != 0)
		return 230;
	HRGN edit_shape = CreateRectRgn(0, 0, 0, 0);
	const bool rounded_edit =
		GetWindowRgn(GetDlgItem(editor, 204), edit_shape) == COMPLEXREGION && !PtInRegion(edit_shape, 0, 0);
	DeleteObject(edit_shape);
	if (!rounded_edit)
		return 214;
	GetClientRect(editor, &client);
	GetWindowRect(editor, &outer);
	if (client.right != ScaleForDpi(420) || client.bottom != ScaleForDpi(526))
		return 93;
	const auto profile_bounds = outer;
	SetWindowPos(editor, nullptr, 0, 0, ScaleForDpi(300), ScaleForDpi(260), SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
	ui::RevealControl(editor, GetDlgItem(editor, 213));
	if (ui::ViewportOffset(editor).y <= 0)
		return 286;
	SetWindowPos(editor, nullptr, 0, 0, profile_bounds.right - profile_bounds.left,
	             profile_bounds.bottom - profile_bounds.top, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
	if (ui::ViewportOffset(editor).x || ui::ViewportOffset(editor).y)
		return 287;
	if (SendMessageW(editor, WM_NCHITTEST, 0, MAKELPARAM(outer.right - ScaleForDpi(65), outer.top + ScaleForDpi(18))) !=
	    HTCAPTION)
		return 94;
	POINT close { outer.right - ScaleForDpi(20), outer.top + ScaleForDpi(18) };
	ScreenToClient(editor, &close);
	SendMessageW(editor, WM_NCLBUTTONDOWN, HTCLOSE, 0);
	SendMessageW(editor, WM_LBUTTONUP, 0, MAKELPARAM(close.x, close.y));
	MSG message {};
	while (PeekMessageW(&message, editor, WM_SYSCOMMAND, WM_SYSCOMMAND, PM_REMOVE))
		DispatchMessageW(&message);
	if (profile_editor::ProfileEditorHandle())
		return 95;
	const std::wstring previous_family = ui::FontFamily();
	SendMessageW(font_choice, CB_SETCURSEL, 1, 0);
	SendMessageW(main_window, WM_COMMAND, MAKEWPARAM(ID_FONT_CHOICE, CBN_SELCHANGE), 0);
	for (HWND control : { monitor_mode, monitor_profile })
	{
		LOGFONTW style {};
		auto handle = reinterpret_cast<HFONT>(SendMessageW(control, WM_GETFONT, 0, 0));
		if (handle != ui_font || !GetObjectW(handle, sizeof(style), &style) || wcscmp(style.lfFaceName, L"Segoe UI"))
			return 218;
	}
	ui::SetFontFamily(previous_family.c_str());
	LayoutEditorControls();
	return live_session.IsActive() || corrections_requested ? 96 : 0;
}

void CleanPresetUiTest()
{
	// Remove only the exact fixtures created by preparePresetUiTest; no recursive deletion.
	try
	{
		for (const auto& path :
		     { PresetFolder(preset_role::Look) / L"A.lutpreset", PresetFolder(preset_role::Look) / L"B.lutpreset",
		       PresetFolder(preset_role::Look) / L"Copy.lutpreset",
		       PresetFolder(preset_role::Look) / L"Farver-\u00e6\u00f8\u00e5.lutpreset",
		       PresetFolder(preset_role::Look) / L"Off.lutpreset",
		       PresetFolder(preset_role::Correction) / L"Off.lutpreset",
		       PresetFolder(preset_role::Correction) / L"C.lutpreset",
		       PresetFolder(preset_role::Correction) / L"Added from folder.lutpreset",
		       std::filesystem::path(test_settings_path) })
			std::filesystem::remove(path);
		std::filesystem::remove(PresetFolder(preset_role::Look));
		std::filesystem::remove(PresetFolder(preset_role::Correction));
		std::filesystem::remove(PresetFolder(preset_role::Look).parent_path());
		std::filesystem::remove(std::filesystem::path(test_settings_path).parent_path());
	}
	catch (...)
	{
	}
}
}

int WINAPI wWinMain(HINSTANCE app, HINSTANCE, PWSTR, int show)
{
	app_instance = app;
	SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
	int argument_count = 0;
	LPWSTR* arguments = CommandLineToArgvW(GetCommandLineW(), &argument_count);
	if (arguments && argument_count == 3 && std::wstring(arguments[1]) == L"--benchmark-matrix")
	{
		const int result = matrix_effect::Benchmark(arguments[2]);
		LocalFree(arguments);
		return result;
	}
	if (arguments && argument_count == 2 && std::wstring(arguments[1]) == L"--self-test-matrix-effect")
	{
		LocalFree(arguments);
		return matrix_effect::SelfTest();
	}
	if (arguments && argument_count == 4 &&
	    (std::wstring(arguments[1]) == L"--matrix-effect-worker" ||
	     std::wstring(arguments[1]) == L"--matrix-effect-probe"))
	{
		wchar_t* end = nullptr;
		const auto pid = wcstoul(arguments[2], &end, 10);
		const int result = pid && end && !*end
		                       ? matrix_effect::RunWorkerProcess(pid, arguments[3],
		                                                         std::wstring(arguments[1]) == L"--matrix-effect-probe")
		                       : 2;
		LocalFree(arguments);
		return result;
	}
	if (arguments && argument_count == 2 && std::wstring(arguments[1]) == L"--matrix-preview")
	{
		LocalFree(arguments);
		return matrix_preview::RunStandalone(app);
	}
	if (arguments && argument_count == 2 && std::wstring(arguments[1]) == L"--self-test-matrix-preview")
	{
		const int result = matrix_preview::SelfTest(arguments[0]);
		LocalFree(arguments);
		return result;
	}
	if (arguments && argument_count == 2 && std::wstring(arguments[1]) == L"--matrix-preview-owner")
	{
		LocalFree(arguments);
		Sleep(INFINITE);
		return 0;
	}
	if (arguments && argument_count == 4 &&
	    (std::wstring(arguments[1]) == L"--matrix-worker" || std::wstring(arguments[1]) == L"--matrix-worker-probe"))
	{
		wchar_t* end = nullptr;
		const auto pid = wcstoul(arguments[2], &end, 10);
		const int result = pid && end && !*end
		                       ? matrix_preview::RunWorkerProcess(
									 pid, arguments[3], std::wstring(arguments[1]) == L"--matrix-worker-probe")
		                       : 2;
		LocalFree(arguments);
		return result;
	}
	if (arguments && argument_count == 2 && std::wstring(arguments[1]) == L"--self-test-icons")
	{
		LocalFree(arguments);
		if (app_icons::ChooseState(false, false, true) != app_icons::State::Disabled ||
		    app_icons::ChooseState(false, true, false) != app_icons::State::DisabledHdr ||
		    app_icons::ChooseState(false, true, true) != app_icons::State::DisabledHdr ||
		    app_icons::ChooseState(true, false, false) != app_icons::State::Active ||
		    app_icons::ChooseState(true, true, false) != app_icons::State::Hdr ||
		    app_icons::ChooseState(true, false, true) != app_icons::State::Application ||
		    app_icons::ChooseState(true, true, true) != app_icons::State::ApplicationHdr)
			return 1;
		for (const auto state :
		     { app_icons::State::Disabled, app_icons::State::DisabledHdr, app_icons::State::Active,
		       app_icons::State::Hdr, app_icons::State::Application, app_icons::State::ApplicationHdr })
		{
			for (int size : { 16, 20, 24, 32, 40, 48, 64, 96, 128, 256 })
			{
				HICON icon = icon_cache.GetIcon(app_instance, state, size);
				if (!icon)
					return 2;
				if (icon_cache.GetIcon(app_instance, state, size) != icon)
					return 3;
				ICONINFO info {};
				if (!GetIconInfo(icon, &info))
					return 4;
				BITMAP bitmap {};
				const bool correct = info.hbmColor && GetObjectW(info.hbmColor, sizeof(bitmap), &bitmap) &&
				                     bitmap.bmWidth == size && bitmap.bmHeight == size;
				if (info.hbmColor)
					DeleteObject(info.hbmColor);
				if (info.hbmMask)
					DeleteObject(info.hbmMask);
				if (!correct)
					return 5;
			}
		}
		return 0;
	}
	if (arguments && argument_count == 2 && std::wstring(arguments[1]) == L"--self-test-hotkeys")
	{
		LocalFree(arguments);
		return RunHotkeySelfTest();
	}
	if (arguments && argument_count == 2 && std::wstring(arguments[1]) == L"--live-probe-owner")
	{
		LocalFree(arguments);
		Sleep(INFINITE);
		return 0;
	}
	if (arguments && argument_count == 7 &&
	    (std::wstring(arguments[1]) == L"--live-guard" || std::wstring(arguments[1]) == L"--live-guard-probe"))
	{
		wchar_t* end = nullptr;
		const unsigned long parent = wcstoul(arguments[3], &end, 10);
		const int result =
			parent && end && !*end
				? live::RunRecoveryGuardian(arguments[2], parent, arguments[4], arguments[5], arguments[6],
		                                    std::wstring(arguments[1]) == L"--live-guard-probe")
				: 1;
		LocalFree(arguments);
		return result;
	}
	if (arguments && argument_count == 2 &&
	    (std::wstring(arguments[1]) == L"--self-test-live" || std::wstring(arguments[1]) == L"--self-test-live-crash"))
	{
		const int result =
			live::RecoverySelfTest(arguments[0], std::wstring(arguments[1]) == L"--self-test-live-crash");
		LocalFree(arguments);
		return result;
	}
	if (arguments && argument_count == 3 && std::wstring(arguments[1]) == L"--recover-live")
	{
		const int result = live::RecoverFromSnapshotFile(arguments[2]);
		LocalFree(arguments);
		return result;
	}
	const bool render =
		(arguments && argument_count == 3 &&
	     (std::wstring(arguments[1]) == L"--render-small" || std::wstring(arguments[1]) == L"--render-small-profiles" ||
	      std::wstring(arguments[1]) == L"--render-mixer")) ||
		arguments && argument_count == 3 &&
			(std::wstring(arguments[1]) == L"--render-saturation" ||
	         std::wstring(arguments[1]) == L"--render-matrix-preview" ||
	         std::wstring(arguments[1]) == L"--render-preview" || std::wstring(arguments[1]) == L"--render-hdr" ||
	         std::wstring(arguments[1]) == L"--render-profiles" ||
	         std::wstring(arguments[1]) == L"--render-running-programs" ||
	         std::wstring(arguments[1]) == L"--render-application" || std::wstring(arguments[1]) == L"--render-tray" ||
	         std::wstring(arguments[1]) == L"--render-balance" || std::wstring(arguments[1]) == L"--render-curves" ||
	         std::wstring(arguments[1]) == L"--render-settings" ||
	         std::wstring(arguments[1]) == L"--render-color-filters" ||
	         std::wstring(arguments[1]) == L"--render-night-light" ||
	         std::wstring(arguments[1]) == L"--render-font-segoe" ||
	         std::wstring(arguments[1]) == L"--render-unsaved" || std::wstring(arguments[1]) == L"--render-error" ||
	         std::wstring(arguments[1]) == L"--render-carryover" || std::wstring(arguments[1]) == L"--render-ready" ||
	         std::wstring(arguments[1]) == L"--render-title-menu");
	const std::wstring render_file = render ? arguments[2] : L"";
	const std::wstring render_mode = render ? arguments[1] : L"";
	const bool render_tray = render && std::wstring(arguments[1]) == L"--render-tray";
	const bool render_hdr = render && std::wstring(arguments[1]) == L"--render-hdr";
	const bool render_profiles = render && (std::wstring(arguments[1]) == L"--render-profiles" ||
	                                        std::wstring(arguments[1]) == L"--render-small-profiles");
	const bool render_application = render && std::wstring(arguments[1]) == L"--render-application";
	const bool tray_exit_test =
		arguments && argument_count == 2 && std::wstring(arguments[1]) == L"--self-test-tray-exit";
	const bool tray_startup_test =
		tray_exit_test || arguments && argument_count == 2 && std::wstring(arguments[1]) == L"--self-test-tray-startup";
	const bool tray_benchmark =
		arguments && argument_count == 3 && std::wstring(arguments[1]) == L"--benchmark-tray-startup";
	const std::filesystem::path tray_benchmark_report = tray_benchmark ? arguments[2] : L"";
	if (tray_benchmark && std::filesystem::exists(tray_benchmark_report))
	{
		LocalFree(arguments);
		return 1;
	}
	const bool benchmark = arguments && argument_count == 3 && std::wstring(arguments[1]) == L"--benchmark-ui";
	const std::filesystem::path benchmark_report = benchmark ? arguments[2] : L"";
	if (benchmark && std::filesystem::exists(benchmark_report))
	{
		LocalFree(arguments);
		return 1;
	}
	profile_ui_test = arguments && argument_count == 2 && std::wstring(arguments[1]) == L"--self-test-profile-ui";
	effect_ui_test = arguments && argument_count == 2 && std::wstring(arguments[1]) == L"--self-test-effect-ui";
	const bool surfaces96 =
		arguments && argument_count == 2 && std::wstring(arguments[1]) == L"--self-test-surfaces-96";
	if (surfaces96 && !SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_UNAWARE))
	{
		LocalFree(arguments);
		return 1;
	}
	surface_ui_test =
		surfaces96 || (arguments && argument_count == 2 && std::wstring(arguments[1]) == L"--self-test-surfaces");
	preset_ui_test = tray_startup_test || tray_benchmark || benchmark || surface_ui_test || effect_ui_test ||
	                 profile_ui_test ||
	                 (arguments && argument_count == 2 && std::wstring(arguments[1]) == L"--self-test-preset-ui");
	if (arguments && argument_count == 3 && std::wstring(arguments[1]) == L"--idle-probe")
		idle_report = arguments[2];
	if (!idle_report.empty() && GetFileAttributesW(idle_report.c_str()) != INVALID_FILE_ATTRIBUTES)
	{
		LocalFree(arguments);
		return 1;
	}
	const bool preview_only = arguments && argument_count == 2 && std::wstring(arguments[1]) == L"--preview-ui";
	render_only = preview_only || render || !idle_report.empty() || preset_ui_test;
	if (arguments)
		LocalFree(arguments);
	if (preset_ui_test && !PreparePresetUiTest())
		return 1;
	const auto mutex_name = render_only ? L"Local\\LutWorkbenchRender-" + std::to_wstring(GetCurrentProcessId())
	                                    : std::wstring(L"Local\\LutWorkbenchPreview");
	HANDLE mutex = CreateMutexW(nullptr, FALSE, mutex_name.c_str());
	if (!mutex)
		return 1;
	if (GetLastError() == ERROR_ALREADY_EXISTS)
	{
		HWND existing = FindWindowW(L"LutWorkbenchPreview", nullptr);
		if (existing)
		{
			ShowWindow(existing, SW_SHOW);
			SetForegroundWindow(existing);
		}
		CloseHandle(mutex);
		return 0;
	}
	defer_editor = tray_startup_test || tray_benchmark ||
	               !render_only && GetPrivateProfileIntW(L"Startup", L"Minimized", 0, SettingsFilePath().c_str()) == 1;
	const DWORD setup_fault_start = benchmark ? ProcessPageFaults() : 0;
	const auto setup_start = std::chrono::steady_clock::now();
	taskbar_created_message = RegisterWindowMessageW(L"TaskbarCreated");
	WNDCLASSW preview {};
	preview.hInstance = app_instance;
	preview.lpfnWndProc = CurveWindowProc;
	preview.style = CS_DBLCLKS;
	preview.lpszClassName = L"LutCurvePreview";
	preview.hCursor = LoadCursorW(nullptr, IDC_ARROW);
	WNDCLASSW main {};
	main.hInstance = app_instance;
	main.lpfnWndProc = MainWindowProc;
	main.lpszClassName = L"LutWorkbenchPreview";
	main.hCursor = preview.hCursor;
	main.hIcon = LoadIconW(app_instance, MAKEINTRESOURCEW(IDI_LUT_ACTIVE));
	main.hbrBackground = ui::BackgroundBrush();
	if (!RegisterClassW(&preview) || !RegisterClassW(&main))
	{
		CloseHandle(mutex);
		return 1;
	}
	POINT cursor {};
	GetCursorPos(&cursor);
	HMONITOR monitor = MonitorFromPoint(cursor, MONITOR_DEFAULTTONEAREST);
	MONITORINFO monitor_info {};
	monitor_info.cbSize = sizeof(monitor_info);
	GetMonitorInfoW(monitor, &monitor_info);
	HWND probe =
		CreateWindowExW(0, L"STATIC", L"", WS_POPUP, cursor.x, cursor.y, 1, 1, nullptr, nullptr, app_instance, nullptr);
	if (probe)
	{
		dpi = static_cast<int>(GetDpiForWindow(probe));
		DestroyWindow(probe);
	}
	RECT rect { 0, 0, ScaleForDpi(CLIENT_WIDTH), ScaleForDpi(CLIENT_HEIGHT) };
	constexpr DWORD WINDOW_STYLE = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_CLIPCHILDREN;
	ui::AdjustThemedWindowRect(rect, static_cast<UINT>(dpi));
	const int width = rect.right - rect.left, height = rect.bottom - rect.top;
	const int x =
		monitor_info.rcWork.left + std::max(0L, (monitor_info.rcWork.right - monitor_info.rcWork.left - width) / 2);
	const int y =
		monitor_info.rcWork.top + std::max(0L, (monitor_info.rcWork.bottom - monitor_info.rcWork.top - height) / 2);
	main_window = CreateWindowExW(0, main.lpszClassName, L"MonitorMage - live contrast and HDR correction",
	                              WINDOW_STYLE, x, y, width, height, nullptr, nullptr, app_instance, nullptr);
	if (!main_window)
	{
		CloseHandle(mutex);
		return 1;
	}
	if (!render_only || !idle_report.empty())
	{
		foreground_hook =
			SetWinEventHook(EVENT_SYSTEM_FOREGROUND, EVENT_SYSTEM_FOREGROUND, nullptr, ForegroundChangedCallback, 0, 0,
		                    WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS);
		if (!foreground_hook)
		{
			profile_configuration.Enabled = false;
			profile_message = L"Foreground notifications unavailable. Application profiles are disabled.";
		}
		CheckForegroundProgram();
		if (foreground_program.FocusedProgram().empty())
			ApplyApplicationProfile(true);
		RefreshInterface();
	}
	if (!render_only)
		TurnOnCorrections();
	if (tray_startup_test || tray_benchmark)
	{
		const auto milliseconds = [](auto a, auto b)
		{
			return std::chrono::duration<double, std::milli>(b - a).count();
		};
		const double setup_ms = milliseconds(setup_start, std::chrono::steady_clock::now());
		PROCESS_MEMORY_COUNTERS_EX usage {};
		usage.cb = sizeof(usage);
		GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&usage), sizeof(usage));
		int result = 0;
		RECT client {};
		GetClientRect(main_window, &client);
		if (client.right != ScaleForDpi(CLIENT_WIDTH) || client.bottom != ScaleForDpi(CLIENT_HEIGHT))
			result = 11;
		if (editor_ready || GetWindow(main_window, GW_CHILD) || ui_font || hint_font || curve_scratch.ByteCount() ||
		    preset_library[preset_role::Look].size() != 2)
			result = 1;
		const auto original = CurrentPreset(preset_role::Look).current;
		const auto original_index = preset_index[preset_role::Look];
		// A profile change must load its look and update processing state with
		// no controls. All hardware/session operations are disabled here.
		profile_configuration.Enabled = true;
		profile_configuration.Rules = { { L"C:\\Tests\\TrayGame.exe", PresetFolder(preset_role::Look) / L"B.lutpreset",
			                              applications::Action::Look } };
		corrections_requested = true;
		OnForegroundProgramChanged(L"C:\\Tests\\TrayGame.exe");
		if (!application_context_active || contrast_position != -10 || editor_ready || GetWindow(main_window, GW_CHILD))
			result = 2;
		ToggleBypass();
		if (!IsLookBypassed() || editor_ready)
			result = 3;
		ToggleBypass();
		OnForegroundProgramChanged(L"C:\\Tests\\Desktop.exe");
		if (application_context_active || preset_index[preset_role::Look] != original_index ||
		    CurrentPreset(preset_role::Look).current != original || contrast_position != original.Contrast)
			result = 4;
		ToggleCorrections();
		if (corrections_requested || editor_ready)
			result = 8;
		ToggleCorrections();
		if (!corrections_requested || editor_ready)
			result = 9;
		if (tray_exit_test)
		{
			DestroyWindow(main_window);
			CloseHandle(mutex);
			CleanPresetUiTest();
			return result;
		}
		ShowProfiles(true);
		if (!profile_editor::ProfileEditorHandle() || editor_ready || GetWindow(main_window, GW_CHILD))
			result = 10;
		if (profile_editor::ProfileEditorHandle())
			DestroyWindow(profile_editor::ProfileEditorHandle());
		const auto open_start = std::chrono::steady_clock::now();
		CreateEditorIfNeeded();
		const double open_ms = milliseconds(open_start, std::chrono::steady_clock::now());
		const HWND first_canvas = curve_canvas;
		CreateEditorIfNeeded();
		if (!editor_ready || !curve_canvas || curve_canvas != first_canvas || !IsWindow(contrast_slider) ||
		    CurrentPreset(preset_role::Look).current != original || GetWindowTextLengthW(monitor_profile) == 0)
			result = 5;
		if (SendMessageW(contrast_slider, TBM_GETPOS, 0, 0) != original.Contrast ||
		    SelectedSourceIndex() != selected_source)
			result = 6;
		if (tray_benchmark)
		{
			std::ofstream out(tray_benchmark_report);
			out.imbue(std::locale::classic());
			out << "{\"tray_setup_ms\":" << setup_ms << ",\"first_editor_setup_ms\":" << open_ms
				<< ",\"tray_private_mib\":" << usage.PrivateUsage / 1048576.0
				<< ",\"tray_working_set_mib\":" << usage.WorkingSetSize / 1048576.0 << ",\"result\":" << result
				<< ",\"mode\":\"hidden fixture; no tray registration, hotkey registration or display changes; excludes OS loader and live helper startup\"}\n";
			if (!out)
				result = 7;
		}
		DestroyWindow(main_window);
		CloseHandle(mutex);
		CleanPresetUiTest();
		return result;
	}
	if (benchmark)
	{
		const auto now = []()
		{
			return std::chrono::steady_clock::now();
		};
		const auto elapsed_ms = [](auto a, auto b)
		{
			return std::chrono::duration<double, std::milli>(b - a).count();
		};
		const double setup_ms = elapsed_ms(setup_start, now());
		const DWORD setup_faults = ProcessPageFaults() - setup_fault_start;
		RECT bounds {};
		GetWindowRect(main_window, &bounds);
		HDC screen = GetDC(nullptr), memory = CreateCompatibleDC(screen);
		HBITMAP image = CreateCompatibleBitmap(screen, bounds.right - bounds.left, bounds.bottom - bounds.top);
		ReleaseDC(nullptr, screen);
		if (!memory || !image)
		{
			if (image)
				DeleteObject(image);
			if (memory)
				DeleteDC(memory);
			DestroyWindow(main_window);
			CloseHandle(mutex);
			CleanPresetUiTest();
			return 1;
		}
		const auto old = SelectObject(memory, image);
		const auto paint = [&]()
		{
			SendMessageW(main_window, WM_PRINT, reinterpret_cast<WPARAM>(memory),
			             PRF_NONCLIENT | PRF_CLIENT | PRF_CHILDREN | PRF_ERASEBKGND);
			GdiFlush();
		};
		const DWORD first_fault_start = ProcessPageFaults();
		auto start = now();
		paint();
		const double first_ms = elapsed_ms(start, now());
		const DWORD first_faults = ProcessPageFaults() - first_fault_start;
		const DWORD repaint_fault_start = ProcessPageFaults();
		start = now();
		for (int i = 0; i < 10; ++i)
			paint();
		const double repaint_ms = elapsed_ms(start, now()) / 10;
		const DWORD repaint_faults = ProcessPageFaults() - repaint_fault_start;
		const auto full_buffer_bytes = last_graph_paint_bytes;
		const auto dirty = GraphUpdateBounds(curve_canvas);
		const int dirty_saved = SaveDC(memory);
		IntersectClipRect(memory, dirty.left, dirty.top, dirty.right, dirty.bottom);
		const DWORD dirty_fault_start = ProcessPageFaults();
		start = now();
		for (int i = 0; i < 10; ++i)
		{
			SendMessageW(curve_canvas, WM_PRINTCLIENT, reinterpret_cast<WPARAM>(memory), PRF_CLIENT);
			GdiFlush();
		}
		const double dirty_ms = elapsed_ms(start, now()) / 10;
		const DWORD dirty_faults = ProcessPageFaults() - dirty_fault_start;
		const auto dirty_buffer_bytes = last_graph_paint_bytes;
		RestoreDC(memory, dirty_saved);
		PROCESS_MEMORY_COUNTERS_EX usage {};
		usage.cb = sizeof(usage);
		GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&usage), sizeof(usage));
		SelectObject(memory, old);
		DeleteObject(image);
		DeleteDC(memory);
		const auto tile_bytes = curve_scratch.ByteCount();
		SendMessageW(main_window, WM_SHOWWINDOW, FALSE, 0);
		PROCESS_MEMORY_COUNTERS_EX hidden {};
		hidden.cb = sizeof(hidden);
		GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&hidden), sizeof(hidden));
		start = now();
		DestroyWindow(main_window);
		const double shutdown_ms = elapsed_ms(start, now());
		start = now();
		const int helper = live::RecoverySelfTest(app_settings::ExecutablePath().c_str());
		const double helper_ms = elapsed_ms(start, now());
		CloseHandle(mutex);
		CleanPresetUiTest();
		std::ofstream report(benchmark_report);
		report.imbue(std::locale::classic());
		report
			<< "{\"editor_setup_ms\":" << setup_ms << ",\"first_paint_ms\":" << first_ms
			<< ",\"mean_repaint_ms\":" << repaint_ms << ",\"setup_page_faults\":" << setup_faults
			<< ",\"first_paint_page_faults\":" << first_faults << ",\"repeated_paint_page_faults\":" << repaint_faults
			<< ",\"paint_count\":10,\"fault_counter\":\"process total; does not distinguish hard from soft faults\""
			<< ",\"full_graph_buffer_mib\":" << full_buffer_bytes / 1048576.0
			<< ",\"plot_update_buffer_mib\":" << dirty_buffer_bytes / 1048576.0
			<< ",\"plot_update_mean_ms\":" << dirty_ms << ",\"plot_update_page_faults\":" << dirty_faults
			<< ",\"graph_surface_bits\":32"
			<< ",\"editor_shutdown_ms\":" << shutdown_ms << ",\"probe_helper_roundtrip_ms\":" << helper_ms
			<< ",\"helper_ok\":" << (helper == 0 ? "true" : "false")
			<< ",\"peak_working_set_mib\":" << usage.PeakWorkingSetSize / 1048576.0
			<< ",\"private_commit_mib\":" << usage.PrivateUsage / 1048576.0
			<< ",\"curve_tile_mib\":" << tile_bytes / 1048576.0
			<< ",\"after_hide_private_commit_mib\":" << hidden.PrivateUsage / 1048576.0 << ",\"dpi\":" << dpi
			<< ",\"mode\":\"hidden fixture editor; helper probe never reads or writes a display LUT; excludes OS loader, real user library and actual LUT restoration\"}\n";
		return report && helper == 0 ? 0 : 1;
	}
	if (preset_ui_test)
	{
		const int result = surface_ui_test   ? RunSurfaceUiTest()
		                   : effect_ui_test  ? RunEffectUiTest()
		                   : profile_ui_test ? RunProfileUiTest()
		                                     : RunPresetUiTest();
		DestroyWindow(main_window);
		CloseHandle(mutex);
		CleanPresetUiTest();
		return result;
	}
	if (render)
	{
		if (render_mode == L"--render-title-menu")
		{
			const bool rendered = ui::PreviewCaptionMenu(main_window, [&](HWND target)
			                                             { return RenderWindowToFile(render_file.c_str(), target); });
			DestroyWindow(main_window);
			CloseHandle(mutex);
			return rendered ? 0 : 1;
		}
		if (render_tray)
		{
			const bool rendered =
				ui::PreviewPopup(main_window, IconForState(SM_CXICON), TrayMenuItems(),
			                     [&](HWND target) { return RenderWindowToFile(render_file.c_str(), target); });
			DestroyWindow(main_window);
			CloseHandle(mutex);
			return rendered ? 0 : 1;
		}
		HWND target = main_window;
		if (render_mode == L"--render-saturation")
		{
			selected_effect = effect::Saturation;
			CurrentPreset(preset_role::Look).current.SaturationEnabled = true;
			CurrentPreset(preset_role::Look).current.Saturation = 50;
			corrections_requested = true;
			RefreshInterface();
		}
		if (render_mode == L"--render-mixer")
		{
			selected_effect = effect::ChannelMixer;
			CurrentPreset(preset_role::Look).current.MonochromeEnabled = true;
			CurrentPreset(preset_role::Look).current.Monochrome = { 100, 20, 0 };
			corrections_requested = true;
			RefreshInterface();
		}
		if (render_mode == L"--render-matrix-preview")
		{
			matrix_preview::ShowPreviewWindow(main_window, true);
			target = matrix_preview::PreviewWindowHandle();
			if (!target)
			{
				DestroyWindow(main_window);
				CloseHandle(mutex);
				return 1;
			}
		}
		if (render_application)
		{
			presets::Data data;
			data.Contrast = 15;
			preset_library[preset_role::Look].push_back({ L"C:\\Examples\\Gentle contrast.lutpreset", data, data });
			preset_index[preset_role::Look] = 1;
			application_context_active = true;
			profile_configuration.Enabled = true;
			foreground_program.UpdateFocusedProgram(L"C:\\Games\\Example game.exe");
			profile_action = applications::Action::PauseHdr;
			LoadPresetControls();
		}
		if (render_profiles || render_mode == L"--render-running-programs")
		{
			const auto data = preset_library[preset_role::Look][0].current;
			preset_library[preset_role::Look].push_back({ L"C:\\Examples\\Neutral.lutpreset", data, data });
			preset_library[preset_role::Look].push_back({ L"C:\\Examples\\Gentle contrast.lutpreset", data, data });
			profile_configuration.Rules = { { L"C:\\Games\\Example game.exe", preset_library[preset_role::Look][2].path,
				                              applications::Action::Look },
				                            { L"C:\\Video\\HDR player.exe", {}, applications::Action::PauseHdr } };
			ShowProfiles(true);
			target = profile_editor::ProfileEditorHandle();
			if (!target)
			{
				DestroyWindow(main_window);
				CloseHandle(mutex);
				return 1;
			}
			SendMessageW(GetDlgItem(target, 203), LB_SETCURSEL, 1, 0);
			SendMessageW(target, WM_COMMAND, MAKEWPARAM(203, LBN_SELCHANGE), 0);
		}
		if (render_mode == L"--render-running-programs")
			profile_editor::PreviewPrograms(
				{ { L"C:\\Games\\Example game.exe", L"Example game", SIZE_T { 2400 } * 1048576, true, true },
			      { L"C:\\Video\\vlc.exe", L"VLC media player", SIZE_T { 420 } * 1048576, true, true },
			      { L"C:\\Browser\\browser.exe", L"Web browser", SIZE_T { 1300 } * 1048576, true, true } });
		if (render_hdr || render_application)
		{
			selected_effect = effect::HdrGamma;
			RefreshPresetChoice();
			RefreshInterface();
		}
		if (render_mode == L"--render-balance")
		{
			selected_effect = effect::ColorBalance;
			CurrentPreset(preset_role::Look).current.Adjustments.Balance = { 100, 100, 100, 20, 0 };
			RefreshInterface();
		}
		if (render_mode == L"--render-curves")
		{
			selected_effect = effect::CustomCurves;
			auto& curves = CurrentPreset(preset_role::Look).current.Adjustments.Curves;
			curves[0] = { 0, .09, .2, .33, .5, .67, .8, .91, 1 };
			curves[3] = { 0, .06, .15, .26, .4, .56, .72, .87, 1 };
			RefreshInterface();
		}
		if (render_mode == L"--render-error")
		{
			live_message = ui::messages::FriendlyError("Driver rejected the correction");
			live_error = true;
			RefreshInterface();
		}
		if (render_mode == L"--render-carryover")
		{
			live_message =
				L"The previous correction carried into this display mode. Restore Windows calibration before re-enabling";
			live_error = true;
			RefreshInterface();
		}
		if (render_mode == L"--render-ready")
		{
			TurnOnCorrections();
		}
		if (render_mode == L"--render-settings")
		{
			settings_page = true;
			LayoutEditorControls();
		}
		if (render_mode == L"--render-color-filters")
		{
			settings_page = false;
			selected_effect = effect::ColorFilters;
			LayoutEditorControls();
		}
		if (render_mode == L"--render-night-light")
		{
			settings_page = false;
			selected_effect = effect::NightLight;
			night_config.Enabled = true;
			UpdateNightLightSchedule();
			LayoutEditorControls();
		}
		if (render_mode == L"--render-font-segoe")
		{
			ui::SetFontFamily(L"Segoe UI");
			LayoutEditorControls();
		}
		if (render_mode == L"--render-unsaved")
		{
			target = unsaved::PreviewDialog(main_window);
			if (!target)
			{
				DestroyWindow(main_window);
				CloseHandle(mutex);
				return 1;
			}
		}
		if (render_mode == L"--render-small")
		{
			selected_effect = effect::ColorBalance;
			RefreshInterface();
			SetWindowPos(main_window, nullptr, 0, 0, ScaleForDpi(480), ScaleForDpi(360),
			             SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
			ui::RevealControl(main_window, adjustment_sliders[4]);
		}
		if (render_mode == L"--render-small-profiles")
		{
			SetWindowPos(target, nullptr, 0, 0, ScaleForDpi(300), ScaleForDpi(340),
			             SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
			ui::RevealControl(target, GetDlgItem(target, 213));
		}
		if (target == main_window)
		{
			ui::SetCaptionSettingsButton(main_window, ID_SETTINGS_PAGE, settings_page);
			ui::SetIconButton(power_button, ui::Glyph::Power, corrections_requested, true);
		}
		const bool rendered = RenderWindowToFile(render_file.c_str(), target);
		DestroyWindow(main_window);
		CloseHandle(mutex);
		return rendered ? 0 : 1;
	}
	if (!idle_report.empty())
	{
		idle_cpu_start = ProcessCpuTime();
		idle_start = GetTickCount64();
		idle_fault_start = ProcessPageFaults();
		if (!SetTimer(main_window, TIMER_IDLE, 3000, nullptr))
		{
			DestroyWindow(main_window);
			CloseHandle(mutex);
			return 1;
		}
	}
	else if (render_only || !start_minimized || !tray_installed)
	{
		ShowWindow(main_window, show);
		UpdateWindow(main_window);
	}
	MSG message {};
	int result = 0;
	while ((result = matrix_session.WaitForMessageOrWorkerExit(message)) > 0)
	{
		if (matrix_preview::HandleDialogMessage(message))
			continue;
		if (NavigateTab(message))
			continue;
		if (GetFocus() != shortcut_control && message.message == WM_SYSKEYDOWN &&
		    (message.wParam == L'P' || message.wParam == L'S') && (message.lParam & (1L << 29)) &&
		    GetAncestor(message.hwnd, GA_ROOT) == main_window)
		{
			if (!(message.lParam & (1L << 30)))
				SendMessageW(main_window, WM_COMMAND, message.wParam == L'P' ? ID_TEST_CONTRAST : ID_SETTINGS_PAGE, 0);
			continue;
		}
		if (profile_editor::ProfileEditorHandle() && IsDialogMessageW(profile_editor::ProfileEditorHandle(), &message))
		{
			ui::RevealControl(profile_editor::ProfileEditorHandle(), GetFocus());
			continue;
		}
		if (!IsDialogMessageW(main_window, &message))
		{
			TranslateMessage(&message);
			DispatchMessageW(&message);
		}
	}
	CloseHandle(mutex);
	return result < 0 ? 1 : static_cast<int>(message.wParam);
}
