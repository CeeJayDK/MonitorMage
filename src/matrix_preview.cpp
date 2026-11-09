#include "matrix_preview.h"
#include "matrix_math.h"
#include "matrix_api.h"
#include "ui_theme.h"
#include "windows_controls.h"
#include <magnification.h>
#include <commctrl.h>
#include <shellapi.h>
#include <filesystem>
#include <sstream>
#include <iomanip>
#include <memory>
#include <string>
#include <vector>
#include <psapi.h>
namespace matrix_preview
{
namespace
{
constexpr DWORD SHARED_MEMORY_MAGIC = 0x4d435032;
constexpr UINT TIMER_PREVIEW = 1;
constexpr int ID_MODE = 301, ID_DURATION = 302, ID_START = 303, ID_STOP = 304, ID_REPORTS = 305, ID_STATUS = 306,
			  ID_NOTE = 307, ID_SWATCHES = 308, ID_SHORTCUT = 309, ID_SET_SHORTCUT = 310, ID_SHORTCUT_LABEL = 311;
constexpr int HOTKEY_A = 0x4d31, HOTKEY_B = 0x4d32;
enum CancelReason : LONG
{
	NoReason,
	StopButton,
	ShortcutPress,
	DisplayChange,
	SettingsChange,
	WindowClose,
	SessionEnd
};

enum Phase : LONG
{
	Starting,
	Applied,
	Finished
};

enum Result : LONG
{
	None,
	Restored,
	External,
	Failed,
	Blocked,
	NotStarted,
	Stopped,
	ReportFailed
};

struct Shared
{
	DWORD magic = SHARED_MEMORY_MAGIC;
	int mode = 0;
	DWORD seconds = 15;
	volatile LONG phase = Starting, result = None, cancel_reason = NoReason;
	ULONGLONG applied_at = 0;
	DWORD error = 0;
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

std::filesystem::path ExecutablePath()
{
	wchar_t path[32768] {};
	const auto n = GetModuleFileNameW(nullptr, path, 32768);
	if (!n || n >= 32768)
		throw std::runtime_error("Cannot locate MonitorMage");
	return path;
}

std::filesystem::path ReportFolder()
{
	return ExecutablePath().parent_path() / L"reports" / L"matrix-preview";
}

std::wstring SharedObjectName(const wchar_t* token, const wchar_t* suffix)
{
	return L"Local\\MonitorMageMatrix-" + std::wstring(token) + suffix;
}

bool IsValidToken(const wchar_t* token)
{
	if (!token || !*token || wcslen(token) > 80)
		return false;
	for (const auto* p = token; *p; ++p)
		if ((*p < L'0' || *p > L'9') && *p != L'-')
			return false;
	return true;
}

std::wstring QuoteArgument(const std::wstring& text)
{
	if (text.find(L'"') != text.npos)
		throw std::runtime_error("Invalid preview path");
	return L"\"" + text + L"\"";
}

using Api = matrix_color::WindowsApi;
struct Fake final : matrix_color::Api
{
	matrix_color::Matrix value = matrix_color::IdentityMatrix();
	bool Read(matrix_color::Matrix& matrix) override
	{
		matrix = value;
		return true;
	}
	bool Write(const matrix_color::Matrix& matrix) override
	{
		value = matrix;
		return true;
	}
};

void WriteLog(HANDLE file, std::ostringstream& text)
{
	const auto bytes = text.str();
	DWORD written = 0;
	if (file != INVALID_HANDLE_VALUE &&
	    (!WriteFile(file, bytes.data(), static_cast<DWORD>(bytes.size()), &written, nullptr) ||
	     written != bytes.size() || !FlushFileBuffers(file)))
		throw std::runtime_error("Cannot write preview report");
	text.str("");
	text.clear();
}

void LogMatrix(std::ostringstream& log, const char* name, const matrix_color::Matrix& matrix)
{
	log << name << "=\n";
	for (const auto& row : matrix)
	{
		for (float value : row)
			log << value << ' ';
		log << '\n';
	}
}

void LogDisplayInfo(std::ostringstream& log)
{
	UINT32 path_count = 0, mode_count = 0;
	if (GetDisplayConfigBufferSizes(QDC_ONLY_ACTIVE_PATHS, &path_count, &mode_count))
		return;
	std::vector<DISPLAYCONFIG_PATH_INFO> paths(path_count);
	std::vector<DISPLAYCONFIG_MODE_INFO> modes(mode_count);
	if (QueryDisplayConfig(QDC_ONLY_ACTIVE_PATHS, &path_count, paths.data(), &mode_count, modes.data(), nullptr))
		return;
	log << "desktop_resolution=" << GetSystemMetrics(SM_CXSCREEN) << 'x' << GetSystemMetrics(SM_CYSCREEN) << '\n';
	DWORD foreground = 0;
	GetWindowThreadProcessId(GetForegroundWindow(), &foreground);
	log << "foreground_pid=" << foreground << '\n';
	log << "display_paths=" << path_count << '\n';
	for (UINT32 i = 0; i < path_count; ++i)
	{
		const auto& path = paths[i];
		DISPLAYCONFIG_GET_ADVANCED_COLOR_INFO color {};
		color.header = { DISPLAYCONFIG_DEVICE_INFO_GET_ADVANCED_COLOR_INFO, sizeof(color), path.targetInfo.adapterId,
			             path.targetInfo.id };
		const auto error = DisplayConfigGetDeviceInfo(&color.header);
		log << "display[" << i << "] source=" << path.sourceInfo.id << " target=" << path.targetInfo.id
			<< " advanced_color_query=" << error;
		if (path.sourceInfo.modeInfoIdx < mode_count &&
		    modes[path.sourceInfo.modeInfoIdx].infoType == DISPLAYCONFIG_MODE_INFO_TYPE_SOURCE)
			log << " resolution=" << modes[path.sourceInfo.modeInfoIdx].sourceMode.width << 'x'
				<< modes[path.sourceInfo.modeInfoIdx].sourceMode.height;
		if (!error)
			log << " supported=" << color.advancedColorSupported << " enabled=" << color.advancedColorEnabled
				<< " bits_per_channel=" << color.bitsPerColorChannel;
		log << '\n';
	}
}

ULONGLONG ProcessCpuTime()
{
	FILETIME creation_time {}, exit_time {}, kernel_time {}, user_time {};
	if (!GetProcessTimes(GetCurrentProcess(), &creation_time, &exit_time, &kernel_time, &user_time))
		return 0;
	ULARGE_INTEGER kernel_ticks {}, user_ticks {};
	kernel_ticks.LowPart = kernel_time.dwLowDateTime;
	kernel_ticks.HighPart = kernel_time.dwHighDateTime;
	user_ticks.LowPart = user_time.dwLowDateTime;
	user_ticks.HighPart = user_time.dwHighDateTime;
	return kernel_ticks.QuadPart + user_ticks.QuadPart;
}

struct Session
{
	Handle mapping, cancel, process;
	std::unique_ptr<Mapping> view;
	bool IsBusy() const { return process.handle && WaitForSingleObject(process.handle, 0) == WAIT_TIMEOUT; }
	void Stop(CancelReason reason = SessionEnd)
	{
		if (view && view->shared)
			InterlockedCompareExchange(&view->shared->cancel_reason, reason, NoReason);
		if (cancel.handle)
			SetEvent(cancel.handle);
	}
	~Session() { Stop(); }
};

std::unique_ptr<Session> LaunchWorker(int mode, DWORD seconds, DWORD parent, bool fake)
{
	auto result = std::make_unique<Session>();
	const auto token = std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64());
	result->mapping.handle = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, sizeof(Shared),
	                                            SharedObjectName(token.c_str(), L"-map").c_str());
	if (!result->mapping.handle || GetLastError() == ERROR_ALREADY_EXISTS)
		throw std::runtime_error("Preview session unavailable");
	result->view = std::make_unique<Mapping>(result->mapping.handle);
	result->cancel.handle = CreateEventW(nullptr, TRUE, FALSE, SharedObjectName(token.c_str(), L"-cancel").c_str());
	if (!result->view->shared || !result->cancel.handle)
		throw std::runtime_error("Cannot prepare preview");
	*result->view->shared = Shared {};
	result->view->shared->mode = mode;
	result->view->shared->seconds = seconds;
	auto command = QuoteArgument(ExecutablePath().wstring()) +
	               (fake ? L" --matrix-worker-probe " : L" --matrix-worker ") + std::to_wstring(parent) + L" " + token;
	STARTUPINFOW start { sizeof(start) };
	start.dwFlags = STARTF_USESHOWWINDOW | STARTF_FORCEOFFFEEDBACK;
	start.wShowWindow = SW_HIDE;
	PROCESS_INFORMATION process {};
	if (!CreateProcessW(ExecutablePath().c_str(), command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr,
	                    nullptr, &start, &process))
		throw std::runtime_error("Cannot start preview worker");
	result->process.handle = process.hProcess;
	CloseHandle(process.hThread);
	return result;
}

HWND preview_window = nullptr;
bool standalone = false;
struct Window
{
	HWND mode = nullptr, duration = nullptr, start = nullptr, stop = nullptr, status = nullptr;
	HFONT font = nullptr;
	std::unique_ptr<Session> session;
	bool closing = false, testing = false, probe = false, hotkey_registered = false;
	WORD shortcut = MAKEWORD('M', HOTKEYF_CONTROL | HOTKEYF_ALT | HOTKEYF_SHIFT);
	int hotkey_id = HOTKEY_A;
};

Window* GetWindowState(HWND h)
{
	return reinterpret_cast<Window*>(GetWindowLongPtrW(h, GWLP_USERDATA));
}

void SetStatusText(HWND window, const wchar_t* text)
{
	SetWindowTextW(GetDlgItem(window, ID_STATUS), text);
}

bool BindPreviewHotkey(HWND window, Window& state, WORD value)
{
	const BYTE key = LOBYTE(value), flags = HIBYTE(value);
	const UINT mods = ((flags & HOTKEYF_CONTROL) ? MOD_CONTROL : 0) | ((flags & HOTKEYF_ALT) ? MOD_ALT : 0) |
	                  ((flags & HOTKEYF_SHIFT) ? MOD_SHIFT : 0) | MOD_NOREPEAT;
	if (!key || key == VK_F12 || key == VK_CONTROL || key == VK_MENU || key == VK_SHIFT ||
	    !(flags & (HOTKEYF_CONTROL | HOTKEYF_ALT | HOTKEYF_SHIFT)) ||
	    (flags & ~(HOTKEYF_CONTROL | HOTKEYF_ALT | HOTKEYF_SHIFT | HOTKEYF_EXT)))
		return false;
	if (state.hotkey_registered && state.shortcut == value)
		return true;
	const int next = state.hotkey_registered ? (state.hotkey_id == HOTKEY_A ? HOTKEY_B : HOTKEY_A) : HOTKEY_A;
	if (!RegisterHotKey(window, next, mods, key))
		return false;
	if (state.hotkey_registered)
		UnregisterHotKey(window, state.hotkey_id);
	state.shortcut = value;
	state.hotkey_id = next;
	state.hotkey_registered = true;
	return true;
}

void UpdateButtons(Window& state, bool active)
{
	EnableWindow(state.start, !active && !state.testing);
	EnableWindow(state.mode, !active);
	EnableWindow(state.duration, !active);
	EnableWindow(state.stop, active);
	EnableWindow(GetDlgItem(GetParent(state.start), ID_SHORTCUT), !active);
	EnableWindow(GetDlgItem(GetParent(state.start), ID_SET_SHORTCUT), !active);
	if (active && GetForegroundWindow() == GetAncestor(state.start, GA_ROOT))
		SetFocus(state.stop);
}

void FinishPreview(HWND window, Window& state)
{
	DWORD code = 1;
	GetExitCodeProcess(state.session->process.handle, &code);
	const auto result = state.session->view->shared->result;
	const auto error = state.session->view->shared->error;
	state.session.reset();
	KillTimer(window, TIMER_PREVIEW);
	UpdateButtons(state, false);
	if (result == Restored && code == 0)
		SetStatusText(window, L"Preview ended. Previous colors restored.");
	else if (result == External)
		SetStatusText(window, L"Preview ended. Another app's color change was kept.");
	else if (result == Blocked)
		SetStatusText(window, error == 2 ? L"Close other color-matrix or Magnifier tools first."
		                                 : L"The preview could not start. See its report.");
	else if (result == NotStarted)
		SetStatusText(window, L"Preview could not start. No colors were changed. See its report.");
	else if (result == Stopped)
		SetStatusText(window, L"Preview ended early. Previous colors restored. See its report.");
	else if (result == ReportFailed)
		SetStatusText(window, L"Previous colors restored, but the report could not be saved.");
	else if (result == None)
		SetStatusText(window, L"The preview worker stopped unexpectedly. Check the picture.");
	else
		SetStatusText(window, L"The preview could not confirm restoration. See its report.");
	if (state.closing)
		DestroyWindow(window);
}

void LayoutPreviewWindow(HWND window, Window& state)
{
	const UINT dpi = GetDpiForWindow(window);
	const auto px = [&](int n)
	{
		return MulDiv(n, dpi, 96);
	};
	if (state.font)
		DeleteObject(state.font);
	state.font = ui::CreateUiFont(dpi);
	for (int id : { ID_NOTE, ID_MODE, ID_DURATION, ID_START, ID_STOP, ID_REPORTS, ID_STATUS, ID_SHORTCUT,
	                ID_SET_SHORTCUT, ID_SHORTCUT_LABEL })
		SendDlgItemMessageW(window, id, WM_SETFONT, reinterpret_cast<WPARAM>(state.font), TRUE);
	const auto place = [&](int id, int x, int y, int w, int h)
	{
		MoveWindow(GetDlgItem(window, id), px(x), px(y), px(w), px(h), TRUE);
	};
	place(ID_NOTE, 18, 14, 484, 48);
	place(ID_MODE, 18, 72, 282, 160);
	place(ID_DURATION, 314, 72, 188, 120);
	place(ID_SWATCHES, 18, 164, 484, 30);
	place(ID_START, 18, 112, 232, 32);
	place(ID_STOP, 266, 112, 236, 32);
	place(ID_STATUS, 18, 208, 484, 48);
	place(ID_SHORTCUT_LABEL, 18, 268, 66, 28);
	place(ID_SHORTCUT, 90, 268, 250, 28);
	place(ID_SET_SHORTCUT, 354, 268, 148, 28);
	place(ID_REPORTS, 314, 310, 188, 30);
	ui::ApplyWindowFrameTheme(window);
}

LRESULT CALLBACK PreviewWindowProc(HWND window, UINT message, WPARAM w_param, LPARAM l_param)
{
	auto state = GetWindowState(window);
	LRESULT themed = 0;
	if (ui::HandleThemeMessage(window, message, w_param, l_param, themed))
		return themed;
	switch (message)
	{
		case WM_CREATE:
		{
			auto created = std::make_unique<Window>();
			state = created.get();
			state->testing = reinterpret_cast<LPCREATESTRUCTW>(l_param)->lpCreateParams != nullptr;
			ui::FixedWindowSize(window, 520, 358);
			SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));
			created.release();
			preview_window = window;
			const auto child = [&](const wchar_t* cls, const wchar_t* text, DWORD flags, int id)
			{
				if (wcscmp(cls, L"COMBOBOX") == 0)
					flags |= CBS_OWNERDRAWFIXED | CBS_HASSTRINGS;
				auto h = CreateWindowExW(0, cls, text, WS_CHILD | WS_VISIBLE | flags, 0, 0, 1, 1, window,
				                         reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), GetModuleHandleW(nullptr),
				                         nullptr);
				ui::ApplyControlTheme(h);
				return h;
			};
			child(
				L"STATIC",
				L"Temporary test on all displays. It stops automatically.\nEscape or Stop ends it early. Shortcut starts/stops it inside a game.",
				SS_NOPREFIX, ID_NOTE);
			state->mode = child(L"COMBOBOX", L"Preview effect", WS_TABSTOP | CBS_DROPDOWNLIST, ID_MODE);
			for (const auto* text : { L"Grayscale", L"Half saturation", L"Saturation +20%" })
				SendMessageW(state->mode, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(text));
			SendMessageW(state->mode, CB_SETCURSEL, 0, 0);
			state->duration = child(L"COMBOBOX", L"Preview duration", WS_TABSTOP | CBS_DROPDOWNLIST, ID_DURATION);
			for (const auto* text : { L"15 seconds", L"30 seconds" })
				SendMessageW(state->duration, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(text));
			SendMessageW(state->duration, CB_SETCURSEL, 0, 0);
			state->start = child(L"BUTTON", L"Preview", WS_TABSTOP | BS_PUSHBUTTON, ID_START);
			state->stop = child(L"BUTTON", L"Stop", WS_TABSTOP | BS_DEFPUSHBUTTON, ID_STOP);
			EnableWindow(state->stop, FALSE);
			child(L"STATIC", L"Color swatches", SS_OWNERDRAW, ID_SWATCHES);
			state->status = child(L"STATIC", L"Ready. No preview is running.", SS_NOPREFIX, ID_STATUS);
			child(L"BUTTON", L"Open reports", WS_TABSTOP | BS_PUSHBUTTON, ID_REPORTS);
			child(L"STATIC", L"Shortcut", SS_CENTERIMAGE, ID_SHORTCUT_LABEL);
			auto shortcut = child(HOTKEY_CLASSW, L"Preview shortcut", WS_TABSTOP, ID_SHORTCUT);
			SendMessageW(shortcut, HKM_SETHOTKEY, state->shortcut, 0);
			child(L"BUTTON", L"Set shortcut", WS_TABSTOP | BS_PUSHBUTTON, ID_SET_SHORTCUT);
			ui::SetHelpText(shortcut, L"Preview shortcut");
			ui::SetHelpText(GetDlgItem(window, ID_SET_SHORTCUT), L"Set preview shortcut");
			if (!state->testing && !BindPreviewHotkey(window, *state, state->shortcut))
				SetStatusText(window, L"Shortcut unavailable. Choose another and click Set shortcut.");
			ui::SetHelpText(state->mode, L"Preview effect");
			ui::SetHelpText(state->duration, L"Preview duration");
			ui::SetHelpText(state->start, L"Start timed preview");
			ui::SetHelpText(state->stop, L"Stop and restore colors");
			ui::SetHelpText(GetDlgItem(window, ID_REPORTS), L"Open preview reports");
			LayoutPreviewWindow(window, *state);
			return 0;
		}
		case WM_DRAWITEM:
		{
			const auto item = reinterpret_cast<DRAWITEMSTRUCT*>(l_param);
			if (item->CtlID != ID_SWATCHES)
				break;
			FillRect(item->hDC, &item->rcItem, ui::BackgroundBrush());
			constexpr COLORREF colors[] { RGB(0, 0, 0),     RGB(128, 128, 128), RGB(255, 255, 255), RGB(210, 45, 45),
				                          RGB(45, 180, 65), RGB(45, 80, 210),   RGB(185, 145, 115), RGB(220, 180, 45) };
			const int width = item->rcItem.right - item->rcItem.left, gap = MulDiv(4, GetDpiForWindow(window), 96);
			for (int i = 0; i < 8; ++i)
			{
				RECT r { item->rcItem.left + width * i / 8, item->rcItem.top,
					     item->rcItem.left + width * (i + 1) / 8 - gap, item->rcItem.bottom };
				auto brush = CreateSolidBrush(colors[i]);
				FillRect(item->hDC, &r, brush);
				DeleteObject(brush);
			}
			return TRUE;
		}
		case WM_HOTKEY:
			if (state->testing || !state->hotkey_registered || static_cast<int>(w_param) != state->hotkey_id)
				return 0;
			if (state->closing)
				return 0;
			if (state->session && !state->session->IsBusy())
				FinishPreview(window, *state);
			if (state->session)
			{
				state->session->Stop(ShortcutPress);
				SetStatusText(window, L"Stopping preview...");
			}
			else
				SendMessageW(window, WM_COMMAND, ID_START, 0);
			return 0;
		case WM_COMMAND:
			if (LOWORD(w_param) == ID_SET_SHORTCUT && !state->testing && !state->session)
			{
				const WORD value = static_cast<WORD>(SendDlgItemMessageW(window, ID_SHORTCUT, HKM_GETHOTKEY, 0, 0));
				if (BindPreviewHotkey(window, *state, value))
					SetStatusText(window, L"Shortcut ready. It works while the game has focus.");
				else
				{
					SendDlgItemMessageW(window, ID_SHORTCUT, HKM_SETHOTKEY, state->shortcut, 0);
					SetStatusText(window, L"Shortcut unavailable. Choose another combination.");
				}
				return 0;
			}
			if (LOWORD(w_param) == ID_MODE && HIWORD(w_param) == CBN_SELCHANGE)
			{
				SetStatusText(window, SendMessageW(state->mode, CB_GETCURSEL, 0, 0) == 2
				                          ? L"Saturation +20% may clip strong colors."
				                          : L"Ready. No preview is running.");
				return 0;
			}
			if (LOWORD(w_param) == ID_START && !state->session && !state->testing)
			{
				try
				{
					const int mode = static_cast<int>(SendMessageW(state->mode, CB_GETCURSEL, 0, 0));
					const DWORD seconds = SendMessageW(state->duration, CB_GETCURSEL, 0, 0) == 1 ? 30 : 15;
					state->session = LaunchWorker(mode, seconds, GetCurrentProcessId(), state->probe);
					if (!SetTimer(window, TIMER_PREVIEW, 100, nullptr))
					{
						state->session->Stop();
						throw std::runtime_error("Cannot monitor preview");
					}
					UpdateButtons(*state, true);
					SetStatusText(window, L"Starting preview...");
				}
				catch (...)
				{
					if (state->session)
					{
						state->session->Stop();
						state->session.reset();
					}
					SetStatusText(window, L"Preview could not start. Check the reports folder.");
				}
				return 0;
			}
			if (LOWORD(w_param) == ID_STOP || LOWORD(w_param) == IDCANCEL || LOWORD(w_param) == IDOK)
			{
				if (state->session)
				{
					state->session->Stop(StopButton);
					SetStatusText(window, L"Stopping preview...");
				}
				return 0;
			}
			if (LOWORD(w_param) == ID_REPORTS)
			{
				try
				{
					std::filesystem::create_directories(ReportFolder());
					ShellExecuteW(window, L"open", ReportFolder().c_str(), nullptr, nullptr, SW_SHOWNORMAL);
				}
				catch (...)
				{
					SetStatusText(window, L"The reports folder could not be opened.");
				}
				return 0;
			}
			break;
		case WM_TIMER:
			if (w_param == TIMER_PREVIEW && state->session)
			{
				if (!state->session->IsBusy())
				{
					FinishPreview(window, *state);
					return 0;
				}
				const auto shared = state->session->view->shared;
				if (InterlockedCompareExchange(&shared->phase, 0, 0) == Applied)
				{
					const auto elapsed = (GetTickCount64() - shared->applied_at) / 1000;
					const auto left = elapsed < shared->seconds ? shared->seconds - elapsed : 0;
					const auto text = L"Previewing. Restoring colors in " + std::to_wstring(left) + L" seconds.";
					wchar_t current[160] {};
					GetWindowTextW(state->status, current, 160);
					if (text != current)
						SetStatusText(window, text.c_str());
				}
				return 0;
			}
			break;
		case WM_DISPLAYCHANGE:
		case WM_SETTINGCHANGE:
			if (state && state->session)
			{
				state->session->Stop(message == WM_DISPLAYCHANGE ? DisplayChange : SettingsChange);
				SetStatusText(window, L"Display settings changed. Stopping preview...");
			}
			return 0;
		case WM_CLOSE:
			if (state->session && state->session->IsBusy())
			{
				state->closing = true;
				state->session->Stop(WindowClose);
				SetStatusText(window, L"Stopping preview...");
				return 0;
			}
			DestroyWindow(window);
			return 0;
		case WM_DPICHANGED:
		{
			const auto r = reinterpret_cast<RECT*>(l_param);
			SetWindowPos(window, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top,
			             SWP_NOZORDER | SWP_NOACTIVATE);
			LayoutPreviewWindow(window, *state);
			return 0;
		}
		case WM_DESTROY:
			KillTimer(window, TIMER_PREVIEW);
			if (state->hotkey_registered)
				UnregisterHotKey(window, state->hotkey_id);
			if (state->session)
				state->session->Stop();
			if (state->font)
				DeleteObject(state->font);
			delete state;
			SetWindowLongPtrW(window, GWLP_USERDATA, 0);
			preview_window = nullptr;
			if (standalone)
				PostQuitMessage(0);
			return 0;
	}
	return DefWindowProcW(window, message, w_param, l_param);
}
}

