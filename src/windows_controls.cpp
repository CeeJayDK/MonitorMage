#include "windows_controls.h"
#include <shellapi.h>
#include <stdexcept>
namespace windows_controls
{
namespace
{
constexpr auto COLOR_FILTER_REGISTRY_KEY = L"Software\\Microsoft\\ColorFiltering";
struct Value
{
	DWORD data = 0;
	bool present = false;
};

Value ReadRegistryDword(HKEY key, const wchar_t* name)
{
	Value value;
	DWORD type = 0, size = sizeof(value.data);
	const auto result = RegQueryValueExW(key, name, nullptr, &type, reinterpret_cast<BYTE*>(&value.data), &size);
	if (result == ERROR_FILE_NOT_FOUND)
		return {};
	if (result != ERROR_SUCCESS || type != REG_DWORD || size != sizeof(DWORD))
		throw std::runtime_error("Windows color filter settings could not be read");
	value.present = true;
	return value;
}

std::wstring ColorFilterBrokerPath()
{
	wchar_t folder[MAX_PATH] {};
	const auto n = GetSystemDirectoryW(folder, MAX_PATH);
	if (!n || n >= MAX_PATH)
		throw std::runtime_error("Cannot locate Windows system folder");
	return std::wstring(folder) + L"\\atbroker.exe";
}

void WriteRegistryDword(HKEY key, const wchar_t* name, DWORD value)
{
	if (RegSetValueExW(key, name, 0, REG_DWORD, reinterpret_cast<const BYTE*>(&value), sizeof(value)))
		throw std::runtime_error("Windows color filter settings could not be saved");
}

void RestoreRegistryValue(HKEY key, const wchar_t* name, Value value)
{
	if (value.present)
		RegSetValueExW(key, name, 0, REG_DWORD, reinterpret_cast<const BYTE*>(&value.data), sizeof(value.data));
	else
		RegDeleteValueW(key, name);
}
}

State ReadColorFilterState()
{
	State state;
	try
	{
		if (GetFileAttributesW(ColorFilterBrokerPath().c_str()) == INVALID_FILE_ATTRIBUTES)
		{
			state.reason = L"Windows color filters are unavailable.";
			return state;
		}
		HKEY key = nullptr;
		const auto result = RegOpenKeyExW(HKEY_CURRENT_USER, COLOR_FILTER_REGISTRY_KEY, 0, KEY_QUERY_VALUE, &key);
		if (result == ERROR_FILE_NOT_FOUND)
		{
			state.available = true;
			return state;
		}
		if (result != ERROR_SUCCESS)
			throw std::runtime_error("Windows color filter settings are unavailable");
		Value active, filter;
		try
		{
			active = ReadRegistryDword(key, L"Active");
			filter = ReadRegistryDword(key, L"FilterType");
		}
		catch (...)
		{
			RegCloseKey(key);
			throw;
		}
		RegCloseKey(key);
		if (active.data > 1 || filter.data > 5)
			throw std::runtime_error("Windows uses an unrecognized color filter setting");
		state.on = active.data != 0;
		state.filter = filter.data;
		state.available = true;
	}
	catch (const std::exception& error)
	{
		const std::string message = error.what();
		state.reason.assign(message.begin(), message.end());
	}
	return state;
}

Controller::~Controller()
{
	if (_process)
		CloseHandle(_process);
}

void Controller::StopWaiting()
{
	if (_process)
		CloseHandle(_process);
	_process = nullptr;
}

Completion CheckCompletion(const State& observed, bool on, DWORD filter, bool exited, DWORD code)
{
	if (!observed.available)
		return Completion::Unreadable;
	if (observed.on != on || observed.filter != filter)
		return Completion::DifferentSettings;
	return exited && code == 0 ? Completion::ConfirmedRequest : Completion::SettingsSaved;
}

const wchar_t* CompletionNotice(Completion value)
{
	switch (value)
	{
		case Completion::ConfirmedRequest:
			return L"";
		case Completion::SettingsSaved:
			return L"Settings saved. Check the picture or open Windows settings.";
		case Completion::DifferentSettings:
			return L"Showing the current Windows color filter settings.";
		default:
			return L"Windows color filter settings could not be read.";
	}
}

void Controller::Set(const State& expected, bool on, DWORD filter, bool selecting_filter)
{
	if (IsBusy())
		throw std::runtime_error("Windows is already updating color filters");
	if (filter > 5)
		throw std::invalid_argument("Invalid Windows color filter");
	const auto current = ReadColorFilterState();
	if (!expected.available || !current.available)
		throw std::runtime_error("Windows color filter settings could not be read");
	const auto exe = ColorFilterBrokerPath();
	auto command = L"\"" + exe + L"\" /colorfiltershortcut /resettransferkeys";
	HKEY key = nullptr;
	if (RegCreateKeyExW(HKEY_CURRENT_USER, COLOR_FILTER_REGISTRY_KEY, 0, nullptr, 0, KEY_QUERY_VALUE | KEY_SET_VALUE,
	                    nullptr, &key, nullptr))
		throw std::runtime_error("Windows color filter settings could not be opened");
	Value active, kind;
	bool captured = false;
	try
	{
		active = ReadRegistryDword(key, L"Active");
		kind = ReadRegistryDword(key, L"FilterType");
		captured = true;
		// A toggle preserves the latest Windows filter choice, even if our
		// visible snapshot is stale. Only a dropdown selection changes type.
		if (!selecting_filter)
		{
			if (kind.data > 5)
				throw std::runtime_error("Windows uses an unrecognized color filter setting");
			filter = kind.data;
		}
		WriteRegistryDword(key, L"FilterType", filter);
		WriteRegistryDword(key, L"Active", on ? 1 : 0);
		STARTUPINFOW startup { sizeof(startup) };
		startup.dwFlags = STARTF_USESHOWWINDOW | STARTF_FORCEOFFFEEDBACK;
		startup.wShowWindow = SW_HIDE;
		PROCESS_INFORMATION process {};
		if (!CreateProcessW(exe.c_str(), command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr,
		                    &startup, &process))
			throw std::runtime_error("Windows could not apply the color filter; use Windows settings");
		CloseHandle(process.hThread);
		_process = process.hProcess;
		_expected_on = on;
		_expected_filter = filter;
		_attempts = 0;
		_deadline = GetTickCount64() + 6000;
		_notice.clear();
	}
	catch (...)
	{
		if (captured)
		{
			RestoreRegistryValue(key, L"Active", active);
			RestoreRegistryValue(key, L"FilterType", kind);
		}
		RegCloseKey(key);
		throw;
	}
	RegCloseKey(key);
}

bool Controller::Poll()
{
	if (!_process)
		return true;
	const bool exited = WaitForSingleObject(_process, 0) == WAIT_OBJECT_0;
	if (!exited && GetTickCount64() < _deadline)
		return false;
	// The broker can finish before Windows publishes its final saved state.
	// Give it three timer turns to settle; never keep polling while idle.
	if (exited && ++_attempts < 3)
		return false;
	DWORD code = STILL_ACTIVE;
	GetExitCodeProcess(_process, &code);
	StopWaiting();
	const auto state = ReadColorFilterState();
	const auto result = CheckCompletion(state, _expected_on, _expected_filter, exited, code);
	_notice = CompletionNotice(result);
	if (result == Completion::Unreadable)
		throw std::runtime_error("Windows color filter settings could not be read");
	return true;
}

void OpenColorFilterSettings(HWND owner)
{
	const auto result =
		ShellExecuteW(owner, L"open", L"ms-settings:easeofaccess-colorfilter", nullptr, nullptr, SW_SHOWNORMAL);
	if (reinterpret_cast<INT_PTR>(result) <= 32)
		throw std::runtime_error("Windows settings could not be opened");
}
}
