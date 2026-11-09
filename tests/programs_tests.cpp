#include "programs.h"
#include <iostream>
#include <stdexcept>
void Check(bool value, const char* text)
{
	if (!value)
		throw std::runtime_error(text);
}

int main()
{
	try
	{
		Check(programs::IsUnderFolder(L"c:\\WINDOWS\\System32\\host.exe", L"C:\\Windows"),
		      "Windows path filtering must be case insensitive");
		Check(!programs::IsUnderFolder(L"C:\\WindowsGames\\Game.exe", L"C:\\Windows"),
		      "Windows prefix must end at a directory boundary");
		Check(!programs::IsUnderFolder(L"C:\\Games\\Game.exe", L""), "Missing root must not exclude every program");
		std::vector<programs::Program> list { { L"C:\\z.exe", L"Zulu", 300, true, true },
			                                  { L"C:\\b.exe", L"beta", 100, true, true },
			                                  { L"C:\\a.exe", L"Alpha", 100, true, true },
			                                  { L"C:\\unknown.exe", L"Unknown", 0, true, false } };
		programs::SortPrograms(list, true);
		Check(list[0].name == L"Zulu" && list[1].name == L"Alpha" && list[2].name == L"beta" && !list[3].memory_known,
		      "Memory ordering must put known larger values first with name tie breaks");
		programs::SortPrograms(list, false);
		Check(list[0].name == L"Alpha" && list[1].name == L"beta" && list[3].name == L"Zulu",
		      "Name ordering must ignore case");
		Check(programs::DisplayName(L"C:\\MonitorMage-missing-test-program\\game.exe") == L"game.exe",
		      "Unavailable metadata must retain the executable name");
		Check(programs::DisplayName({}).empty(), "Empty program selection must remain empty");
		wchar_t module[32768] {};
		GetModuleFileNameW(nullptr, module, 32768);
		Check(programs::DisplayName(std::filesystem::path(module).parent_path() / L"MonitorMage.exe") == L"MonitorMage",
		      "Friendly names must come from executable version information");
		wchar_t folder[MAX_PATH] {};
		GetWindowsDirectoryW(folder, MAX_PATH);
		const auto running = programs::RunningPrograms(false, true);
		for (size_t i = 0; i < running.size(); ++i)
		{
			Check(running[i].visible && !running[i].name.empty() && running[i].path.is_absolute() &&
			          !programs::IsUnderFolder(running[i].path, folder),
			      "Picker must list visible user programs with full matching paths");
			for (size_t j = 0; j < i; ++j)
				Check(CompareStringOrdinal(running[i].path.c_str(), -1, running[j].path.c_str(), -1, TRUE) !=
				          CSTR_EQUAL,
				      "Executable paths must be deduplicated");
		}
		std::cout << "Program names, directory filtering, sorting and read-only process inventory passed.\n";
		return 0;
	}
	catch (const std::exception& error)
	{
		std::cerr << error.what() << '\n';
		return 1;
	}
}
