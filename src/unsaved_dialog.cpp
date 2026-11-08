#include "unsaved_dialog.h"
#include "ui_theme.h"
#include <commctrl.h>
#include <algorithm>
#include <vector>

namespace unsaved
{
namespace
{
struct Check
{
	int action;
	bool valid = true;
};

std::vector<WORD> DialogTemplate()
{
	std::vector<WORD> words((sizeof(DLGTEMPLATE) + 6) / 2, 0);
	auto& dialog = *reinterpret_cast<DLGTEMPLATE*>(words.data());
	dialog.style = WS_POPUP | WS_CAPTION | WS_SYSMENU | DS_MODALFRAME;
	dialog.cx = 260;
	dialog.cy = 90;
	return words;
}

LRESULT CALLBACK ThemeSubclassProc(HWND window, UINT message, WPARAM wp, LPARAM lp, UINT_PTR, DWORD_PTR)
{
	LRESULT result = 0;
	const bool handled = ui::HandleThemeMessage(window, message, wp, lp, result);
	if (message == WM_NCDESTROY)
		RemoveWindowSubclass(window, ThemeSubclassProc, 1);
	return handled ? result : DefSubclassProc(window, message, wp, lp);
}

void LayoutDialog(HWND window)
{
	auto previous = reinterpret_cast<HFONT>(RemovePropW(window, L"UnsavedFont"));
	if (previous)
		DeleteObject(previous);
	const UINT dpi = GetDpiForWindow(window);
	const auto px = [&](int n)
	{
		return MulDiv(n, dpi, 96);
	};
	HFONT font = ui::CreateUiFont(dpi);
	SetPropW(window, L"UnsavedFont", font);
	for (int id : { 100, IDNO, IDYES })
		SendDlgItemMessageW(window, id, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
	MoveWindow(GetDlgItem(window, 100), px(20), px(20), px(440), px(48), TRUE);
	MoveWindow(GetDlgItem(window, IDNO), px(172), px(88), px(112), px(32), TRUE);
	MoveWindow(GetDlgItem(window, IDYES), px(296), px(88), px(164), px(32), TRUE);
	ui::ApplyWindowFrameTheme(window);
}

INT_PTR CALLBACK DialogProc(HWND window, UINT message, WPARAM wp, LPARAM lp)
{
	switch (message)
	{
		case WM_INITDIALOG:
		{
			SetWindowSubclass(window, ThemeSubclassProc, 1, 0);
			LRESULT ignored = 0;
			ui::HandleThemeMessage(window, WM_CREATE, 0, 0, ignored);
			ui::FixedWindowSize(window, 480, 140);
			SetWindowTextW(window, L"Unsaved presets");
			const auto instance = reinterpret_cast<HINSTANCE>(GetWindowLongPtrW(window, GWLP_HINSTANCE));
			const auto child = [&](const wchar_t* cls, const wchar_t* text, DWORD style, int id)
			{
				HWND control = CreateWindowExW(0, cls, text, WS_CHILD | WS_VISIBLE | style, 0, 0, 1, 1, window,
				                               reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), instance, nullptr);
				ui::ApplyControlTheme(control);
				return control;
			};
			child(L"STATIC",
			      L"Some presets have unsaved changes.\nReturn to save them, or exit and discard those edits.",
			      SS_NOPREFIX, 100);
			HWND keep = child(L"BUTTON", L"Keep editing", WS_TABSTOP | BS_DEFPUSHBUTTON, IDNO);
			child(L"BUTTON", L"Exit without saving", WS_TABSTOP | BS_PUSHBUTTON, IDYES);
			const UINT dpi = GetDpiForWindow(window);
			RECT rect { 0, 0, MulDiv(480, dpi, 96), MulDiv(140, dpi, 96) };
			ui::AdjustThemedWindowRect(rect, dpi);
			const int width = rect.right - rect.left, height = rect.bottom - rect.top;
			RECT owner {};
			GetWindowRect(GetWindow(window, GW_OWNER), &owner);
			MONITORINFO monitor { sizeof(monitor) };
			GetMonitorInfoW(MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST), &monitor);
			const int x = std::clamp((owner.left + owner.right - width) / 2, monitor.rcWork.left,
			                         std::max(monitor.rcWork.left, monitor.rcWork.right - width));
			const int y = std::clamp((owner.top + owner.bottom - height) / 2, monitor.rcWork.top,
			                         std::max(monitor.rcWork.top, monitor.rcWork.bottom - height));
			SetWindowPos(window, nullptr, x, y, width, height, SWP_NOZORDER | SWP_NOACTIVATE);
			LayoutDialog(window);
			SendMessageW(window, DM_SETDEFID, IDNO, 0);
			SetFocus(keep);
			if (auto check = reinterpret_cast<Check*>(lp))
			{
				SetWindowLongPtrW(window, DWLP_USER, lp);
				SetTimer(window, 1, 1500, nullptr);
				check->valid = GetPropW(window, L"LutCaptionFont") && GetPropW(window, L"UnsavedFont") &&
				               LOWORD(SendMessageW(window, DM_GETDEFID, 0, 0)) == IDNO && GetFocus() == keep;
				if (check->action < 0)
					PostMessageW(window, WM_CLOSE, 0, 0);
				else if (check->action == VK_RETURN || check->action == VK_ESCAPE)
					PostMessageW(window, WM_KEYDOWN, check->action, 0);
				else
					PostMessageW(window, WM_COMMAND, check->action, 0);
			}
			return FALSE;
		}
		case WM_COMMAND:
			if (LOWORD(wp) == IDYES || LOWORD(wp) == IDNO || LOWORD(wp) == IDCANCEL)
			{
				EndDialog(window, LOWORD(wp) == IDYES ? IDYES : IDNO);
				return TRUE;
			}
			break;
		case WM_CLOSE:
			EndDialog(window, IDNO);
			return TRUE;
		case WM_TIMER:
			if (wp == 1)
			{
				if (auto check = reinterpret_cast<Check*>(GetWindowLongPtrW(window, DWLP_USER)))
					check->valid = false;
				EndDialog(window, IDNO);
				return TRUE;
			}
			break;
		case WM_DPICHANGED:
		{
			const auto rect = reinterpret_cast<RECT*>(lp);
			SetWindowPos(window, nullptr, rect->left, rect->top, rect->right - rect->left, rect->bottom - rect->top,
			             SWP_NOZORDER | SWP_NOACTIVATE);
			LayoutDialog(window);
			return TRUE;
		}
		case WM_DESTROY:
		{
			auto font = reinterpret_cast<HFONT>(RemovePropW(window, L"UnsavedFont"));
			if (font)
				DeleteObject(font);
			break;
		}
	}
	return FALSE;
}

INT_PTR RunDialog(HWND owner, Check* check)
{
	const auto words = DialogTemplate();
	return DialogBoxIndirectParamW(GetModuleHandleW(nullptr), reinterpret_cast<const DLGTEMPLATE*>(words.data()), owner,
	                               DialogProc, reinterpret_cast<LPARAM>(check));
}
}

bool Confirm(HWND owner)
{
	return RunDialog(owner, nullptr) == IDYES;
}

HWND PreviewDialog(HWND owner)
{
	const auto words = DialogTemplate();
	return CreateDialogIndirectParamW(GetModuleHandleW(nullptr), reinterpret_cast<const DLGTEMPLATE*>(words.data()),
	                                  owner, DialogProc, 0);
}

int SelfTest(HWND owner)
{
	for (int action : { IDNO, IDYES, IDCANCEL, -1, VK_RETURN, VK_ESCAPE })
	{
		Check check { action };
		const auto result = RunDialog(owner, &check);
		if (!check.valid || result != (action == IDYES ? IDYES : IDNO))
			return 166;
	}
	return 0;
}
}
