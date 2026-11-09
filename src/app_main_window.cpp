// Creating the main window's controls and handling its messages.
#include "app_state.h"

namespace app
{
static LRESULT CALLBACK ProfileLinkProc(HWND hwnd, UINT message, WPARAM w_param, LPARAM l_param, UINT_PTR, DWORD_PTR)
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

static LRESULT CALLBACK MonitorLabelPaintProc(HWND hwnd, UINT message, WPARAM w_param, LPARAM l_param, UINT_PTR,
                                              DWORD_PTR)
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

bool RenderWindowToFile(const wchar_t* path, HWND target)
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

static HWND CreateChildControl(const wchar_t* class_name, const wchar_t* text, DWORD style, int id,
                               HWND parent = nullptr)
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
	diagnostics_control =
		CreateChildControl(L"BUTTON", L"Save diagnostic logs", WS_TABSTOP | BS_AUTOCHECKBOX, ID_SAVE_DIAGNOSTICS);
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
			                                           { diagnostics_control, L"Keep log files in the temp folder" },
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

static HBRUSH SidebarBrush()
{
	static const HBRUSH sidebar = CreateSolidBrush(ui::THEME_SIDEBAR);
	return sidebar;
}

static void PaintMainWindowBackground(HWND hwnd, HDC dc)
{
	RECT rect {};
	GetClientRect(hwnd, &rect);
	FillRect(dc, &rect, ui::BackgroundBrush());
	rect.right = std::max(0L, ScaleForDpi(SIDEBAR_WIDTH) - ui::ViewportOffset(hwnd).x);
	FillRect(dc, &rect, SidebarBrush());
}

// The labels and lists on the dark sidebar ask for the sidebar's colors instead of the normal ones.
static bool IsSidebarColorRequest(UINT message, HWND control)
{
	return (message == WM_CTLCOLORSTATIC &&
	        (control == corrections_heading || control == effects_label || control == order_hint)) ||
	       (message == WM_CTLCOLORLISTBOX &&
	        (control == effect_list || control == correction_list || control == windows_effect_list));
}

static void OnCreate(HWND hwnd)
{
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
}

static void DrawOrderHint(const DRAWITEMSTRUCT& item)
{
	HBRUSH brush = CreateSolidBrush(ui::THEME_SIDEBAR);
	FillRect(item.hDC, &item.rcItem, brush);
	DeleteObject(brush);
	const auto previous = SelectObject(item.hDC, reinterpret_cast<HFONT>(SendMessageW(order_hint, WM_GETFONT, 0, 0)));
	SetBkMode(item.hDC, TRANSPARENT);
	SetTextColor(item.hDC, ui::THEME_MUTED);
	wchar_t text[128] {};
	GetWindowTextW(order_hint, text, 128);
	RECT area = item.rcItem;
	DrawTextW(item.hDC, text, -1, &area, DT_SINGLELINE | DT_CENTER | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX);
	SelectObject(item.hDC, previous);
}

// Windows asks for a tooltip's text just before showing it.
static void FillMonitorTooltip(NMTTDISPINFOW* tip)
{
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
		text = button == hdr_button     ? std::wstring(ui::HdrTooltip(hdr_state.on, hdr_state.available))
		       : button == power_button ? std::wstring(corrections_requested ? L"Turn off (Alt+P)" : L"Turn on (Alt+P)")
		       : button == monitor_settings_button ? std::wstring(ui::wording::LABEL_SETTINGS)
		                                           : std::wstring(ui::wording::LABEL_APPLICATION_PROFILES);
	tip->lpszText = text.data();
}

static void UpdateSliderDragState(HWND hwnd, WPARAM w_param)
{
	if (LOWORD(w_param) == TB_THUMBTRACK && !slider_dragging)
	{
		look_dirty_before_drag = CurrentPreset(preset_role::Look).IsDirty();
		slider_dragging = true;
		if (selected_effect != effect::NightLight)
			KillTimer(hwnd, TIMER_PRESET);
	}
	if (LOWORD(w_param) == TB_THUMBPOSITION || LOWORD(w_param) == TB_ENDTRACK)
		slider_dragging = false;
}

// Copies a moved slider's position into the setting it controls.
static void StoreAdjustmentSliderValue(WPARAM w_param, LPARAM l_param)
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

static void OnSliderMoved(HWND hwnd, WPARAM w_param, LPARAM l_param)
{
	UpdateSliderDragState(hwnd, w_param);
	StoreAdjustmentSliderValue(w_param, l_param);
	contrast_position = static_cast<int>(SendMessageW(contrast_slider, TBM_GETPOS, 0, 0));
	pivot_position = static_cast<int>(SendMessageW(pivot_slider, TBM_GETPOS, 0, 0));
	white_position = static_cast<int>(SendMessageW(white_slider, TBM_GETPOS, 0, 0));
	if (reinterpret_cast<HWND>(l_param) == contrast_slider || reinterpret_cast<HWND>(l_param) == pivot_slider)
		OnPresetEdited(preset_role::Look);
	RefreshInterface();
	if (corrections_requested)
		SetTimer(hwnd, TIMER_APPLY, 100, nullptr);
}

// ---- One function for each button or list that needs more than a line or two ----

static void ChangeNightOption(int id)
{
	if (id == ID_NIGHT_SCHEDULE)
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
}

static void ChangeNightTime(int id, HWND edit)
{
	if (is_closing)
		return;
	wchar_t text[16] {};
	GetWindowTextW(edit, text, 16);
	const int value = night::ParseTime(text);
	const bool start = id == ID_NIGHT_START;
	if (value < 0 || value == (start ? night_config.End : night_config.Start))
	{
		night_message = L"Use HH:MM, with different start and end times.";
		RefreshInterface();
		return;
	}
	if (value == (start ? night_config.Start : night_config.End))
		return;
	(start ? night_config.Start : night_config.End) = value;
	night_message.clear();
	if (!render_only)
		PersistPresetChoices();
	ApplyLiveCorrections();
	RefreshInterface();
}

static void OpenSaturationReport()
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
}

