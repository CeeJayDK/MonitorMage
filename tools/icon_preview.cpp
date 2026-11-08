#include <windows.h>
#include <shellapi.h>
#include <array>
#include <filesystem>
#include <string>

namespace
{
constexpr UINT TrayMessage = WM_APP + 1;
constexpr int SampleCount = 4;
constexpr std::array<int, 4> Sizes { 16, 20, 24, 32 };
constexpr std::array<const wchar_t*, SampleCount> Files { L"current.ico", L"a-graphite.ico", L"b-charcoal.ico",
	                                                      L"c-nearblack.ico" };
constexpr std::array<const wchar_t*, SampleCount> Labels { L"Current - silver", L"A - graphite", L"B - charcoal",
	                                                       L"C - near-black" };
std::array<std::array<HICON, 4>, SampleCount> samples {};
std::array<HICON, SampleCount> trayIcons {};
std::array<bool, SampleCount> installed {};
std::array<HWND, SampleCount> buttons {};
HWND allButton, closeButton;
HFONT font;
UINT dpi = 96, taskbarCreated;
int selection = -1; // All samples, or one sample in the real notification area.
std::filesystem::path folder;

int ScaleForDpi(int n)
{
	return MulDiv(n, dpi, 96);
}

void RemoveTray(HWND window)
{
	for (int i = 0; i < SampleCount; ++i)
		if (installed[i])
		{
			NOTIFYICONDATAW data {};
			data.cbSize = sizeof(data);
			data.hWnd = window;
			data.uID = i + 1;
			Shell_NotifyIconW(NIM_DELETE, &data);
			installed[i] = false;
		}
}

bool InstallTray(HWND window)
{
	bool ok = true;
	for (int i = 0; i < SampleCount; ++i)
	{
		if (selection >= 0 && selection != i)
			continue;
		NOTIFYICONDATAW data {};
		data.cbSize = sizeof(data);
		data.hWnd = window;
		data.uID = i + 1;
		data.uFlags = NIF_ICON | NIF_TIP | NIF_MESSAGE;
		data.hIcon = trayIcons[i];
		data.uCallbackMessage = TrayMessage;
		const auto tip = std::wstring(L"LUT icon preview: ") + Labels[i] + L" (artwork sample)";
		wcsncpy_s(data.szTip, tip.c_str(), _TRUNCATE);
		installed[i] = Shell_NotifyIconW(NIM_ADD, &data) != FALSE;
		ok = ok && installed[i];
	}
	return ok;
}

void FreeIcons()
{
	for (auto& row : samples)
		for (auto& icon : row)
		{
			if (icon)
				DestroyIcon(icon);
			icon = nullptr;
		}
	for (auto& icon : trayIcons)
	{
		if (icon)
			DestroyIcon(icon);
		icon = nullptr;
	}
}

bool LoadIcons()
{
	FreeIcons();
	const int traySize = GetSystemMetricsForDpi(SM_CXSMICON, dpi);
	for (int i = 0; i < SampleCount; ++i)
	{
		const auto path = folder / Files[i];
		for (size_t n = 0; n < Sizes.size(); ++n)
		{
			samples[i][n] =
				static_cast<HICON>(LoadImageW(nullptr, path.c_str(), IMAGE_ICON, Sizes[n], Sizes[n], LR_LOADFROMFILE));
			if (!samples[i][n])
				return false;
		}
		trayIcons[i] =
			static_cast<HICON>(LoadImageW(nullptr, path.c_str(), IMAGE_ICON, traySize, traySize, LR_LOADFROMFILE));
		if (!trayIcons[i])
			return false;
	}
	return true;
}

void Layout(HWND window)
{
	NONCLIENTMETRICSW metrics {};
	metrics.cbSize = sizeof(metrics);
	SystemParametersInfoForDpi(SPI_GETNONCLIENTMETRICS, sizeof(metrics), &metrics, 0, dpi);
	HFONT previous = font;
	font = CreateFontIndirectW(&metrics.lfMessageFont);
	for (int i = 0; i < SampleCount; ++i)
	{
		MoveWindow(buttons[i], ScaleForDpi(20), ScaleForDpi(154 + i * 55), ScaleForDpi(180), ScaleForDpi(32), TRUE);
		SendMessageW(buttons[i], WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
	}
	MoveWindow(allButton, ScaleForDpi(20), ScaleForDpi(388), ScaleForDpi(180), ScaleForDpi(32), TRUE);
	MoveWindow(closeButton, ScaleForDpi(438), ScaleForDpi(388), ScaleForDpi(160), ScaleForDpi(32), TRUE);
	SendMessageW(allButton, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
	SendMessageW(closeButton, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
	if (previous)
		DeleteObject(previous);
	InvalidateRect(window, nullptr, TRUE);
}

void Show(HWND window)
{
	ShowWindow(window, SW_RESTORE);
	SetForegroundWindow(window);
}

LRESULT CALLBACK IconPreviewWindowProc(HWND window, UINT message, WPARAM wp, LPARAM lp)
{
	if (message == taskbarCreated)
	{
		installed.fill(false);
		InstallTray(window);
		return 0;
	}
	switch (message)
	{
		case WM_CREATE:
		{
			dpi = GetDpiForWindow(window);
			if (!LoadIcons())
				return -1;
			const auto instance = reinterpret_cast<LPCREATESTRUCTW>(lp)->hInstance;
			for (int i = 0; i < SampleCount; ++i)
				buttons[i] = CreateWindowW(L"BUTTON", Labels[i], WS_CHILD | WS_VISIBLE | WS_TABSTOP, 0, 0, 1, 1, window,
				                           reinterpret_cast<HMENU>(static_cast<INT_PTR>(100 + i)), instance, nullptr);
			allButton = CreateWindowW(L"BUTTON", L"Show all in tray", WS_CHILD | WS_VISIBLE | WS_TABSTOP, 0, 0, 1, 1,
			                          window, reinterpret_cast<HMENU>(200), instance, nullptr);
			closeButton = CreateWindowW(L"BUTTON", L"Close preview", WS_CHILD | WS_VISIBLE | WS_TABSTOP, 0, 0, 1, 1,
			                            window, reinterpret_cast<HMENU>(201), instance, nullptr);
			Layout(window);
			InstallTray(window);
			return 0;
		}
		case WM_COMMAND:
			if (LOWORD(wp) == 201 || LOWORD(wp) == IDCANCEL)
			{
				DestroyWindow(window);
				return 0;
			}
			if ((LOWORD(wp) >= 100 && LOWORD(wp) < 104) || LOWORD(wp) == 200)
			{
				selection = LOWORD(wp) == 200 ? -1 : LOWORD(wp) - 100;
				RemoveTray(window);
				if (!InstallTray(window))
					MessageBoxW(window, L"Windows could not add every preview icon.", L"Icon preview", MB_OK);
				InvalidateRect(window, nullptr, TRUE);
			}
			return 0;
		case TrayMessage:
			if (lp == WM_LBUTTONUP || lp == WM_LBUTTONDBLCLK)
				Show(window);
			if (lp == WM_RBUTTONUP || lp == WM_CONTEXTMENU)
			{
				HMENU menu = CreatePopupMenu();
				AppendMenuW(menu, MF_STRING, 1, L"Open preview");
				AppendMenuW(menu, MF_STRING, 2, L"Close preview");
				POINT cursor {};
				GetCursorPos(&cursor);
				SetForegroundWindow(window);
				const int command =
					TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, cursor.x, cursor.y, 0, window, nullptr);
				DestroyMenu(menu);
				PostMessageW(window, WM_NULL, 0, 0);
				if (command == 1)
					Show(window);
				else if (command == 2)
					DestroyWindow(window);
			}
			return 0;
		case WM_DPICHANGED:
		{
			RemoveTray(window);
			dpi = HIWORD(wp);
			if (!LoadIcons())
			{
				DestroyWindow(window);
				return 0;
			}
			const auto rect = reinterpret_cast<RECT*>(lp);
			SetWindowPos(window, nullptr, rect->left, rect->top, rect->right - rect->left, rect->bottom - rect->top,
			             SWP_NOZORDER | SWP_NOACTIVATE);
			Layout(window);
			InstallTray(window);
			return 0;
		}
		case WM_PAINT:
		{
			PAINTSTRUCT paint {};
			HDC dc = BeginPaint(window, &paint);
			HGDIOBJ previous = SelectObject(dc, font);
			SetBkMode(dc, TRANSPARENT);
			SetTextColor(dc, GetSysColor(COLOR_WINDOWTEXT));
			RECT intro { ScaleForDpi(20), ScaleForDpi(18), ScaleForDpi(600), ScaleForDpi(112) };
			DrawTextW(
				dc,
				L"The real Windows tray now contains these artwork samples.\nOpen the ^ overflow area if they are hidden; hover to see A, B or C.\nClick a row button to keep only that icon in the tray.\nThe sun is part of the sample artwork, independent of HDR mode.",
				-1, &intro, DT_LEFT | DT_WORDBREAK);
			for (size_t n = 0; n < Sizes.size(); ++n)
			{
				const auto label = std::to_wstring(Sizes[n]) + L" px";
				TextOutW(dc, ScaleForDpi(230 + static_cast<int>(n) * 92), ScaleForDpi(122), label.c_str(),
				         static_cast<int>(label.size()));
			}
			for (int i = 0; i < SampleCount; ++i)
				for (size_t n = 0; n < Sizes.size(); ++n)
				{
					// These samples are literal physical pixels, regardless of the window's DPI.
					const int x = ScaleForDpi(230 + static_cast<int>(n) * 92),
							  y = ScaleForDpi(154 + i * 55) + (ScaleForDpi(32) - Sizes[n]) / 2;
					DrawIconEx(dc, x, y, samples[i][n], Sizes[n], Sizes[n], 0, nullptr, DI_NORMAL);
				}
			SelectObject(dc, previous);
			EndPaint(window, &paint);
			return 0;
		}
		case WM_CLOSE:
			DestroyWindow(window);
			return 0;
		case WM_DESTROY:
			RemoveTray(window);
			FreeIcons();
			if (font)
				DeleteObject(font);
			font = nullptr;
			PostQuitMessage(0);
			return 0;
	}
	return DefWindowProcW(window, message, wp, lp);
}
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR command, int showWindow)
{
	SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
	wchar_t path[32768] {};
	if (!GetModuleFileNameW(nullptr, path, static_cast<DWORD>(std::size(path))))
		return 1;
	folder = std::filesystem::path(path).parent_path();
	if (std::wstring(command) == L"--check")
	{
		const bool ok = LoadIcons();
		FreeIcons();
		return ok ? 0 : 1;
	}
	HANDLE singleton = CreateMutexW(nullptr, FALSE, L"Local\\LutIconComparisonPreview");
	if (!singleton)
		return 1;
	if (GetLastError() == ERROR_ALREADY_EXISTS)
	{
		if (HWND existing = FindWindowW(L"LutIconComparisonPreview", nullptr))
			Show(existing);
		CloseHandle(singleton);
		return 0;
	}
	taskbarCreated = RegisterWindowMessageW(L"TaskbarCreated");
	WNDCLASSW cls {};
	cls.hInstance = instance;
	cls.lpfnWndProc = IconPreviewWindowProc;
	cls.lpszClassName = L"LutIconComparisonPreview";
	cls.hCursor = LoadCursorW(nullptr, IDC_ARROW);
	cls.hbrBackground = GetSysColorBrush(COLOR_WINDOW);
	if (!RegisterClassW(&cls))
	{
		CloseHandle(singleton);
		return 1;
	}
	dpi = GetDpiForSystem();
	RECT rect { 0, 0, ScaleForDpi(620), ScaleForDpi(440) };
	constexpr DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
	AdjustWindowRectExForDpi(&rect, style, FALSE, 0, dpi);
	HWND window = CreateWindowW(cls.lpszClassName, L"LUT Workbench - real tray icon comparison", style, CW_USEDEFAULT,
	                            CW_USEDEFAULT, rect.right - rect.left, rect.bottom - rect.top, nullptr, nullptr,
	                            instance, nullptr);
	if (!window)
	{
		FreeIcons();
		CloseHandle(singleton);
		MessageBoxW(nullptr, L"Keep the four comparison ICO files beside LutIconPreview.exe.", L"Icon preview",
		            MB_OK | MB_ICONERROR);
		return 1;
	}
	ShowWindow(window, showWindow);
	UpdateWindow(window);
	MSG message {};
	int result;
	while ((result = GetMessageW(&message, nullptr, 0, 0)) > 0)
		if (!IsDialogMessageW(window, &message))
		{
			TranslateMessage(&message);
			DispatchMessageW(&message);
		}
	CloseHandle(singleton);
	return result < 0 ? 1 : 0;
}
