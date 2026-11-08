#include <windows.h>
#include <icm.h>
#include <winternl.h>
#include <d3dkmthk.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>
#include <sstream>
#include <locale>
#include "color_math.h"

namespace
{
using Ramp = std::array<std::array<WORD, 256>, 3>;
static_assert(sizeof(Ramp) == 1536);
struct Handle
{
	HANDLE h = nullptr;
	explicit Handle(HANDLE value = nullptr) : h(value) {}
	~Handle()
	{
		if (h && h != INVALID_HANDLE_VALUE)
			CloseHandle(h);
	}
	Handle(const Handle&) = delete;
	Handle& operator=(const Handle&) = delete;
};

struct DC
{
	HDC h;
	explicit DC(const std::wstring& name) : h(CreateDCW(L"DISPLAY", name.c_str(), nullptr, nullptr)) {}
	~DC()
	{
		if (h)
			DeleteDC(h);
	}
};

struct Path
{
	DISPLAYCONFIG_PATH_INFO info {};
	std::wstring source, name, identity;
	DISPLAYCONFIG_GET_ADVANCED_COLOR_INFO color {};
	LONG colorResult = ERROR_NOT_SUPPORTED;
};

template <class T>
void InitDeviceInfoHeader(T& value, DISPLAYCONFIG_DEVICE_INFO_TYPE type, LUID adapter, UINT32 id)
{
	value.header = { type, sizeof(T), adapter, id };
}

std::vector<Path> ActiveDisplayPaths()
{
	for (int attempt = 0; attempt < 5; ++attempt)
	{
		UINT32 pc = 0, mc = 0;
		LONG result = GetDisplayConfigBufferSizes(QDC_ONLY_ACTIVE_PATHS, &pc, &mc);
		if (result != ERROR_SUCCESS)
			throw std::runtime_error("Cannot enumerate display configuration: " + std::to_string(result));
		std::vector<DISPLAYCONFIG_PATH_INFO> raw(pc);
		std::vector<DISPLAYCONFIG_MODE_INFO> modes(mc);
		result = QueryDisplayConfig(QDC_ONLY_ACTIVE_PATHS, &pc, raw.data(), &mc, modes.data(), nullptr);
		if (result == ERROR_INSUFFICIENT_BUFFER)
			continue;
		if (result != ERROR_SUCCESS)
			throw std::runtime_error("QueryDisplayConfig failed: " + std::to_string(result));
		std::vector<Path> out;
		for (UINT32 i = 0; i < pc; ++i)
		{
			Path p;
			p.info = raw[i];
			DISPLAYCONFIG_SOURCE_DEVICE_NAME source {};
			InitDeviceInfoHeader(source, DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_NAME, p.info.sourceInfo.adapterId,
			                     p.info.sourceInfo.id);
			if (DisplayConfigGetDeviceInfo(&source.header) != ERROR_SUCCESS)
				throw std::runtime_error("Cannot resolve active source name");
			p.source = source.viewGdiDeviceName;
			DISPLAYCONFIG_TARGET_DEVICE_NAME target {};
			InitDeviceInfoHeader(target, DISPLAYCONFIG_DEVICE_INFO_GET_TARGET_NAME, p.info.targetInfo.adapterId,
			                     p.info.targetInfo.id);
			if (DisplayConfigGetDeviceInfo(&target.header) != ERROR_SUCCESS)
				throw std::runtime_error("Cannot resolve physical output identity");
			p.name = target.monitorFriendlyDeviceName;
			p.identity = target.monitorDevicePath;
			InitDeviceInfoHeader(p.color, DISPLAYCONFIG_DEVICE_INFO_GET_ADVANCED_COLOR_INFO,
			                     p.info.targetInfo.adapterId, p.info.targetInfo.id);
			p.colorResult = DisplayConfigGetDeviceInfo(&p.color.header);
			out.push_back(std::move(p));
		}
		return out;
	}
	throw std::runtime_error("Display configuration kept changing; retry once it settles");
}

std::wstring DisplaySignature(const std::wstring& source)
{
	std::vector<std::wstring> ids;
	for (const auto& p : ActiveDisplayPaths())
		if (p.source == source)
		{
			if (p.identity.empty() || p.colorResult != ERROR_SUCCESS)
				throw std::runtime_error("Cannot establish output identity and color mode for a reversible test");
			ids.push_back(p.identity + L":" + std::to_wstring(p.color.value));
		}
	std::sort(ids.begin(), ids.end());
	std::wstring out;
	for (const auto& id : ids)
		out += id + L"\n";
	return out;
}

// The ABI is taken from icm.h. Load dynamically: these exports may be absent on Win10.
struct MhcCaps
{
	UINT size;
	BOOL supported;
	UINT entries, rows, columns;
};

struct VcgtCaps
{
	UINT size;
	BOOL supported;
};

using GetCaps = HRESULT(WINAPI*)(WCS_PROFILE_MANAGEMENT_SCOPE, LUID, UINT32, int, void*);
using GetDisplayDefault = HRESULT(WINAPI*)(WCS_PROFILE_MANAGEMENT_SCOPE, LUID, UINT32, COLORPROFILETYPE,
                                           COLORPROFILESUBTYPE, LPWSTR*);
void PrintDisplayProfileDefaults(const Path& p, GetDisplayDefault query)
{
	if (!query)
	{
		std::wcout << L"  Display profile-default query unavailable\n";
		return;
	}
	for (auto scope : { WCS_PROFILE_MANAGEMENT_SCOPE_CURRENT_USER, WCS_PROFILE_MANAGEMENT_SCOPE_SYSTEM_WIDE })
	{
		for (auto subtype : { CPST_STANDARD_DISPLAY_COLOR_MODE, CPST_EXTENDED_DISPLAY_COLOR_MODE })
		{
			LPWSTR name = nullptr;
			const HRESULT result =
				query(scope, p.info.sourceInfo.adapterId, p.info.sourceInfo.id, CPT_ICC, subtype, &name);
			std::wcout << L"  " << (scope == WCS_PROFILE_MANAGEMENT_SCOPE_CURRENT_USER ? L"User" : L"System")
					   << (subtype == CPST_STANDARD_DISPLAY_COLOR_MODE ? L" standard" : L" Advanced Color")
					   << L" profile default: ";
			if (SUCCEEDED(result))
				std::wcout << (name && *name ? name : L"none returned");
			else
				std::wcout << L"query failed HRESULT=0x" << std::hex << static_cast<unsigned long>(result) << std::dec;
			std::wcout << L"\n";
			if (name)
				LocalFree(name);
		}
	}
}

void PrintDriverModel(LUID luid)
{
	HMODULE gdi = GetModuleHandleW(L"gdi32.dll");
	const auto open =
		reinterpret_cast<decltype(&D3DKMTOpenAdapterFromLuid)>(GetProcAddress(gdi, "D3DKMTOpenAdapterFromLuid"));
	const auto query =
		reinterpret_cast<decltype(&D3DKMTQueryAdapterInfo)>(GetProcAddress(gdi, "D3DKMTQueryAdapterInfo"));
	const auto close = reinterpret_cast<decltype(&D3DKMTCloseAdapter)>(GetProcAddress(gdi, "D3DKMTCloseAdapter"));
	if (!open || !query || !close)
	{
		std::wcout << L"  WDDM query unavailable\n";
		return;
	}
	D3DKMT_OPENADAPTERFROMLUID adapter {};
	adapter.AdapterLuid = luid;
	const auto opened = open(&adapter);
	if (opened < 0)
	{
		std::wcout << L"  WDDM adapter open failed NTSTATUS=0x" << std::hex << static_cast<unsigned long>(opened)
				   << std::dec << L"\n";
		return;
	}
	D3DKMT_DRIVERVERSION version {};
	D3DKMT_QUERYADAPTERINFO info {};
	info.hAdapter = adapter.hAdapter;
	info.Type = KMTQAITYPE_DRIVERVERSION;
	info.pPrivateDriverData = &version;
	info.PrivateDriverDataSize = sizeof(version);
	const auto result = query(&info);
	D3DKMT_CLOSEADAPTER closed {};
	closed.hAdapter = adapter.hAdapter;
	close(&closed);
	if (result >= 0)
	{
		const auto raw = static_cast<unsigned>(version);
		std::wcout << L"  Display driver model: WDDM " << raw / 1000 << L"." << (raw % 1000) / 100 << L" (raw=" << raw
				   << L")\n";
	}
	else
		std::wcout << L"  WDDM query failed NTSTATUS=0x" << std::hex << static_cast<unsigned long>(result) << std::dec
				   << L"\n";
}

void ProfileTags(const std::wstring& path)
{
	PROFILE profile {};
	profile.dwType = PROFILE_FILENAME;
	profile.pProfileData = const_cast<wchar_t*>(path.c_str());
	profile.cbDataSize = static_cast<DWORD>((path.size() + 1) * sizeof(wchar_t));
	HPROFILE handle = OpenColorProfileW(&profile, PROFILE_READ, FILE_SHARE_READ, OPEN_EXISTING);
	if (!handle)
	{
		std::wcout << L"  ICC tag inspection unavailable: " << GetLastError() << L"\n";
		return;
	}
	struct Tag
	{
		TAGTYPE id;
		const wchar_t* name;
	};
	for (const auto& tag : { Tag { 0x4d484332, L"MHC2" }, Tag { 0x76636774, L"vcgt" }, Tag { 0x6c756d69, L"lumi" } })
	{
		BOOL present = FALSE;
		if (IsColorProfileTagPresent(handle, tag.id, &present))
			std::wcout << L"  GDI profile tag " << tag.name << L": " << (present ? L"present" : L"absent") << L"\n";
		else
			std::wcout << L"  GDI profile tag " << tag.name << L": query failed\n";
	}
	CloseColorProfile(handle);
}

void PrintCalibrationCapabilities(const Path& p, GetCaps query)
{
	if (!query)
	{
		std::wcout << L"  MHC2/VCGT capability query: unavailable (not proof of unsupported hardware)\n";
		return;
	}
	for (auto scope : { WCS_PROFILE_MANAGEMENT_SCOPE_CURRENT_USER, WCS_PROFILE_MANAGEMENT_SCOPE_SYSTEM_WIDE })
	{
		MhcCaps mhc { sizeof(MhcCaps), FALSE, 0, 0, 0 };
		VcgtCaps vcgt { sizeof(VcgtCaps), FALSE };
		const HRESULT mr = query(scope, p.info.sourceInfo.adapterId, p.info.sourceInfo.id, 2, &mhc);
		const HRESULT vr = query(scope, p.info.sourceInfo.adapterId, p.info.sourceInfo.id, 1, &vcgt);
		std::wcout << L"  " << (scope == WCS_PROFILE_MANAGEMENT_SCOPE_CURRENT_USER ? L"User" : L"System")
				   << L" capabilities: ";
		if (SUCCEEDED(mr))
			std::wcout << L"MHC2=" << (mhc.supported ? L"yes" : L"no") << L", entries=" << mhc.entries << L", matrix="
					   << mhc.rows << L"x" << mhc.columns;
		else
			std::wcout << L"MHC2 query failed HRESULT=0x" << std::hex << static_cast<unsigned long>(mr) << std::dec;
		if (SUCCEEDED(vr))
			std::wcout << L"; VCGT=" << (vcgt.supported ? L"yes" : L"no");
		else
			std::wcout << L"; VCGT query failed HRESULT=0x" << std::hex << static_cast<unsigned long>(vr) << std::dec;
		std::wcout << L"\n";
	}
}

void PrintDiagnosticReport()
{
	std::wcout << L"LUT Diagnostic 0.4 - read-only report\n";
	using RtlVersion = LONG(WINAPI*)(OSVERSIONINFOW*);
	const auto rtl = reinterpret_cast<RtlVersion>(GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "RtlGetVersion"));
	OSVERSIONINFOW version {};
	version.dwOSVersionInfoSize = sizeof(version);
	if (rtl && rtl(&version) == 0)
		std::wcout << L"Windows " << version.dwMajorVersion << L"." << version.dwMinorVersion << L" build "
				   << version.dwBuildNumber << L"\n";
	DISPLAY_DEVICEW adapter {};
	adapter.cb = sizeof(adapter);
	for (DWORD i = 0; EnumDisplayDevicesW(nullptr, i, &adapter, 0); ++i)
	{
		if (adapter.StateFlags & DISPLAY_DEVICE_ATTACHED_TO_DESKTOP)
			std::wcout << L"Adapter: " << adapter.DeviceString << L" (" << adapter.DeviceName << L")\n";
	}
	HMODULE mscms = LoadLibraryExW(L"mscms.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
	const auto query =
		mscms ? reinterpret_cast<GetCaps>(GetProcAddress(mscms, "ColorProfileGetDeviceCapabilities")) : nullptr;
	const auto getDefault =
		mscms ? reinterpret_cast<GetDisplayDefault>(GetProcAddress(mscms, "ColorProfileGetDisplayDefault")) : nullptr;
	std::wcout << L"\nColor-management API exports:\n";
	for (const char* name :
	     { "ColorProfileGetDeviceCapabilities", "ColorProfileGetDisplayDefault", "ColorProfileAddDisplayAssociation",
	       "ColorProfileSetDisplayDefaultAssociation", "WcsGetDefaultColorProfile", "WcsSetDefaultColorProfile" })
		std::cout << "  " << name << ": " << (mscms && GetProcAddress(mscms, name) ? "available" : "unavailable")
				  << '\n';
	BOOL calibration = FALSE;
	if (WcsGetCalibrationManagementState(&calibration))
		std::wcout << L"Legacy Windows calibration loader: " << (calibration ? L"enabled" : L"disabled")
				   << L" (not an MHC2 capability check)\n";
	const auto list = ActiveDisplayPaths();
	std::vector<std::wstring> done;
	for (const auto& p : list)
	{
		if (std::find(done.begin(), done.end(), p.source) != done.end())
			continue;
		done.push_back(p.source);
		std::wcout << L"\nSource " << p.source << L" [adapter=" << p.info.sourceInfo.adapterId.HighPart << L":"
				   << p.info.sourceInfo.adapterId.LowPart << L", source ID=" << p.info.sourceInfo.id << L"]\n";
		size_t count = 0;
		for (const auto& target : list)
			if (target.source == p.source)
			{
				++count;
				std::wcout << L"  Output: " << target.name << L" (target ID=" << target.info.targetInfo.id
						   << L")\n  Identity: " << target.identity << L"\n";
				if (target.colorResult == ERROR_SUCCESS)
				{
					std::wcout << L"  Advanced Color supported=" << target.color.advancedColorSupported << L", enabled="
							   << target.color.advancedColorEnabled << L", wide-color enforced="
							   << target.color.wideColorEnforced << L", bits/channel="
							   << target.color.bitsPerColorChannel << L"\n";
				}
				else
					std::wcout << L"  Advanced Color query failed: " << target.colorResult << L"\n";
				DISPLAYCONFIG_SDR_WHITE_LEVEL white {};
				InitDeviceInfoHeader(white, DISPLAYCONFIG_DEVICE_INFO_GET_SDR_WHITE_LEVEL,
				                     target.info.targetInfo.adapterId, target.info.targetInfo.id);
				if (DisplayConfigGetDeviceInfo(&white.header) == ERROR_SUCCESS)
					std::wcout << L"  Windows SDR white: " << white.SDRWhiteLevel * 0.08 << L" nits\n";
			}
		std::wcout << L"  Shared-source outputs: " << count
				   << (count > 1 ? L" (clone group; GDI test addresses this whole source)" : L"") << L"\n";
		PrintDriverModel(p.info.sourceInfo.adapterId);
		PrintCalibrationCapabilities(p, query);
		PrintDisplayProfileDefaults(p, getDefault);
		DC dc(p.source);
		Ramp ramp {};
		if (dc.h && GetDeviceGammaRamp(dc.h, &ramp))
		{
			std::wcout << L"  GDI gamma ramp: readable (256 x 3; readback does not prove scanout behavior)\n";
			for (size_t c = 0; c < 3; ++c)
				std::wcout << L"    " << L"RGB"[c] << L" samples [0,64,128,192,255]: " << ramp[c][0] << L", "
						   << ramp[c][64] << L", " << ramp[c][128] << L", " << ramp[c][192] << L", " << ramp[c][255]
						   << L"\n";
		}
		else
			std::wcout << L"  GDI gamma ramp: not readable\n";
		DWORD length = 32768;
		std::vector<wchar_t> profile(length);
		if (dc.h && GetICMProfileW(dc.h, &length, profile.data()))
		{
			std::wcout << L"  GDI ICC profile: " << profile.data() << L"\n";
			ProfileTags(profile.data());
		}
		else
			std::wcout << L"  GDI ICC profile: unavailable\n";
	}
	if (mscms)
		FreeLibrary(mscms);
	std::wcout << L"\nNo curves or profile associations changed. Advanced Color is not an HDR-only flag.\n"
				  L"GDI ICC inspection does not enumerate the separate Advanced Color profile association.\n"
				  L"This report does not prove independent per-output LUTs, fullscreen behavior, or MHC2 activation.\n";
}

// Endpoint-preserving, monotonic S curve, with a fixed pivot. Amount=0 is identity.
double ContrastCurve(double x, double amount, double pivot)
{
	return color::ApplyContrast(x, amount, pivot);
}

void ExportHdr(const std::wstring& file, double white, double gamma, std::size_t entries)
{
	const color::HdrGammaCurve correction(white, gamma);
	const auto values = correction.SampleLut(entries);
	std::ostringstream csv;
	csv.imbue(std::locale::classic());
	csv << std::setprecision(17);
	csv << "input_pq,output_pq,input_nits,output_nits\n";
	for (std::size_t i = 0; i < entries; ++i)
	{
		const double x = static_cast<double>(i) / (entries - 1);
		csv << x << ',' << values[i] << ',' << color::PqToNits(x) << ',' << color::PqToNits(values[i]) << '\n';
	}
	const auto text = csv.str();
	Handle h(CreateFileW(file.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr));
	if (h.h == INVALID_HANDLE_VALUE)
		throw std::runtime_error("Cannot create CSV (existing files are not overwritten)");
	DWORD written = 0;
	if (!WriteFile(h.h, text.data(), static_cast<DWORD>(text.size()), &written, nullptr) || written != text.size())
		throw std::runtime_error("CSV write failed");
	std::wcout << L"Exported " << entries << L" PQ-domain curve samples to " << file << L"\nSDR white=" << white
			   << L" nits; target gamma=" << gamma
			   << L"; black floor=0. No display APIs used.\n"
				  L"Offline model only: applying this curve requires a verified PQ-domain backend.\n";
}

void CompareHdr(const std::wstring& source, double white, double gamma)
{
	const color::HdrGammaCurve correction(white, gamma);
	const auto expected = correction.SampleLut(256);
	if (DisplaySignature(source).empty())
		throw std::runtime_error("Display source is not active");
	DC dc(source);
	Ramp actual {};
	if (!dc.h || !GetDeviceGammaRamp(dc.h, &actual))
		throw std::runtime_error("Cannot read gamma ramp");
	std::wcout << L"Read-only HDR model comparison for " << source << L"\nSDR white=" << white << L" nits; gamma="
			   << gamma << L"\n";
	for (size_t channel = 0; channel < 3; ++channel)
	{
		int maximum = 0, exact = 0, close = 0;
		for (size_t i = 0; i < 256; ++i)
		{
			const int target = static_cast<int>(std::lround(expected[i] * 65535));
			const int difference = std::abs(static_cast<int>(actual[channel][i]) - target);
			maximum = std::max(maximum, difference);
			if (!difference)
				++exact;
			if (difference <= 2)
				++close;
		}
		std::wcout << L"  " << L"RGB"[channel] << L": exact samples=" << exact << L"/256; within 2 WORD steps=" << close
				   << L"/256; maximum difference=" << maximum << L"\n";
	}
	std::wcout
		<< L"Matching readback does not establish visible HDR behavior or identify which application loaded the ramp. No display changes.\n";
}

Ramp ComposeContrastRamp(const Ramp& baseline, double amount, double pivot)
{
	Ramp result {};
	for (size_t c = 0; c < 3; ++c)
		for (size_t i = 0; i < 256; ++i)
		{
			const double at = ContrastCurve(static_cast<double>(i) / 255.0, amount, pivot) * 255.0;
			const auto lo = static_cast<size_t>(std::floor(at));
			const auto hi = std::min(lo + 1, size_t { 255 });
			result[c][i] =
				static_cast<WORD>(std::lround(baseline[c][lo] + (baseline[c][hi] - baseline[c][lo]) * (at - lo)));
		}
	return result;
}

void RunCurveSelfTest()
{
	Ramp base {};
	for (size_t c = 0; c < 3; ++c)
		for (size_t i = 0; i < 256; ++i)
			base[c][i] = static_cast<WORD>(i * 257);
	if (ComposeContrastRamp(base, 0, 0.5) != base)
		throw std::runtime_error("Identity composition failed");
	for (double pivot : { 0.2, 0.5, 0.8 })
		for (double amount : { -0.25, 0.0, 0.25 })
		{
			double previous = -1;
			for (int i = 0; i <= 10000; ++i)
			{
				const double y = ContrastCurve(i / 10000.0, amount, pivot);
				if (!std::isfinite(y) || y < previous || y < 0 || y > 1)
					throw std::runtime_error("Curve monotonicity/range failed");
				previous = y;
			}
			if (ContrastCurve(0, amount, pivot) != 0 || ContrastCurve(1, amount, pivot) != 1 ||
			    std::abs(ContrastCurve(pivot, amount, pivot) - pivot) > 1e-12)
				throw std::runtime_error("Endpoint/pivot invariant failed");
		}
	// Confirm actual composition direction with a channel-specific calibrated ramp.
	for (size_t i = 0; i < 256; ++i)
		base[0][i] = static_cast<WORD>(i * 200);
	const auto adjusted = ComposeContrastRamp(base, 0.2, 0.5);
	if (adjusted[0][0] != base[0][0] || adjusted[0][255] != base[0][255] || adjusted[0][64] >= base[0][64] ||
	    adjusted[0][192] <= base[0][192])
		throw std::runtime_error("Calibrated ramp composition failed");
	std::wcout << L"Curve and baseline-composition tests passed. No display APIs used.\n";
}

struct Snapshot
{
	DWORD magic = 0x3154554c; // LUT1
	wchar_t source[32] {};
	wchar_t signature[2048] {};
	Ramp ramp {};
};

void SaveSnapshot(const std::wstring& file, const Snapshot& value)
{
	Handle h(CreateFileW(file.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr));
	DWORD bytes = 0;
	if (h.h == INVALID_HANDLE_VALUE || !WriteFile(h.h, &value, sizeof(value), &bytes, nullptr) ||
	    bytes != sizeof(value) || !FlushFileBuffers(h.h))
		throw std::runtime_error("Cannot persist recovery snapshot");
}

Snapshot LoadSnapshot(const std::wstring& file)
{
	Handle h(CreateFileW(file.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL,
	                     nullptr));
	Snapshot value {};
	DWORD bytes = 0;
	LARGE_INTEGER size {};
	if (h.h == INVALID_HANDLE_VALUE || !GetFileSizeEx(h.h, &size) || size.QuadPart != sizeof(value) ||
	    !ReadFile(h.h, &value, sizeof(value), &bytes, nullptr) || bytes != sizeof(value) || value.magic != 0x3154554c ||
	    value.source[31] != 0 || value.signature[2047] != 0 || value.source[0] == 0 || value.signature[0] == 0)
		throw std::runtime_error("Invalid recovery snapshot");
	return value;
}

int RestoreSnapshot(const Snapshot& value)
{
	if (DisplaySignature(value.source) != value.signature)
	{
		std::wcerr
			<< L"Output identities or color mode changed. Refusing to write an old baseline to a different configuration.\n";
		return 3;
	}
	DC dc(value.source);
	if (!dc.h || !SetDeviceGammaRamp(dc.h, const_cast<Ramp*>(&value.ramp)))
		return 4;
	Ramp actual {};
	if (!GetDeviceGammaRamp(dc.h, &actual) || actual != value.ramp)
		return 5;
	return 0;
}

std::wstring QuoteArgument(const std::wstring& value)
{
	// These arguments are generated paths/names; none may contain a literal quote.
	if (value.find(L'"') != std::wstring::npos)
		throw std::runtime_error("Unexpected quote in generated argument");
	return L"\"" + value + L"\"";
}

HANDLE cancelEvent = nullptr;
BOOL WINAPI ConsoleCtrlHandler(DWORD kind)
{
	if ((kind == CTRL_C_EVENT || kind == CTRL_BREAK_EVENT) && cancelEvent)
	{
		SetEvent(cancelEvent);
		return TRUE;
	}
	return FALSE; // Closing/killing the parent is handled by the independent guardian.
}

int RunRecoveryGuardian(const std::wstring& file, DWORD parentId, const std::wstring& readyName,
                        const std::wstring& doneName, bool probe = false)
{
	const auto snapshot = LoadSnapshot(file);
	Handle parent(OpenProcess(SYNCHRONIZE, FALSE, parentId));
	Handle ready(OpenEventW(EVENT_MODIFY_STATE, FALSE, readyName.c_str()));
	Handle done(OpenEventW(SYNCHRONIZE, FALSE, doneName.c_str()));
	Handle lease(probe ? nullptr : OpenMutexW(SYNCHRONIZE, FALSE, L"Local\\LutWorkbench-LutWriter"));
	if (!parent.h || !ready.h || !done.h || (!probe && !lease.h))
		return 6;
	SetEvent(ready.h);
	HANDLE wait[] = { parent.h, done.h };
	const DWORD result = WaitForMultipleObjects(2, wait, FALSE, 30000);
	if (result == WAIT_FAILED)
		return 7;
	return probe ? 0 : RestoreSnapshot(snapshot);
}

void RunRecoverySelfTest()
{
	wchar_t temp[MAX_PATH] {}, exe[32768] {};
	const DWORD length = GetTempPathW(MAX_PATH, temp);
	if (!length || length >= MAX_PATH || !GetModuleFileNameW(nullptr, exe, static_cast<DWORD>(std::size(exe))))
		throw std::runtime_error("Cannot locate test paths");
	const auto key = std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64());
	const auto file = std::wstring(temp) + L"LutDiagnostic-selftest-" + key + L".bin";
	Snapshot snapshot;
	wcscpy_s(snapshot.source, L"test-source");
	wcscpy_s(snapshot.signature, L"test-identity");
	snapshot.ramp[0][128] = 12345;
	SaveSnapshot(file, snapshot);
	const auto roundtrip = LoadSnapshot(file);
	if (std::memcmp(&snapshot, &roundtrip, sizeof(snapshot)) != 0)
		throw std::runtime_error("Snapshot roundtrip failed");
	const auto readyName = L"Local\\LutDiagnostic-probe-ready-" + key;
	const auto doneName = L"Local\\LutDiagnostic-probe-done-" + key;
	Handle ready(CreateEventW(nullptr, TRUE, FALSE, readyName.c_str()));
	Handle done(CreateEventW(nullptr, TRUE, FALSE, doneName.c_str()));
	if (!ready.h || !done.h)
		throw std::runtime_error("Recovery test events failed");
	auto command = QuoteArgument(exe) + L" --guardian-probe " + QuoteArgument(file) + L" " +
	               std::to_wstring(GetCurrentProcessId()) + L" " + QuoteArgument(readyName) + L" " +
	               QuoteArgument(doneName);
	STARTUPINFOW startup {};
	startup.cb = sizeof(startup);
	PROCESS_INFORMATION process {};
	if (!CreateProcessW(exe, command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &startup,
	                    &process))
		throw std::runtime_error("Recovery test process failed");
	Handle child(process.hProcess), thread(process.hThread);
	HANDLE wait[] = { ready.h, child.h };
	const bool readyOk = WaitForMultipleObjects(2, wait, FALSE, 5000) == WAIT_OBJECT_0;
	const bool stillWaiting = readyOk && WaitForSingleObject(child.h, 0) == WAIT_TIMEOUT;
	SetEvent(done.h);
	const DWORD finished = WaitForSingleObject(child.h, 5000);
	DWORD code = STILL_ACTIVE;
	GetExitCodeProcess(child.h, &code);
	DeleteFileW(file.c_str()); // Only this test's uniquely-created temporary snapshot.
	if (!stillWaiting || finished != WAIT_OBJECT_0 || code != 0)
		throw std::runtime_error("Guardian handshake failed");
	std::wcout << L"Snapshot and independent guardian handshake passed. No display APIs used.\n";
}

