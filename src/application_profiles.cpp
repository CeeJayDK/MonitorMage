#include "application_profiles.h"
#include <windows.h>
#include <stdexcept>

namespace applications
{
namespace
{
std::wstring ReadIniValue(const std::filesystem::path& file, const wchar_t* section, const wchar_t* key,
                          const wchar_t* fallback = L"")
{
	wchar_t value[32768] {};
	const DWORD count =
		GetPrivateProfileStringW(section, key, fallback, value, static_cast<DWORD>(std::size(value)), file.c_str());
	if (count >= std::size(value) - 1)
		throw std::runtime_error("Application profile setting is too long");
	return value;
}

std::filesystem::path ToAbsolutePath(const std::filesystem::path& settings, const std::wstring& path)
{
	if (path.empty())
		return {};
	const std::filesystem::path value(path);
	return (value.is_absolute() ? value : settings.parent_path() / value).lexically_normal();
}

std::wstring ToPortablePath(const std::filesystem::path& settings, const std::filesystem::path& path)
{
	if (path.empty())
		return L"";
	const auto relative = path.lexically_relative(settings.parent_path());
	return !relative.empty() && !relative.is_absolute() && *relative.begin() != L".." ? relative.wstring()
	                                                                                  : path.wstring();
}
}

bool SamePath(const std::filesystem::path& a, const std::filesystem::path& b)
{
	const auto left = a.lexically_normal().wstring(), right = b.lexically_normal().wstring();
	return CompareStringOrdinal(left.c_str(), -1, right.c_str(), -1, TRUE) == CSTR_EQUAL;
}

void Validate(const Configuration& config)
{
	if (config.Rules.size() > 256)
		throw std::runtime_error("At most 256 application rules are supported");
	for (size_t i = 0; i < config.Rules.size(); ++i)
	{
		const auto& rule = config.Rules[i];
		if (!rule.Executable.is_absolute() || rule.Executable.filename().empty())
			throw std::runtime_error("Application rules require a full executable path");
		if (rule.Action != Action::Look && rule.Action != Action::Bypass && rule.Action != Action::Disable &&
		    rule.Action != Action::PauseHdr)
			throw std::runtime_error("Unknown application rule action");
		if (rule.Action == Action::Look && rule.Look.empty())
			throw std::runtime_error("Choose a saved look for the application rule");
		if (rule.Action != Action::Look && !rule.Look.empty())
			throw std::runtime_error("Only look rules may specify a preset");
		for (size_t j = 0; j < i; ++j)
			if (SamePath(rule.Executable, config.Rules[j].Executable))
				throw std::runtime_error("Duplicate executable rule");
	}
}

Configuration Read(const std::filesystem::path& settings)
{
	Configuration config;
	const auto version = ReadIniValue(settings, L"Applications", L"Version");
	if (version.empty())
		return config; // Existing settings from before application profiles.
	if (version != L"1" && version != L"2" && version != L"3")
		throw std::runtime_error("Unsupported application profile version");
	const auto enabled = ReadIniValue(settings, L"Applications", L"Enabled", L"0");
	if (enabled != L"0" && enabled != L"1")
		throw std::runtime_error("Invalid automatic profile flag");
	config.Enabled = enabled == L"1";
	// Version 1's DesktopLook is intentionally ignored: unmatched apps retain the user's look.
	const auto count_text = ReadIniValue(settings, L"Applications", L"RuleCount", L"0");
	if (count_text.empty() || count_text.find_first_not_of(L"0123456789") != std::wstring::npos ||
	    count_text.size() > 3)
		throw std::runtime_error("Invalid application rule count");
	const unsigned long count = wcstoul(count_text.c_str(), nullptr, 10);
	if (count > 256)
		throw std::runtime_error("Too many application rules");
	for (unsigned long i = 0; i < count; ++i)
	{
		const auto section = L"Application." + std::to_wstring(i);
		Rule rule;
		rule.Executable = ToAbsolutePath(settings, ReadIniValue(settings, section.c_str(), L"Executable"));
		const auto action = ReadIniValue(settings, section.c_str(), L"Action");
		if (action != L"Look" && action != L"Bypass" && action != L"Disable" &&
		    !(version == L"3" && action == L"PauseHdr"))
			throw std::runtime_error("Unknown application action");
		rule.Action = action == L"Look"      ? Action::Look
		              : action == L"Bypass"  ? Action::Bypass
		              : action == L"Disable" ? Action::Disable
		                                     : Action::PauseHdr;
		rule.Look = ToAbsolutePath(settings, ReadIniValue(settings, section.c_str(), L"Look"));
		config.Rules.push_back(rule);
	}
	Validate(config);
	return config;
}

std::wstring Encode(const Configuration& config, const std::filesystem::path& settings)
{
	Validate(config);
	std::wstring text = L"\r\n[Applications]\r\nVersion=3\r\nEnabled=" + std::wstring(config.Enabled ? L"1" : L"0") +
	                    L"\r\nRuleCount=" + std::to_wstring(config.Rules.size()) + L"\r\n";
	for (size_t i = 0; i < config.Rules.size(); ++i)
	{
		const auto& rule = config.Rules[i];
		text += L"\r\n[Application." + std::to_wstring(i) + L"]\r\nExecutable=" + rule.Executable.wstring() +
		        L"\r\nAction=" +
		        (rule.Action == Action::Look      ? L"Look"
		         : rule.Action == Action::Bypass  ? L"Bypass"
		         : rule.Action == Action::Disable ? L"Disable"
		                                          : L"PauseHdr") +
		        L"\r\nLook=" + ToPortablePath(settings, rule.Look) + L"\r\n";
	}
	return text;
}

Decision ResolveForProgram(const Configuration& config, const std::filesystem::path& executable)
{
	if (config.Enabled)
		for (const auto& rule : config.Rules)
			if (SamePath(rule.Executable, executable))
				return { rule.Action, rule.Look, true };
	return { Action::Look, {}, false }; // Caller restores the look from before the application override.
}

bool Focus::UpdateFocusedProgram(const std::filesystem::path& executable)
{
	if (SamePath(_current, executable))
		return false;
	_current = executable;
	_manual = false;
	return true;
}
}
