#include "matrix_effect.h"
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
constexpr DWORD Magic = 0x434d5831;
struct Shared
{
	DWORD magic = Magic;
	volatile LONG wanted = -1, revision = 0, processed = 0, state = static_cast<LONG>(State::Off), mono = 0,
				  mix[3] { 21, 72, 7 };
	UINT_PTR owner = 0;
};

struct Handle
{
	HANDLE h = nullptr;
	explicit Handle(HANDLE v = nullptr) : h(v) {}
	~Handle()
	{
		if (h && h != INVALID_HANDLE_VALUE)
			CloseHandle(h);
	}
	Handle(const Handle&) = delete;
	Handle& operator=(const Handle&) = delete;
};

struct Mapping
{
	Shared* p = nullptr;
	explicit Mapping(HANDLE h)
	{
		if (h)
			p = static_cast<Shared*>(MapViewOfFile(h, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(Shared)));
	}
	~Mapping()
	{
		if (p)
			UnmapViewOfFile(p);
	}
};

std::filesystem::path ExecutablePath()
{
	wchar_t path[32768] {};
	const auto n = GetModuleFileNameW(nullptr, path, 32768);
	if (!n || n >= 32768)
		throw std::runtime_error("Cannot locate MonitorMage");
	return path;
}

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
	bool Read(matrix_color::Matrix& m) override
	{
		m = value;
		return true;
	}
	bool Write(const matrix_color::Matrix& m) override
	{
		++writes;
		if (reject)
			return false;
		value = m;
		return true;
	}
};
}

struct Controller::Impl
{
	Handle mapping, event, process;
	std::unique_ptr<Mapping> view;
	int amount = -1;
	bool exitReported = false, mono = false;
	std::array<int, 3> mix { 21, 72, 7 };
	HWND owner = nullptr;
	~Impl()
	{
		if (view && view->p)
		{
			InterlockedIncrement(&view->p->revision);
			InterlockedExchange(&view->p->wanted, -2);
			InterlockedIncrement(&view->p->revision);
			if (event.h)
				SetEvent(event.h);
		}
	}
};

Controller::Controller() = default;
Controller::~Controller() = default;
int Controller::WaitForMessageOrWorkerExit(MSG& message)
{
	for (;;)
	{
		if (!impl_ || impl_->exitReported)
			return static_cast<int>(GetMessageW(&message, nullptr, 0, 0));
		const HANDLE process = impl_->process.h;
		const DWORD wake = MsgWaitForMultipleObjectsEx(1, &process, INFINITE, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
		if (wake == WAIT_FAILED)
			return -1;
		if (wake == WAIT_OBJECT_0)
		{
			impl_->exitReported = true;
			message = {};
			message.hwnd = impl_->owner;
			message.message = Changed;
			return 1;
		}
		// Peek also dispatches synchronous sent messages. One of those may
		// replace the session, so re-read impl_ before waiting again.
		if (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE))
			return message.message == WM_QUIT ? 0 : 1;
	}
}

void Controller::Stop()
{
	if (impl_)
		Update(nullptr, -1);
}

State Controller::CurrentState() const
{
	if (!impl_)
		return State::Off;
	auto& shared = *impl_->view->p;
	if (WaitForSingleObject(impl_->process.h, 0) != WAIT_TIMEOUT)
		return impl_->amount < 0 && AtomicRead(shared.state) == static_cast<LONG>(State::Off) &&
		               AtomicRead(shared.revision) == AtomicRead(shared.processed)
		           ? State::Off
		           : State::Failed;
	if (AtomicRead(shared.revision) != AtomicRead(shared.processed))
		return State::Starting;
	return static_cast<State>(AtomicRead(shared.state));
}

