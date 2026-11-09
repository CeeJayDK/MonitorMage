#include <windows.h>
#include <psapi.h>
#include <filesystem>
#include <iostream>
#include <string>

// Development-only wrapper. Run a read-only CLI mode and observe the child
// through an owned process handle; never attach to a user's running instance.
int wmain(int argc, wchar_t** argv)
{
	if (argc != 4)
		return 1;
	std::wstring command = L"\"" + std::wstring(argv[1]) + L"\" " + argv[2] + L" \"" + argv[3] + L"\"";
	STARTUPINFOW startup { sizeof(startup) };
	startup.dwFlags = STARTF_FORCEOFFFEEDBACK;
	PROCESS_INFORMATION child {};
	LARGE_INTEGER start {}, end {}, frequency {};
	QueryPerformanceFrequency(&frequency);
	QueryPerformanceCounter(&start);
	if (!CreateProcessW(argv[1], command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &startup,
	                    &child))
		return 2;
	CloseHandle(child.hThread);
	SIZE_T peak_working = 0, peak_private = 0;
	DWORD faults = 0;
	for (;;)
	{
		PROCESS_MEMORY_COUNTERS_EX memory {};
		memory.cb = sizeof(memory);
		if (GetProcessMemoryInfo(child.hProcess, reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&memory), sizeof(memory)))
		{
			peak_working = (std::max)(peak_working, memory.PeakWorkingSetSize);
			peak_private = (std::max)(peak_private, memory.PrivateUsage);
			faults = (std::max)(faults, memory.PageFaultCount);
		}
		const auto wait = WaitForSingleObject(child.hProcess, 2);
		if (wait == WAIT_OBJECT_0)
			break;
		if (wait == WAIT_FAILED)
		{
			CloseHandle(child.hProcess);
			return 3;
		}
	}
	QueryPerformanceCounter(&end);
	FILETIME created {}, exited {}, kernel {}, user {};
	DWORD code = 1;
	GetExitCodeProcess(child.hProcess, &code);
	const bool timed = GetProcessTimes(child.hProcess, &created, &exited, &kernel, &user) != FALSE;
	const auto ticks = [](FILETIME t)
	{
		return (static_cast<unsigned long long>(t.dwHighDateTime) << 32) | t.dwLowDateTime;
	};
	std::cout << "{\"exit_code\":" << code
			  << ",\"wall_ms\":" << (end.QuadPart - start.QuadPart) * 1000.0 / frequency.QuadPart
			  << ",\"cpu_ms\":" << (ticks(kernel) + ticks(user)) / 10000.0
			  << ",\"timed\":" << (timed ? "true" : "false") << ",\"sampled_total_page_faults\":" << faults
			  << ",\"peak_working_set_mib\":" << peak_working / 1048576.0
			  << ",\"sampled_peak_private_mib\":" << peak_private / 1048576.0 << "}\n";
	CloseHandle(child.hProcess);
	return static_cast<int>(code);
}
