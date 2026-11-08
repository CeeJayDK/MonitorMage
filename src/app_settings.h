#pragma once
#include <windows.h>
#include <string>
#include <stdexcept>
#include <vector>

namespace app_settings
{
inline std::wstring ExecutablePath()
{
	wchar_t path[32768] {};
	const auto length = GetModuleFileNameW(nullptr, path, 32768);
	if (!length || length >= 32768)
		throw std::runtime_error("Cannot locate MonitorMage.exe");
	return std::wstring(path, length);
}

inline constexpr const wchar_t* RunKey = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
inline void RegistryError(LSTATUS status)
{
	if (status != ERROR_SUCCESS)
		throw std::runtime_error("Windows startup setting failed (error " + std::to_string(status) + ")");
}

struct Key
{
	HKEY value {};
	~Key()
	{
		if (value)
			RegCloseKey(value);
	}
};

inline std::wstring StartupCommand(const std::wstring& exe)
{
	return L"\"" + exe + L"\"";
}

inline bool StartsWithWindows(const std::wstring& exe, const wchar_t* keyPath = RunKey)
{
	Key key;
	auto status = RegOpenKeyExW(HKEY_CURRENT_USER, keyPath, 0, KEY_QUERY_VALUE, &key.value);
	if (status == ERROR_FILE_NOT_FOUND)
		return false;
	RegistryError(status);
	DWORD type = 0, size = 0;
	status = RegQueryValueExW(key.value, L"MonitorMage", nullptr, &type, nullptr, &size);
	if (status == ERROR_FILE_NOT_FOUND)
		return false;
	RegistryError(status);
	if (type != REG_SZ || size < sizeof(wchar_t) || size > 65536 || size % sizeof(wchar_t))
		return false;
	std::vector<wchar_t> text(size / sizeof(wchar_t) + 1, 0);
	RegistryError(
		RegQueryValueExW(key.value, L"MonitorMage", nullptr, &type, reinterpret_cast<BYTE*>(text.data()), &size));
	return type == REG_SZ && _wcsicmp(text.data(), StartupCommand(exe).c_str()) == 0;
}

inline void StartWithWindows(const std::wstring& exe, bool enabled, const wchar_t* keyPath = RunKey)
{
	Key key;
	if (enabled)
	{
		RegistryError(
			RegCreateKeyExW(HKEY_CURRENT_USER, keyPath, 0, nullptr, 0, KEY_SET_VALUE, nullptr, &key.value, nullptr));
		const auto command = StartupCommand(exe);
		RegistryError(RegSetValueExW(key.value, L"MonitorMage", 0, REG_SZ,
		                             reinterpret_cast<const BYTE*>(command.c_str()),
		                             static_cast<DWORD>((command.size() + 1) * sizeof(wchar_t))));
	}
	else
	{
		auto status = RegOpenKeyExW(HKEY_CURRENT_USER, keyPath, 0, KEY_SET_VALUE, &key.value);
		if (status == ERROR_FILE_NOT_FOUND)
			return;
		RegistryError(status);
		status = RegDeleteValueW(key.value, L"MonitorMage");
		if (status != ERROR_FILE_NOT_FOUND)
			RegistryError(status);
	}
}
}