void Controller::Update(HWND owner, int amount, bool refresh, bool fake, const std::array<int, 3>* monochrome)
{
	if (amount < -1 || amount > color::SaturationMaximum)
		throw std::invalid_argument("Invalid saturation");
	if (monochrome)
		matrix_color::MonochromeMatrix(*monochrome); // Validate before starting or changing the worker.
	if (amount == 100 && !monochrome)
		amount = -1;
	const bool mono = monochrome && amount >= 0;
	const auto mix = mono ? *monochrome : std::array<int, 3> { 21, 72, 7 };
	if (impl_ && WaitForSingleObject(impl_->process.h, 0) != WAIT_TIMEOUT)
		impl_.reset();
	if (!impl_)
	{
		if (amount < 0)
			return;
		auto next = std::make_unique<Impl>();
		next->owner = owner;
		static unsigned serial = 0;
		const auto token = std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64()) + L"-" +
		                   std::to_wstring(++serial);
		next->mapping.h = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, sizeof(Shared),
		                                     SharedObjectName(token.c_str(), L"-map").c_str());
		if (!next->mapping.h || GetLastError() == ERROR_ALREADY_EXISTS)
			throw std::runtime_error("Color effect is unavailable");
		next->view = std::make_unique<Mapping>(next->mapping.h);
		next->event.h = CreateEventW(nullptr, FALSE, FALSE, SharedObjectName(token.c_str(), L"-event").c_str());
		if (!next->view->p || !next->event.h)
			throw std::runtime_error("Cannot prepare color effect");
		*next->view->p = Shared {};
		next->view->p->owner = reinterpret_cast<UINT_PTR>(owner);
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
		next->process.h = child.hProcess;
		CloseHandle(child.hThread);
		impl_ = std::move(next);
	}
	if (impl_->amount == amount && impl_->mono == mono && impl_->mix == mix &&
	    (!refresh || (amount < 0 && CurrentState() == State::Off)))
		return;
	impl_->amount = amount;
	impl_->mono = mono;
	impl_->mix = mix;
	auto& shared = *impl_->view->p;
	// Odd/even publication prevents a rapid edit from mixing channels from
	// different requests; the worker never spins waiting for an editor.
	InterlockedIncrement(&shared.revision);
	InterlockedExchange(&shared.wanted, amount);
	InterlockedExchange(&shared.mono, mono);
	for (int i = 0; i < 3; ++i)
		InterlockedExchange(&shared.mix[i], mix[i]);
	InterlockedIncrement(&shared.revision);
	SetEvent(impl_->event.h);
}