int RunWorkerProcess(DWORD parent_id, const wchar_t* token, bool fake)
{
	if (!parent_id || !IsValidToken(token))
		return 2;
	Handle parent(OpenProcess(SYNCHRONIZE, FALSE, parent_id));
	Handle cancel(OpenEventW(SYNCHRONIZE, FALSE, SharedObjectName(token, L"-cancel").c_str()));
	Handle mapping(OpenFileMappingW(FILE_MAP_ALL_ACCESS, FALSE, SharedObjectName(token, L"-map").c_str()));
	Mapping view(mapping.handle);
	if (!parent.handle || !cancel.handle || !view.shared || view.shared->magic != SHARED_MEMORY_MAGIC ||
	    view.shared->seconds < 1 || view.shared->seconds > 30 || view.shared->mode < 0 || view.shared->mode > 2)
		return 3;
	auto& shared = *view.shared;
	Api api;
	Fake mock;
	matrix_color::Api& backend = fake ? static_cast<matrix_color::Api&>(mock) : api;
	const auto target = matrix_color::PreviewMatrix(shared.mode);
	matrix_color::Matrix baseline {};
	bool attempted = false;
	Handle file(INVALID_HANDLE_VALUE);
	std::ostringstream log;
	const auto end = [&](Result result, DWORD error)
	{
		if (result == Failed && !attempted && !shared.applied_at)
			result = NotStarted;
		log << "final_result=" << result << " error=" << error
			<< " (1=restored,2=external,3=failed,4=blocked,5=not_started,6=stopped,7=report_failed)\n";
		try
		{
			if (!fake)
				WriteLog(file.handle, log);
		}
		catch (...)
		{
			if (result == Restored || result == Stopped)
				result = ReportFailed;
		}
		shared.error = error;
		InterlockedExchange(&shared.result, result);
		InterlockedExchange(&shared.phase, Finished);
		return result == Restored ? 0 : 1;
	};
	try
	{
		if (!fake)
		{
			std::filesystem::create_directories(ReportFolder());
			const auto path = ReportFolder() / (L"preview-" + std::to_wstring(GetCurrentProcessId()) + L"-" +
			                                    std::to_wstring(GetTickCount64()) + L".txt");
			file.handle = CreateFileW(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_NEW,
			                          FILE_ATTRIBUTE_NORMAL, nullptr);
			if (file.handle == INVALID_HANDLE_VALUE)
				return end(Failed, GetLastError());
			log << "MonitorMage timed matrix preview\nmode=" << shared.mode << " seconds=" << shared.seconds
				<< "\nEncoded-RGB experiment; not an HDR colorimetric transform.\n";
			LogDisplayInfo(log);
			const auto filters = windows_controls::ReadColorFilterState();
			log << "windows_filters available=" << filters.available << " on=" << filters.on
				<< " type=" << filters.filter << '\n';
			WriteLog(file.handle, log);
			const bool initialized = api.Open();
			log << "initialize_ok=" << initialized << '\n';
			if (!initialized)
				return end(Failed, GetLastError());
			float scale = 0;
			int x = 0, y = 0;
			if (!api.ReadZoom(&scale, &x, &y) || scale != 1 || x || y)
				return end(Blocked, 2);
		}
		if (!backend.Read(baseline))
			return end(Failed, GetLastError());
		if (matrix_color::MaxDifference(baseline, matrix_color::IdentityMatrix()) > 1e-5f)
			return end(Blocked, 2);
		if (WaitForSingleObject(parent.handle, 0) != WAIT_TIMEOUT ||
		    WaitForSingleObject(cancel.handle, 0) == WAIT_OBJECT_0)
			return end(Restored, 0);
		LogMatrix(log, "baseline", baseline);
		LogMatrix(log, "requested", target);
		if (!fake)
			WriteLog(file.handle, log);
		attempted = true;
		const auto cpu_start = ProcessCpuTime(), start = GetTickCount64();
		const bool accepted = backend.Write(target);
		matrix_color::Matrix readback {};
		const bool verified = backend.Read(readback) && matrix_color::MaxDifference(readback, target) < 1e-5f;
		log << "set_return=" << accepted << " readback_matches=" << verified << '\n';
		if (!verified)
			throw std::runtime_error("Preview readback mismatch");
		shared.applied_at = GetTickCount64();
		InterlockedExchange(&shared.phase, Applied);
		if (!fake)
			WriteLog(file.handle, log);
		HANDLE wait[] { parent.handle, cancel.handle };
		const auto deadline = GetTickCount64() + shared.seconds * 1000;
		DWORD wake = WAIT_TIMEOUT;
		for (;;)
		{
			const auto now = GetTickCount64();
			if (now >= deadline)
				break;
			wake = MsgWaitForMultipleObjects(2, wait, FALSE, static_cast<DWORD>(deadline - now), QS_ALLINPUT);
			if (wake != WAIT_OBJECT_0 + 2)
				break;
			MSG message {};
			while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE))
			{
				TranslateMessage(&message);
				DispatchMessageW(&message);
			}
		}
		log << "cancel_reason=" << InterlockedCompareExchange(&shared.cancel_reason, 0, 0)
			<< " (0=none,1=button,2=shortcut,3=display_change,4=settings_change,5=window_close,6=session_end)\n";
		log << "wait_result=" << wake << " elapsed_ms=" << GetTickCount64() - start
			<< " worker_cpu_ms=" << (ProcessCpuTime() - cpu_start) / 10000.0 << '\n';
		const auto restored = matrix_color::RestoreBaseline(backend, baseline, target);
		attempted = false;
		const bool cleaned = fake || api.Close();
		log << "restore_result=" << static_cast<int>(restored) << " uninitialize_ok=" << cleaned
			<< "\nRestoration is API readback, not a visual measurement.\nWorker CPU excludes DWM/GPU rendering cost.\n";
		if (!fake)
			WriteLog(file.handle, log);
		return end(!cleaned                                                ? Failed
		           : restored == matrix_color::Restoration::Restored       ? Restored
		           : restored == matrix_color::Restoration::ExternalChange ? External
		                                                                   : Failed,
		           0);
	}
	catch (const std::exception& error)
	{
		auto restored = matrix_color::Restoration::Restored;
		if (attempted)
			restored = matrix_color::RestoreBaseline(backend, baseline, target);
		const bool cleaned = fake || api.Close();
		log << "error=" << error.what() << " restoration=" << static_cast<int>(restored) << " cleanup=" << cleaned
			<< '\n';
		try
		{
			if (!fake)
				WriteLog(file.handle, log);
		}
		catch (...)
		{
		}
		return end(!attempted                                              ? NotStarted
		           : !cleaned                                              ? Failed
		           : restored == matrix_color::Restoration::Restored       ? Stopped
		           : restored == matrix_color::Restoration::ExternalChange ? External
		                                                                   : Failed,
		           GetLastError());
	}
}

