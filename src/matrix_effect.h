#pragma once
#include <windows.h>
#include <memory>
#include <filesystem>
#include <array>
namespace matrix_effect
{
constexpr UINT MESSAGE_MATRIX_CHANGED = WM_APP + 40;
// Value 3 was the retired Windows color-filter block; retain report numbering.
enum class State : LONG
{
	Off,
	Starting,
	Active,
	OtherTool = 4,
	Failed,
	ExternalChange
};

class Controller
{
	friend int SelfTest();
	friend int Benchmark(const wchar_t*);
	struct Impl;
	std::unique_ptr<Impl> _impl;

public:
	Controller();
	~Controller();
	// -1 means off. The worker sleeps between events; no idle polling.
	void Update(HWND owner, int saturation, bool refresh = false, bool fake = false,
	            const std::array<int, 3>* monochrome = nullptr);
	State CurrentState() const;
	void Stop();
	// GetMessage semantics, also waking once on an unexpected worker exit.
	// No polling timer, extra thread or thread-pool wait is required.
	int WaitForMessageOrWorkerExit(MSG&);
	std::filesystem::path WriteReport() const;
};

int RunWorkerProcess(DWORD parent, const wchar_t* token, bool fake);
int SelfTest();
int Benchmark(const wchar_t* path);
}
