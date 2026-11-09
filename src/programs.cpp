#include "programs.h"
#include <psapi.h>
#include <algorithm>
#include <set>
#include <stdexcept>
namespace programs
{
namespace
{
int CompareIgnoringCase(const std::wstring& a, const std::wstring& b)
{
	return CompareStringOrdinal(a.c_str(), -1, b.c_str(), -1, TRUE);
}

struct CachedName
{
	std::filesystem::path path;
	std::wstring name;
};

std::vector<CachedName> names;
std::wstring FileDescription(const std::filesystem::path& path)
{
	if (!path.is_absolute() || path.wstring().rfind(L"\\\\", 0) == 0)
		return {};
	DWORD unused = 0;
	const auto size = GetFileVersionInfoSizeW(path.c_str(), &unused);
	if (!size || size > 4 * 1024 * 1024)
		return {};
	std::vector<BYTE> data(size);
	if (!GetFileVersionInfoW(path.c_str(), 0, size, data.data()))
		return {};
	struct Translation
	{
		WORD language, codepage;
	};
	Translation* languages = nullptr;
	UINT length = 0;
	std::vector<Translation> choices;
	if (VerQueryValueW(data.data(), L"\\VarFileInfo\\Translation", reinterpret_cast<void**>(&languages), &length))
		choices.assign(languages, languages + length / sizeof(Translation));
	choices.push_back({ 0x0409, 0x04b0 });
	choices.push_back({ 0x0409, 0x04e4 });
	for (const auto* field : { L"FileDescription", L"ProductName" })
		for (const auto choice : choices)
		{
			wchar_t key[100] {};
			swprintf_s(key, L"\\StringFileInfo\\%04x%04x\\%s", choice.language, choice.codepage, field);
			wchar_t* value = nullptr;
			UINT chars = 0;
			if (VerQueryValueW(data.data(), key, reinterpret_cast<void**>(&value), &chars) && value && chars > 1)
			{
				std::wstring text(value, std::find(value, value + chars, L'\0'));
				text.erase(std::remove_if(text.begin(), text.end(), [](wchar_t c) { return c < L' '; }), text.end());
				if (!text.empty())
				{
					if (text.size() > 160)
						text.resize(160);
					return text;
				}
			}
		}
	return {};
}

BOOL CALLBACK CollectVisibleWindow(HWND hwnd, LPARAM param)
{
	if (IsWindowVisible(hwnd) && !GetWindow(hwnd, GW_OWNER) &&
	    !(GetWindowLongPtrW(hwnd, GWL_EXSTYLE) & WS_EX_TOOLWINDOW))
	{
		DWORD pid = 0;
		GetWindowThreadProcessId(hwnd, &pid);
		if (pid)
			reinterpret_cast<std::set<DWORD>*>(param)->insert(pid);
	}
	return TRUE;
}
}

std::wstring DisplayName(const std::filesystem::path& path)
{
	const auto normalized = path.lexically_normal();
	for (const auto& entry : names)
		if (CompareIgnoringCase(entry.path.wstring(), normalized.wstring()) == CSTR_EQUAL)
			return entry.name;
	auto result = FileDescription(normalized);
	if (result.empty())
		result = normalized.filename().wstring();
	if (names.size() >= 128)
		names.erase(names.begin());
	names.push_back({ normalized, result });
	return result;
}

bool IsUnderFolder(const std::filesystem::path& path, const std::filesystem::path& root)
{
	auto value = path.lexically_normal().wstring(), base = root.lexically_normal().wstring();
	while (!base.empty() && (base.back() == L'\\' || base.back() == L'/'))
		base.pop_back();
	if (base.empty() || value.size() <= base.size() || value[base.size()] != L'\\')
		return false;
	return CompareIgnoringCase(value.substr(0, base.size()), base) == CSTR_EQUAL;
}

void SortPrograms(std::vector<Program>& list, bool by_memory)
{
	std::sort(list.begin(), list.end(),
	          [&](const Program& a, const Program& b)
	          {
				  if (by_memory && a.memory_known != b.memory_known)
					  return a.memory_known;
				  if (by_memory && a.memory != b.memory)
					  return a.memory > b.memory;
				  const auto order = CompareIgnoringCase(a.name, b.name);
				  if (order != CSTR_EQUAL)
					  return order == CSTR_LESS_THAN;
				  return CompareIgnoringCase(a.path.wstring(), b.path.wstring()) == CSTR_LESS_THAN;
			  });
}

std::vector<Program> RunningPrograms(bool include_background, bool by_memory)
{
	std::set<DWORD> visible;
	EnumWindows(CollectVisibleWindow, reinterpret_cast<LPARAM>(&visible));
	wchar_t windows[MAX_PATH] {};
	if (!GetWindowsDirectoryW(windows, MAX_PATH))
		throw std::runtime_error("Cannot identify Windows system programs");
	std::vector<DWORD> ids(1024);
	DWORD bytes = 0;
	for (;;)
	{
		if (!EnumProcesses(ids.data(), static_cast<DWORD>(ids.size() * sizeof(DWORD)), &bytes))
			throw std::runtime_error("Cannot list running programs");
		if (bytes < ids.size() * sizeof(DWORD))
			break;
		if (ids.size() >= 65536)
			throw std::runtime_error("Too many processes to list");
		ids.resize(ids.size() * 2);
	}
	std::vector<Program> list;
	for (size_t i = 0; i < bytes / sizeof(DWORD); ++i)
	{
		if (!ids[i] || ids[i] == GetCurrentProcessId())
			continue;
		HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, ids[i]);
		if (!process)
			continue;
		wchar_t file[32768] {};
		DWORD size = static_cast<DWORD>(std::size(file));
		if (!QueryFullProcessImageNameW(process, 0, file, &size))
		{
			CloseHandle(process);
			continue;
		}
		const std::filesystem::path path(file);
		if (IsUnderFolder(path, windows) ||
		    CompareIgnoringCase(path.filename().wstring(), L"MonitorMage.exe") == CSTR_EQUAL)
		{
			CloseHandle(process);
			continue;
		}
		auto at = std::find_if(list.begin(), list.end(),
		                       [&](const Program& item)
		                       { return CompareIgnoringCase(item.path.wstring(), path.wstring()) == CSTR_EQUAL; });
		if (at == list.end())
		{
			list.push_back({ path, L"" });
			at = list.end() - 1;
		}
		at->visible = at->visible || visible.count(ids[i]) != 0;
		PROCESS_MEMORY_COUNTERS counters {};
		counters.cb = sizeof(counters);
		if (GetProcessMemoryInfo(process, &counters, sizeof(counters)))
		{
			at->memory += counters.WorkingSetSize;
			at->memory_known = true;
		}
		CloseHandle(process);
	}
	list.erase(std::remove_if(list.begin(), list.end(),
	                          [&](const Program& item) { return !include_background && !item.visible; }),
	           list.end());
	for (auto& item : list)
		item.name = DisplayName(item.path);
	SortPrograms(list, by_memory);
	return list;
}
}
