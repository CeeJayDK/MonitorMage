#include "live_lut.h"
#include "app_settings.h"
#include "color_math.h"
#include <algorithm>
#include <cstddef>
#include <cmath>
#include <fstream>
#include <stdexcept>
#include <mutex>

namespace live
{
namespace
{
constexpr const wchar_t* WRITER_LEASE_NAME = L"Local\\LutWorkbench-LutWriter";
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

// A process may own several output sessions. Share one writer lease between
// them, but keep other processes excluded until every recovery helper exits.
struct WriterLease
{
	Handle handle;
	DWORD thread = GetCurrentThreadId();
	explicit WriterLease(const wchar_t* name)
	{
		handle.handle = CreateMutexW(nullptr, TRUE, name);
		const DWORD error = GetLastError();
		if (!handle.handle || error == ERROR_ALREADY_EXISTS)
			throw std::runtime_error("Another LUT session or timed test is active");
	}
	~WriterLease() { ReleaseMutex(handle.handle); }
};

struct WriterLeases
{
	std::mutex mutex;
	std::weak_ptr<WriterLease> current;
	std::shared_ptr<WriterLease> Acquire(const wchar_t* name)
	{
		std::lock_guard<std::mutex> lock(mutex);
		if (auto lease = current.lock())
		{
			if (lease->thread != GetCurrentThreadId())
				throw std::runtime_error("LUT sessions must use the same owner thread");
			return lease;
		}
		auto lease = std::make_shared<WriterLease>(name);
		current = lease;
		return lease;
	}
};

WriterLeases writer_leases;
struct DC
{
	HDC handle;
	explicit DC(const std::wstring& name) : handle(CreateDCW(L"DISPLAY", name.c_str(), nullptr, nullptr)) {}
	~DC()
	{
		if (handle)
			DeleteDC(handle);
	}
};

// ABI from Windows SDK wingdi.h. Query dynamically so Win10 can reject this
// newer request without importing a newer Windows entry point.
struct AdvancedColor2
{
	DISPLAYCONFIG_DEVICE_INFO_HEADER header;
	UINT32 flags = 0, encoding = 0, bits = 0, mode = 0;
};

bool LegacyHdrFlagReliable()
{
	using Version = LONG(WINAPI*)(OSVERSIONINFOW*);
	const auto query = reinterpret_cast<Version>(GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "RtlGetVersion"));
	OSVERSIONINFOW info {};
	info.dwOSVersionInfoSize = sizeof(info);
	return query && query(&info) == 0 && info.dwBuildNumber < 22000;
}

bool diagnostics_enabled = false;

struct Snapshot
{
	wchar_t source[32] {}, identity[2048] {};
	Ramp baseline {};
};

// How to get back the ramp found on the display. Only a ramp that cannot be rebuilt is stored.
enum class BaselineKind : LONG
{
	Stored,
	Identity,
	HdrGamma
};

struct BaselineRecipe
{
	BaselineKind kind = BaselineKind::Stored;
	double white = 0, gamma = 0;
};

// Memory shared with the recovery guardian. The guardian keeps its own copy of the recovery data.
struct Shared
{
	volatile LONG applied = 0, pending = 0;
	Ramp last {}, candidate {};
	wchar_t source[32] {}, identity[2048] {};
	BaselineRecipe recipe;
	Ramp baseline {}; // Must stay last: left out of the shared memory when the recipe can rebuild it.
};

struct View
{
	Shared* shared = nullptr;
	explicit View(HANDLE mapping) : shared(static_cast<Shared*>(MapViewOfFile(mapping, FILE_MAP_ALL_ACCESS, 0, 0, 0)))
	{
	}
	~View()
	{
		if (shared)
			UnmapViewOfFile(shared);
	}
};

using app_settings::QuoteArgument;

Output FindOutputForSnapshot(const Snapshot& snapshot)
{
	for (const auto& output : ActiveOutputs())
		if (output.identity == snapshot.identity)
			return output;
	return {};
}

std::wstring NewTempFilePath()
{
	static LONG serial = 0;
	wchar_t folder[MAX_PATH] {};
	const DWORD size = GetTempPathW(MAX_PATH, folder);
	if (!size || size >= MAX_PATH)
		throw std::runtime_error("Cannot locate recovery folder");
	return std::wstring(folder) + L"LutWorkbench-live-" + std::to_wstring(GetCurrentProcessId()) + L"-" +
	       std::to_wstring(GetTickCount64()) + L"-" + std::to_wstring(InterlockedIncrement(&serial));
}

Ramp IdentityRamp()
{
	Ramp ramp {};
	for (auto& channel : ramp)
		for (size_t i = 0; i < 256; ++i)
			channel[i] = static_cast<std::uint16_t>(i * 257);
	return ramp;
}

// A neutral ramp is sometimes i * 256 rather than i * 257, which is one 8-bit step lower at the top.
bool IsNeutralRamp(const Ramp& ramp)
{
	Ramp shifted {};
	for (auto& channel : shifted)
		for (size_t i = 0; i < 256; ++i)
			channel[i] = static_cast<std::uint16_t>(i * 256);
	return MaxRampDifference(ramp, IdentityRamp()) <= 2 || MaxRampDifference(ramp, shifted) <= 2;
}

Ramp HdrGammaBaseline(double white, double gamma)
{
	Output output;
	output.hdr = true;
	output.white = white;
	return BuildGammaRamp(IdentityRamp(), output, Settings { 0, 0.5, gamma, true, false });
}

// Only an exact match counts, so a rebuilt ramp is always the same as the one that was found.
BaselineRecipe RecipeFor(const Ramp& baseline, const Output& output)
{
	if (baseline == IdentityRamp())
		return { BaselineKind::Identity };
	if (output.hdr)
		for (double gamma : { 2.2, 2.4 })
			if (baseline == HdrGammaBaseline(output.white, gamma))
				return { BaselineKind::HdrGamma, output.white, gamma };
	return {};
}

DWORD MappingSize(const BaselineRecipe& recipe)
{
	return static_cast<DWORD>(recipe.kind == BaselineKind::Stored ? sizeof(Shared) : offsetof(Shared, baseline));
}

void WriteRecoveryData(Shared& shared, const Snapshot& snapshot, const BaselineRecipe& recipe)
{
	std::copy(std::begin(snapshot.source), std::end(snapshot.source), shared.source);
	std::copy(std::begin(snapshot.identity), std::end(snapshot.identity), shared.identity);
	shared.recipe = recipe;
	if (recipe.kind == BaselineKind::Stored)
		shared.baseline = snapshot.baseline;
}

Snapshot ReadRecoveryData(const Shared& shared)
{
	if (shared.source[31] || shared.identity[2047] || !shared.source[0] || !shared.identity[0])
		throw std::runtime_error("Invalid live recovery data");
	Snapshot snapshot;
	std::copy(std::begin(shared.source), std::end(shared.source), snapshot.source);
	std::copy(std::begin(shared.identity), std::end(shared.identity), snapshot.identity);
	switch (shared.recipe.kind)
	{
		case BaselineKind::Stored:
			snapshot.baseline = shared.baseline;
			break;
		case BaselineKind::Identity:
			snapshot.baseline = IdentityRamp();
			break;
		case BaselineKind::HdrGamma:
			snapshot.baseline = HdrGammaBaseline(shared.recipe.white, shared.recipe.gamma);
			break;
		default:
			throw std::runtime_error("Invalid live recovery data");
	}
	return snapshot;
}

int RestoreOutput(const Output& output, const Snapshot& snapshot, Shared* shared)
{
	DC dc(output.name);
	Ramp current {};
	if (!dc.handle || !GetDeviceGammaRamp(dc.handle, &current))
		return 12;
	const bool pending = InterlockedCompareExchange(&shared->pending, 0, 0) != 0;
	const bool applied = InterlockedCompareExchange(&shared->applied, 0, 0) != 0;
	if (!IsRampOurs(current, snapshot.baseline, shared->last, shared->candidate, applied, pending))
		return 10;
	if (!SetDeviceGammaRamp(dc.handle, const_cast<Ramp*>(&snapshot.baseline)))
		return 13;
	if (!GetDeviceGammaRamp(dc.handle, &current) || MaxRampDifference(current, snapshot.baseline) > 2)
		return 14;
	return 0;
}

int RestoreSnapshot(const Snapshot& snapshot, Shared* shared)
{
	if (InterlockedCompareExchange(&shared->applied, 0, 0) == 0 &&
	    InterlockedCompareExchange(&shared->pending, 0, 0) == 0)
		return 0;
	const auto output = FindOutputForSnapshot(snapshot);
	if (!output.name.empty())
		return RestoreOutput(output, snapshot, shared);
	const auto rearranged = OutputsAfterLayoutChange(snapshot.identity, snapshot.baseline, ActiveOutputs());
	if (rearranged.empty())
		return 11;
	int worst = 0; // The result codes rise from success through a kept ramp to errors.
	for (const auto& moved : rearranged)
		worst = std::max(worst, RestoreOutput(moved, snapshot, shared));
	return worst;
}

std::wstring DisplayTargetsFromIdentity(const std::wstring& mode)
{
	// Each identity line ends in :advanced-color-flags:mode. Keep the physical
	// monitor paths, including clone-group membership, and discard only mode.
	std::wstring result;
	for (size_t start = 0; start < mode.size();)
	{
		const auto end = mode.find(L'\n', start);
		auto line = mode.substr(start, end - start);
		const auto mode_at = line.rfind(L':');
		const auto flags_at = mode_at == std::wstring::npos ? std::wstring::npos : line.rfind(L':', mode_at - 1);
		if (flags_at == std::wstring::npos)
			return {};
		result += line.substr(0, flags_at) + L"\n";
		if (end == std::wstring::npos)
			break;
		start = end + 1;
	}
	return result;
}
}