static void ToggleSettingsPage()
{
	settings_page = !settings_page;
	RefreshColorFilters();
	LayoutEditorControls();
	SetFocus(settings_page ? font_choice : EffectListForRole(selected_effect));
}

static void ChooseColorFilter()
{
	const auto choice = SendMessageW(filter_choice, CB_GETCURSEL, 0, 0);
	if (choice >= 0)
		ChangeWindowsFilter(filter_state.on, static_cast<DWORD>(choice), true);
}

static void OpenWindowsColorFilterPage()
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
}

static void ChangeStartWithWindows()
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
}

static void ChangeStartMinimized()
{
	const bool previous = start_minimized;
	start_minimized = SendMessageW(start_minimized_control, BM_GETCHECK, 0, 0) == BST_CHECKED;
	if (!PersistPresetChoices())
		start_minimized = previous;
	RefreshInterface();
}

static void ChangeSaveDiagnostics()
{
	const bool previous = save_diagnostics;
	save_diagnostics = SendMessageW(diagnostics_control, BM_GETCHECK, 0, 0) == BST_CHECKED;
	if (!PersistPresetChoices())
		save_diagnostics = previous;
	live::SetDiagnosticsEnabled(save_diagnostics);
	RefreshInterface();
}

static void ChangeFontFamily()
{
	wchar_t name[80] {};
	GetWindowTextW(font_choice, name, 80);
	ui::SetFontFamily(name);
	PersistPresetChoices();
	LayoutEditorControls();
	profile_editor::RefreshAppearance();
}

static void RescanPresets()
{
	DiscoverPresets(SelectedPresetRole());
	RefreshPresetChoice();
	RefreshInterface();
}

static void ChangeEditedCurveChannel()
{
	editing_channel = static_cast<int>(SendMessageW(curve_channel, CB_GETCURSEL, 0, 0));
	InvalidateGraph();
}

static void OpenWebsite(HWND hwnd)
{
	if (!render_only)
		ShellExecuteW(hwnd, L"open", L"https://CeeJay.dk", nullptr, nullptr, SW_SHOWNORMAL);
}

