// Program startup: reads the command line, then runs the main window or a helper mode.
#include "app_state.h"

#ifdef _MSC_VER
#pragma comment(                                                                                                       \
	linker,                                                                                                            \
	"\"/manifestdependency:type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")
#endif

using namespace app;

#include <optional>

struct StartupOptions
{
	bool render;
	std::wstring render_file, render_mode;
	bool render_tray, render_hdr, render_profiles, render_application;
	bool tray_exit_test, tray_startup_test, tray_benchmark;
	std::filesystem::path tray_benchmark_report;
	bool benchmark;
	std::filesystem::path benchmark_report;
};

static int RunIconSelfTest()
{
	if (app_icons::ChooseState(false, false, true) != app_icons::State::Disabled ||
	    app_icons::ChooseState(false, true, false) != app_icons::State::DisabledHdr ||
	    app_icons::ChooseState(false, true, true) != app_icons::State::DisabledHdr ||
	    app_icons::ChooseState(true, false, false) != app_icons::State::Active ||
	    app_icons::ChooseState(true, true, false) != app_icons::State::Hdr ||
	    app_icons::ChooseState(true, false, true) != app_icons::State::Application ||
	    app_icons::ChooseState(true, true, true) != app_icons::State::ApplicationHdr)
		return 1;
	for (const auto state : { app_icons::State::Disabled, app_icons::State::DisabledHdr, app_icons::State::Active,
	                          app_icons::State::Hdr, app_icons::State::Application, app_icons::State::ApplicationHdr })
	{
		for (int size : { 16, 20, 24, 32, 40, 48, 64, 96, 128, 256 })
		{
			HICON icon = icon_cache.GetIcon(app_instance, state, size);
			if (!icon)
				return 2;
			if (icon_cache.GetIcon(app_instance, state, size) != icon)
				return 3;
			ICONINFO info {};
			if (!GetIconInfo(icon, &info))
				return 4;
			BITMAP bitmap {};
			const bool correct = info.hbmColor && GetObjectW(info.hbmColor, sizeof(bitmap), &bitmap) &&
			                     bitmap.bmWidth == size && bitmap.bmHeight == size;
			if (info.hbmColor)
				DeleteObject(info.hbmColor);
			if (info.hbmMask)
				DeleteObject(info.hbmMask);
			if (!correct)
				return 5;
		}
	}
	return 0;
}

// Modes that do one job and exit without opening the editor. Returns nothing when the normal program should start.
static std::optional<int> RunCommandLineTool(int argument_count, LPWSTR* arguments)
{
	if (arguments && argument_count == 3 && std::wstring(arguments[1]) == L"--benchmark-matrix")
	{
		const int result = matrix_effect::Benchmark(arguments[2]);
		LocalFree(arguments);
		return result;
	}
	if (arguments && argument_count == 2 && std::wstring(arguments[1]) == L"--self-test-matrix-effect")
	{
		LocalFree(arguments);
		return matrix_effect::SelfTest();
	}
	if (arguments && argument_count == 4 &&
	    (std::wstring(arguments[1]) == L"--matrix-effect-worker" ||
	     std::wstring(arguments[1]) == L"--matrix-effect-probe"))
	{
		wchar_t* end = nullptr;
		const auto pid = wcstoul(arguments[2], &end, 10);
		const int result = pid && end && !*end
		                       ? matrix_effect::RunWorkerProcess(pid, arguments[3],
		                                                         std::wstring(arguments[1]) == L"--matrix-effect-probe")
		                       : 2;
		LocalFree(arguments);
		return result;
	}
	if (arguments && argument_count == 2 && std::wstring(arguments[1]) == L"--matrix-preview")
	{
		LocalFree(arguments);
		return matrix_preview::RunStandalone(app_instance);
	}
	if (arguments && argument_count == 2 && std::wstring(arguments[1]) == L"--self-test-matrix-preview")
	{
		const int result = matrix_preview::SelfTest(arguments[0]);
		LocalFree(arguments);
		return result;
	}
	if (arguments && argument_count == 2 && std::wstring(arguments[1]) == L"--matrix-preview-owner")
	{
		LocalFree(arguments);
		Sleep(INFINITE);
		return 0;
	}
	if (arguments && argument_count == 4 &&
	    (std::wstring(arguments[1]) == L"--matrix-worker" || std::wstring(arguments[1]) == L"--matrix-worker-probe"))
	{
		wchar_t* end = nullptr;
		const auto pid = wcstoul(arguments[2], &end, 10);
		const int result = pid && end && !*end
		                       ? matrix_preview::RunWorkerProcess(
									 pid, arguments[3], std::wstring(arguments[1]) == L"--matrix-worker-probe")
		                       : 2;
		LocalFree(arguments);
		return result;
	}
	if (arguments && argument_count == 2 && std::wstring(arguments[1]) == L"--self-test-icons")
	{
		LocalFree(arguments);
		return RunIconSelfTest();
	}
	if (arguments && argument_count == 2 && std::wstring(arguments[1]) == L"--self-test-hotkeys")
	{
		LocalFree(arguments);
		return RunHotkeySelfTest();
	}
	if (arguments && argument_count == 2 && std::wstring(arguments[1]) == L"--live-probe-owner")
	{
		LocalFree(arguments);
		Sleep(INFINITE);
		return 0;
	}
	if (arguments && argument_count == 7 &&
	    (std::wstring(arguments[1]) == L"--live-guard" || std::wstring(arguments[1]) == L"--live-guard-probe"))
	{
		wchar_t* end = nullptr;
		const unsigned long parent = wcstoul(arguments[3], &end, 10);
		const int result =
			parent && end && !*end
				? live::RunRecoveryGuardian(arguments[2], parent, arguments[4], arguments[5], arguments[6],
		                                    std::wstring(arguments[1]) == L"--live-guard-probe")
				: 1;
		LocalFree(arguments);
		return result;
	}
	if (arguments && argument_count == 2 &&
	    (std::wstring(arguments[1]) == L"--self-test-live" || std::wstring(arguments[1]) == L"--self-test-live-crash"))
	{
		const int result =
			live::RecoverySelfTest(arguments[0], std::wstring(arguments[1]) == L"--self-test-live-crash");
		LocalFree(arguments);
		return result;
	}
	if (arguments && argument_count == 3 && std::wstring(arguments[1]) == L"--recover-live")
	{
		const int result = live::RecoverFromSnapshotFile(arguments[2]);
		LocalFree(arguments);
		return result;
	}
	return std::nullopt;
}