Ramp BuildHdrRamp(double white, double gamma)
{
	const auto samples = color::HdrGammaCurve(white, gamma).SampleLut(256);
	Ramp ramp {};
	for (size_t c = 0; c < 3; ++c)
		for (size_t i = 0; i < 256; ++i)
			ramp[c][i] = static_cast<WORD>(std::lround(samples[i] * 65535));
	return ramp;
}

int RampDifference(const Ramp& a, const Ramp& b)
{
	int maximum = 0;
	for (size_t c = 0; c < 3; ++c)
		for (size_t i = 0; i < 256; ++i)
			maximum = std::max(maximum, std::abs(static_cast<int>(a[c][i]) - b[c][i]));
	return maximum;
}

double QuerySdrWhiteNits(const std::wstring& source)
{
	bool nvidia = false;
	DISPLAY_DEVICEW adapter {};
	adapter.cb = sizeof(adapter);
	for (DWORD i = 0; EnumDisplayDevicesW(nullptr, i, &adapter, 0); ++i)
		if (source == adapter.DeviceName && std::wstring(adapter.DeviceString).find(L"NVIDIA") != std::wstring::npos)
			nvidia = true;
	if (!nvidia)
		throw std::runtime_error("This experimental PQ-domain GDI test currently requires an NVIDIA output");
	std::vector<Path> targets;
	for (const auto& p : ActiveDisplayPaths())
		if (p.source == source)
			targets.push_back(p);
	if (targets.size() != 1 || targets[0].colorResult != ERROR_SUCCESS || !targets[0].color.advancedColorEnabled)
		throw std::runtime_error(
			"HDR comparison requires one active Advanced Color output; clone groups are not supported");
	DISPLAYCONFIG_SDR_WHITE_LEVEL white {};
	InitDeviceInfoHeader(white, DISPLAYCONFIG_DEVICE_INFO_GET_SDR_WHITE_LEVEL, targets[0].info.targetInfo.adapterId,
	                     targets[0].info.targetInfo.id);
	if (DisplayConfigGetDeviceInfo(&white.header) != ERROR_SUCCESS)
		throw std::runtime_error("Cannot query current SDR white level");
	const double nits = white.SDRWhiteLevel * 0.08;
	color::RequireRange(nits, 1, 10000);
	return nits;
}