std::vector<Output> ActiveOutputs()
{
	for (int attempt = 0; attempt < 5; ++attempt)
	{
		UINT32 path_count = 0, mode_count = 0;
		if (GetDisplayConfigBufferSizes(QDC_ONLY_ACTIVE_PATHS, &path_count, &mode_count) != ERROR_SUCCESS)
			throw std::runtime_error("Cannot enumerate outputs");
		std::vector<DISPLAYCONFIG_PATH_INFO> paths(path_count);
		std::vector<DISPLAYCONFIG_MODE_INFO> modes(mode_count);
		const LONG result =
			QueryDisplayConfig(QDC_ONLY_ACTIVE_PATHS, &path_count, paths.data(), &mode_count, modes.data(), nullptr);
		if (result == ERROR_INSUFFICIENT_BUFFER)
			continue;
		if (result != ERROR_SUCCESS)
			throw std::runtime_error("Cannot read display configuration");
		std::vector<Output> list;
		std::vector<std::vector<std::wstring>> identities;
		for (UINT32 i = 0; i < path_count; ++i)
		{
			const auto& path = paths[i];
			DISPLAYCONFIG_SOURCE_DEVICE_NAME source {};
			source.header = { DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_NAME, sizeof(source), path.sourceInfo.adapterId,
				              path.sourceInfo.id };
			DISPLAYCONFIG_TARGET_DEVICE_NAME target {};
			target.header = { DISPLAYCONFIG_DEVICE_INFO_GET_TARGET_NAME, sizeof(target), path.targetInfo.adapterId,
				              path.targetInfo.id };
			if (DisplayConfigGetDeviceInfo(&source.header) || DisplayConfigGetDeviceInfo(&target.header))
				throw std::runtime_error("Cannot identify display output");
			DISPLAYCONFIG_GET_ADVANCED_COLOR_INFO color {};
			color.header = { DISPLAYCONFIG_DEVICE_INFO_GET_ADVANCED_COLOR_INFO, sizeof(color),
				             path.targetInfo.adapterId, path.targetInfo.id };
			const bool color_known = DisplayConfigGetDeviceInfo(&color.header) == ERROR_SUCCESS;
			AdvancedColor2 modern {};
			modern.header = { static_cast<DISPLAYCONFIG_DEVICE_INFO_TYPE>(15), sizeof(modern),
				              path.targetInfo.adapterId, path.targetInfo.id };
			const bool modern_known = DisplayConfigGetDeviceInfo(&modern.header) == ERROR_SUCCESS;
			static const bool reliable_legacy = LegacyHdrFlagReliable();
			const bool target_hdr =
				modern_known ? modern.mode == 2 : reliable_legacy && color_known && color.advancedColorEnabled;
			const bool mode_known = modern_known ? modern.mode == 0 || modern.mode == 2
			                                     : color_known && (!color.advancedColorEnabled || reliable_legacy);
			auto at = std::find_if(list.begin(), list.end(),
			                       [&](const Output& output) { return output.name == source.viewGdiDeviceName; });
			if (at == list.end())
			{
				list.push_back({});
				list.back().name = source.viewGdiDeviceName;
				identities.push_back({});
				at = list.end() - 1;
			}
			const auto index = static_cast<size_t>(at - list.begin());
			if (!at->label.empty())
				at->label += L" + ";
			at->label += target.monitorFriendlyDeviceName;
			identities[index].push_back(std::wstring(target.monitorDevicePath) + L":" + std::to_wstring(color.value) +
			                            L":" +
			                            std::to_wstring(modern_known ? modern.mode
			                                            : target_hdr ? 2u
			                                                         : 0u));
			at->supported = at->supported && color_known && mode_known && target.monitorDevicePath[0];
			at->hdr = at->hdr || target_hdr;
			at->safe = at->safe && mode_known && !target_hdr;
			if (target_hdr)
			{
				DISPLAYCONFIG_SDR_WHITE_LEVEL white {};
				white.header = { DISPLAYCONFIG_DEVICE_INFO_GET_SDR_WHITE_LEVEL, sizeof(white),
					             path.targetInfo.adapterId, path.targetInfo.id };
				if (DisplayConfigGetDeviceInfo(&white.header) == ERROR_SUCCESS)
					at->white = white.SDRWhiteLevel * 0.08;
				else
					at->supported = false;
			}
		}
		// Enumerate adapters once for the entire topology, rather than once
		// for each independent output group.
		std::vector<std::wstring> nvidia_sources;
		for (DWORD a = 0;; ++a)
		{
			DISPLAY_DEVICEW adapter {};
			adapter.cb = sizeof(adapter);
			if (!EnumDisplayDevicesW(nullptr, a, &adapter, 0))
				break;
			if (std::wstring(adapter.DeviceString).find(L"NVIDIA") != std::wstring::npos)
				nvidia_sources.emplace_back(adapter.DeviceName);
		}
		for (size_t i = 0; i < list.size(); ++i)
		{
			auto& output = list[i];
			auto& ids = identities[i];
			std::sort(ids.begin(), ids.end());
			for (const auto& id : ids)
				output.identity += id + L"\n";
			output.nvidia =
				std::find(nvidia_sources.begin(), nvidia_sources.end(), output.name) != nvidia_sources.end();
			if (output.hdr)
				output.supported =
					output.supported && output.nvidia && ids.size() == 1 && output.white >= 1 && output.white <= 10000;
		}
		return list;
	}
	throw std::runtime_error("Display configuration is changing");
}