// Reads the flags for the modes that use the real window. Frees the arguments. Returns nothing on bad input.
static std::optional<StartupOptions> ReadStartupOptions(int argument_count, LPWSTR* arguments)
{
	const bool render =
		(arguments && argument_count == 3 &&
	     (std::wstring(arguments[1]) == L"--render-small" || std::wstring(arguments[1]) == L"--render-small-profiles" ||
	      std::wstring(arguments[1]) == L"--render-mixer")) ||
		arguments && argument_count == 3 &&
			(std::wstring(arguments[1]) == L"--render-saturation" ||
	         std::wstring(arguments[1]) == L"--render-matrix-preview" ||
	         std::wstring(arguments[1]) == L"--render-preview" || std::wstring(arguments[1]) == L"--render-hdr" ||
	         std::wstring(arguments[1]) == L"--render-profiles" ||
	         std::wstring(arguments[1]) == L"--render-running-programs" ||
	         std::wstring(arguments[1]) == L"--render-application" || std::wstring(arguments[1]) == L"--render-tray" ||
	         std::wstring(arguments[1]) == L"--render-balance" || std::wstring(arguments[1]) == L"--render-curves" ||
	         std::wstring(arguments[1]) == L"--render-settings" ||
	         std::wstring(arguments[1]) == L"--render-color-filters" ||
	         std::wstring(arguments[1]) == L"--render-night-light" ||
	         std::wstring(arguments[1]) == L"--render-font-segoe" ||
	         std::wstring(arguments[1]) == L"--render-unsaved" || std::wstring(arguments[1]) == L"--render-error" ||
	         std::wstring(arguments[1]) == L"--render-carryover" || std::wstring(arguments[1]) == L"--render-ready" ||
	         std::wstring(arguments[1]) == L"--render-title-menu");
	const std::wstring render_file = render ? arguments[2] : L"";
	const std::wstring render_mode = render ? arguments[1] : L"";
	const bool render_tray = render && std::wstring(arguments[1]) == L"--render-tray";
	const bool render_hdr = render && std::wstring(arguments[1]) == L"--render-hdr";
	const bool render_profiles = render && (std::wstring(arguments[1]) == L"--render-profiles" ||
	                                        std::wstring(arguments[1]) == L"--render-small-profiles");
	const bool render_application = render && std::wstring(arguments[1]) == L"--render-application";
	const bool tray_exit_test =
		arguments && argument_count == 2 && std::wstring(arguments[1]) == L"--self-test-tray-exit";
	const bool tray_startup_test =
		tray_exit_test || arguments && argument_count == 2 && std::wstring(arguments[1]) == L"--self-test-tray-startup";
	const bool tray_benchmark =
		arguments && argument_count == 3 && std::wstring(arguments[1]) == L"--benchmark-tray-startup";
	const std::filesystem::path tray_benchmark_report = tray_benchmark ? arguments[2] : L"";
	if (tray_benchmark && std::filesystem::exists(tray_benchmark_report))
	{
		LocalFree(arguments);
		return std::nullopt;
	}
	const bool benchmark = arguments && argument_count == 3 && std::wstring(arguments[1]) == L"--benchmark-ui";
	const std::filesystem::path benchmark_report = benchmark ? arguments[2] : L"";
	if (benchmark && std::filesystem::exists(benchmark_report))
	{
		LocalFree(arguments);
		return std::nullopt;
	}
	profile_ui_test = arguments && argument_count == 2 && std::wstring(arguments[1]) == L"--self-test-profile-ui";
	effect_ui_test = arguments && argument_count == 2 && std::wstring(arguments[1]) == L"--self-test-effect-ui";
	const bool surfaces96 =
		arguments && argument_count == 2 && std::wstring(arguments[1]) == L"--self-test-surfaces-96";
	if (surfaces96 && !SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_UNAWARE))
	{
		LocalFree(arguments);
		return std::nullopt;
	}
	surface_ui_test =
		surfaces96 || (arguments && argument_count == 2 && std::wstring(arguments[1]) == L"--self-test-surfaces");
	preset_ui_test = tray_startup_test || tray_benchmark || benchmark || surface_ui_test || effect_ui_test ||
	                 profile_ui_test ||
	                 (arguments && argument_count == 2 && std::wstring(arguments[1]) == L"--self-test-preset-ui");
	if (arguments && argument_count == 3 && std::wstring(arguments[1]) == L"--idle-probe")
		idle_report = arguments[2];
	if (!idle_report.empty() && GetFileAttributesW(idle_report.c_str()) != INVALID_FILE_ATTRIBUTES)
	{
		LocalFree(arguments);
		return std::nullopt;
	}
	const bool preview_only = arguments && argument_count == 2 && std::wstring(arguments[1]) == L"--preview-ui";
	render_only = preview_only || render || !idle_report.empty() || preset_ui_test;
	if (arguments)
		LocalFree(arguments);
	return StartupOptions { render,
		                    render_file,
		                    render_mode,
		                    render_tray,
		                    render_hdr,
		                    render_profiles,
		                    render_application,
		                    tray_exit_test,
		                    tray_startup_test,
		                    tray_benchmark,
		                    tray_benchmark_report,
		                    benchmark,
		                    benchmark_report };
}

