#include <windows.h>
#include <physicalmonitorenumerationapi.h>
#include <lowlevelmonitorconfigurationapi.h>
#include <iostream>
#include <vector>
#include <string>
#include <fstream>
#include <filesystem>
#include "live_lut.h"

// Development-only probe. --inspect never changes monitor settings.
struct Physical
{
	std::vector<PHYSICAL_MONITOR> monitors;
	~Physical()
	{
		if (!monitors.empty())
			DestroyPhysicalMonitors(static_cast<DWORD>(monitors.size()), monitors.data());
	}
};

std::ofstream report;
std::string ToUtf8(const std::wstring& s)
{
	if (s.empty())
		return {};
	int n = WideCharToMultiByte(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), nullptr, 0, nullptr, nullptr);
	std::string utf8_text(n, 0);
	WideCharToMultiByte(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), utf8_text.data(), n, nullptr, nullptr);
	return utf8_text;
}

void Line(const std::string& s)
{
	std::cout << s << std::endl;
	report << s << std::endl;
}

void Topology()
{
	try
	{
		for (const auto& output : live::ActiveOutputs())
		{
			Line("output=" + ToUtf8(output.name) + " label=" + ToUtf8(output.label) +
			     " hdr=" + std::to_string(output.hdr));
			Line("identity=" + ToUtf8(output.identity));
		}
	}
	catch (const std::exception& e)
	{
		Line(std::string("topology_error=") + e.what());
	}
}

BOOL CALLBACK Inspect(HMONITOR monitor, HDC, LPRECT, LPARAM)
{
	MONITORINFOEXW info {};
	info.cbSize = sizeof(info);
	GetMonitorInfoW(monitor, &info);
	Line("logical=" + ToUtf8(info.szDevice));
	DWORD count = 0;
	if (!GetNumberOfPhysicalMonitorsFromHMONITOR(monitor, &count))
	{
		Line("physical_count_error=" + std::to_string(GetLastError()));
		return TRUE;
	}
	Physical p;
	p.monitors.resize(count);
	if (count && !GetPhysicalMonitorsFromHMONITOR(monitor, count, p.monitors.data()))
	{
		Line("physical_list_error=" + std::to_string(GetLastError()));
		p.monitors.clear();
		return TRUE;
	}
	Line("physical_count=" + std::to_string(count));
	for (const auto& m : p.monitors)
	{
		Line("physical=" + ToUtf8(m.szPhysicalMonitorDescription));
		DWORD current = 0;
		MC_VCP_CODE_TYPE type {};
		const BOOL ok = GetVCPFeatureAndVCPFeatureReply(m.hPhysicalMonitor, 0xd6, &type, &current, nullptr);
		Line("power_read_ok=" + std::to_string(ok != FALSE) +
		     (ok ? " current=" + std::to_string(current) : " error=" + std::to_string(GetLastError())));
		DWORD length = 0;
		if (GetCapabilitiesStringLength(m.hPhysicalMonitor, &length) && length > 0 && length < 65536)
		{
			std::vector<char> caps(length + 1);
			if (CapabilitiesRequestAndCapabilitiesReply(m.hPhysicalMonitor, caps.data(), length))
				Line(std::string("capabilities=") + caps.data());
			else
				Line("capabilities_error=" + std::to_string(GetLastError()));
		}
		else
			Line("capabilities_length_error=" + std::to_string(GetLastError()));
	}
	return TRUE;
}

struct Samsung
{
	Physical physical;
	HANDLE handle = nullptr;
	std::wstring source;
	unsigned matches = 0;
};

BOOL CALLBACK FindSamsung(HMONITOR monitor, HDC, LPRECT, LPARAM context)
{
	auto& target = *reinterpret_cast<Samsung*>(context);
	MONITORINFOEXW info {};
	info.cbSize = sizeof(info);
	if (!GetMonitorInfoW(monitor, &info))
		return TRUE;
	Line("candidate_source=" + ToUtf8(info.szDevice) + " expected=" + ToUtf8(target.source));
	if (target.source != info.szDevice)
		return TRUE;
	DWORD count = 0;
	if (!GetNumberOfPhysicalMonitorsFromHMONITOR(monitor, &count))
	{
		Line("candidate_count_error=" + std::to_string(GetLastError()));
		return TRUE;
	}
	Line("candidate_count=" + std::to_string(count));
	target.physical.monitors.resize(count);
	if (!count || !GetPhysicalMonitorsFromHMONITOR(monitor, count, target.physical.monitors.data()))
	{
		target.physical.monitors.clear();
		return TRUE;
	}
	for (const auto& m : target.physical.monitors)
	{
		DWORD power = 0;
		GetVCPFeatureAndVCPFeatureReply(m.hPhysicalMonitor, 0xd6, nullptr, &power, nullptr);
		for (int attempt = 0; attempt < 3; ++attempt)
		{
			DWORD size = 0;
			if (GetCapabilitiesStringLength(m.hPhysicalMonitor, &size) && size && size <= 65535)
			{
				std::vector<char> caps(size + 1);
				if (CapabilitiesRequestAndCapabilitiesReply(m.hPhysicalMonitor, caps.data(), size))
				{
					const std::string text(caps.data());
					Line("candidate_capabilities=" + text);
					if (text.find("SAMSUNG") != std::string::npos && text.find("D6(01 04 05)") != std::string::npos)
					{
						target.handle = m.hPhysicalMonitor;
						++target.matches;
					}
					break;
				}
			}
			Sleep(150);
		}
	}
	return TRUE;
}

