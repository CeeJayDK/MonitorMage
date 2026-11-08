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

inline constexpr const wchar_t* FilterNames[] { L"Grayscale",
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
Completion CheckCompletion(const State&, bool requestedOn, DWORD requestedFilter, bool helperExited, DWORD helperCode);
const wchar_t* CompletionNotice(Completion);
class Controller
{
	HANDLE process_ = nullptr;
	unsigned attempts_ = 0;
	bool expectedOn_ = false;
	DWORD expectedFilter_ = 0;
	ULONGLONG deadline_ = 0;
	std::wstring notice_;

public:
	~Controller();
	bool IsBusy() const { return process_ != nullptr; }
	void Set(const State& expected, bool on, DWORD filter, bool selectingFilter = false);
	// Nonblocking; false while Windows is processing the request.
	bool Poll();
	void StopWaiting();
	const std::wstring& Notice() const { return notice_; }
};

void OpenColorFilterSettings(HWND owner);
}
