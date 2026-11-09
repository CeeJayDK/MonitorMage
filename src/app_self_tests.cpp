// Self-tests that drive the real interface. They run only with a --self-test flag.
#include "app_state.h"

namespace app
{
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

static LRESULT CALLBACK CountSurfaceChanges(HWND hwnd, UINT message, WPARAM w_param, LPARAM l_param, UINT_PTR,
                                            DWORD_PTR data)
{
	if (message == WM_SETTEXT || message == WM_ENABLE || message == BM_SETCHECK || message == CB_RESETCONTENT ||
	    message == LB_SETCURSEL || message == LB_SETITEMHEIGHT)
		++*reinterpret_cast<int*>(data);
	return DefSubclassProc(hwnd, message, w_param, l_param);
}

static int RunTabUiTest()
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

static bool PartialGraphMatchesFull()
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

static bool ClippedListMatchesFull()
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

// ---- The checks run by RunSurfaceUiTest. Each returns 0 on success, or a number saying which step failed. ----

static int TestHelpTexts()
{
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
	return 0;
}

static int TestWindowsEffectsAndNightLight()
{
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
	return 0;
}

static int TestMatrixEffects()
{
	const int saved_selected = selected_effect;
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
	return 0;
}

static int TestProfileEditorHelpTexts()
{
	ShowProfiles(true);
	const auto help_editor = profile_editor::ProfileEditorHandle();
	if (!help_editor)
		return 240;
	for (int id : { 201, 203, 204, 205, 206, 207, 208, 209, 210, 211, 212, 213, 214, 215, 216, 217, 218, 219, 220 })
		if (!*ui::HelpTextForRegion(GetDlgItem(help_editor, id)) ||
		    wcslen(ui::HelpTextForRegion(GetDlgItem(help_editor, id))) > 48)
			return 241;
	DestroyWindow(help_editor);
	return 0;
}

static int TestWindowSizeRepair()
{
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
	return 0;
}

static int TestEffectPageLayout()
{
	const int saved_selected = selected_effect;
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
	return 0;
}

static int TestColorFilterPage()
{
	const int saved_selected = selected_effect;
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
	return 0;
}

static int TestPowerAndDisplayRetries()
{
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
	return 0;
}

static int TestCaptionTooltips()
{
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
	return 0;
}

static int TestOwnerDrawIsolation()
{
	// Owner-drawn links and status labels may share a DC during native painting.
	// Neither their colors nor their pixels may escape the supplied item bounds.
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
	return 0;
}

static int TestStatusMessages()
{
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
	return 0;
}

static int TestCaptionButtons()
{
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
	return 0;
}

static int TestShortcutCapture()
{
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
	return 0;
}

static int TestHdrControls()
{
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
	return 0;
}

static int TestMonitorAndSidebarLayout()
{
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
	return 0;
}

static int TestSliderRowLayout()
{
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
	return 0;
}

static int TestSliderInputStability()
{
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
	return 0;
}

static int TestComboPopupPainting(HDC screen, HDC buffer)
{
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
	return 0;
}

static int TestToggleAndInputPainting(HDC buffer)
{
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
	return 0;
}

static int TestRgbCurveDrawing(HDC buffer, bool& rgb_ok)
{
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
	return 0;
}

static int TestNativeControlPainting()
{
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
	if (const int failure = TestComboPopupPainting(screen, buffer))
		return failure;
	if (const int failure = TestToggleAndInputPainting(buffer))
		return failure;
	bool rgb_ok = true;
	if (const int failure = TestRgbCurveDrawing(buffer, rgb_ok))
		return failure;
	SelectObject(buffer, old);
	DeleteObject(bitmap);
	DeleteDC(buffer);
	ReleaseDC(main_window, screen);
	if (!handled || result != CDRF_SKIPDEFAULT || pixel != ui::THEME_SURFACE)
		return 99;
	if (!rgb_ok)
		return 162;
	return 0;
}

static int TestPopupsAndCaptionMenu()
{
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
	return 0;
}

static int TestProfileEditorWindow()
{
	RECT client {}, outer {};
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
	return 0;
}

static int TestFontChange()
{
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
	return 0;
}

int RunSurfaceUiTest()
{
	if (!ClippedListMatchesFull())
		return 255;
	if (!PartialGraphMatchesFull())
		return 260;
	using Check = int (*)();
	const Check checks[] { TestHelpTexts,
		                   TestWindowsEffectsAndNightLight,
		                   TestMatrixEffects,
		                   TestProfileEditorHelpTexts,
		                   TestWindowSizeRepair,
		                   TestEffectPageLayout,
		                   RunTabUiTest,
		                   TestColorFilterPage,
		                   TestPowerAndDisplayRetries,
		                   TestCaptionTooltips,
		                   TestOwnerDrawIsolation,
		                   TestStatusMessages,
		                   TestCaptionButtons,
		                   TestShortcutCapture,
		                   TestHdrControls,
		                   TestMonitorAndSidebarLayout,
		                   TestSliderRowLayout,
		                   TestSliderInputStability,
		                   TestNativeControlPainting,
		                   TestPopupsAndCaptionMenu,
		                   TestProfileEditorWindow,
		                   TestFontChange };
	for (const Check check : checks)
		if (const int failure = check())
			return failure;
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
