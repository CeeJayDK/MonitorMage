#include "application_profiles.h"
#include "presets.h"
#include <windows.h>
#include <iostream>
#include <stdexcept>

namespace {
void check(bool value, const char* text) { if (!value) throw std::runtime_error(text); }
template<class F> void rejects(F action, const char* text) {
    try { action(); } catch (const std::exception&) { return; }
    throw std::runtime_error(text);
}
void writeConfig(const std::filesystem::path& path, const std::wstring& text) {
    std::string bytes("\xff\xfe", 2);
    for (wchar_t unit : text) { bytes.push_back(static_cast<char>(unit & 255)); bytes.push_back(static_cast<char>(unit >> 8)); }
    presets::writeBytes(path, bytes, true); WritePrivateProfileStringW(nullptr, nullptr, nullptr, path.c_str());
}
}
int main() {
    try {
        using namespace applications;
        Configuration config; config.enabled = true;
        config.rules = {{L"C:\\Games\\Game.exe", L"C:\\LUT\\presets\\Film.lutpreset", Action::Look},
                        {L"C:\\Movie\\Player.exe", {}, Action::Bypass}, {L"C:\\HDR\\Player.exe", {}, Action::Disable}, {L"C:\\HDR\\Game.exe", {}, Action::PauseHdr}};
        validate(config);
        const auto game = resolve(config, L"c:/games/./GAME.EXE");
        check(game.matched && game.look == config.rules[0].look, "Full paths must match without case sensitivity");
        check(!resolve(config, L"C:\\Elsewhere\\Game.exe").matched, "Same filename in another directory must not match");
        check(resolve(config, L"C:\\Movie\\Player.exe").action == Action::Bypass, "Bypass rule must retain its distinct action");
        check(resolve(config, L"C:\\HDR\\Player.exe").action == Action::Disable, "Disable rule must retain its distinct action");
        const auto fallback = resolve(config, L"C:\\Other.exe");
        check(!fallback.matched && fallback.look.empty(), "Unmatched apps must release their override without selecting a default");
        Focus focus; check(focus.changed(L"C:\\Games\\Game.exe"), "First application must resolve"); focus.overrideLook();
        check(!focus.changed(L"c:\\games\\GAME.EXE") && focus.manual(), "Same application's focus events must retain manual override");
        check(focus.changed(L"C:\\Other.exe") && !focus.manual(), "Switching application must end manual override");
        focus.overrideLook(); focus.resetOverride(); check(!focus.manual(), "Explicit configuration change must reset override");
        Configuration empty; empty.enabled = true; validate(empty); // No default preset is required.
        auto bad = config; bad.rules.push_back({L"c:\\GAMES\\game.exe", {}, Action::Bypass}); rejects([&] { validate(bad); }, "Duplicate path rules must be rejected");
        bad = config; bad.rules[0].executable = L"Game.exe"; rejects([&] { validate(bad); }, "Filename-only rules must be rejected");
        bad = config; bad.rules[0].look.clear(); rejects([&] { validate(bad); }, "Look action requires a named look");
        bad = config; bad.rules[1].look = config.rules[0].look; rejects([&] { validate(bad); }, "Non-look action must not smuggle in a look");
        config.enabled = false; check(!resolve(config, config.rules[0].executable).matched, "Disabled rules must not match"); config.enabled = true;
        wchar_t temp[32768]{}; check(GetTempPathW(static_cast<DWORD>(std::size(temp)), temp) != 0, "No temporary path");
        const auto folder = std::filesystem::path(temp) / (L"LutApplicationTests-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64()));
        check(std::filesystem::create_directory(folder), "Temporary test directory must be new");
        const auto settings = folder / L"LutWorkbench.ini";
        config.rules[0].look = folder / L"presets\\Farver-\u00e6\u00f8\u00e5.lutpreset";
        writeConfig(settings, encode(config, settings));
        const auto loaded = read(settings);
        check(loaded.enabled && loaded.rules.size() == 4 && loaded.rules[0].look == config.rules[0].look,
            "Unicode preset paths and complete configuration must round-trip");
        check(loaded.rules[1].action == Action::Bypass && loaded.rules[2].action == Action::Disable, "Actions must survive restart");
        check(loaded.rules[3].action == Action::PauseHdr && resolve(loaded, L"C:\\HDR\\Game.exe").action == Action::PauseHdr, "HDR-only pause must survive restart and match its program");
        writeConfig(settings, L"[Applications]\r\nVersion=99\r\n"); rejects([&] { read(settings); }, "Unknown settings version must be refused");
        writeConfig(settings, L"[Applications]\r\nVersion=1\r\nEnabled=1\r\nDesktopLook=missing-default.lutpreset\r\nRuleCount=0\r\n");
        check(read(settings).enabled && !resolve(read(settings), L"C:\\Other.exe").matched, "Old configuration must migrate without requiring its former desktop default");
        writeConfig(settings, L"[Applications]\r\nVersion=1\r\nRuleCount=999\r\n"); rejects([&] { read(settings); }, "Oversized rule count must be refused");
        writeConfig(settings, L"[Hotkey]\r\nBypass=123\r\n"); check(!read(settings).enabled, "Older settings must start with application profiles disabled");
        std::filesystem::remove(settings); std::filesystem::remove(folder);
        std::cout << "Application matching, manual override lifetime, action selection and configuration persistence passed.\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