int RunTimedLutTest(const std::wstring& source, double amount, double pivot, DWORD seconds, bool hdr = false,
                    double gamma = 2.2)
{
	Handle lease(CreateMutexW(nullptr, TRUE, L"Local\\LutWorkbench-LutWriter"));
	const DWORD leaseError = GetLastError();
	if (!lease.h || leaseError == ERROR_ALREADY_EXISTS)
		throw std::runtime_error("Another live LUT session or timed test is active; disable it before testing");
	Snapshot snapshot;
	if (source.size() >= std::size(snapshot.source))
		throw std::runtime_error("Invalid source name");
	const auto before = DisplaySignature(source);
	if (before.empty() || before.size() >= std::size(snapshot.signature))
		throw std::runtime_error("Source is not active or identity is too long");
	std::copy(source.begin(), source.end(), snapshot.source);
	std::copy(before.begin(), before.end(), snapshot.signature);
	DC dc(source);
	if (!dc.h || !GetDeviceGammaRamp(dc.h, &snapshot.ramp))
		throw std::runtime_error("Cannot capture baseline; nothing changed");
	Ramp identity {};
	for (auto& channel : identity)
		for (size_t i = 0; i < 256; ++i)
			channel[i] = static_cast<WORD>(i * 257);
	const double white = hdr ? QuerySdrWhiteNits(source) : 0;
	Ramp adjusted = hdr ? BuildHdrRamp(white, gamma) : ComposeContrastRamp(snapshot.ramp, amount, pivot);
	if (hdr && RampDifference(snapshot.ramp, identity) > 2 &&
	    RampDifference(snapshot.ramp, BuildHdrRamp(white, 2.2)) > 2 &&
	    RampDifference(snapshot.ramp, BuildHdrRamp(white, 2.4)) > 2)
		throw std::runtime_error(
			"Existing ramp is not identity or a recognized HDR correction; refusing to replace unknown calibration");
	wchar_t temp[MAX_PATH] {};
	const DWORD tempLength = GetTempPathW(MAX_PATH, temp);
	if (!tempLength || tempLength >= MAX_PATH)
		throw std::runtime_error("Cannot locate recovery directory");
	const auto key = std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64());
	const auto file = std::wstring(temp) + L"LutDiagnostic-" + key + L".bin";
	SaveSnapshot(file, snapshot);
	const auto readyName = L"Local\\LutDiagnostic-ready-" + key;
	const auto doneName = L"Local\\LutDiagnostic-done-" + key;
	Handle ready(CreateEventW(nullptr, TRUE, FALSE, readyName.c_str()));
	Handle done(CreateEventW(nullptr, TRUE, FALSE, doneName.c_str()));
	Handle cancel(CreateEventW(nullptr, TRUE, FALSE, nullptr));
	if (!ready.h || !done.h || !cancel.h)
		throw std::runtime_error("Cannot create recovery events; nothing changed");
	wchar_t exe[32768] {};
	if (!GetModuleFileNameW(nullptr, exe, static_cast<DWORD>(std::size(exe))))
		throw std::runtime_error("Cannot locate executable");
	auto command = QuoteArgument(exe) + L" --guardian " + QuoteArgument(file) + L" " +
	               std::to_wstring(GetCurrentProcessId()) + L" " + QuoteArgument(readyName) + L" " +
	               QuoteArgument(doneName);
	STARTUPINFOW startup {};
	startup.cb = sizeof(startup);
	PROCESS_INFORMATION process {};
	if (!CreateProcessW(exe, command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &startup,
	                    &process))
		throw std::runtime_error("Cannot start restoration guardian; nothing changed");
	Handle child(process.hProcess), thread(process.hThread);
	HANDLE startWait[] = { ready.h, child.h };
	if (WaitForMultipleObjects(2, startWait, FALSE, 5000) != WAIT_OBJECT_0)
	{
		SetEvent(done.h);
		WaitForSingleObject(child.h, 5000);
		throw std::runtime_error("Restoration guardian did not become ready; nothing changed");
	}
	cancelEvent = cancel.h;
	SetConsoleCtrlHandler(ConsoleCtrlHandler, TRUE);
	std::wcout << L"Recovery snapshot: " << file << L"\nTesting source " << source << L" for " << seconds
			   << L" seconds. All outputs sharing this source may change. Ctrl+C restores early.\n"
			   << std::flush;
	int result = 0;
	try
	{
		if (DisplaySignature(source) != before)
			throw std::runtime_error("Display changed before application");
		HANDLE wait[] = { cancel.h, child.h };
		bool apply = true;
		if (hdr)
		{
			std::wcout << L"HDR comparison: SDR white=" << white << L" nits, gamma=" << gamma
					   << L". Identity reference for 2 seconds, then correction for " << seconds - 2 << L" seconds.\n"
					   << std::flush;
			if (WaitForSingleObject(child.h, 0) != WAIT_TIMEOUT)
				throw std::runtime_error("Restoration guardian exited before application");
			if (!SetDeviceGammaRamp(dc.h, &identity))
				throw std::runtime_error("Identity reference application failed");
			Ramp reference {};
			std::wcout << L"Identity reference exact readback="
					   << (GetDeviceGammaRamp(dc.h, &reference) && reference == identity ? L"yes" : L"no") << L"\n"
					   << std::flush;
			apply = WaitForMultipleObjects(2, wait, FALSE, 2000) == WAIT_TIMEOUT;
		}
		if (apply)
		{
			if (DisplaySignature(source) != before || (hdr && std::abs(QuerySdrWhiteNits(source) - white) > 0.001))
				throw std::runtime_error("Display configuration or SDR brightness changed during the test");
			if (WaitForSingleObject(child.h, 0) != WAIT_TIMEOUT)
				throw std::runtime_error("Restoration guardian exited before application");
			if (!SetDeviceGammaRamp(dc.h, &adjusted))
				throw std::runtime_error("SetDeviceGammaRamp failed");
			Ramp actual {};
			const bool match = GetDeviceGammaRamp(dc.h, &actual) && actual == adjusted;
			std::wcout << L"Correction API accepted; exact readback=" << (match ? L"yes" : L"no")
					   << L". Observe the image; readback alone proves no visible effect.\n"
					   << std::flush;
			WaitForMultipleObjects(2, wait, FALSE, (hdr ? seconds - 2 : seconds) * 1000);
		}
	}
	catch (const std::exception& error)
	{
		std::cerr << error.what() << '\n';
		result = 1;
	}
	SetEvent(done.h);
	const DWORD waited = WaitForSingleObject(child.h, 10000);
	DWORD restored = STILL_ACTIVE;
	GetExitCodeProcess(child.h, &restored);
	SetConsoleCtrlHandler(ConsoleCtrlHandler, FALSE);
	cancelEvent = nullptr;
	if (waited != WAIT_OBJECT_0 || restored != 0)
	{
		std::wcerr << L"Restoration not verified (guardian code " << restored << L"). Snapshot retained: " << file
				   << L"\n";
		return 2;
	}
	std::wcout << L"Captured baseline restored with exact GDI readback. Snapshot retained for recovery.\n";
	return result;
}