static bool RegisterWindowClasses()
{
	WNDCLASSW preview {};
	preview.hInstance = app_instance;
	preview.lpfnWndProc = CurveWindowProc;
	preview.style = CS_DBLCLKS;
	preview.lpszClassName = L"LutCurvePreview";
	preview.hCursor = LoadCursorW(nullptr, IDC_ARROW);
	WNDCLASSW main {};
	main.hInstance = app_instance;
	main.lpfnWndProc = MainWindowProc;
	main.lpszClassName = L"LutWorkbenchPreview";
	main.hCursor = preview.hCursor;
	main.hIcon = LoadIconW(app_instance, MAKEINTRESOURCEW(IDI_LUT_ACTIVE));
	main.hbrBackground = ui::BackgroundBrush();
	return RegisterClassW(&preview) && RegisterClassW(&main);
}

// Creates the main window centered on the monitor under the mouse cursor.
static bool CreateMainWindow()
{
	POINT cursor {};
	GetCursorPos(&cursor);
	HMONITOR monitor = MonitorFromPoint(cursor, MONITOR_DEFAULTTONEAREST);
	MONITORINFO monitor_info {};
	monitor_info.cbSize = sizeof(monitor_info);
	GetMonitorInfoW(monitor, &monitor_info);
	HWND probe =
		CreateWindowExW(0, L"STATIC", L"", WS_POPUP, cursor.x, cursor.y, 1, 1, nullptr, nullptr, app_instance, nullptr);
	if (probe)
	{
		dpi = static_cast<int>(GetDpiForWindow(probe));
		DestroyWindow(probe);
	}
	RECT rect { 0, 0, ScaleForDpi(CLIENT_WIDTH), ScaleForDpi(CLIENT_HEIGHT) };
	constexpr DWORD WINDOW_STYLE = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_CLIPCHILDREN;
	ui::AdjustThemedWindowRect(rect, static_cast<UINT>(dpi));
	const int width = rect.right - rect.left, height = rect.bottom - rect.top;
	const int x =
		monitor_info.rcWork.left + std::max(0L, (monitor_info.rcWork.right - monitor_info.rcWork.left - width) / 2);
	const int y =
		monitor_info.rcWork.top + std::max(0L, (monitor_info.rcWork.bottom - monitor_info.rcWork.top - height) / 2);
	main_window = CreateWindowExW(0, L"LutWorkbenchPreview", L"MonitorMage - live contrast and HDR correction",
	                              WINDOW_STYLE, x, y, width, height, nullptr, nullptr, app_instance, nullptr);
	return main_window != nullptr;
}

