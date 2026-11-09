#include "matrix_effect.h"
#include "app_settings.h"
#include "matrix_effect_rules.h"
#include "matrix_recovery.h"
#include "matrix_api.h"
#include "windows_controls.h"
#include <filesystem>
#include <fstream>
#include <string>
#include <stdexcept>
#include <psapi.h>
#include <vector>
#include <sstream>
#include "app_version.h"

namespace matrix_effect
{
namespace
{
constexpr DWORD SHARED_MEMORY_MAGIC = 0x434d5831;
struct Shared
{
	DWORD magic = SHARED_MEMORY_MAGIC;
	volatile LONG wanted = -1, revision = 0, processed = 0, state = static_cast<LONG>(State::Off), mono = 0,
				  mix[3] { 21, 72, 7 };
	UINT_PTR owner = 0;
};

struct Handle
{
	HANDLE handle = nullptr;
	explicit Handle(HANDLE value = nullptr) : handle(value) {}
	~Handle()
	{
		if (handle && handle != INVALID_HANDLE_VALUE)
			CloseHandle(handle);
	}
	Handle(const Handle&) = delete;
	Handle& operator=(const Handle&) = delete;
};

struct Mapping
{
	Shared* shared = nullptr;
	explicit Mapping(HANDLE h)
	{
		if (h)
			shared = static_cast<Shared*>(MapViewOfFile(h, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(Shared)));
	}
	~Mapping()
	{
		if (shared)
			UnmapViewOfFile(shared);
	}
};

using app_settings::ExecutablePath;

std::wstring SharedObjectName(const wchar_t* token, const wchar_t* suffix)
{
	return L"Local\\MonitorMageEffect-" + std::wstring(token) + suffix;
}

bool IsValidToken(const wchar_t* token)
{
	if (!token || !*token || wcslen(token) > 80)
		return false;
	for (const auto* c = token; *c; ++c)
		if ((*c < L'0' || *c > L'9') && *c != L'-')
			return false;
	return true;
}

LONG AtomicRead(volatile LONG& value)
{
	return InterlockedCompareExchange(&value, 0, 0);
}

struct Fake final : matrix_color::Api
{
	matrix_color::Matrix value = matrix_color::IdentityMatrix();
	bool reject = false;
	unsigned writes = 0;
	bool Read(matrix_color::Matrix& matrix) override
	{
		matrix = value;
		return true;
	}
	bool Write(const matrix_color::Matrix& matrix) override
	{
		++writes;
		if (reject)
			return false;
		value = matrix;
		return true;
	}
};
}

struct Controller::Impl
{
	Handle mapping, event, process;
	std::unique_ptr<Mapping> view;
	int amount = -1;
	bool exit_reported = false, mono = false;
	std::array<int, 3> mix { 21, 72, 7 };
	HWND owner = nullptr;
	~Impl()
	{
		if (view && view->shared)
		{
			InterlockedIncrement(&view->shared->revision);
			InterlockedExchange(&view->shared->wanted, -2);
			InterlockedIncrement(&view->shared->revision);
			if (event.handle)
				SetEvent(event.handle);
		}
	}
};

Controller::Controller() = default;
Controller::~Controller() = default;
int Controller::WaitForMessageOrWorkerExit(MSG& message)
{
	for (;;)
	{
		if (!_impl || _impl->exit_reported)
			return static_cast<int>(GetMessageW(&message, nullptr, 0, 0));
		const HANDLE process = _impl->process.handle;
		const DWORD wake = MsgWaitForMultipleObjectsEx(1, &process, INFINITE, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
		if (wake == WAIT_FAILED)
			return -1;
		if (wake == WAIT_OBJECT_0)
		{
			_impl->exit_reported = true;
			message = {};
			message.hwnd = _impl->owner;
			message.message = MESSAGE_MATRIX_CHANGED;
			return 1;
		}
		// Peek also dispatches synchronous sent messages. One of those may
		// replace the session, so re-read _impl before waiting again.
		if (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE))
			return message.message == WM_QUIT ? 0 : 1;
	}
}

void Controller::Stop()
{
	if (_impl)
		Update(nullptr, -1);
}

State Controller::CurrentState() const
{
	if (!_impl)
		return State::Off;
	auto& shared = *_impl->view->shared;
	if (WaitForSingleObject(_impl->process.handle, 0) != WAIT_TIMEOUT)
		return _impl->amount < 0 && AtomicRead(shared.state) == static_cast<LONG>(State::Off) &&
		               AtomicRead(shared.revision) == AtomicRead(shared.processed)
		           ? State::Off
		           : State::Failed;
	if (AtomicRead(shared.revision) != AtomicRead(shared.processed))
		return State::Starting;
	return static_cast<State>(AtomicRead(shared.state));
}

void Controller::Update(HWND owner, int amount, bool refresh, bool fake, const std::array<int, 3>* monochrome)
{
	if (amount < -1 || amount > color::SATURATION_MAXIMUM)
		throw std::invalid_argument("Invalid saturation");
	if (monochrome)
		matrix_color::MonochromeMatrix(*monochrome); // Validate before starting or changing the worker.
	if (amount == 100 && !monochrome)
		amount = -1;
	const bool mono = monochrome && amount >= 0;
	const auto mix = mono ? *monochrome : std::array<int, 3> { 21, 72, 7 };
	if (_impl && WaitForSingleObject(_impl->process.handle, 0) != WAIT_TIMEOUT)
		_impl.reset();
	if (!_impl)
	{
		if (amount < 0)
			return;
		auto next = std::make_unique<Impl>();
		next->owner = owner;
		static unsigned serial = 0;
		const auto token = std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64()) + L"-" +
		                   std::to_wstring(++serial);
		next->mapping.handle = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, sizeof(Shared),
		                                          SharedObjectName(token.c_str(), L"-map").c_str());
		if (!next->mapping.handle || GetLastError() == ERROR_ALREADY_EXISTS)
			throw std::runtime_error("Color effect is unavailable");
		next->view = std::make_unique<Mapping>(next->mapping.handle);
		next->event.handle = CreateEventW(nullptr, FALSE, FALSE, SharedObjectName(token.c_str(), L"-event").c_str());
		if (!next->view->shared || !next->event.handle)
			throw std::runtime_error("Cannot prepare color effect");
		*next->view->shared = Shared {};
		next->view->shared->owner = reinterpret_cast<UINT_PTR>(owner);
		auto command = L"\"" + ExecutablePath().wstring() + L"\" " +
		               (fake ? L"--matrix-effect-probe " : L"--matrix-effect-worker ") +
		               std::to_wstring(GetCurrentProcessId()) + L" " + token;
		STARTUPINFOW startup { sizeof(startup) };
		startup.dwFlags = STARTF_USESHOWWINDOW | STARTF_FORCEOFFFEEDBACK;
		startup.wShowWindow = SW_HIDE;
		PROCESS_INFORMATION child {};
		if (!CreateProcessW(ExecutablePath().c_str(), command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW,
		                    nullptr, nullptr, &startup, &child))
			throw std::runtime_error("Cannot start color effect");
		next->process.handle = child.hProcess;
		CloseHandle(child.hThread);
		_impl = std::move(next);
	}
	if (_impl->amount == amount && _impl->mono == mono && _impl->mix == mix &&
	    (!refresh || (amount < 0 && CurrentState() == State::Off)))
		return;
	_impl->amount = amount;
	_impl->mono = mono;
	_impl->mix = mix;
	auto& shared = *_impl->view->shared;
	// Odd/even publication prevents a rapid edit from mixing channels from
	// different requests; the worker never spins waiting for an editor.
	InterlockedIncrement(&shared.revision);
	InterlockedExchange(&shared.wanted, amount);
	InterlockedExchange(&shared.mono, mono);
	for (int i = 0; i < 3; ++i)
		InterlockedExchange(&shared.mix[i], mix[i]);
	InterlockedIncrement(&shared.revision);
	SetEvent(_impl->event.handle);
}

