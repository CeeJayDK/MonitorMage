#pragma once
#include <windows.h>
#include <string>
namespace windows_controls
{
struct State
{
	bool available = false, on = false;
	DWORD filter = 0;
	std::wstring reason;
};

inline constexpr const wchar_t* FILTER_NAMES[] { L"Grayscale",
	                                             L"Inverted",
	                                             L"Grayscale inverted",
	                                             L"Red-green (deuteranopia)",
	                                             L"Red-green (protanopia)",
	                                             L"Blue-yellow (tritanopia)" };
State ReadColorFilterState();
enum class Completion
{
	ConfirmedRequest,
	SettingsSaved,
	DifferentSettings,
	Unreadable
};

// Saved values and helper completion are separate observations. Neither is
// a measurement of the displayed picture.
Completion CheckCompletion(const State&, bool requested_on, DWORD requested_filter, bool helper_exited,
                           DWORD helper_code);
const wchar_t* CompletionNotice(Completion);
class Controller
{
	HANDLE _process = nullptr;
	unsigned _attempts = 0;
	bool _expected_on = false;
	DWORD _expected_filter = 0;
	ULONGLONG _deadline = 0;
	std::wstring _notice;

public:
	~Controller();
	bool IsBusy() const { return _process != nullptr; }
	void Set(const State& expected, bool on, DWORD filter, bool selecting_filter = false);
	// Nonblocking; false while Windows is processing the request.
	bool Poll();
	void StopWaiting();
	const std::wstring& Notice() const { return _notice; }
};

void OpenColorFilterSettings(HWND owner);
}