void ShowPreviewWindow(HWND owner, bool hidden)
{
	if (preview_window)
	{
		if (!hidden)
		{
			ShowWindow(preview_window, SW_SHOW);
			SetForegroundWindow(preview_window);
		}
		return;
	}
	WNDCLASSW window_class {};
	window_class.hInstance = GetModuleHandleW(nullptr);
	window_class.lpszClassName = L"MonitorMageMatrixPreview";
	window_class.lpfnWndProc = PreviewWindowProc;
	window_class.hCursor = LoadCursorW(nullptr, IDC_ARROW);
	window_class.hbrBackground = ui::BackgroundBrush();
	RegisterClassW(&window_class);
	const UINT dpi = owner ? GetDpiForWindow(owner) : GetDpiForSystem();
	RECT rect { 0, 0, MulDiv(520, dpi, 96), MulDiv(358, dpi, 96) };
	ui::AdjustThemedWindowRect(rect, dpi);
	const auto h =
		CreateWindowExW(WS_EX_CONTROLPARENT, window_class.lpszClassName, L"MonitorMage - Matrix preview",
	                    WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_CLIPCHILDREN, CW_USEDEFAULT, CW_USEDEFAULT,
	                    rect.right - rect.left, rect.bottom - rect.top, owner, nullptr, window_class.hInstance,
	                    reinterpret_cast<void*>(static_cast<INT_PTR>(hidden ? 1 : 0)));
	if (h && hidden)
		GetWindowState(h)->testing = true;
	if (h && !hidden)
	{
		ShowWindow(h, SW_SHOW);
		SetForegroundWindow(h);
	}
}