namespace
{
std::string ToUtf8(const wchar_t* text)
{
	const int n = WideCharToMultiByte(CP_UTF8, 0, text, -1, nullptr, 0, nullptr, nullptr);
	if (n < 1)
		return {};
	std::string utf8_text(n, '\0');
	WideCharToMultiByte(CP_UTF8, 0, text, -1, utf8_text.data(), n, nullptr, nullptr);
	utf8_text.pop_back();
	return utf8_text;
}

std::string ProcessName(DWORD pid)
{
	Handle process(OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid));
	wchar_t image[32768] {};
	DWORD length = 32768;
	return process.handle && QueryFullProcessImageNameW(process.handle, 0, image, &length)
	           ? ToUtf8(std::filesystem::path(image).filename().c_str())
	           : std::string {};
}

void DisplayReport(std::ostream& out)
{
	OSVERSIONINFOW version {};
	version.dwOSVersionInfoSize = sizeof(version);
	using Version = LONG(WINAPI*)(OSVERSIONINFOW*);
	const auto query = reinterpret_cast<Version>(GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "RtlGetVersion"));
	if (query && query(&version) == 0)
		out << "Windows=" << version.dwMajorVersion << '.' << version.dwMinorVersion
			<< " build=" << version.dwBuildNumber << '\n';
	for (DWORD i = 0;; ++i)
	{
		DISPLAY_DEVICEW device {};
		device.cb = sizeof(device);
		if (!EnumDisplayDevicesW(nullptr, i, &device, 0))
			break;
		if (device.StateFlags & DISPLAY_DEVICE_ATTACHED_TO_DESKTOP)
			out << "adapter=" << ToUtf8(device.DeviceString) << " device=" << ToUtf8(device.DeviceName) << '\n';
	}
	UINT32 path_count = 0, mode_count = 0;
	LONG error = GetDisplayConfigBufferSizes(QDC_ONLY_ACTIVE_PATHS, &path_count, &mode_count);
	if (error)
	{
		out << "display_query_error=" << error << '\n';
		return;
	}
	std::vector<DISPLAYCONFIG_PATH_INFO> paths(path_count);
	std::vector<DISPLAYCONFIG_MODE_INFO> modes(mode_count);
	error = QueryDisplayConfig(QDC_ONLY_ACTIVE_PATHS, &path_count, paths.data(), &mode_count, modes.data(), nullptr);
	if (error)
	{
		out << "display_query_error=" << error << '\n';
		return;
	}
	for (UINT32 i = 0; i < path_count; ++i)
	{
		const auto& path = paths[i];
		out << "display[" << i << "] source=" << path.sourceInfo.id;
		if (path.sourceInfo.modeInfoIdx < mode_count &&
		    modes[path.sourceInfo.modeInfoIdx].infoType == DISPLAYCONFIG_MODE_INFO_TYPE_SOURCE)
		{
			const auto& source_mode = modes[path.sourceInfo.modeInfoIdx].sourceMode;
			out << " resolution=" << source_mode.width << 'x' << source_mode.height;
		}
		DISPLAYCONFIG_TARGET_DEVICE_NAME name {};
		name.header = { DISPLAYCONFIG_DEVICE_INFO_GET_TARGET_NAME, sizeof(name), path.targetInfo.adapterId,
			            path.targetInfo.id };
		if (!DisplayConfigGetDeviceInfo(&name.header))
			out << " name=" << ToUtf8(name.monitorFriendlyDeviceName);
		DISPLAYCONFIG_GET_ADVANCED_COLOR_INFO color {};
		color.header = { DISPLAYCONFIG_DEVICE_INFO_GET_ADVANCED_COLOR_INFO, sizeof(color), path.targetInfo.adapterId,
			             path.targetInfo.id };
		const auto advanced = DisplayConfigGetDeviceInfo(&color.header);
		out << " advanced_color_query=" << advanced;
		if (!advanced)
			out << " advanced_color_enabled=" << color.advancedColorEnabled
				<< " bits_per_channel=" << color.bitsPerColorChannel;
		out << '\n';
	}
}
}