bool SameDisplays(const Output& a, const Output& b)
{
	const auto key = DisplayTargetsFromIdentity(a.identity);
	return !key.empty() && key == DisplayTargetsFromIdentity(b.identity);
}

std::vector<Output> OutputsAfterLayoutChange(const std::wstring& session_identity, const Ramp& baseline,
                                             const std::vector<Output>& outputs)
{
	// Only a neutral ramp means the same thing in the new layout, which may be in another SDR/HDR mode.
	if (!IsNeutralRamp(baseline))
		return {};
	const auto monitors = DisplayTargetsFromIdentity(session_identity);
	std::vector<Output> sharing_a_monitor;
	for (const auto& output : outputs)
	{
		const auto targets = DisplayTargetsFromIdentity(output.identity);
		if (targets == monitors)
			return {}; // The same monitors are still together, so this is only a change of mode.
		for (size_t start = 0; start < monitors.size();)
		{
			const auto end = monitors.find(L'\n', start);
			if (targets.find(monitors.substr(start, end + 1 - start)) != std::wstring::npos)
			{
				sharing_a_monitor.push_back(output);
				break;
			}
			start = end + 1;
		}
	}
	return sharing_a_monitor;
}

double EvaluateOutputCode(double code, int channel, const Output& output, const Settings& settings)
{
	const auto warm = [&](double value)
	{
		if (settings.NightWarmth <= 0)
			return value;
		const double gain = std::exp2(-settings.NightWarmth * .5 * channel);
		return output.hdr ? color::NitsToPq(color::PqToNits(value) * gain)
		                  : color::SrgbEncode(color::SrgbDecode(value) * gain);
	};
	if (settings.Order.empty())
	{
		const double amount = settings.Bypass ? 0 : settings.Contrast;
		const bool sdr_fix = !output.hdr && settings.HdrFix && settings.ApplyInSdr;
		if (sdr_fix && settings.CorrectionFirst)
			code = color::SrgbEncode(std::pow(code, settings.Gamma));
		code = output.hdr ? color::ApplyHdrEffectsInOrder(code, output.white, settings.Gamma, amount, settings.Pivot,
		                                                  settings.HdrFix, settings.CorrectionFirst)
		                  : color::ApplyContrast(code, amount, settings.Pivot);
		if (sdr_fix && !settings.CorrectionFirst)
			code = color::SrgbEncode(std::pow(code, settings.Gamma));
		if (!settings.Bypass)
			for (int stage = 2; stage <= 7; ++stage)
				code = color::ApplyStageToCode(code, channel, stage, settings.Adjustments, output.hdr, output.white);
		return warm(code);
	}
	for (int stage : settings.Order)
	{
		if (stage == 1 && settings.HdrFix)
		{
			if (output.hdr)
				code = color::HdrGammaCurve(output.white, settings.Gamma).Map(code);
			else if (settings.ApplyInSdr)
				code = color::SrgbEncode(std::pow(code, settings.Gamma));
		}
		else if (!settings.Bypass)
		{
			if (stage == 0)
				code = output.hdr ? color::ApplyHdrContrastAndGamma(code, output.white, settings.Gamma,
				                                                    settings.Contrast, settings.Pivot, false)
				                  : color::ApplyContrast(code, settings.Contrast, settings.Pivot);
			else
				code = color::ApplyStageToCode(code, channel, stage, settings.Adjustments, output.hdr, output.white);
		}
	}
	return warm(code);
}