static void StartWatchingForegroundProgram()
{
	foreground_hook = SetWinEventHook(EVENT_SYSTEM_FOREGROUND, EVENT_SYSTEM_FOREGROUND, nullptr,
	                                  ForegroundChangedCallback, 0, 0, WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS);
	if (!foreground_hook)
	{
		profile_configuration.Enabled = false;
		profile_message = L"Foreground notifications unavailable. Application profiles are disabled.";
	}
	CheckForegroundProgram();
	if (foreground_program.FocusedProgram().empty())
		ApplyApplicationProfile(true);
	RefreshInterface();
}

static int RunTrayStartupTest(const StartupOptions& options, std::chrono::steady_clock::time_point setup_start)
{
	const auto milliseconds = [](auto a, auto b)
	{
		return std::chrono::duration<double, std::milli>(b - a).count();
	};
	const double setup_ms = milliseconds(setup_start, std::chrono::steady_clock::now());
	PROCESS_MEMORY_COUNTERS_EX usage {};
	usage.cb = sizeof(usage);
	GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&usage), sizeof(usage));
	int result = 0;
	RECT client {};
	GetClientRect(main_window, &client);
	if (client.right != ScaleForDpi(CLIENT_WIDTH) || client.bottom != ScaleForDpi(CLIENT_HEIGHT))
		result = 11;
	if (editor_ready || GetWindow(main_window, GW_CHILD) || ui_font || hint_font || curve_scratch.ByteCount() ||
	    preset_library[preset_role::Look].size() != 2)
		result = 1;
	const auto original = CurrentPreset(preset_role::Look).current;
	const auto original_index = preset_index[preset_role::Look];
	// A profile change must load its look and update processing state with
	// no controls. All hardware/session operations are disabled here.
	profile_configuration.Enabled = true;
	profile_configuration.Rules = { { L"C:\\Tests\\TrayGame.exe", PresetFolder(preset_role::Look) / L"B.lutpreset",
		                              applications::Action::Look } };
	corrections_requested = true;
	OnForegroundProgramChanged(L"C:\\Tests\\TrayGame.exe");
	if (!application_context_active || contrast_position != -10 || editor_ready || GetWindow(main_window, GW_CHILD))
		result = 2;
	ToggleBypass();
	if (!IsLookBypassed() || editor_ready)
		result = 3;
	ToggleBypass();
	OnForegroundProgramChanged(L"C:\\Tests\\Desktop.exe");
	if (application_context_active || preset_index[preset_role::Look] != original_index ||
	    CurrentPreset(preset_role::Look).current != original || contrast_position != original.Contrast)
		result = 4;
	ToggleCorrections();
	if (corrections_requested || editor_ready)
		result = 8;
	ToggleCorrections();
	if (!corrections_requested || editor_ready)
		result = 9;
	if (options.tray_exit_test)
	{
		DestroyWindow(main_window);
		CleanPresetUiTest();
		return result;
	}
	ShowProfiles(true);
	if (!profile_editor::ProfileEditorHandle() || editor_ready || GetWindow(main_window, GW_CHILD))
		result = 10;
	if (profile_editor::ProfileEditorHandle())
		DestroyWindow(profile_editor::ProfileEditorHandle());
	const auto open_start = std::chrono::steady_clock::now();
	CreateEditorIfNeeded();
	const double open_ms = milliseconds(open_start, std::chrono::steady_clock::now());
	const HWND first_canvas = curve_canvas;
	CreateEditorIfNeeded();
	if (!editor_ready || !curve_canvas || curve_canvas != first_canvas || !IsWindow(contrast_slider) ||
	    CurrentPreset(preset_role::Look).current != original || GetWindowTextLengthW(monitor_profile) == 0)
		result = 5;
	if (SendMessageW(contrast_slider, TBM_GETPOS, 0, 0) != original.Contrast ||
	    SelectedSourceIndex() != selected_source)
		result = 6;
	if (options.tray_benchmark)
	{
		std::ofstream out(options.tray_benchmark_report);
		out.imbue(std::locale::classic());
		out << "{\"tray_setup_ms\":" << setup_ms << ",\"first_editor_setup_ms\":" << open_ms
			<< ",\"tray_private_mib\":" << usage.PrivateUsage / 1048576.0
			<< ",\"tray_working_set_mib\":" << usage.WorkingSetSize / 1048576.0 << ",\"result\":" << result
			<< ",\"mode\":\"hidden fixture; no tray registration, hotkey registration or display changes; excludes OS loader and live helper startup\"}\n";
		if (!out)
			result = 7;
	}
	DestroyWindow(main_window);
	CleanPresetUiTest();
	return result;
}