static void RevertPreset()
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
}

static void ChoosePreset()
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
			return;
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
}

static void SavePreset()
{
	if (CurrentPreset(SelectedPresetRole()).path.empty())
		PresetFileDialog(true);
	else
	{
		SaveExistingPreset(SelectedPresetRole());
		RefreshPresetChoice();
		RefreshInterface();
	}
}

static void OpenPresetFolder()
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
}

static void ChangeAutosave()
{
	autosave_enabled = SendMessageW(autosave_control, BM_GETCHECK, 0, 0) == BST_CHECKED;
	FlushPresetAutosave();
	PersistPresetChoices();
	RefreshPresetChoice();
	RefreshInterface();
}

static void ChangeShortcut()
{
	const WORD requested = static_cast<WORD>(SendMessageW(shortcut_control, HKM_GETHOTKEY, 0, 0));
	if (render_only)
		return;
	if (BindHotkey(requested))
	{
		preferred_shortcut = requested;
		hotkey_message =
			PersistPresetChoices() ? L"" : L"This shortcut works now, but could not be saved for next time.";
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
}

static void ChangeDisplaySource()
{
	const auto source_index = SelectedSourceIndex();
	hdr_state = source_index >= 0 && static_cast<size_t>(source_index) < display_sources.size()
	                ? hdr::ReadHdrState(display_sources[source_index].name)
	                : hdr::State {};
	ReconcileLiveCorrections();
}

static void ChangeHdrFixOption()
{
	CurrentPreset(preset_role::Correction).current.ApplyInSdr =
		SendMessageW(hdr_fix_control, BM_GETCHECK, 0, 0) == BST_CHECKED;
	OnPresetEdited(preset_role::Correction);
	ApplyLiveCorrections();
	RefreshInterface();
}

static void SelectEffectFromList(int id)
{
	const auto list = id == ID_EFFECT_LIST                 ? effect_list
	                  : id == ID_WINDOWS_CORRECTION_LAYERS ? windows_effect_list
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

static void ChangeGammaChoice(int id)
{
	gamma_position = id == ID_GAMMA ? 0 : 1;
	OnPresetEdited(preset_role::Correction);
	ApplyLiveCorrections();
	RefreshInterface();
}

static void ResetSelectedEffect()
{
	if (selected_effect == effect::NightLight)
	{
		night_config.Strength = 35;
		night_config.Fade = 10;
		if (!render_only)
			PersistPresetChoices();
		ApplyLiveCorrections();
		RefreshInterface();
		return;
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

// Windows sends WM_COMMAND when a button is clicked or a list changes. "id" says which control it was.
static void OnCommand(HWND hwnd, WPARAM w_param, LPARAM l_param)
{
	const int id = LOWORD(w_param);
	const int notification = HIWORD(w_param);
	switch (id)
	{
		case ID_NIGHT_DISPLAY:
			if (notification == CBN_SELCHANGE)
				RefreshInterface();
			break;
		case ID_NIGHT_SCHEDULE:
		case ID_NIGHT_INCLUDE:
			ChangeNightOption(id);
			break;
		case ID_NIGHT_START:
		case ID_NIGHT_END:
			if (notification == EN_KILLFOCUS)
				ChangeNightTime(id, reinterpret_cast<HWND>(l_param));
			break;
		case ID_SATURATION_REPORT:
			OpenSaturationReport();
			break;
		case ID_OUTPUT_INFO:
			SendMessageW(monitor_tips, TTM_POPUP, 0, 0);
			break;
		case ID_SELECTED_EFFECT_TOGGLE:
			if (notification == BN_CLICKED)
				ToggleEffect(selected_effect);
			break;
		case ID_TOGGLE_WINDOWS_HDR:
			ToggleWindowsHdrMode();
			break;
		case ID_IMPORT_1D:
		case ID_EXPORT_1D:
			ImportOrExportLut(id == ID_IMPORT_1D);
			break;
		case ID_SETTINGS_PAGE:
			ToggleSettingsPage();
			break;
		case ID_COLOR_FILTER_CHOICE:
			if (notification == CBN_SELCHANGE)
				ChooseColorFilter();
			break;
		case ID_COLOR_FILTER_SETTINGS:
			OpenWindowsColorFilterPage();
			break;
		case ID_START_WITH_WINDOWS:
			ChangeStartWithWindows();
			break;
		case ID_START_MINIMIZED:
			if (notification == BN_CLICKED)
				ChangeStartMinimized();
			break;
		case ID_SAVE_DIAGNOSTICS:
			if (notification == BN_CLICKED)
				ChangeSaveDiagnostics();
			break;
		case ID_FONT_CHOICE:
			if (notification == CBN_SELCHANGE)
				ChangeFontFamily();
			break;
		case ID_CURVE_CHANNEL:
			if (notification == CBN_SELCHANGE)
				ChangeEditedCurveChannel();
			break;
		case ID_APPLICATION_PROFILES:
			ShowProfiles();
			break;
		case ID_MONITOR_PROFILE:
			if (notification == STN_CLICKED)
				ShowProfiles(render_only);
			break;
		case ID_WEBSITE_LINK:
			if (notification == STN_CLICKED)
				OpenWebsite(hwnd);
			break;
		case ID_REVERT_PRESET:
			RevertPreset();
			break;
		case ID_PRESET_CHOICE:
			if (notification == CBN_DROPDOWN)
				RescanPresets();
			else if (notification == CBN_SELCHANGE)
				ChoosePreset();
			break;
		case ID_SAVE_PRESET:
			SavePreset();
			break;
		case ID_SAVE_PRESET_AS:
			PresetFileDialog(true);
			break;
		case ID_OPEN_PRESET:
			OpenPresetFolder();
			break;
		case ID_AUTOSAVE:
			if (notification == BN_CLICKED)
				ChangeAutosave();
			break;
		case ID_SET_SHORTCUT:
			if (notification == BN_CLICKED)
				ChangeShortcut();
			break;
		case ID_EXIT:
			if (ConfirmExit())
				DestroyWindow(hwnd);
			break;
		case ID_SOURCE_CHOICE:
			if (notification == CBN_SELCHANGE)
				ChangeDisplaySource();
			break;
		case ID_TEST_CONTRAST:
			if (notification == BN_CLICKED)
				ToggleCorrections();
			break;
		case ID_BYPASS:
			if (notification == BN_CLICKED)
				ToggleBypass();
			break;
		case ID_HDR_FIX:
			if (notification == BN_CLICKED)
				ChangeHdrFixOption();
			break;
		case ID_EFFECT_LIST:
		case ID_CORRECTION_LAYERS:
		case ID_WINDOWS_CORRECTION_LAYERS:
			if (notification == LBN_SELCHANGE)
				SelectEffectFromList(id);
			break;
		case ID_GAMMA:
		case ID_GAMMA_OTHER:
			if (notification == BN_CLICKED)
				ChangeGammaChoice(id);
			break;
		case ID_EXPORT:
			ExportCurve();
			break;
		case ID_RESET:
			if (selected_effect < effect::FirstWindows || selected_effect == effect::NightLight ||
			    selected_effect >= effect::Saturation)
				ResetSelectedEffect();
			break;
	}
}

// ---- Timers ----

static void OnColorFilterTimer(HWND hwnd)
{
	try
	{
		if (!filter_controller.Poll())
			return;
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
}

static void OnHdrTimer(HWND hwnd)
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
}

static void OnForegroundTimer(HWND hwnd)
{
	KillTimer(hwnd, TIMER_FOREGROUND);
	CheckForegroundProgram(pending_foreground);
	SyncColorMatrix(true);
	ScheduleColorMatrixRecovery();
	pending_foreground = nullptr;
}

static void OnMatrixRecoveryTimer(HWND hwnd)
{
	KillTimer(hwnd, TIMER_MATRIX_RECOVERY);
	const auto state = matrix_session.CurrentState();
	if (!IsColorMatrixNeeded() || state == matrix_effect::State::OtherTool ||
	    state == matrix_effect::State::ExternalChange)
		return;
	SyncColorMatrix(true);
	if (const auto delay = matrix_recovery.NextDelay())
		SetTimer(hwnd, TIMER_MATRIX_RECOVERY, delay, nullptr);
}

static void OnIdleTimer(HWND hwnd)
{
	KillTimer(hwnd, TIMER_IDLE);
	const double cpu_ms = (ProcessCpuTime() - idle_cpu_start) / 10000.0;
	PROCESS_MEMORY_COUNTERS_EX memory {};
	memory.cb = sizeof(memory);
	const bool measured = GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&memory),
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
}

static void OnDisplayTimer(HWND hwnd)
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

static void OnTimer(HWND hwnd, WPARAM timer)
{
	switch (timer)
	{
		case TIMER_NIGHT:
			KillTimer(hwnd, TIMER_NIGHT);
			ApplyLiveCorrections();
			break;
		case TIMER_COLOR_FILTER:
			OnColorFilterTimer(hwnd);
			break;
		case TIMER_HDR:
			OnHdrTimer(hwnd);
			break;
		case TIMER_FOREGROUND:
			OnForegroundTimer(hwnd);
			break;
		case TIMER_MATRIX_RECOVERY:
			OnMatrixRecoveryTimer(hwnd);
			break;
		case TIMER_PRESET:
			FlushPresetAutosave();
			RefreshPresetChoice();
			RefreshInterface();
			break;
		case TIMER_IDLE:
			OnIdleTimer(hwnd);
			break;
		case TIMER_APPLY:
			KillTimer(hwnd, TIMER_APPLY);
			ApplyLiveCorrections();
			break;
		case TIMER_DISPLAY:
			OnDisplayTimer(hwnd);
			break;
	}
}

// Corrections are turned off before the computer sleeps and restored after it wakes.
static void OnPowerEvent(HWND hwnd, WPARAM w_param)
{
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
}

static void OnDestroy(HWND hwnd)
{
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
}

LRESULT CALLBACK MainWindowProc(HWND hwnd, UINT message, WPARAM w_param, LPARAM l_param)
{
	if (message == WM_ERASEBKGND || message == WM_PRINTCLIENT)
	{
		PaintMainWindowBackground(hwnd, reinterpret_cast<HDC>(w_param));
		return 1;
	}
	if (message == WM_PAINT)
	{
		PAINTSTRUCT paint {};
		const auto dc = BeginPaint(hwnd, &paint);
		PaintMainWindowBackground(hwnd, dc);
		EndPaint(hwnd, &paint);
		return 0;
	}
	if (IsSidebarColorRequest(message, reinterpret_cast<HWND>(l_param)))
	{
		const auto dc = reinterpret_cast<HDC>(w_param);
		SetBkColor(dc, ui::THEME_SIDEBAR);
		SetTextColor(dc, ui::THEME_TEXT);
		return reinterpret_cast<LRESULT>(SidebarBrush());
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
			OnCreate(hwnd);
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
				DrawOrderHint(*reinterpret_cast<DRAWITEMSTRUCT*>(l_param));
				return TRUE;
			}
			if (effects::Draw(*reinterpret_cast<DRAWITEMSTRUCT*>(l_param)))
				return TRUE;
			break;
		case WM_NOTIFY:
			if (reinterpret_cast<NMHDR*>(l_param)->hwndFrom == monitor_tips &&
			    reinterpret_cast<NMHDR*>(l_param)->code == TTN_GETDISPINFOW)
			{
				FillMonitorTooltip(reinterpret_cast<NMTTDISPINFOW*>(l_param));
				return 0;
			}
			break;
		case WM_HSCROLL:
			OnSliderMoved(hwnd, w_param, l_param);
			return 0;
		case WM_COMMAND:
			OnCommand(hwnd, w_param, l_param);
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
			OnTimer(hwnd, w_param);
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
			OnPowerEvent(hwnd, w_param);
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
			OnDestroy(hwnd);
			return 0;
	}
	return DefWindowProcW(hwnd, message, w_param, l_param);
}
}