Ramp BuildGammaRamp(const Ramp& baseline, const Output& output, const Settings& settings)
{
	color::RequireRange(settings.NightWarmth, 0, 1);
	color::RequireRange(settings.Contrast, -.25, .25);
	color::RequireRange(settings.Pivot, .2, .8);
	color::RequireRange(settings.Gamma, 1, 4);
	color::Validate(settings.Adjustments);
	if (!settings.Order.empty())
	{
		std::array<bool, 8> seen {};
		for (int stage : settings.Order)
		{
			if (stage < 0 || stage > 7 || seen[stage])
				throw std::invalid_argument("Invalid effect order");
			seen[stage] = true;
		}
		if (settings.Order.size() != 8)
			throw std::invalid_argument("Incomplete effect order");
	}
	Ramp ramp {};
	for (size_t c = 0; c < 3; ++c)
		for (size_t i = 0; i < 256; ++i)
		{
			const double at =
				std::clamp(EvaluateOutputCode(static_cast<double>(i) / 255, static_cast<int>(c), output, settings), 0.0,
			               1.0) *
				255;
			const auto lower = static_cast<size_t>(std::floor(at)), upper = std::min(lower + 1, size_t { 255 });
			ramp[c][i] = static_cast<std::uint16_t>(std::lround(
				baseline[c][lower] + (static_cast<int>(baseline[c][upper]) - baseline[c][lower]) * (at - lower)));
		}
	return ramp;
}

