// Read-only Magnification API inspection. No setter is loaded or called.
#include <windows.h>
#include <magnification.h>
#include <psapi.h>
#include <iostream>
#include <iomanip>
#include <vector>
#include <cmath>
#include <string>
#include "windows_controls.h"
namespace
{
void Memory(const wchar_t* stage)
{
	PROCESS_MEMORY_COUNTERS_EX m {};
	m.cb = sizeof(m);
	if (GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&m), sizeof(m)))
		std::wcout << stage << L" working_set_bytes=" << m.WorkingSetSize << L" private_bytes=" << m.PrivateUsage
				   << L'\n';
}

void Displays()
{
	UINT32 pc = 0, mc = 0;
	LONG result = GetDisplayConfigBufferSizes(QDC_ONLY_ACTIVE_PATHS, &pc, &mc);
	if (result)
	{
		std::wcout << L"display_query_error=" << result << L'\n';
		return;
	}
	std::vector<DISPLAYCONFIG_PATH_INFO> paths(pc);
	std::vector<DISPLAYCONFIG_MODE_INFO> modes(mc);
	result = QueryDisplayConfig(QDC_ONLY_ACTIVE_PATHS, &pc, paths.data(), &mc, modes.data(), nullptr);
	if (result)
	{
		std::wcout << L"display_query_error=" << result << L'\n';
		return;
	}
	std::wcout << L"active_display_paths=" << pc << L'\n';
	for (UINT32 i = 0; i < pc; ++i)
	{
		const auto& p = paths[i];
		DISPLAYCONFIG_TARGET_DEVICE_NAME name {};
		name.header = { DISPLAYCONFIG_DEVICE_INFO_GET_TARGET_NAME, sizeof(name), p.targetInfo.adapterId,
			            p.targetInfo.id };
		const auto named = DisplayConfigGetDeviceInfo(&name.header);
		DISPLAYCONFIG_GET_ADVANCED_COLOR_INFO color {};
		color.header = { DISPLAYCONFIG_DEVICE_INFO_GET_ADVANCED_COLOR_INFO, sizeof(color), p.targetInfo.adapterId,
			             p.targetInfo.id };
		const auto queried = DisplayConfigGetDeviceInfo(&color.header);
		std::wcout << L"display[" << i << L"]=" << (named ? L"unknown" : name.monitorFriendlyDeviceName)
				   << L" source_id=" << p.sourceInfo.id;
		if (!queried)
			std::wcout << L" advanced_color_supported=" << color.advancedColorSupported << L" advanced_color_enabled="
					   << color.advancedColorEnabled << L" bits_per_channel=" << color.bitsPerColorChannel;
		else
			std::wcout << L" advanced_color_query_error=" << queried;
		std::wcout << L'\n';
	}
}
}

int wmain(int argc, wchar_t** argv)
{
	if (argc > 2 || (argc == 2 && std::wstring(argv[1]) != L"--inspect"))
	{
		std::wcerr << L"Usage: MonitorMageMatrixProbe.exe [--inspect]\n";
		return 2;
	}
	std::wcout << L"MonitorMage matrix probe 1\nRead only: no matrix, LUT, HDR or Windows setting is written.\n";
	std::wcout << L"pointer_bits=" << sizeof(void*) * 8 << L" remote_session=" << GetSystemMetrics(SM_REMOTESESSION)
			   << L'\n';
	Displays();
	const auto filter = windows_controls::ReadColorFilterState();
	std::wcout << L"windows_filter_available=" << filter.available << L" active=" << filter.on << L" type="
			   << filter.filter << L'\n';
	Memory(L"before_dll_load");
	const auto dll = LoadLibraryExW(L"Magnification.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
	if (!dll)
	{
		std::wcout << L"dll_load_error=" << GetLastError() << L'\n';
		return 1;
	}
	using Init = BOOL(WINAPI*)();
	using Read = BOOL(WINAPI*)(PMAGCOLOREFFECT);
	using Zoom = BOOL(WINAPI*)(float*, int*, int*);
	const auto init = reinterpret_cast<Init>(GetProcAddress(dll, "MagInitialize"));
	const auto done = reinterpret_cast<Init>(GetProcAddress(dll, "MagUninitialize"));
	const auto read = reinterpret_cast<Read>(GetProcAddress(dll, "MagGetFullscreenColorEffect"));
	const auto zoom = reinterpret_cast<Zoom>(GetProcAddress(dll, "MagGetFullscreenTransform"));
	if (!init || !done || !read || !zoom)
	{
		std::wcout << L"required_exports_missing=1\n";
		FreeLibrary(dll);
		return 1;
	}
	Memory(L"after_dll_load");
	LARGE_INTEGER frequency {}, start {}, end {};
	QueryPerformanceFrequency(&frequency);
	QueryPerformanceCounter(&start);
	SetLastError(ERROR_SUCCESS);
	const bool initialized = init() != FALSE;
	const DWORD initError = initialized ? 0 : GetLastError();
	QueryPerformanceCounter(&end);
	std::wcout << std::fixed << std::setprecision(3) << L"initialize_ok=" << initialized << L" error=" << initError
			   << L" elapsed_ms=" << (end.QuadPart - start.QuadPart) * 1000.0 / frequency.QuadPart << L'\n';
	if (!initialized)
	{
		FreeLibrary(dll);
		return 1;
	}
	Memory(L"after_initialize");
	MAGCOLOREFFECT matrix {};
	SetLastError(ERROR_SUCCESS);
	const bool readable = read(&matrix) != FALSE;
	const auto readError = readable ? 0 : GetLastError();
	std::wcout << L"matrix_read_ok=" << readable << L" error=" << readError << L'\n';
	if (readable)
	{
		bool identity = true;
		for (int r = 0; r < 5; ++r)
		{
			for (int c = 0; c < 5; ++c)
			{
				const float value = matrix.transform[r][c];
				identity = identity && std::isfinite(value) && std::abs(value - (r == c ? 1.0f : 0.0f)) < 1e-6f;
				std::wcout << value << L' ';
			}
			std::wcout << L'\n';
		}
		std::wcout << L"matrix_is_identity=" << identity << L'\n';
	}
	float scale = 0;
	int x = 0, y = 0;
	const bool zoomRead = zoom(&scale, &x, &y) != FALSE;
	std::wcout << L"magnification_read_ok=" << zoomRead << L" scale=" << scale << L" offset_x=" << x << L" offset_y="
			   << y << L'\n';
	const bool stopped = done() != FALSE;
	std::wcout << L"uninitialize_ok=" << stopped << L'\n';
	FreeLibrary(dll);
	Memory(L"after_cleanup");
	std::wcout
		<< L"No setter was called. Readback does not prove visual coverage, HDR accuracy, gamma space, latency or GPU cost.\n"
		   L"Memory figures describe this short-lived probe, not MonitorMage or DWM rendering overhead.\n";
	return readable && stopped ? 0 : 1;
}