static int RunUiBenchmark(const StartupOptions& options, std::chrono::steady_clock::time_point setup_start,
                          DWORD setup_fault_start)
{
	const auto now = []()
	{
		return std::chrono::steady_clock::now();
	};
	const auto elapsed_ms = [](auto a, auto b)
	{
		return std::chrono::duration<double, std::milli>(b - a).count();
	};
	const double setup_ms = elapsed_ms(setup_start, now());
	const DWORD setup_faults = ProcessPageFaults() - setup_fault_start;
	RECT bounds {};
	GetWindowRect(main_window, &bounds);
	HDC screen = GetDC(nullptr), memory = CreateCompatibleDC(screen);
	HBITMAP image = CreateCompatibleBitmap(screen, bounds.right - bounds.left, bounds.bottom - bounds.top);
	ReleaseDC(nullptr, screen);
	if (!memory || !image)
	{
		if (image)
			DeleteObject(image);
		if (memory)
			DeleteDC(memory);
		DestroyWindow(main_window);
		CleanPresetUiTest();
		return 1;
	}
	const auto old = SelectObject(memory, image);
	const auto paint = [&]()
	{
		SendMessageW(main_window, WM_PRINT, reinterpret_cast<WPARAM>(memory),
		             PRF_NONCLIENT | PRF_CLIENT | PRF_CHILDREN | PRF_ERASEBKGND);
		GdiFlush();
	};
	const DWORD first_fault_start = ProcessPageFaults();
	auto start = now();
	paint();
	const double first_ms = elapsed_ms(start, now());
	const DWORD first_faults = ProcessPageFaults() - first_fault_start;
	const DWORD repaint_fault_start = ProcessPageFaults();
	start = now();
	for (int i = 0; i < 10; ++i)
		paint();
	const double repaint_ms = elapsed_ms(start, now()) / 10;
	const DWORD repaint_faults = ProcessPageFaults() - repaint_fault_start;
	const auto full_buffer_bytes = last_graph_paint_bytes;
	const auto dirty = GraphUpdateBounds(curve_canvas);
	const int dirty_saved = SaveDC(memory);
	IntersectClipRect(memory, dirty.left, dirty.top, dirty.right, dirty.bottom);
	const DWORD dirty_fault_start = ProcessPageFaults();
	start = now();
	for (int i = 0; i < 10; ++i)
	{
		SendMessageW(curve_canvas, WM_PRINTCLIENT, reinterpret_cast<WPARAM>(memory), PRF_CLIENT);
		GdiFlush();
	}
	const double dirty_ms = elapsed_ms(start, now()) / 10;
	const DWORD dirty_faults = ProcessPageFaults() - dirty_fault_start;
	const auto dirty_buffer_bytes = last_graph_paint_bytes;
	RestoreDC(memory, dirty_saved);
	PROCESS_MEMORY_COUNTERS_EX usage {};
	usage.cb = sizeof(usage);
	GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&usage), sizeof(usage));
	SelectObject(memory, old);
	DeleteObject(image);
	DeleteDC(memory);
	const auto tile_bytes = curve_scratch.ByteCount();
	SendMessageW(main_window, WM_SHOWWINDOW, FALSE, 0);
	PROCESS_MEMORY_COUNTERS_EX hidden {};
	hidden.cb = sizeof(hidden);
	GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&hidden), sizeof(hidden));
	start = now();
	DestroyWindow(main_window);
	const double shutdown_ms = elapsed_ms(start, now());
	start = now();
	const int helper = live::RecoverySelfTest(app_settings::ExecutablePath().c_str());
	const double helper_ms = elapsed_ms(start, now());
	CleanPresetUiTest();
	std::ofstream report(options.benchmark_report);
	report.imbue(std::locale::classic());
	report
		<< "{\"editor_setup_ms\":" << setup_ms << ",\"first_paint_ms\":" << first_ms
		<< ",\"mean_repaint_ms\":" << repaint_ms << ",\"setup_page_faults\":" << setup_faults
		<< ",\"first_paint_page_faults\":" << first_faults << ",\"repeated_paint_page_faults\":" << repaint_faults
		<< ",\"paint_count\":10,\"fault_counter\":\"process total; does not distinguish hard from soft faults\""
		<< ",\"full_graph_buffer_mib\":" << full_buffer_bytes / 1048576.0
		<< ",\"plot_update_buffer_mib\":" << dirty_buffer_bytes / 1048576.0 << ",\"plot_update_mean_ms\":" << dirty_ms
		<< ",\"plot_update_page_faults\":" << dirty_faults << ",\"graph_surface_bits\":32"
		<< ",\"editor_shutdown_ms\":" << shutdown_ms << ",\"probe_helper_roundtrip_ms\":" << helper_ms
		<< ",\"helper_ok\":" << (helper == 0 ? "true" : "false")
		<< ",\"peak_working_set_mib\":" << usage.PeakWorkingSetSize / 1048576.0
		<< ",\"private_commit_mib\":" << usage.PrivateUsage / 1048576.0
		<< ",\"curve_tile_mib\":" << tile_bytes / 1048576.0
		<< ",\"after_hide_private_commit_mib\":" << hidden.PrivateUsage / 1048576.0 << ",\"dpi\":" << dpi
		<< ",\"mode\":\"hidden fixture editor; helper probe never reads or writes a display LUT; excludes OS loader, real user library and actual LUT restoration\"}\n";
	return report && helper == 0 ? 0 : 1;
}