Ramp CompositionBaseline(const Ramp& captured, const Output& output, const Ramp* carried_correction,
                         const Ramp* carried_baseline, const Output* carried_output, bool reset_to_identity)
{
	const auto neutral = IdentityRamp();
	if (reset_to_identity)
		return neutral;
	if (carried_correction && carried_baseline && carried_output &&
	    !DisplayTargetsFromIdentity(output.identity).empty() &&
	    DisplayTargetsFromIdentity(output.identity) == DisplayTargetsFromIdentity(carried_output->identity) &&
	    MaxRampDifference(captured, *carried_correction) <= 2)
	{
		// Neutral calibration has the same meaning in either mode. Nonneutral
		// calibration is reusable only within the same SDR/HDR interpretation.
		if (output.hdr == carried_output->hdr || MaxRampDifference(*carried_baseline, neutral) <= 2)
			return *carried_baseline;
		throw std::runtime_error(
			"Display mode changed with a non-neutral calibration. Its new calibration cannot be inferred from the retained correction");
	}
	if (output.hdr)
		for (double gamma : { 2.2, 2.4 })
		{
			const auto known = BuildGammaRamp(neutral, output, Settings { 0, 0.5, gamma, true, false });
			if (MaxRampDifference(captured, known) <= 2)
				return neutral;
		}
	return captured;
}

struct Session::Impl
{
	Snapshot snapshot;
	Ramp baseline {};
	Output output;
	Handle ready, done, mapping, child;
	std::shared_ptr<WriterLease> lease;
	std::unique_ptr<View> view;
	std::wstring file;
	bool running = false, finishing = false;
	int Finish()
	{
		if (!running)
			return 0;
		finishing = true;
		SetEvent(done.handle);
		const DWORD waited = WaitForSingleObject(child.handle, 5000);
		DWORD code = 15;
		GetExitCodeProcess(child.handle, &code);
		if (waited != WAIT_OBJECT_0)
			return 15;
		lease.reset();
		running = false;
		return static_cast<int>(code);
	}
	~Impl() { Finish(); }
};

void SetDiagnosticsEnabled(bool enabled)
{
	diagnostics_enabled = enabled;
}

void RemoveSessionLogs(const std::wstring& log_base)
{
	DeleteFileW((log_base + L".session.txt").c_str());
	DeleteFileW((log_base + L".result.txt").c_str());
}

Session::Session() = default;
Session::~Session() = default;
bool Session::IsActive() const
{
	return _impl && _impl->running && !_impl->finishing;
}

bool Session::HasSameDisplayMode(const Output& output) const
{
	return IsActive() && output.identity == _impl->snapshot.identity;
}

void Session::InheritCarryOver(const Session& other)
{
	if (IsActive() || !other._check_carry_over)
		return;
	_previous_correction = other._previous_correction;
	_previous_baseline = other._previous_baseline;
	_previous_output = other._previous_output;
	_check_carry_over = true;
}