void PrintUsage()
{
	std::wcout << LR"(Usage:
  LutDiagnostic [--report]                     Read-only display report
  LutDiagnostic --self-test                    Test curve math, no display access
  LutDiagnostic --self-test-recovery           Test recovery IPC, no display access
  LutDiagnostic --export-hdr curve.csv [--white 200] [--gamma 2.2] [--entries 1024]
  LutDiagnostic --compare-hdr \\.\DISPLAY1 --white 280 [--gamma 2.2]
  LutDiagnostic --test \\.\DISPLAY1 [--contrast 0.15] [--pivot 0.5] [--seconds 10]
  LutDiagnostic --test-hdr \\.\DISPLAY1 [--gamma 2.2] [--seconds 10]
  LutDiagnostic --test-hdr-auto                Compare on the sole active HDR output
  LutDiagnostic --restore "path-to-snapshot.bin"

Test limits: contrast [-0.25,0.25], pivot [0.2,0.8], duration [1,15] seconds.
SDR tests compose with the captured GDI ramp. HDR tests use a 2-second identity
reference followed by a replacement correction at the detected SDR white level.
HDR test duration is [3,15] seconds. Tests launch an independent restoration
guardian before applying. Do not switch display modes or run competing LUT tools
during a test. HDR behavior is experimental. No ICC associations are modified.
)";
}

