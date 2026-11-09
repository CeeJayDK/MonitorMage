// Small lookups and control helpers used by every part of the main program.
#include "app_state.h"

namespace app
{
void EnsureCommonControls()
{
	static const bool initialized = []()
	{
		INITCOMMONCONTROLSEX common { sizeof(common), ICC_BAR_CLASSES | ICC_HOTKEY_CLASS };
		return InitCommonControlsEx(&common) != FALSE;
	}();
	(void)initialized;
}

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

void PlaceControl(HWND control, int x, int y, int width, int height, bool body)
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

std::wstring FormatDecimal(double value, int precision)
{
	std::wostringstream text;
	text.imbue(std::locale::classic());
	text << std::fixed << std::setprecision(precision) << value;
	return text.str();
}
}