void Session::Start(const Output& requested, const Settings& settings, bool reset_to_identity)
{
	if (_impl && _impl->running)
		throw std::runtime_error("The previous correction or its restoration is still running");
	const auto current = ActiveOutputs();
	const auto found = std::find_if(current.begin(), current.end(),
	                                [&](const Output& output) { return output.identity == requested.identity; });
	if (found == current.end())
		throw std::runtime_error("Display configuration changed before activation");
	const Output output = *found;
	if (!output.supported)
		throw std::runtime_error("This display mode is not supported by the live backend");
	auto session = std::make_unique<Impl>();
	session->lease = writer_leases.Acquire(WRITER_LEASE_NAME);
	if (output.name.size() >= std::size(session->snapshot.source) ||
	    output.identity.size() >= std::size(session->snapshot.identity))
		throw std::runtime_error("Display identity is too long");
	std::copy(output.name.begin(), output.name.end(), session->snapshot.source);
	std::copy(output.identity.begin(), output.identity.end(), session->snapshot.identity);
	DC dc(output.name);
	if (!dc.handle || !GetDeviceGammaRamp(dc.handle, &session->snapshot.baseline))
		throw std::runtime_error("Cannot capture baseline calibration");
	const Ramp captured = session->snapshot.baseline;
	session->baseline = CompositionBaseline(captured, output, _check_carry_over ? &_previous_correction : nullptr,
	                                        _check_carry_over ? &_previous_baseline : nullptr,
	                                        _check_carry_over ? &_previous_output : nullptr, reset_to_identity);
	session->output = output;
	const bool own_carry_over =
		_check_carry_over && !DisplayTargetsFromIdentity(output.identity).empty() &&
		DisplayTargetsFromIdentity(output.identity) == DisplayTargetsFromIdentity(_previous_output.identity) &&
		MaxRampDifference(captured, _previous_correction) <= 2;
	// For our own stale ramp, restore the saved clean calibration, not the
	// stale correction. For an external HDR fix, preserve exactly what we found.
	if (own_carry_over || reset_to_identity)
		session->snapshot.baseline = session->baseline;
	session->file = NewTempFilePath();
	const auto recipe = RecipeFor(session->snapshot.baseline, output);
	static LONG serial = 0;
	const auto key = std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64()) + L"-" +
	                 std::to_wstring(InterlockedIncrement(&serial));
	const auto ready_name = L"Local\\LutLive-ready-" + key, done_name = L"Local\\LutLive-done-" + key,
			   map_name = L"Local\\LutLive-map-" + key;
	session->ready.handle = CreateEventW(nullptr, TRUE, FALSE, ready_name.c_str());
	session->done.handle = CreateEventW(nullptr, TRUE, FALSE, done_name.c_str());
	session->mapping.handle =
		CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, MappingSize(recipe), map_name.c_str());
	if (!session->ready.handle || !session->done.handle || !session->mapping.handle)
		throw std::runtime_error("Cannot initialize recovery helper");
	session->view = std::make_unique<View>(session->mapping.handle);
	if (!session->view->shared)
		throw std::runtime_error("Cannot map recovery state");
	WriteRecoveryData(*session->view->shared, session->snapshot, recipe);
	if (own_carry_over || reset_to_identity)
	{
		session->view->shared->last = captured;
		InterlockedExchange(&session->view->shared->applied, 1);
	}
	const auto exe = app_settings::ExecutablePath();
	auto command = QuoteArgument(exe) + L" --live-guard " + QuoteArgument(session->file) + L" " +
	               std::to_wstring(GetCurrentProcessId()) + L" " + QuoteArgument(ready_name) + L" " +
	               QuoteArgument(done_name) + L" " + QuoteArgument(map_name);
	STARTUPINFOW startup {};
	startup.cb = sizeof(startup);
	startup.dwFlags = STARTF_FORCEOFFFEEDBACK;
	PROCESS_INFORMATION process {};
	if (!CreateProcessW(exe.c_str(), command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr,
	                    &startup, &process))
		throw std::runtime_error("Cannot start recovery helper");
	session->child.handle = process.hProcess;
	CloseHandle(process.hThread);
	session->running = true;
	HANDLE wait[] = { session->ready.handle, session->child.handle };
	if (WaitForMultipleObjects(2, wait, FALSE, 5000) != WAIT_OBJECT_0)
		throw std::runtime_error("Recovery helper did not become ready");
	_impl = std::move(session);
	_check_carry_over = false;
	try
	{
		Update(output, settings);
	}
	catch (...)
	{
		Stop();
		throw;
	}
}

void Session::Update(const Output& output, const Settings& settings)
{
	if (!HasSameDisplayMode(output))
		throw std::runtime_error("Display mode changed; re-enable after checking the new configuration");
	if (WaitForSingleObject(_impl->child.handle, 0) != WAIT_TIMEOUT)
		throw std::runtime_error("Recovery helper stopped; disabling corrections");
	const auto current_output = FindOutputForSnapshot(_impl->snapshot);
	if (current_output.name.empty() || !current_output.supported)
		throw std::runtime_error("Display configuration changed or is unsupported");
	DC dc(current_output.name);
	Ramp current {};
	if (!dc.handle || !GetDeviceGammaRamp(dc.handle, &current))
		throw std::runtime_error("Cannot read active calibration");
	auto* shared = _impl->view->shared;
	const bool applied = InterlockedCompareExchange(&shared->applied, 0, 0) != 0;
	if (!IsRampOurs(current, _impl->snapshot.baseline, shared->last, shared->candidate, applied, false))
		throw std::runtime_error("Another calibration changed the ramp; its adjustment will be preserved");
	const Ramp next = BuildGammaRamp(_impl->baseline, current_output, settings);
	if (MaxRampDifference(current, next) == 0)
		return;
	shared->candidate = next;
	MemoryBarrier();
	InterlockedExchange(&shared->pending, 1);
	const bool accepted = SetDeviceGammaRamp(dc.handle, &shared->candidate) != FALSE;
	// Some drivers report failure despite retaining the ramp. The verified
	// readback, rather than that return value alone, decides whether it worked.
	if (!GetDeviceGammaRamp(dc.handle, &current))
		throw std::runtime_error("Cannot verify the requested correction");
	if (MaxRampDifference(current, next) > 2)
		throw std::runtime_error(accepted ? "Driver did not retain the requested correction"
		                                  : "Driver rejected the correction");
	shared->last = next;
	MemoryBarrier();
	InterlockedExchange(&shared->applied, 1);
	InterlockedExchange(&shared->pending, 0);
	if (!diagnostics_enabled)
		return;
	std::ofstream log(_impl->file + L".session.txt", std::ios::app);
	log.imbue(std::locale::classic());
	log << "tick=" << GetTickCount64() << " hdr=" << current_output.hdr << " white_nits=" << current_output.white
		<< " contrast=" << settings.Contrast << " pivot=" << settings.Pivot << " gamma=" << settings.Gamma
		<< " hdr_fix=" << settings.HdrFix << " bypass=" << settings.Bypass
		<< " readback_max_error=" << MaxRampDifference(current, next) << '\n';
}