std::filesystem::path Controller::WriteReport() const
{
	const auto folder = ExecutablePath().parent_path() / L"reports" / L"matrix-effects";
	std::filesystem::create_directories(folder);
	static unsigned serial = 0;
	const auto path = folder / (L"compatibility-" + std::to_wstring(GetCurrentProcessId()) + L"-" +
	                            std::to_wstring(GetTickCount64()) + L"-" + std::to_wstring(++serial) + L".txt");
	std::ofstream out(path);
	if (!out)
		throw std::runtime_error("Cannot create saturation report");
	out << "MonitorMage " MONITORMAGE_VERSION " saturation compatibility report (UTF-8)\n"
		<< "Read-only snapshot. No color, HDR or fullscreen settings were changed.\n"
		<< "State: 0=off,1=starting,2=active,4=other_tool,5=failed,6=external_change\n"
		<< "saturation=" << (_impl ? _impl->amount : -1) << " state=" << static_cast<int>(CurrentState()) << '\n';
	if (_impl)
		out << "worker_pid=" << GetProcessId(_impl->process.handle)
			<< " revision=" << AtomicRead(_impl->view->shared->revision)
			<< " processed=" << AtomicRead(_impl->view->shared->processed) << '\n';
	if (_impl)
		out << "monochrome=" << _impl->mono << " mix=" << _impl->mix[0] << ',' << _impl->mix[1] << ',' << _impl->mix[2]
			<< '\n';
	const auto filters = windows_controls::ReadColorFilterState();
	out << "Windows_filters_available=" << filters.available << " on=" << filters.on << " type=" << filters.filter
		<< '\n';
	DWORD pid = 0;
	GetWindowThreadProcessId(GetForegroundWindow(), &pid);
	out << "foreground_pid=" << pid;
	out << " executable=" << ProcessName(pid) << '\n';
	DisplayReport(out);
	out << "\nThe sibling effect-*.txt logs record matrix resets and readback when switching applications or display modes.\n"
		<< "Active/matching readback confirms Windows accepted a matrix; it cannot prove a game displays it.\n"
		<< "This report does not detect exclusive vs borderless presentation.\n"
		<< "Compare Grayscale in the game and on the desktop. Try bypass twice after the resolution switch.\n"
		<< "If readback stays active while only the game remains colored, record the game's API and fullscreen setting.\n";
	out.flush();
	if (!out)
		throw std::runtime_error("Cannot save saturation report");
	return path;
}