namespace
{
std::string ToUtf8(const wchar_t* text)
{
	const int n = WideCharToMultiByte(CP_UTF8, 0, text, -1, nullptr, 0, nullptr, nullptr);
	if (n < 1)
		return {};
	std::string s(n, '\0');
	WideCharToMultiByte(CP_UTF8, 0, text, -1, s.data(), n, nullptr, nullptr);
	s.pop_back();
	return s;
}

std::string ProcessName(DWORD pid)
{
	Handle process(OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid));
	wchar_t image[32768] {};
	DWORD length = 32768;
	return process.h && QueryFullProcessImageNameW(process.h, 0, image, &length)
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
	UINT32 pc = 0, mc = 0;
	LONG error = GetDisplayConfigBufferSizes(QDC_ONLY_ACTIVE_PATHS, &pc, &mc);
	if (error)
	{
		out << "display_query_error=" << error << '\n';
		return;
	}
	std::vector<DISPLAYCONFIG_PATH_INFO> paths(pc);
	std::vector<DISPLAYCONFIG_MODE_INFO> modes(mc);
	error = QueryDisplayConfig(QDC_ONLY_ACTIVE_PATHS, &pc, paths.data(), &mc, modes.data(), nullptr);
	if (error)
	{
		out << "display_query_error=" << error << '\n';
		return;
	}
	for (UINT32 i = 0; i < pc; ++i)
	{
		const auto& p = paths[i];
		out << "display[" << i << "] source=" << p.sourceInfo.id;
		if (p.sourceInfo.modeInfoIdx < mc &&
		    modes[p.sourceInfo.modeInfoIdx].infoType == DISPLAYCONFIG_MODE_INFO_TYPE_SOURCE)
		{
			const auto& m = modes[p.sourceInfo.modeInfoIdx].sourceMode;
			out << " resolution=" << m.width << 'x' << m.height;
		}
		DISPLAYCONFIG_TARGET_DEVICE_NAME name {};
		name.header = { DISPLAYCONFIG_DEVICE_INFO_GET_TARGET_NAME, sizeof(name), p.targetInfo.adapterId,
			            p.targetInfo.id };
		if (!DisplayConfigGetDeviceInfo(&name.header))
			out << " name=" << ToUtf8(name.monitorFriendlyDeviceName);
		DISPLAYCONFIG_GET_ADVANCED_COLOR_INFO color {};
		color.header = { DISPLAYCONFIG_DEVICE_INFO_GET_ADVANCED_COLOR_INFO, sizeof(color), p.targetInfo.adapterId,
			             p.targetInfo.id };
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
		<< "saturation=" << (impl_ ? impl_->amount : -1) << " state=" << static_cast<int>(CurrentState()) << '\n';
	if (impl_)
		out << "worker_pid=" << GetProcessId(impl_->process.h) << " revision=" << AtomicRead(impl_->view->p->revision)
			<< " processed=" << AtomicRead(impl_->view->p->processed) << '\n';
	if (impl_)
		out << "monochrome=" << impl_->mono << " mix=" << impl_->mix[0] << ',' << impl_->mix[1] << ',' << impl_->mix[2]
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

int RunWorkerProcess(DWORD parentId, const wchar_t* token, bool fake)
{
	if (!parentId || !IsValidToken(token))
		return 2;
	Handle parent(OpenProcess(SYNCHRONIZE, FALSE, parentId));
	Handle event(OpenEventW(SYNCHRONIZE, FALSE, SharedObjectName(token, L"-event").c_str()));
	Handle mapping(OpenFileMappingW(FILE_MAP_ALL_ACCESS, FALSE, SharedObjectName(token, L"-map").c_str()));
	Mapping view(mapping.h);
	if (!parent.h || !event.h || !view.p || view.p->magic != Magic)
		return 3;
	auto& shared = *view.p;
	const HWND owner = reinterpret_cast<HWND>(shared.owner);
	DWORD ownerPid = 0;
	if (owner)
	{
		GetWindowThreadProcessId(owner, &ownerPid);
		if (ownerPid != parentId)
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
	DWORD previousPid = 0;
	std::string foregroundName;
	LONG previousAmount = -2;
	const auto log = [&](const char* action, LONG amount, State state)
	{
		if (report && entries++ < 1024)
		{
			DWORD foreground = 0;
			GetWindowThreadProcessId(GetForegroundWindow(), &foreground);
			if (foreground != previousPid)
			{
				foregroundName = ProcessName(foreground);
				previousPid = foreground;
			}
			report << "time_ms=" << GetTickCount64() << " action=" << action << " saturation=" << amount
				   << " state=" << static_cast<int>(state) << " foreground_pid=" << foreground
				   << " foreground_executable=" << foregroundName << " resolution=" << GetSystemMetrics(SM_CXSCREEN)
				   << 'x' << GetSystemMetrics(SM_CYSCREEN) << '\n';
			report.flush();
		}
	};
	HANDLE wait[] { parent.h, event.h };
	bool ending = false;
	while (!ending)
	{
		const DWORD wake = MsgWaitForMultipleObjects(2, wait, FALSE, INFINITE, QS_ALLINPUT);
		if (wake == WAIT_OBJECT_0 || wake == WAIT_FAILED)
			break;
		if (wake == WAIT_OBJECT_0 + 2)
		{
			MSG msg {};
			while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE))
			{
				TranslateMessage(&msg);
				DispatchMessageW(&msg);
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
		if (WaitForSingleObject(parent.h, 0) != WAIT_TIMEOUT)
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
		else if (amount > color::SaturationMaximum)
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
		if (report && entries < 1024 && (previousAmount == -2 || previousAmount == amount))
			DisplayReport(report);
		log(previousAmount == amount ? "reapply" : "update", amount, state);
		previousAmount = amount;
		InterlockedExchange(&shared.state, static_cast<LONG>(state));
		InterlockedExchange(&shared.processed, revision);
		if (owner)
			PostMessageW(owner, Changed, 0, 0);
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
	const auto sat = matrix_color::SaturationMatrix(1.7f), combined = matrix_color::MultiplyMatrices(sat, mix);
	const std::array<float, 5> input { .8f, .2f, .4f, 1, 1 };
	std::array<float, 5> first {}, second {}, direct {};
	for (int r = 0; r < 5; ++r)
		for (int c = 0; c < 5; ++c)
		{
			first[c] += input[r] * sat[r][c];
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
	    engine.Apply(color::SaturationMaximum) != State::Active)
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
		controller.Update(nullptr, color::SaturationMaximum);
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
		if (!await(State::Active) || AtomicRead(controller.impl_->view->p->mix[0]) != 99 ||
		    (AtomicRead(controller.impl_->view->p->revision) & 1))
			return 40;
		controller.Update(nullptr, 100, false, true);
		if (!await(State::Off))
			return 41;
		controller.Update(nullptr, 100, false, true, &weights);
		if (!await(State::Active))
			return 42;
		// Keep independent observations alive while controller destruction
		// asks the fake worker to restore and exit. No real API is loaded.
		HANDLE processCopy = nullptr;
		if (!DuplicateHandle(GetCurrentProcess(), controller.impl_->process.h, GetCurrentProcess(), &processCopy,
		                     SYNCHRONIZE, FALSE, 0))
			return 14;
		Handle observedProcess(processCopy);
		Mapping observed(controller.impl_->mapping.h);
		holder.reset();
		if (WaitForSingleObject(observedProcess.h, 3000) != WAIT_OBJECT_0 || !observed.p ||
		    AtomicRead(observed.p->state) != static_cast<LONG>(State::Off))
			return 15;
		// Crash only a disposable owner created for this test. The worker
		// must wake on parent death, restore its fake baseline, then exit.
		auto ownerCommand = L"\"" + ExecutablePath().wstring() + L"\" --matrix-preview-owner";
		STARTUPINFOW startup { sizeof(startup) };
		startup.dwFlags = STARTF_USESHOWWINDOW | STARTF_FORCEOFFFEEDBACK;
		startup.wShowWindow = SW_HIDE;
		PROCESS_INFORMATION ownerInfo {};
		if (!CreateProcessW(ExecutablePath().c_str(), ownerCommand.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW,
		                    nullptr, nullptr, &startup, &ownerInfo))
			return 16;
		struct Disposable
		{
			Handle process;
			explicit Disposable(HANDLE h) : process(h) {}
			~Disposable()
			{
				if (WaitForSingleObject(process.h, 0) == WAIT_TIMEOUT)
					TerminateProcess(process.h, 0);
			}
		} owner(ownerInfo.hProcess);
		CloseHandle(ownerInfo.hThread);
		const auto token = std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64()) + L"-9999";
		Handle mapping(CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, sizeof(Shared),
		                                  SharedObjectName(token.c_str(), L"-map").c_str()));
		Mapping orphan(mapping.h);
		Handle event(CreateEventW(nullptr, FALSE, FALSE, SharedObjectName(token.c_str(), L"-event").c_str()));
		if (!orphan.p || !event.h)
			return 17;
		*orphan.p = Shared {};
		orphan.p->wanted = 0;
		orphan.p->revision = 2;
		auto workerCommand = L"\"" + ExecutablePath().wstring() + L"\" --matrix-effect-probe " +
		                     std::to_wstring(ownerInfo.dwProcessId) + L" " + token;
		PROCESS_INFORMATION workerInfo {};
		if (!CreateProcessW(ExecutablePath().c_str(), workerCommand.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW,
		                    nullptr, nullptr, &startup, &workerInfo))
			return 18;
		Handle orphanWorker(workerInfo.hProcess);
		CloseHandle(workerInfo.hThread);
		SetEvent(event.h);
		const auto deadline = GetTickCount64() + 3000;
		while (AtomicRead(orphan.p->state) != static_cast<LONG>(State::Active) && GetTickCount64() < deadline)
			Sleep(5);
		if (AtomicRead(orphan.p->state) != static_cast<LONG>(State::Active))
			return 19;
		TerminateProcess(owner.process.h, 0);
		if (WaitForSingleObject(orphanWorker.h, 3000) != WAIT_OBJECT_0 ||
		    AtomicRead(orphan.p->state) != static_cast<LONG>(State::Off))
			return 20;
		Controller watched;
		watched.Update(nullptr, 50, false, true);
		// Killing only the fake worker created here checks notification of an
		// unexpected worker exit, without loading the Windows color API.
		TerminateProcess(watched.impl_->process.h, 99);
		if (WaitForSingleObject(watched.impl_->process.h, 3000) != WAIT_OBJECT_0)
			return 24;
		MSG message {};
		if (watched.WaitForMessageOrWorkerExit(message) != 1 || message.message != Changed ||
		    !watched.impl_->exitReported || watched.CurrentState() != State::Failed)
			return 25;
		PeekMessageW(&message, nullptr, 0, 0, PM_NOREMOVE);
		if (!PostThreadMessageW(GetCurrentThreadId(), WM_APP + 41, 123, 0))
			return 26;
		if (watched.WaitForMessageOrWorkerExit(message) != 1 || message.message != WM_APP + 41 || message.wParam != 123)
			return 26;
		watched.Update(nullptr, 50, false, true);
		if (watched.impl_->exitReported)
			return 27;
		Controller readOnly;
		const auto snapshot = readOnly.WriteReport();
		std::ifstream text(snapshot);
		const std::string contents((std::istreambuf_iterator<char>(text)), {});
		text.close();
		const bool valid = contents.find("Read-only snapshot") != contents.npos &&
		                   contents.find("Windows=") != contents.npos &&
		                   contents.find("exclusive vs borderless") != contents.npos;
		DeleteFileW(snapshot.c_str());
		if (!valid || readOnly.CurrentState() != State::Off)
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
			FILETIME c {}, e {}, k {}, u {};
			if (!GetProcessTimes(controller.impl_->process.h, &c, &e, &k, &u))
				throw std::runtime_error("Cannot measure worker CPU");
			return ((static_cast<ULONGLONG>(k.dwHighDateTime) + u.dwHighDateTime) << 32) + k.dwLowDateTime +
			       static_cast<ULONGLONG>(u.dwLowDateTime);
		};
		const auto sample = [&]()
		{
			const auto before = cpu(), start = GetTickCount64();
			Sleep(1000);
			PROCESS_MEMORY_COUNTERS_EX memory {};
			memory.cb = sizeof(memory);
			if (!GetProcessMemoryInfo(controller.impl_->process.h, reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&memory),
			                          sizeof(memory)))
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
		const auto stopStart = GetTickCount64();
		controller.Stop();
		if (!await(State::Off))
			return 2;
		const auto stop = GetTickCount64() - stopStart;
		const auto paused = sample();
		std::ostringstream json;
		json
			<< "{\"startup_ms\":" << startup << ",\"pause_ms\":" << stop << ",\"active\":" << active
			<< ",\"paused\":" << paused
			<< ",\"mode\":\"fake matrix worker only; no color API, DWM or GPU cost measured; display settings unchanged\"}\n";
		const auto bytes = json.str();
		Handle file(CreateFileW(path, GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr));
		DWORD written = 0;
		return file.h != INVALID_HANDLE_VALUE &&
		               WriteFile(file.h, bytes.data(), static_cast<DWORD>(bytes.size()), &written, nullptr) &&
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