int Session::Stop()
{
	if (!_impl)
		return 0;
	const int code = _impl->Finish();
	if ((code == 0 || code == 10) && !diagnostics_enabled)
		RemoveSessionLogs(_impl->file);
	if (code == 11 && _impl->view && _impl->view->shared && _impl->view->shared->applied &&
	    MaxRampDifference(_impl->view->shared->last, _impl->snapshot.baseline) > 2)
	{
		_previous_correction = _impl->view->shared->last;
		_previous_baseline = _impl->baseline;
		_previous_output = _impl->output;
		_check_carry_over = true;
	}
	if (!_impl->running)
		_impl.reset();
	return code;
}

int RunRecoveryGuardian(const wchar_t* file, DWORD parent_id, const wchar_t* ready_name, const wchar_t* done_name,
                        const wchar_t* map_name, bool probe)
{
	try
	{
		Handle parent(OpenProcess(SYNCHRONIZE, FALSE, parent_id));
		Handle ready(OpenEventW(EVENT_MODIFY_STATE, FALSE, ready_name));
		Handle done(OpenEventW(SYNCHRONIZE, FALSE, done_name));
		Handle mapping(OpenFileMappingW(FILE_MAP_ALL_ACCESS, FALSE, map_name));
		View view(mapping.handle);
		Handle lease(probe ? nullptr : OpenMutexW(SYNCHRONIZE, FALSE, WRITER_LEASE_NAME));
		if (!parent.handle || !ready.handle || !done.handle || !mapping.handle || !view.shared ||
		    (!probe && !lease.handle))
			return 16;
		const auto snapshot = ReadRecoveryData(*view.shared);
		SetEvent(ready.handle);
		HANDLE wait[] = { parent.handle, done.handle };
		if (WaitForMultipleObjects(2, wait, FALSE, INFINITE) == WAIT_FAILED)
			return 17;
		const int code = probe ? 0 : RestoreSnapshot(snapshot, view.shared);
		const std::wstring result_file = std::wstring(file) + L".result.txt";
		std::ofstream log(result_file);
		log << "Live restoration result=" << code
			<< "\n0=baseline restored; 10=external ramp preserved; 11=display mode changed; other=restoration error\n";
		return code;
	}
	catch (...)
	{
		return 18;
	}
}

int WriterLeaseSelfTest()
{
	const auto name = L"Local\\MonitorMageLeaseTest-" + std::to_wstring(GetCurrentProcessId()) + L"-" +
	                  std::to_wstring(GetTickCount64());
	WriterLeases owner, other;
	try
	{
		auto first = owner.Acquire(name.c_str()), second = owner.Acquire(name.c_str());
		if (first != second)
			return 1;
		const auto blocked = [&]()
		{
			try
			{
				auto unexpected = other.Acquire(name.c_str());
				return false;
			}
			catch (const std::exception&)
			{
				return true;
			}
		};
		if (!blocked())
			return 2;
		first.reset();
		if (!blocked())
			return 3;
		Handle recovery(OpenMutexW(SYNCHRONIZE, FALSE, name.c_str()));
		if (!recovery.handle)
			return 4;
		second.reset();
		if (!blocked())
			return 5;
		CloseHandle(recovery.handle);
		recovery.handle = nullptr;
		auto next = other.Acquire(name.c_str());
		if (!next)
			return 6;
		return 0;
	}
	catch (...)
	{
		return 7;
	}
}

