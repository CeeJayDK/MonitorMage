// Loading, saving and choosing look and correction presets.
#include "app_state.h"

namespace app
{
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

static void AddPresetToLibrary(int preset_role, const std::filesystem::path& path)
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
}
