// Application profiles: switching looks when the foreground program changes.
#include "app_state.h"

namespace app
{
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

void CheckForegroundProgram(HWND recent)
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

void ShowProfiles(bool hidden)
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
}