static int RunInterfaceTest()
{
	const int result = surface_ui_test   ? RunSurfaceUiTest()
	                   : effect_ui_test  ? RunEffectUiTest()
	                   : profile_ui_test ? RunProfileUiTest()
	                                     : RunPresetUiTest();
	DestroyWindow(main_window);
	CleanPresetUiTest();
	return result;
}

// Sets up the example shown by one --render mode. Returns the window to capture, or null if it could not be opened.
static HWND PrepareRenderScene(const StartupOptions& options)
{
	HWND target = main_window;
	if (options.render_mode == L"--render-saturation")
	{
		selected_effect = effect::Saturation;
		CurrentPreset(preset_role::Look).current.SaturationEnabled = true;
		CurrentPreset(preset_role::Look).current.Saturation = 50;
		corrections_requested = true;
		RefreshInterface();
	}
	if (options.render_mode == L"--render-mixer")
	{
		selected_effect = effect::ChannelMixer;
		CurrentPreset(preset_role::Look).current.MonochromeEnabled = true;
		CurrentPreset(preset_role::Look).current.Monochrome = { 100, 20, 0 };
		corrections_requested = true;
		RefreshInterface();
	}
	if (options.render_mode == L"--render-matrix-preview")
	{
		matrix_preview::ShowPreviewWindow(main_window, true);
		target = matrix_preview::PreviewWindowHandle();
		if (!target)
			return nullptr;
	}
	if (options.render_application)
	{
		presets::Data data;
		data.Contrast = 15;
		preset_library[preset_role::Look].push_back({ L"C:\\Examples\\Gentle contrast.lutpreset", data, data });
		preset_index[preset_role::Look] = 1;
		application_context_active = true;
		profile_configuration.Enabled = true;
		foreground_program.UpdateFocusedProgram(L"C:\\Games\\Example game.exe");
		profile_action = applications::Action::PauseHdr;
		LoadPresetControls();
	}
	if (options.render_profiles || options.render_mode == L"--render-running-programs")
	{
		const auto data = preset_library[preset_role::Look][0].current;
		preset_library[preset_role::Look].push_back({ L"C:\\Examples\\Neutral.lutpreset", data, data });
		preset_library[preset_role::Look].push_back({ L"C:\\Examples\\Gentle contrast.lutpreset", data, data });
		profile_configuration.Rules = { { L"C:\\Games\\Example game.exe", preset_library[preset_role::Look][2].path,
			                              applications::Action::Look },
			                            { L"C:\\Video\\HDR player.exe", {}, applications::Action::PauseHdr } };
		ShowProfiles(true);
		target = profile_editor::ProfileEditorHandle();
		if (!target)
			return nullptr;
		SendMessageW(GetDlgItem(target, 203), LB_SETCURSEL, 1, 0);
		SendMessageW(target, WM_COMMAND, MAKEWPARAM(203, LBN_SELCHANGE), 0);
	}
	if (options.render_mode == L"--render-running-programs")
		profile_editor::PreviewPrograms(
			{ { L"C:\\Games\\Example game.exe", L"Example game", SIZE_T { 2400 } * 1048576, true, true },
		      { L"C:\\Video\\vlc.exe", L"VLC media player", SIZE_T { 420 } * 1048576, true, true },
		      { L"C:\\Browser\\browser.exe", L"Web browser", SIZE_T { 1300 } * 1048576, true, true } });
	if (options.render_hdr || options.render_application)
	{
		selected_effect = effect::HdrGamma;
		RefreshPresetChoice();
		RefreshInterface();
	}
	if (options.render_mode == L"--render-balance")
	{
		selected_effect = effect::ColorBalance;
		CurrentPreset(preset_role::Look).current.Adjustments.Balance = { 100, 100, 100, 20, 0 };
		RefreshInterface();
	}
	if (options.render_mode == L"--render-curves")
	{
		selected_effect = effect::CustomCurves;
		auto& curves = CurrentPreset(preset_role::Look).current.Adjustments.Curves;
		curves[0] = { 0, .09, .2, .33, .5, .67, .8, .91, 1 };
		curves[3] = { 0, .06, .15, .26, .4, .56, .72, .87, 1 };
		RefreshInterface();
	}
	if (options.render_mode == L"--render-error")
	{
		live_message = ui::messages::FriendlyError("Driver rejected the correction");
		live_error = true;
		RefreshInterface();
	}
	if (options.render_mode == L"--render-carryover")
	{
		live_message =
			L"The previous correction carried into this display mode. Restore Windows calibration before re-enabling";
		live_error = true;
		RefreshInterface();
	}
	if (options.render_mode == L"--render-ready")
	{
		TurnOnCorrections();
	}
	if (options.render_mode == L"--render-settings")
	{
		settings_page = true;
		LayoutEditorControls();
	}
	if (options.render_mode == L"--render-color-filters")
	{
		settings_page = false;
		selected_effect = effect::ColorFilters;
		LayoutEditorControls();
	}
	if (options.render_mode == L"--render-night-light")
	{
		settings_page = false;
		selected_effect = effect::NightLight;
		night_config.Enabled = true;
		UpdateNightLightSchedule();
		LayoutEditorControls();
	}
	if (options.render_mode == L"--render-font-segoe")
	{
		ui::SetFontFamily(L"Segoe UI");
		LayoutEditorControls();
	}
	if (options.render_mode == L"--render-unsaved")
	{
		target = unsaved::PreviewDialog(main_window);
		if (!target)
			return nullptr;
	}
	if (options.render_mode == L"--render-small")
	{
		selected_effect = effect::ColorBalance;
		RefreshInterface();
		SetWindowPos(main_window, nullptr, 0, 0, ScaleForDpi(480), ScaleForDpi(360),
		             SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
		ui::RevealControl(main_window, adjustment_sliders[4]);
	}
	if (options.render_mode == L"--render-small-profiles")
	{
		SetWindowPos(target, nullptr, 0, 0, ScaleForDpi(300), ScaleForDpi(340),
		             SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
		ui::RevealControl(target, GetDlgItem(target, 213));
	}
	return target;
}

static int RenderScreenshot(const StartupOptions& options)
{
	if (options.render_mode == L"--render-title-menu")
	{
		const bool rendered = ui::PreviewCaptionMenu(
			main_window, [&](HWND target) { return RenderWindowToFile(options.render_file.c_str(), target); });
		DestroyWindow(main_window);
		return rendered ? 0 : 1;
	}
	if (options.render_tray)
	{
		const bool rendered =
			ui::PreviewPopup(main_window, IconForState(SM_CXICON), TrayMenuItems(),
		                     [&](HWND target) { return RenderWindowToFile(options.render_file.c_str(), target); });
		DestroyWindow(main_window);
		return rendered ? 0 : 1;
	}
	const HWND target = PrepareRenderScene(options);
	if (!target)
	{
		DestroyWindow(main_window);
		return 1;
	}
	if (target == main_window)
	{
		ui::SetCaptionSettingsButton(main_window, ID_SETTINGS_PAGE, settings_page);
		ui::SetIconButton(power_button, ui::Glyph::Power, corrections_requested, true);
	}
	const bool rendered = RenderWindowToFile(options.render_file.c_str(), target);
	DestroyWindow(main_window);
	return rendered ? 0 : 1;
}

static int RunMessageLoop()
{
	MSG message {};
	int result = 0;
	while ((result = matrix_session.WaitForMessageOrWorkerExit(message)) > 0)
	{
		if (matrix_preview::HandleDialogMessage(message))
			continue;
		if (NavigateTab(message))
			continue;
		if (GetFocus() != shortcut_control && message.message == WM_SYSKEYDOWN &&
		    (message.wParam == L'P' || message.wParam == L'S') && (message.lParam & (1L << 29)) &&
		    GetAncestor(message.hwnd, GA_ROOT) == main_window)
		{
			if (!(message.lParam & (1L << 30)))
				SendMessageW(main_window, WM_COMMAND, message.wParam == L'P' ? ID_TEST_CONTRAST : ID_SETTINGS_PAGE, 0);
			continue;
		}
		if (profile_editor::ProfileEditorHandle() && IsDialogMessageW(profile_editor::ProfileEditorHandle(), &message))
		{
			ui::RevealControl(profile_editor::ProfileEditorHandle(), GetFocus());
			continue;
		}
		if (!IsDialogMessageW(main_window, &message))
		{
			TranslateMessage(&message);
			DispatchMessageW(&message);
		}
	}
	return result < 0 ? 1 : static_cast<int>(message.wParam);
}

static int RunSelectedMode(const StartupOptions& options, std::chrono::steady_clock::time_point setup_start,
                           DWORD setup_fault_start, int show)
{
	if (options.tray_startup_test || options.tray_benchmark)
		return RunTrayStartupTest(options, setup_start);
	if (options.benchmark)
		return RunUiBenchmark(options, setup_start, setup_fault_start);
	if (preset_ui_test)
		return RunInterfaceTest();
	if (options.render)
		return RenderScreenshot(options);
	if (!idle_report.empty())
	{
		idle_cpu_start = ProcessCpuTime();
		idle_start = GetTickCount64();
		idle_fault_start = ProcessPageFaults();
		if (!SetTimer(main_window, TIMER_IDLE, 3000, nullptr))
		{
			DestroyWindow(main_window);
			return 1;
		}
	}
	else if (render_only || !start_minimized || !tray_installed)
	{
		ShowWindow(main_window, show);
		UpdateWindow(main_window);
	}
	return RunMessageLoop();
}

// The program starts here.
int WINAPI wWinMain(HINSTANCE app, HINSTANCE, PWSTR, int show)
{
	app_instance = app;
	SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
	int argument_count = 0;
	LPWSTR* arguments = CommandLineToArgvW(GetCommandLineW(), &argument_count);
	if (const auto tool_result = RunCommandLineTool(argument_count, arguments))
		return *tool_result;
	const auto parsed_options = ReadStartupOptions(argument_count, arguments);
	if (!parsed_options)
		return 1;
	const StartupOptions& options = *parsed_options;
	if (preset_ui_test && !PreparePresetUiTest())
		return 1;
	const auto mutex_name = render_only ? L"Local\\LutWorkbenchRender-" + std::to_wstring(GetCurrentProcessId())
	                                    : std::wstring(L"Local\\LutWorkbenchPreview");
	HANDLE mutex = CreateMutexW(nullptr, FALSE, mutex_name.c_str());
	if (!mutex)
		return 1;
	if (GetLastError() == ERROR_ALREADY_EXISTS)
	{
		HWND existing = FindWindowW(L"LutWorkbenchPreview", nullptr);
		if (existing)
		{
			ShowWindow(existing, SW_SHOW);
			SetForegroundWindow(existing);
		}
		CloseHandle(mutex);
		return 0;
	}
	defer_editor = options.tray_startup_test || options.tray_benchmark ||
	               !render_only && GetPrivateProfileIntW(L"Startup", L"Minimized", 0, SettingsFilePath().c_str()) == 1;
	const DWORD setup_fault_start = options.benchmark ? ProcessPageFaults() : 0;
	const auto setup_start = std::chrono::steady_clock::now();
	taskbar_created_message = RegisterWindowMessageW(L"TaskbarCreated");
	if (!RegisterWindowClasses() || !CreateMainWindow())
	{
		CloseHandle(mutex);
		return 1;
	}
	if (!render_only || !idle_report.empty())
		StartWatchingForegroundProgram();
	if (!render_only)
		TurnOnCorrections();
	const int exit_code = RunSelectedMode(options, setup_start, setup_fault_start, show);
	CloseHandle(mutex);
	return exit_code;
}
