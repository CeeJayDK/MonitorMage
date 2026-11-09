// The notification-area icon and its menu.
#include "app_state.h"

namespace app
{
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
}