bool OpenSamsung(Samsung& target, const wchar_t* source)
{
	target.source = source;
	EnumDisplayMonitors(nullptr, nullptr, FindSamsung, reinterpret_cast<LPARAM>(&target));
	return target.matches == 1; // DDC physical handles are opaque; zero can be valid.
}

std::wstring QuoteArgument(const std::wstring& text)
{
	return L"\"" + text + L"\"";
}

int wmain(int argc, wchar_t** argv)
{
	const std::wstring mode = argc > 1 ? argv[1] : L"";
	if (mode == L"--wake-samsung" && argc == 5)
	{
		// The child acquires its own handle before the parent can issue standby.
		Samsung target;
		if (!OpenSamsung(target, argv[2]))
			return 10;
		HANDLE ready = OpenEventW(EVENT_MODIFY_STATE, FALSE, argv[4]);
		if (!ready)
			return 11;
		report.open(std::filesystem::path(argv[3]));
		if (!report)
		{
			CloseHandle(ready);
			return 12;
		}
		SetEvent(ready);
		CloseHandle(ready);
		Sleep(10000);
		for (int attempt = 0; attempt < 3; ++attempt)
		{
			const BOOL ok = SetVCPFeature(target.handle, 0xd6, 1);
			Line("wake_attempt=" + std::to_string(attempt + 1) + " accepted=" + std::to_string(ok != FALSE) +
			     " error=" + std::to_string(ok ? 0 : GetLastError()));
			if (ok)
				return 0;
			Sleep(500);
		}
		return 13;
	}
	const bool power_off = mode == L"--pulse-samsung-off";
	const bool pulse = power_off || mode == L"--pulse-samsung";
	if ((!pulse && (argc != 3 || mode != L"--inspect")) || (pulse && argc != 4))
		return 1;
	const std::filesystem::path path(argv[2]);
	if (std::filesystem::exists(path))
		return 2;
	report.open(path);
	if (!report)
		return 3;
	Line(pulse ? "MonitorMage Samsung standby test; only VCP D6 standby/off/on are written."
	           : "MonitorMage DDC read-only probe; no settings are changed.");
	Topology();
	if (!pulse)
	{
		const BOOL ok = EnumDisplayMonitors(nullptr, nullptr, Inspect, 0);
		if (!ok)
			Line("enumeration_error=" + std::to_string(GetLastError()));
		return ok ? 0 : 4;
	}
	// This research test is specifically for the known Samsung + LG clone setup.
	// Never select a monitor by its physical-array index or generic description.
	const auto outputs = live::ActiveOutputs();
	if (outputs.size() != 1 || outputs[0].name != argv[3] ||
	    outputs[0].identity.find(L"DISPLAY#SAM0C4D#") == std::wstring::npos ||
	    outputs[0].identity.find(L"DISPLAY#GSMC0C0#") == std::wstring::npos)
	{
		Line("refused=expected Samsung and LG clone group not found");
		return 5;
	}
	Samsung target;
	if (!OpenSamsung(target, argv[3]))
	{
		Line("refused=Samsung could not be uniquely identified by capabilities");
		return 6;
	}
	const auto ready_name = L"Local\\MonitorMageDdcWake-" + std::to_wstring(GetCurrentProcessId());
	HANDLE ready = CreateEventW(nullptr, TRUE, FALSE, ready_name.c_str());
	if (!ready)
		return 7;
	wchar_t exe[32768] {};
	GetModuleFileNameW(nullptr, exe, 32768);
	auto command = QuoteArgument(exe) + L" --wake-samsung " + QuoteArgument(argv[3]) + L" " +
	               QuoteArgument(path.wstring() + L".wake.txt") + L" " + QuoteArgument(ready_name);
	STARTUPINFOW startup { sizeof(startup) };
	startup.dwFlags = STARTF_USESHOWWINDOW | STARTF_FORCEOFFFEEDBACK;
	startup.wShowWindow = SW_HIDE;
	PROCESS_INFORMATION child {};
	if (!CreateProcessW(exe, command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &startup,
	                    &child))
	{
		CloseHandle(ready);
		return 8;
	}
	CloseHandle(child.hThread);
	HANDLE waiting[] { ready, child.hProcess };
	const DWORD status = WaitForMultipleObjects(2, waiting, FALSE, 8000);
	CloseHandle(ready);
	if (status != WAIT_OBJECT_0)
	{
		Line("refused=wake helper not ready");
		CloseHandle(child.hProcess);
		return 9;
	}
	Line(power_off ? "wake_helper_ready=1; requesting power off for ten seconds"
	               : "wake_helper_ready=1; requesting standby for ten seconds");
	const BOOL accepted = SetVCPFeature(target.handle, 0xd6, power_off ? 5 : 4);
	Line("power_value=" + std::to_string(power_off ? 5 : 4) + " accepted=" + std::to_string(accepted != FALSE) +
	     " error=" + std::to_string(accepted ? 0 : GetLastError()));
	Sleep(1500);
	Line("after_1_5_seconds:");
	Topology();
	Sleep(3000);
	Line("after_4_5_seconds:");
	Topology();
	WaitForSingleObject(child.hProcess, 15000);
	DWORD exit = 99;
	GetExitCodeProcess(child.hProcess, &exit);
	CloseHandle(child.hProcess);
	Line("wake_helper_exit=" + std::to_string(exit));
	Sleep(1000);
	Line("after_wake:");
	Topology();
	return accepted && exit == 0 ? 0 : 14;
}
