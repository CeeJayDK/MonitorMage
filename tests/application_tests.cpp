#include "application_profiles.h"
#include "presets.h"
#include <windows.h>
#include <iostream>
#include <stdexcept>

namespace
{
void Check(bool value, const char* text)
{
	if (!value)
		throw std::runtime_error(text);
}

template <class F>
void Rejects(F action, const char* text)
{
	try
	{
		action();
	}
	catch (const std::exception&)
	{
		return;
	}
	throw std::runtime_error(text);
}

void WriteConfig(const std::filesystem::path& path, const std::wstring& text)
{
	std::string bytes("\xff\xfe", 2);
	for (wchar_t unit : text)
	{
		bytes.push_back(static_cast<char>(unit & 255));
		bytes.push_back(static_cast<char>(unit >> 8));
	}
	presets::WriteBytes(path, bytes, true);
	WritePrivateProfileStringW(nullptr, nullptr, nullptr, path.c_str());
}
}

int main()
{
	try
	{
		using namespace applications;
		Configuration config;
		config.Enabled = true;
		config.Rules = { { L"C:\\Games\\Game.exe", L"C:\\LUT\\presets\\Film.lutpreset", Action::Look },
			             { L"C:\\Movie\\Player.exe", {}, Action::Bypass },
			             { L"C:\\HDR\\Player.exe", {}, Action::Disable },
			             { L"C:\\HDR\\Game.exe", {}, Action::PauseHdr } };
		Validate(config);
		const auto game = ResolveForProgram(config, L"c:/games/./GAME.EXE");
		Check(game.matched && game.look == config.Rules[0].Look, "Full paths must match without case sensitivity");
		Check(!ResolveForProgram(config, L"C:\\Elsewhere\\Game.exe").matched,
		      "Same filename in another directory must not match");
		Check(ResolveForProgram(config, L"C:\\Movie\\Player.exe").action == Action::Bypass,
		      "Bypass rule must retain its distinct action");
		Check(ResolveForProgram(config, L"C:\\HDR\\Player.exe").action == Action::Disable,
		      "Disable rule must retain its distinct action");
		const auto fallback = ResolveForProgram(config, L"C:\\Other.exe");
		Check(!fallback.matched && fallback.look.empty(),
		      "Unmatched apps must release their override without selecting a default");
		Focus focus;
		Check(focus.UpdateFocusedProgram(L"C:\\Games\\Game.exe"), "First application must resolve");
		focus.SetManualOverride();
		Check(!focus.UpdateFocusedProgram(L"c:\\games\\GAME.EXE") && focus.IsManualOverride(),
		      "Same application's focus events must retain manual override");
		Check(focus.UpdateFocusedProgram(L"C:\\Other.exe") && !focus.IsManualOverride(),
		      "Switching application must end manual override");
		focus.SetManualOverride();
		focus.ClearManualOverride();
		Check(!focus.IsManualOverride(), "Explicit configuration change must reset override");
		Configuration empty;
		empty.Enabled = true;
		Validate(empty); // No default preset is required.
		auto bad = config;
		bad.Rules.push_back({ L"c:\\GAMES\\game.exe", {}, Action::Bypass });
		Rejects([&] { Validate(bad); }, "Duplicate path rules must be rejected");
		bad = config;
		bad.Rules[0].Executable = L"Game.exe";
		Rejects([&] { Validate(bad); }, "Filename-only rules must be rejected");
		bad = config;
		bad.Rules[0].Look.clear();
		Rejects([&] { Validate(bad); }, "Look action requires a named look");
		bad = config;
		bad.Rules[1].Look = config.Rules[0].Look;
		Rejects([&] { Validate(bad); }, "Non-look action must not smuggle in a look");
		config.Enabled = false;
		Check(!ResolveForProgram(config, config.Rules[0].Executable).matched, "Disabled rules must not match");
		config.Enabled = true;
		wchar_t temp[32768] {};
		Check(GetTempPathW(static_cast<DWORD>(std::size(temp)), temp) != 0, "No temporary path");
		const auto folder =
			std::filesystem::path(temp) / (L"LutApplicationTests-" + std::to_wstring(GetCurrentProcessId()) + L"-" +
		                                   std::to_wstring(GetTickCount64()));
		Check(std::filesystem::create_directory(folder), "Temporary test directory must be new");
		const auto settings = folder / L"LutWorkbench.ini";
		config.Rules[0].Look = folder / L"presets\\Farver-\u00e6\u00f8\u00e5.lutpreset";
		WriteConfig(settings, Encode(config, settings));
		const auto loaded = Read(settings);
		Check(loaded.Enabled && loaded.Rules.size() == 4 && loaded.Rules[0].Look == config.Rules[0].Look,
		      "Unicode preset paths and complete configuration must round-trip");
		Check(loaded.Rules[1].Action == Action::Bypass && loaded.Rules[2].Action == Action::Disable,
		      "Actions must survive restart");
		Check(loaded.Rules[3].Action == Action::PauseHdr &&
		          ResolveForProgram(loaded, L"C:\\HDR\\Game.exe").action == Action::PauseHdr,
		      "HDR-only pause must survive restart and match its program");
		WriteConfig(settings, L"[Applications]\r\nVersion=99\r\n");
		Rejects([&] { Read(settings); }, "Unknown settings version must be refused");
		WriteConfig(
			settings,
			L"[Applications]\r\nVersion=1\r\nEnabled=1\r\nDesktopLook=missing-default.lutpreset\r\nRuleCount=0\r\n");
		Check(Read(settings).Enabled && !ResolveForProgram(Read(settings), L"C:\\Other.exe").matched,
		      "Old configuration must migrate without requiring its former desktop default");
		WriteConfig(settings, L"[Applications]\r\nVersion=1\r\nRuleCount=999\r\n");
		Rejects([&] { Read(settings); }, "Oversized rule count must be refused");
		WriteConfig(settings, L"[Hotkey]\r\nBypass=123\r\n");
		Check(!Read(settings).Enabled, "Older settings must start with application profiles disabled");
		std::filesystem::remove(settings);
		std::filesystem::remove(folder);
		std::cout
			<< "Application matching, manual override lifetime, action selection and configuration persistence passed.\n";
		return 0;
	}
	catch (const std::exception& error)
	{
		std::cerr << error.what() << '\n';
		return 1;
	}
}