int RunWorkerProcess(DWORD parent_id, const wchar_t* token, bool fake)
{
	if (!parent_id || !IsValidToken(token))
		return 2;
	Handle parent(OpenProcess(SYNCHRONIZE, FALSE, parent_id));
	Handle event(OpenEventW(SYNCHRONIZE, FALSE, SharedObjectName(token, L"-event").c_str()));
	Handle mapping(OpenFileMappingW(FILE_MAP_ALL_ACCESS, FALSE, SharedObjectName(token, L"-map").c_str()));
	Mapping view(mapping.handle);
	if (!parent.handle || !event.handle || !view.shared || view.shared->magic != SHARED_MEMORY_MAGIC)
		return 3;
	auto& shared = *view.shared;
	const HWND owner = reinterpret_cast<HWND>(shared.owner);
	DWORD owner_pid = 0;
	if (owner)
	{
		GetWindowThreadProcessId(owner, &owner_pid);
		if (owner_pid != parent_id)
			return 4;
	}
	matrix_color::WindowsApi api;
	Fake mock;
	matrix_color::Api& backend = fake ? static_cast<matrix_color::Api&>(mock) : api;
	Engine engine(backend);
	bool opened = fake;
	std::ofstream report;
	const auto restore = [&]()
	{
		auto state = engine.Stop();
		for (int retry = 0; state == State::Failed && retry < 2; ++retry)
		{
			Sleep(50);
			state = engine.Stop();
		}
		return state;
	};
	if (!fake)
		try
		{
			const auto folder = ExecutablePath().parent_path() / L"reports" / L"matrix-effects";
			std::filesystem::create_directories(folder);
			report.open(folder / (L"effect-" + std::to_wstring(GetCurrentProcessId()) + L"-" +
			                      std::to_wstring(GetTickCount64()) + L".txt"));
			report
				<< "MonitorMage saturation worker. State: 0=off,1=starting,2=active,4=other_tool,5=failed,6=external_change\n"
				<< "Active means matching API readback; it does not prove coverage of an exclusive fullscreen game.\n";
		}
		catch (...)
		{
		}
	unsigned entries = 0;
	DWORD previous_pid = 0;
	std::string foreground_name;
	LONG previous_amount = -2;
	const auto log = [&](const char* action, LONG amount, State state)
	{
		if (report && entries++ < 1024)
		{
			DWORD foreground = 0;
			GetWindowThreadProcessId(GetForegroundWindow(), &foreground);
			if (foreground != previous_pid)
			{
				foreground_name = ProcessName(foreground);
				previous_pid = foreground;
			}
			report << "time_ms=" << GetTickCount64() << " action=" << action << " saturation=" << amount
				   << " state=" << static_cast<int>(state) << " foreground_pid=" << foreground
				   << " foreground_executable=" << foreground_name << " resolution=" << GetSystemMetrics(SM_CXSCREEN)
				   << 'x' << GetSystemMetrics(SM_CYSCREEN) << '\n';
			report.flush();
		}
	};
	HANDLE wait[] { parent.handle, event.handle };
	bool ending = false;
	while (!ending)
	{
		const DWORD wake = MsgWaitForMultipleObjects(2, wait, FALSE, INFINITE, QS_ALLINPUT);
		if (wake == WAIT_OBJECT_0 || wake == WAIT_FAILED)
			break;
		if (wake == WAIT_OBJECT_0 + 2)
		{
			MSG message {};
			while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE))
			{
				TranslateMessage(&message);
				DispatchMessageW(&message);
			}
			continue;
		}
		if (wake != WAIT_OBJECT_0 + 1)
			break;
		const LONG revision = AtomicRead(shared.revision), amount = AtomicRead(shared.wanted);
		State state = State::Off;
		const bool mono = AtomicRead(shared.mono) != 0;
		std::array<int, 3> mix {};
		for (int i = 0; i < 3; ++i)
			mix[i] = AtomicRead(shared.mix[i]);
		if ((revision & 1) || revision != AtomicRead(shared.revision))
			continue;
		if (amount < -1)
		{
			ending = true;
			break;
		}
		if (WaitForSingleObject(parent.handle, 0) != WAIT_TIMEOUT)
			break;
		if (amount < 0)
		{
			state = restore();
			if (!fake && state != State::Failed)
			{
				if (!api.Close())
					state = State::Failed;
				opened = false;
			}
		}
		else if (amount > color::SATURATION_MAXIMUM)
			state = State::Failed;
		else
		{
			// Windows color filters are independent user choices, not a reason
			// to pause saturation. Verify the actual matrix API state instead.
			if (!opened)
				opened = api.Open();
			float zoom = 1;
			int x = 0, y = 0;
			if (!opened)
				state = State::Failed;
			else if (!fake && (!api.ReadZoom(&zoom, &x, &y) || zoom != 1 || x || y))
			{
				state = restore();
				if (state != State::Failed)
					state = State::OtherTool;
			}
			else
			{
				auto target = matrix_color::SaturationMatrix(amount / 100.f);
				if (mono)
					target = matrix_color::MultiplyMatrices(target, matrix_color::MonochromeMatrix(mix));
				if (report && entries < 1024)
				{
					matrix_color::Matrix before {};
					if (backend.Read(before))
						report << "before_identity="
							   << (matrix_color::MaxDifference(before, matrix_color::IdentityMatrix()) < 1e-5f)
							   << " before_matches_requested=" << (matrix_color::MaxDifference(before, target) < 1e-5f)
							   << " monochrome=" << mono << " mix=" << mix[0] << ',' << mix[1] << ',' << mix[2] << '\n';
				}
				state = engine.Apply(target);
			}
		}
		if (report && entries < 1024 && (previous_amount == -2 || previous_amount == amount))
			DisplayReport(report);
		log(previous_amount == amount ? "reapply" : "update", amount, state);
		previous_amount = amount;
		InterlockedExchange(&shared.state, static_cast<LONG>(state));
		InterlockedExchange(&shared.processed, revision);
		if (owner)
			PostMessageW(owner, MESSAGE_MATRIX_CHANGED, 0, 0);
	}
	auto restored = restore();
	if (!fake && !api.Close())
		restored = State::Failed;
	log("exit", -1, restored);
	InterlockedExchange(&shared.state, static_cast<LONG>(restored));
	return restored == State::Failed ? 1 : 0;
}