int BaselineStorageSelfTest()
{
	try
	{
		const auto stores_and_rebuilds = [](const Ramp& ramp, const Output& output, BaselineKind expected)
		{
			auto shared = std::make_unique<Shared>();
			Snapshot snapshot;
			wcscpy_s(snapshot.source, L"probe-source");
			wcscpy_s(snapshot.identity, L"probe-identity");
			snapshot.baseline = ramp;
			const auto recipe = RecipeFor(ramp, output);
			WriteRecoveryData(*shared, snapshot, recipe);
			const bool written = shared->baseline != Ramp {};
			return recipe.kind == expected && written == (expected == BaselineKind::Stored) &&
			       ReadRecoveryData(*shared).baseline == ramp;
		};
		Output hdr, sdr;
		hdr.hdr = true;
		hdr.white = 280;
		sdr.white = 200;
		const auto gamma_fix = HdrGammaBaseline(280, 2.4);
		auto almost_identity = IdentityRamp(), almost_gamma_fix = gamma_fix;
		almost_identity[0][100] += 1;
		almost_gamma_fix[2][200] += 1;
		if (!stores_and_rebuilds(IdentityRamp(), sdr, BaselineKind::Identity) ||
		    !stores_and_rebuilds(IdentityRamp(), hdr, BaselineKind::Identity) ||
		    !stores_and_rebuilds(HdrGammaBaseline(280, 2.2), hdr, BaselineKind::HdrGamma) ||
		    !stores_and_rebuilds(gamma_fix, hdr, BaselineKind::HdrGamma) ||
		    !stores_and_rebuilds(gamma_fix, sdr, BaselineKind::Stored) ||
		    !stores_and_rebuilds(almost_identity, sdr, BaselineKind::Stored) ||
		    !stores_and_rebuilds(almost_gamma_fix, hdr, BaselineKind::Stored))
			return 1;
		return offsetof(Shared, baseline) + sizeof(Ramp) == sizeof(Shared) ? 0 : 2;
	}
	catch (...)
	{
		return 3;
	}
}

int RecoverySelfTest(const wchar_t* exe, bool crash_parent)
{
	try
	{
		const auto file = NewTempFilePath();
		const auto key = std::to_wstring(GetCurrentProcessId()) + L"-probe-" + std::to_wstring(GetTickCount64());
		const auto ready_name = L"Local\\" + key + L"-ready", done_name = L"Local\\" + key + L"-done",
				   mutex_name = L"Local\\" + key + L"-map";
		Handle ready(CreateEventW(nullptr, TRUE, FALSE, ready_name.c_str())),
			done(CreateEventW(nullptr, TRUE, FALSE, done_name.c_str()));
		Handle mapping(CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0,
		                                  MappingSize({ BaselineKind::Identity }), mutex_name.c_str()));
		if (!ready.handle || !done.handle || !mapping.handle)
			return 1;
		View view(mapping.handle);
		if (!view.shared)
			return 1;
		wcscpy_s(view.shared->source, L"probe-source");
		wcscpy_s(view.shared->identity, L"probe-identity");
		view.shared->recipe.kind = BaselineKind::Identity;
		Handle owner;
		DWORD parent_id = GetCurrentProcessId();
		if (crash_parent)
		{
			auto owner_command = QuoteArgument(exe) + L" --live-probe-owner";
			STARTUPINFOW owner_start {};
			owner_start.cb = sizeof(owner_start);
			owner_start.dwFlags = STARTF_FORCEOFFFEEDBACK;
			PROCESS_INFORMATION owner_process {};
			if (!CreateProcessW(exe, owner_command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr,
			                    &owner_start, &owner_process))
				return 5;
			owner.handle = owner_process.hProcess;
			CloseHandle(owner_process.hThread);
			parent_id = owner_process.dwProcessId;
		}
		auto command = QuoteArgument(exe) + L" --live-guard-probe " + QuoteArgument(file) + L" " +
		               std::to_wstring(parent_id) + L" " + QuoteArgument(ready_name) + L" " + QuoteArgument(done_name) +
		               L" " + QuoteArgument(mutex_name);
		STARTUPINFOW startup {};
		startup.cb = sizeof(startup);
		startup.dwFlags = STARTF_FORCEOFFFEEDBACK;
		PROCESS_INFORMATION process {};
		if (!CreateProcessW(exe, command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &startup,
		                    &process))
		{
			if (owner.handle)
				TerminateProcess(owner.handle, 1);
			return 2;
		}
		Handle child(process.hProcess), thread(process.hThread);
		HANDLE wait[] = { ready.handle, child.handle };
		const bool started = WaitForMultipleObjects(2, wait, FALSE, 5000) == WAIT_OBJECT_0;
		const bool waiting = started && WaitForSingleObject(child.handle, 0) == WAIT_TIMEOUT;
		// Only terminate the disposable process created by this test. Probe mode
		// exercises parent-death recovery without reading or writing any LUT.
		if (crash_parent)
			TerminateProcess(owner.handle, 1);
		else
			SetEvent(done.handle);
		const DWORD finished = WaitForSingleObject(child.handle, 5000);
		DWORD code = 19;
		GetExitCodeProcess(child.handle, &code);
		if (finished != WAIT_OBJECT_0)
			SetEvent(done.handle);
		DeleteFileW((file + L".result.txt").c_str());
		return waiting && finished == WAIT_OBJECT_0 && code == 0 ? 0 : 3;
	}
	catch (...)
	{
		return 4;
	}
}
} // namespace live