double ParseNumber(const wchar_t* value)
{
	size_t used = 0;
	const std::wstring text(value);
	const double out = std::stod(text, &used);
	if (used != text.size() || !std::isfinite(out))
		throw std::runtime_error("Invalid number");
	return out;
}
} // namespace
int wmain(int argc, wchar_t** argv)
{
	try
	{
		if (argc == 1 || (argc == 2 && std::wstring(argv[1]) == L"--report"))
		{
			PrintDiagnosticReport();
			return 0;
		}
		if (argc == 2 && std::wstring(argv[1]) == L"--help")
		{
			PrintUsage();
			return 0;
		}
		if (argc == 2 && std::wstring(argv[1]) == L"--self-test")
		{
			RunCurveSelfTest();
			return 0;
		}
		if (argc == 2 && std::wstring(argv[1]) == L"--self-test-recovery")
		{
			RunRecoverySelfTest();
			return 0;
		}
		if (argc == 2 && std::wstring(argv[1]) == L"--test-hdr-auto")
		{
			const auto list = ActiveDisplayPaths();
			if (list.size() != 1)
				throw std::runtime_error("Automatic HDR comparison requires exactly one active physical output");
			return RunTimedLutTest(list[0].source, 0, 0.5, 10, true, 2.2);
		}
		if (argc >= 3 && std::wstring(argv[1]) == L"--compare-hdr")
		{
			double white = 0, gamma = 2.2;
			for (int i = 3; i < argc; i += 2)
			{
				if (i + 1 >= argc)
					throw std::runtime_error("Missing comparison option value");
				const std::wstring key(argv[i]);
				const double value = ParseNumber(argv[i + 1]);
				if (key == L"--white")
					white = value;
				else if (key == L"--gamma")
					gamma = value;
				else
					throw std::runtime_error("Unknown comparison option");
			}
			CompareHdr(argv[2], white, gamma);
			return 0;
		}
		if (argc >= 3 && std::wstring(argv[1]) == L"--export-hdr")
		{
			double white = 200, gamma = 2.2, entries = 1024;
			for (int i = 3; i < argc; i += 2)
			{
				if (i + 1 >= argc)
					throw std::runtime_error("Missing export option value");
				const std::wstring key(argv[i]);
				const double value = ParseNumber(argv[i + 1]);
				if (key == L"--white")
					white = value;
				else if (key == L"--gamma")
					gamma = value;
				else if (key == L"--entries")
					entries = value;
				else
					throw std::runtime_error("Unknown export option");
			}
			if (entries < 2 || entries > 4096 || std::floor(entries) != entries)
				throw std::runtime_error("Invalid LUT entry count");
			ExportHdr(argv[2], white, gamma, static_cast<std::size_t>(entries));
			return 0;
		}
		if (argc >= 3 && std::wstring(argv[1]) == L"--test-hdr")
		{
			double gamma = 2.2, seconds = 10;
			for (int i = 3; i < argc; i += 2)
			{
				if (i + 1 >= argc)
					throw std::runtime_error("Missing HDR test option value");
				const std::wstring key(argv[i]);
				const double value = ParseNumber(argv[i + 1]);
				if (key == L"--gamma")
					gamma = value;
				else if (key == L"--seconds")
					seconds = value;
				else
					throw std::runtime_error("Unknown HDR test option");
			}
			if ((gamma != 2.2 && gamma != 2.4) || seconds < 3 || seconds > 15 || std::floor(seconds) != seconds)
				throw std::runtime_error("HDR tests support gamma 2.2/2.4 and duration [3,15] seconds");
			return RunTimedLutTest(argv[2], 0, 0.5, static_cast<DWORD>(seconds), true, gamma);
		}
		if (argc == 3 && std::wstring(argv[1]) == L"--restore")
		{
			const int code = RestoreSnapshot(LoadSnapshot(argv[2]));
			std::wcout << L"Restore result=" << code << L" (0 means exact GDI readback)\n";
			return code;
		}
		if (argc == 6 && (std::wstring(argv[1]) == L"--guardian" || std::wstring(argv[1]) == L"--guardian-probe"))
		{
			const double pid = ParseNumber(argv[3]);
			if (pid <= 0 || pid > MAXDWORD || std::floor(pid) != pid)
				throw std::runtime_error("Invalid parent PID");
			return RunRecoveryGuardian(argv[2], static_cast<DWORD>(pid), argv[4], argv[5],
			                           std::wstring(argv[1]) == L"--guardian-probe");
		}
		if (argc >= 3 && (std::wstring(argv[1]) == L"--test" || std::wstring(argv[1]) == L"--test-window"))
		{
			double amount = 0.15, pivot = 0.5, seconds = 10;
			for (int i = 3; i < argc; i += 2)
			{
				if (i + 1 >= argc)
					throw std::runtime_error("Missing option value");
				const std::wstring key(argv[i]);
				const double value = ParseNumber(argv[i + 1]);
				if (key == L"--contrast")
					amount = value;
				else if (key == L"--pivot")
					pivot = value;
				else if (key == L"--seconds")
					seconds = value;
				else
					throw std::runtime_error("Unknown test option");
			}
			if (amount < -0.25 || amount > 0.25 || pivot < 0.2 || pivot > 0.8 || seconds < 1 || seconds > 15 ||
			    std::floor(seconds) != seconds)
				throw std::runtime_error("Test parameter outside supported range");
			const int result = RunTimedLutTest(argv[2], amount, pivot, static_cast<DWORD>(seconds));
			if (std::wstring(argv[1]) == L"--test-window")
			{
				std::wcout << L"\nPress Enter to close this test window.\n";
				std::wstring line;
				std::getline(std::wcin, line);
			}
			return result;
		}
		PrintUsage();
		return 1;
	}
	catch (const std::exception& error)
	{
		std::cerr << "Error: " << error.what() << '\n';
		return 1;
	}
}