int SelfTest()
{
	const auto red = matrix_color::MonochromeMatrix({ 100, 0, 0 });
	for (int r = 0; r < 3; ++r)
		for (int c = 0; c < 3; ++c)
			if (red[r][c] != (r == 0 ? 1.f : 0.f))
				return 33;
	if (matrix_color::MaxDifference(matrix_color::MonochromeMatrix({ 0, 0, 0 }),
	                                matrix_color::MonochromeMatrix({ 21, 72, 7 })) > 1e-6f)
		return 34;
	const auto mix = matrix_color::MonochromeMatrix({ 90, 40, 10 });
	for (int c = 0; c < 3; ++c)
	{
		float white = 0;
		for (int r = 0; r < 3; ++r)
			white += mix[r][c];
		if (std::abs(white - 1) > 1e-6f)
			return 35;
	}
	const auto saturation = matrix_color::SaturationMatrix(1.7f),
			   combined = matrix_color::MultiplyMatrices(saturation, mix);
	const std::array<float, 5> input { .8f, .2f, .4f, 1, 1 };
	std::array<float, 5> first {}, second {}, direct {};
	for (int r = 0; r < 5; ++r)
		for (int c = 0; c < 5; ++c)
		{
			first[c] += input[r] * saturation[r][c];
			direct[c] += input[r] * combined[r][c];
		}
	for (int r = 0; r < 5; ++r)
		for (int c = 0; c < 5; ++c)
			second[c] += first[r] * mix[r][c];
	for (int c = 0; c < 5; ++c)
		if (std::abs(second[c] - direct[c]) > 1e-6f)
			return 36;
	if (std::abs(direct[0] - direct[1]) > 1e-6f || std::abs(direct[1] - direct[2]) > 1e-6f || direct[3] != 1 ||
	    direct[4] != 1)
		return 37;
	try
	{
		matrix_color::MonochromeMatrix({ 201, 0, 0 });
		return 38;
	}
	catch (const std::invalid_argument&)
	{
	}
	Fake api;
	Engine engine(api);
	if (engine.Apply(0) != State::Active ||
	    matrix_color::MaxDifference(api.value, matrix_color::PreviewMatrix(0)) > 1e-5f)
		return 1;
	const auto writes = api.writes;
	if (engine.Apply(0) != State::Active || api.writes != writes)
		return 30;
	Recovery recovery;
	recovery.Restart();
	for (unsigned delay : { 250u, 750u, 1500u, 0u, 0u })
		if (recovery.NextDelay() != delay)
			return 31;
	recovery.Restart();
	if (recovery.NextDelay() != 250)
		return 32;
	if (engine.Apply(50) != State::Active || engine.Apply(120) != State::Active ||
	    engine.Apply(color::SATURATION_MAXIMUM) != State::Active)
		return 2;
	// Display-mode reset to identity must reapply; a different external matrix
	// must be preserved, including on pause and exit.
	api.value = matrix_color::IdentityMatrix();
	if (engine.Apply(50) != State::Active)
		return 3;
	api.value = matrix_color::SaturationMatrix(.7f);
	const auto external = api.value;
	if (engine.Apply(120) != State::ExternalChange || engine.Stop() != State::Off || api.value != external)
		return 4;
	if (engine.Apply(0) != State::OtherTool)
		return 5;
	api.value = matrix_color::IdentityMatrix();
	if (engine.Apply(120) != State::Active || engine.Stop() != State::Off ||
	    api.value != matrix_color::IdentityMatrix())
		return 6;
	api.reject = true;
	if (engine.Apply(0) != State::Failed || api.value != matrix_color::IdentityMatrix())
		return 7;
	api.reject = false;
	if (engine.Apply(0) != State::Active)
		return 21;
	api.reject = true;
	if (engine.Stop() != State::Failed)
		return 22;
	api.reject = false;
	if (engine.Stop() != State::Off || api.value != matrix_color::IdentityMatrix())
		return 23;
	try
	{
		auto holder = std::make_unique<Controller>();
		auto& controller = *holder;
		controller.Update(nullptr, 0, false, true);
		const auto await = [&](State desired)
		{
			const auto deadline = GetTickCount64() + 3000;
			while (GetTickCount64() < deadline)
			{
				if (controller.CurrentState() == desired)
					return true;
				Sleep(5);
			}
			return false;
		};
		if (!await(State::Active))
			return 8;
		controller.Update(nullptr, 50);
		controller.Update(nullptr, color::SATURATION_MAXIMUM);
		if (!await(State::Active))
			return 9;
		controller.Stop();
		if (!await(State::Off))
			return 10;
		controller.Update(nullptr, 0);
		if (!await(State::Active))
			return 11;
		controller.Update(nullptr, 0, true);
		if (!await(State::Active))
			return 12;
		std::array<int, 3> weights { 100, 0, 0 };
		controller.Update(nullptr, 100, false, true, &weights);
		if (!await(State::Active))
			return 39;
		for (int i = 0; i < 100; ++i)
		{
			weights = { i, 200 - i, i / 2 };
			controller.Update(nullptr, 120, false, true, &weights);
		}
		if (!await(State::Active) || AtomicRead(controller._impl->view->shared->mix[0]) != 99 ||
		    (AtomicRead(controller._impl->view->shared->revision) & 1))
			return 40;
		controller.Update(nullptr, 100, false, true);
		if (!await(State::Off))
			return 41;
		controller.Update(nullptr, 100, false, true, &weights);
		if (!await(State::Active))
			return 42;
		// Keep independent observations alive while controller destruction
		// asks the fake worker to restore and exit. No real API is loaded.
		HANDLE process_copy = nullptr;
		if (!DuplicateHandle(GetCurrentProcess(), controller._impl->process.handle, GetCurrentProcess(), &process_copy,
		                     SYNCHRONIZE, FALSE, 0))
			return 14;
		Handle observed_process(process_copy);
		Mapping observed(controller._impl->mapping.handle);
		holder.reset();
		if (WaitForSingleObject(observed_process.handle, 3000) != WAIT_OBJECT_0 || !observed.shared ||
		    AtomicRead(observed.shared->state) != static_cast<LONG>(State::Off))
			return 15;
		// Crash only a disposable owner created for this test. The worker
		// must wake on parent death, restore its fake baseline, then exit.
		auto owner_command = L"\"" + ExecutablePath().wstring() + L"\" --matrix-preview-owner";
		STARTUPINFOW startup { sizeof(startup) };
		startup.dwFlags = STARTF_USESHOWWINDOW | STARTF_FORCEOFFFEEDBACK;
		startup.wShowWindow = SW_HIDE;
		PROCESS_INFORMATION owner_info {};
		if (!CreateProcessW(ExecutablePath().c_str(), owner_command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW,
		                    nullptr, nullptr, &startup, &owner_info))
			return 16;
		struct Disposable
		{
			Handle process;
			explicit Disposable(HANDLE h) : process(h) {}
			~Disposable()
			{
				if (WaitForSingleObject(process.handle, 0) == WAIT_TIMEOUT)
					TerminateProcess(process.handle, 0);
			}
		} owner(owner_info.hProcess);
		CloseHandle(owner_info.hThread);
		const auto token = std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64()) + L"-9999";
		Handle mapping(CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, sizeof(Shared),
		                                  SharedObjectName(token.c_str(), L"-map").c_str()));
		Mapping orphan(mapping.handle);
		Handle event(CreateEventW(nullptr, FALSE, FALSE, SharedObjectName(token.c_str(), L"-event").c_str()));
		if (!orphan.shared || !event.handle)
			return 17;
		*orphan.shared = Shared {};
		orphan.shared->wanted = 0;
		orphan.shared->revision = 2;
		auto worker_command = L"\"" + ExecutablePath().wstring() + L"\" --matrix-effect-probe " +
		                      std::to_wstring(owner_info.dwProcessId) + L" " + token;
		PROCESS_INFORMATION worker_info {};
		if (!CreateProcessW(ExecutablePath().c_str(), worker_command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW,
		                    nullptr, nullptr, &startup, &worker_info))
			return 18;
		Handle orphan_worker(worker_info.hProcess);
		CloseHandle(worker_info.hThread);
		SetEvent(event.handle);
		const auto deadline = GetTickCount64() + 3000;
		while (AtomicRead(orphan.shared->state) != static_cast<LONG>(State::Active) && GetTickCount64() < deadline)
			Sleep(5);
		if (AtomicRead(orphan.shared->state) != static_cast<LONG>(State::Active))
			return 19;
		TerminateProcess(owner.process.handle, 0);
		if (WaitForSingleObject(orphan_worker.handle, 3000) != WAIT_OBJECT_0 ||
		    AtomicRead(orphan.shared->state) != static_cast<LONG>(State::Off))
			return 20;
		Controller watched;
		watched.Update(nullptr, 50, false, true);
		// Killing only the fake worker created here checks notification of an
		// unexpected worker exit, without loading the Windows color API.
		TerminateProcess(watched._impl->process.handle, 99);
		if (WaitForSingleObject(watched._impl->process.handle, 3000) != WAIT_OBJECT_0)
			return 24;
		MSG message {};
		if (watched.WaitForMessageOrWorkerExit(message) != 1 || message.message != MESSAGE_MATRIX_CHANGED ||
		    !watched._impl->exit_reported || watched.CurrentState() != State::Failed)
			return 25;
		PeekMessageW(&message, nullptr, 0, 0, PM_NOREMOVE);
		if (!PostThreadMessageW(GetCurrentThreadId(), WM_APP + 41, 123, 0))
			return 26;
		if (watched.WaitForMessageOrWorkerExit(message) != 1 || message.message != WM_APP + 41 || message.wParam != 123)
			return 26;
		watched.Update(nullptr, 50, false, true);
		if (watched._impl->exit_reported)
			return 27;
		Controller read_only;
		const auto snapshot = read_only.WriteReport();
		std::ifstream text(snapshot);
		const std::string contents((std::istreambuf_iterator<char>(text)), {});
		text.close();
		const bool valid = contents.find("Read-only snapshot") != contents.npos &&
		                   contents.find("Windows=") != contents.npos &&
		                   contents.find("exclusive vs borderless") != contents.npos;
		DeleteFileW(snapshot.c_str());
		if (!valid || read_only.CurrentState() != State::Off)
			return 28;
	}
	catch (...)
	{
		return 13;
	}
	return 0;
}