HWND PreviewWindowHandle()
{
	return preview_window;
}

bool HandleDialogMessage(MSG& message)
{
	return preview_window && GetAncestor(message.hwnd, GA_ROOT) == preview_window &&
	       IsDialogMessageW(preview_window, &message) != FALSE;
}

int RunStandalone(HINSTANCE)
{
	standalone = true;
	INITCOMMONCONTROLSEX controls { sizeof(controls), ICC_STANDARD_CLASSES };
	InitCommonControlsEx(&controls);
	ShowPreviewWindow(nullptr);
	if (!preview_window)
		return 1;
	MSG message {};
	while (GetMessageW(&message, nullptr, 0, 0) > 0)
	{
		if (!HandleDialogMessage(message))
		{
			TranslateMessage(&message);
			DispatchMessageW(&message);
		}
	}
	return 0;
}

int SelfTest(const wchar_t*)
{
	// No Magnification API is loaded or called by the fake workers below.
	try
	{
		const auto identity = matrix_color::IdentityMatrix();
		if (matrix_color::MaxDifference(identity, matrix_color::SaturationMatrix(1)) > 1e-6f)
			return 1;
		const auto gray = matrix_color::PreviewMatrix(0);
		for (int r = 0; r < 3; ++r)
			if (gray[r][0] != gray[r][1] || gray[r][1] != gray[r][2])
				return 2;
		for (int mode = 0; mode < 3; ++mode)
		{
			const auto matrix = matrix_color::PreviewMatrix(mode);
			for (int output = 0; output < 3; ++output)
			{
				float neutral = 0;
				for (int input = 0; input < 3; ++input)
					neutral += matrix[input][output];
				if (std::abs(neutral - 1) > 1e-6f)
					return 3;
			}
			if (matrix[3][3] != 1 || matrix[4][4] != 1)
				return 4;
		}
		bool rejected = false;
		try
		{
			matrix_color::PreviewMatrix(3);
		}
		catch (...)
		{
			rejected = true;
		}
		if (!rejected)
			return 5;
		Fake api;
		const auto ours = matrix_color::PreviewMatrix(2);
		api.value = ours;
		if (matrix_color::RestoreBaseline(api, identity, ours) != matrix_color::Restoration::Restored ||
		    matrix_color::MaxDifference(api.value, identity) > 1e-6f)
			return 6;
		const auto other = matrix_color::PreviewMatrix(1);
		api.value = other;
		if (matrix_color::RestoreBaseline(api, identity, ours) != matrix_color::Restoration::ExternalChange ||
		    matrix_color::MaxDifference(api.value, other) > 1e-6f)
			return 7;
		struct Fault : matrix_color::Api
		{
			matrix_color::Matrix value = matrix_color::PreviewMatrix(2);
			int reads = 0, writes = 0;
			bool first_read_fails = false, drop_write = false, false_return = false;
			bool Read(matrix_color::Matrix& matrix) override
			{
				if (first_read_fails && reads++ == 0)
					return false;
				matrix = value;
				return true;
			}
			bool Write(const matrix_color::Matrix& matrix) override
			{
				++writes;
				if (!drop_write)
					value = matrix;
				return !false_return && !drop_write;
			}
		};
		Fault unreadable;
		unreadable.first_read_fails = true;
		if (matrix_color::RestoreBaseline(unreadable, identity, ours) != matrix_color::Restoration::Restored ||
		    unreadable.writes != 1)
			return 16;
		Fault refused;
		refused.drop_write = true;
		if (matrix_color::RestoreBaseline(refused, identity, ours) != matrix_color::Restoration::Failed)
			return 17;
		Fault false_flag;
		false_flag.false_return = true;
		if (matrix_color::RestoreBaseline(false_flag, identity, ours) != matrix_color::Restoration::Restored)
			return 18;
		const auto applied = [](Session& session)
		{
			const auto deadline = GetTickCount64() + 5000;
			while (GetTickCount64() < deadline)
			{
				if (InterlockedCompareExchange(&session.view->shared->phase, 0, 0) == Applied)
					return true;
				if (!session.IsBusy())
					return false;
				Sleep(5);
			}
			return false;
		};
		const auto ended = [](Session& session)
		{
			if (WaitForSingleObject(session.process.handle, 5000) != WAIT_OBJECT_0)
				return false;
			DWORD code = 99;
			GetExitCodeProcess(session.process.handle, &code);
			return code == 0 && session.view->shared->phase == Finished && session.view->shared->result == Restored;
		};
		auto cancel = LaunchWorker(0, 30, GetCurrentProcessId(), true);
		if (!applied(*cancel))
			return 8;
		cancel->Stop();
		if (!ended(*cancel))
			return 9;
		cancel.reset();
		auto timed = LaunchWorker(1, 1, GetCurrentProcessId(), true);
		if (!ended(*timed))
			return 10;
		timed.reset();
		auto command = QuoteArgument(ExecutablePath().wstring()) + L" --matrix-preview-owner";
		STARTUPINFOW startup { sizeof(startup) };
		startup.dwFlags = STARTF_USESHOWWINDOW | STARTF_FORCEOFFFEEDBACK;
		startup.wShowWindow = SW_HIDE;
		PROCESS_INFORMATION process {};
		if (!CreateProcessW(ExecutablePath().c_str(), command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW,
		                    nullptr, nullptr, &startup, &process))
			return 11;
		struct DisposableOwner
		{
			HANDLE handle;
			~DisposableOwner()
			{
				if (WaitForSingleObject(handle, 0) == WAIT_TIMEOUT)
					TerminateProcess(handle, 0);
				CloseHandle(handle);
			}
		};
		DisposableOwner owner { process.hProcess };
		CloseHandle(process.hThread);
		auto orphan = LaunchWorker(2, 30, process.dwProcessId, true);
		const bool started = applied(*orphan);
		// Only the disposable child created immediately above is terminated.
		TerminateProcess(owner.handle, 0);
		if (!started || !ended(*orphan))
			return 12;
		orphan.reset();
		INITCOMMONCONTROLSEX controls { sizeof(controls), ICC_STANDARD_CLASSES };
		InitCommonControlsEx(&controls);
		ShowPreviewWindow(nullptr, true);
		if (!preview_window)
			return 13;
		auto state = GetWindowState(preview_window);
		state->testing = true;
		UpdateButtons(*state, false);
		SendMessageW(preview_window, WM_COMMAND, ID_START, 0);
		if (state->session || !*ui::HelpTextForRegion(state->start) || state->hotkey_registered)
			return 14;
		state->testing = false;
		state->probe = true;
		bool bound = false;
		WORD chosen = 0;
		for (int key = VK_F20; key <= VK_F24; ++key)
		{
			chosen = MAKEWORD(key, HOTKEYF_CONTROL | HOTKEYF_ALT | HOTKEYF_SHIFT);
			if (BindPreviewHotkey(preview_window, *state, chosen))
			{
				bound = true;
				break;
			}
		}
		if (!bound)
			return 19;
		const HWND collision = CreateWindowExW(0, L"STATIC", L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr,
		                                       GetModuleHandleW(nullptr), nullptr);
		if (!collision)
			return 20;
		bool collided = false;
		for (int key = VK_F13; key <= VK_F19; ++key)
		{
			const auto blocked_shortcut = MAKEWORD(key, HOTKEYF_CONTROL | HOTKEYF_SHIFT);
			if (RegisterHotKey(collision, 1, MOD_CONTROL | MOD_SHIFT | MOD_NOREPEAT, key))
			{
				collided = !BindPreviewHotkey(preview_window, *state, blocked_shortcut) && state->shortcut == chosen;
				UnregisterHotKey(collision, 1);
				break;
			}
		}
		DestroyWindow(collision);
		if (!collided || BindPreviewHotkey(preview_window, *state, MAKEWORD(VK_F12, HOTKEYF_CONTROL)))
			return 21;
		SendMessageW(preview_window, WM_HOTKEY, state->hotkey_id, 0);
		if (!state->session || !applied(*state->session))
			return 22;
		SendMessageW(preview_window, WM_HOTKEY, state->hotkey_id, 0);
		if (!ended(*state->session) || state->session->view->shared->cancel_reason != ShortcutPress)
			return 23;
		SendMessageW(preview_window, WM_TIMER, TIMER_PREVIEW, 0);
		if (state->session)
			return 24;
		RECT normal {};
		GetWindowRect(preview_window, &normal);
		SetWindowPos(preview_window, nullptr, 0, 0, 100, 100, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
		if (!ui::WindowNeedsSizeRepair(preview_window))
			return 25;
		SendMessageW(preview_window, WM_SYSCOMMAND, SC_RESTORE, 0);
		RECT repaired {};
		GetWindowRect(preview_window, &repaired);
		if (repaired.right - repaired.left != normal.right - normal.left ||
		    repaired.bottom - repaired.top != normal.bottom - normal.top)
			return 26;
		DestroyWindow(preview_window);
		if (!RegisterHotKey(nullptr, HOTKEY_A, MOD_CONTROL | MOD_ALT | MOD_SHIFT | MOD_NOREPEAT, LOBYTE(chosen)))
			return 27;
		UnregisterHotKey(nullptr, HOTKEY_A);
		return 0;
	}
	catch (...)
	{
		return 15;
	}
}
}