int Benchmark(const wchar_t* path)
{
	try
	{
		Controller controller;
		const auto await = [&](State desired)
		{
			const auto end = GetTickCount64() + 3000;
			while (GetTickCount64() < end)
			{
				if (controller.CurrentState() == desired)
					return true;
				Sleep(5);
			}
			return false;
		};
		const auto cpu = [&]()
		{
			FILETIME creation_time {}, exit_time {}, kernel_time {}, user_time {};
			if (!GetProcessTimes(controller._impl->process.handle, &creation_time, &exit_time, &kernel_time,
			                     &user_time))
				throw std::runtime_error("Cannot measure worker CPU");
			return ((static_cast<ULONGLONG>(kernel_time.dwHighDateTime) + user_time.dwHighDateTime) << 32) +
			       kernel_time.dwLowDateTime + static_cast<ULONGLONG>(user_time.dwLowDateTime);
		};
		const auto sample = [&]()
		{
			const auto before = cpu(), start = GetTickCount64();
			Sleep(1000);
			PROCESS_MEMORY_COUNTERS_EX memory {};
			memory.cb = sizeof(memory);
			if (!GetProcessMemoryInfo(controller._impl->process.handle,
			                          reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&memory), sizeof(memory)))
				throw std::runtime_error("Cannot measure worker memory");
			std::ostringstream json;
			json << "{\"elapsed_ms\":" << GetTickCount64() - start << ",\"cpu_ms\":" << (cpu() - before) / 10000.0
				 << ",\"working_set_mib\":" << memory.WorkingSetSize / 1048576.0
				 << ",\"private_commit_mib\":" << memory.PrivateUsage / 1048576.0 << '}';
			return json.str();
		};
		const auto start = GetTickCount64();
		controller.Update(nullptr, 50, false, true);
		if (!await(State::Active))
			return 1;
		const auto startup = GetTickCount64() - start;
		const auto active = sample();
		const auto stop_start = GetTickCount64();
		controller.Stop();
		if (!await(State::Off))
			return 2;
		const auto stop = GetTickCount64() - stop_start;
		const auto paused = sample();
		std::ostringstream json;
		json
			<< "{\"startup_ms\":" << startup << ",\"pause_ms\":" << stop << ",\"active\":" << active
			<< ",\"paused\":" << paused
			<< ",\"mode\":\"fake matrix worker only; no color API, DWM or GPU cost measured; display settings unchanged\"}\n";
		const auto bytes = json.str();
		Handle file(CreateFileW(path, GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr));
		DWORD written = 0;
		return file.handle != INVALID_HANDLE_VALUE &&
		               WriteFile(file.handle, bytes.data(), static_cast<DWORD>(bytes.size()), &written, nullptr) &&
		               written == bytes.size()
		           ? 0
		           : 3;
	}
	catch (...)
	{
		return 4;
	}
}
}
