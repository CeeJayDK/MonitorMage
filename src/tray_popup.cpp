#include "tray_popup.h"
#include "ui_theme.h"
#include "app_version.h"
#include "paint_buffer.h"
#include <commctrl.h>
#include <uxtheme.h>
#include <windowsx.h>
#include <algorithm>
#include <cstring>

namespace ui
{
namespace
{
constexpr UINT MESSAGE_ITEM_CHOSEN = WM_APP + 40;
struct Popup
{
	HWND window {}, list {};
	HFONT font {};
	HICON icon {};
	UINT dpi = 96, command = 0;
	const std::vector<MenuItem>* items {};
	bool below = false, compact = false, ignore_opening_release = false, painting_list = false;
};

int ScaleForDpi(const Popup& state, int n)
{
	return MulDiv(n, state.dpi, 96);
}

void ChooseSelectedRow(Popup& state)
{
	const auto row = SendMessageW(state.list, LB_GETCURSEL, 0, 0);
	if (row < 0 || static_cast<size_t>(row) >= state.items->size() || !(*state.items)[row].enabled)
		return;
	state.command = (*state.items)[row].id;
	DestroyWindow(state.window);
}

void PaintList(Popup& state, HDC dc)
{
	RECT bounds {};
	GetClientRect(state.list, &bounds);
	paint::Buffer frame(dc, bounds);
	if (frame && frame.IsBuffered())
	{
		FillRect(frame.DeviceContext(), &bounds, BackgroundBrush());
		state.painting_list = true;
		SendMessageW(state.list, WM_PRINTCLIENT, reinterpret_cast<WPARAM>(frame.DeviceContext()), PRF_CLIENT);
		state.painting_list = false;
	}
}

LRESULT CALLBACK PopupListProc(HWND list, UINT message, WPARAM w_param, LPARAM l_param, UINT_PTR, DWORD_PTR ref)
{
	auto& state = *reinterpret_cast<Popup*>(ref);
	if (message == WM_ERASEBKGND)
		return 1; // The buffered paint supplies the background.
	if (message == WM_PAINT)
	{
		PAINTSTRUCT p {};
		HDC dc = BeginPaint(list, &p);
		PaintList(state, dc);
		EndPaint(list, &p);
		return 0;
	}
	if (message == WM_KEYDOWN && (w_param == VK_RETURN || w_param == VK_ESCAPE))
	{
		if (w_param == VK_RETURN)
			ChooseSelectedRow(state);
		else
			DestroyWindow(state.window);
		return 0;
	}
	if (message == WM_KEYDOWN && (w_param == VK_UP || w_param == VK_DOWN || w_param == VK_HOME || w_param == VK_END))
	{
		const int count = static_cast<int>(state.items->size());
		int row = static_cast<int>(SendMessageW(list, LB_GETCURSEL, 0, 0));
		const int direction = (w_param == VK_UP || w_param == VK_END) ? -1 : 1;
		if (w_param == VK_HOME)
			row = -1;
		else if (w_param == VK_END)
			row = count;
		for (int i = 0; i < count; ++i)
		{
			row = (row + count + direction) % count;
			if ((*state.items)[row].enabled)
			{
				SendMessageW(list, LB_SETCURSEL, row, 0);
				break;
			}
		}
		return 0;
	}
	return DefSubclassProc(list, message, w_param, l_param);
}

int PointerRow(Popup& state, LPARAM point)
{
	POINT at { GET_X_LPARAM(point), GET_Y_LPARAM(point) };
	ClientToScreen(state.window, &at);
	ScreenToClient(state.list, &at);
	const auto hit = SendMessageW(state.list, LB_ITEMFROMPOINT, 0, MAKELPARAM(at.x, at.y));
	return HIWORD(hit) ? -1 : LOWORD(hit);
}

void PaintPopup(Popup& state, HDC dc)
{
	RECT rect {};
	GetClientRect(state.window, &rect);
	FillRect(dc, &rect, BackgroundBrush());
	HPEN pen = CreatePen(PS_SOLID, 1, RGB(60, 64, 71));
	auto old_pen = SelectObject(dc, pen);
	auto old_brush = SelectObject(dc, GetStockObject(NULL_BRUSH));
	RoundRect(dc, 0, 0, rect.right, rect.bottom, ScaleForDpi(state, 16), ScaleForDpi(state, 16));
	SelectObject(dc, old_brush);
	SelectObject(dc, old_pen);
	DeleteObject(pen);
	if (state.compact)
		return;
	if (state.icon)
		DrawIconEx(dc, ScaleForDpi(state, 18), ScaleForDpi(state, 17), state.icon, ScaleForDpi(state, 20),
		           ScaleForDpi(state, 20), 0, nullptr, DI_NORMAL);
	auto old_font = SelectObject(dc, state.font);
	SetBkMode(dc, TRANSPARENT);
	SetTextColor(dc, THEME_MUTED);
	RECT label { ScaleForDpi(state, 50), ScaleForDpi(state, 8), rect.right - ScaleForDpi(state, 12),
		         ScaleForDpi(state, 47) };
	DrawTextW(dc, L"MonitorMage " MONITORMAGE_VERSION_W, -1, &label, DT_SINGLELINE | DT_VCENTER);
	SelectObject(dc, old_font);
}

LRESULT CALLBACK TrayPopupWindowProc(HWND window, UINT message, WPARAM w_param, LPARAM l_param)
{
	auto state = reinterpret_cast<Popup*>(GetWindowLongPtrW(window, GWLP_USERDATA));
	if (message == WM_NCCREATE)
	{
		state = static_cast<Popup*>(reinterpret_cast<LPCREATESTRUCTW>(l_param)->lpCreateParams);
		state->window = window;
		SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));
	}
	if (!state)
		return DefWindowProcW(window, message, w_param, l_param);
	switch (message)
	{
		case WM_CREATE:
		{
			state->dpi = GetDpiForWindow(window);
			state->font = CreateUiFont(state->dpi);
			if (state->compact)
			{
				LOGFONTW font {};
				GetObjectW(state->font, sizeof(font), &font);
				font.lfHeight = -ScaleForDpi(*state, style::SECONDARY_TEXT_SIZE);
				auto smaller = CreateFontIndirectW(&font);
				if (smaller)
				{
					DeleteObject(state->font);
					state->font = smaller;
				}
			}
			state->list = CreateWindowW(
				L"LISTBOX", L"MonitorMage tray actions",
				WS_CHILD | WS_VISIBLE | WS_TABSTOP | LBS_OWNERDRAWVARIABLE | LBS_HASSTRINGS | LBS_NOINTEGRALHEIGHT, 0,
				0, 1, 1, window, reinterpret_cast<HMENU>(10), GetModuleHandleW(nullptr), nullptr);
			SetWindowTheme(state->list, L"", L"");
			SendMessageW(state->list, WM_SETFONT, reinterpret_cast<WPARAM>(state->font), 0);
			SetWindowSubclass(state->list, PopupListProc, 1, reinterpret_cast<DWORD_PTR>(state));
			int height = 0;
			for (const auto& item : *state->items)
			{
				const bool stateful = item.glyph == Glyph::Sun || item.glyph == Glyph::Power ||
				                      item.glyph == Glyph::Diamond || item.glyph == Glyph::ColorFilter;
				const auto text = item.text +
				                  (stateful       ? (item.checked ? L" (on)" : L" (off)")
				                   : item.checked ? L" (on)"
				                                  : L"") +
				                  (item.enabled ? L"" : L" (unavailable)");
				const auto row = SendMessageW(state->list, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(text.c_str()));
				SendMessageW(state->list, LB_SETITEMDATA, row, item.id);
				const int row_height = ScaleForDpi(*state, state->compact ? (item.separator_before ? 30 : 24)
				                                                          : (item.separator_before ? 44 : 36));
				SendMessageW(state->list, LB_SETITEMHEIGHT, row, row_height);
				height += row_height;
			}
			HDC measure = GetDC(window);
			const auto old_font = SelectObject(measure, state->font);
			int width = ScaleForDpi(*state, state->compact ? 160 : 200);
			for (const auto& item : *state->items)
			{
				SIZE text {}, hint {};
				GetTextExtentPoint32W(measure, item.text.c_str(), static_cast<int>(item.text.size()), &text);
				GetTextExtentPoint32W(measure, item.shortcut.c_str(), static_cast<int>(item.shortcut.size()), &hint);
				width = std::max(width, static_cast<int>(text.cx + hint.cx) +
				                            ScaleForDpi(*state, (state->compact ? (item.shortcut.empty() ? 24 : 36)
				                                                                : (item.shortcut.empty() ? 40 : 56)) +
				                                                    (item.glyph != Glyph::None ? 24 : 0)));
			}
			SelectObject(measure, old_font);
			ReleaseDC(window, measure);
			const int margin = state->compact ? 4 : 6, top = state->compact ? 4 : 51, extra = state->compact ? 8 : 61;
			MoveWindow(state->list, ScaleForDpi(*state, margin), ScaleForDpi(*state, top),
			           width - ScaleForDpi(*state, margin * 2), height, TRUE);
			SetWindowPos(window, nullptr, 0, 0, width, height + ScaleForDpi(*state, extra),
			             SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
			HRGN region = CreateRoundRectRgn(0, 0, width + 1, height + ScaleForDpi(*state, extra) + 1,
			                                 ScaleForDpi(*state, 16), ScaleForDpi(*state, 16));
			if (!SetWindowRgn(window, region, FALSE))
				DeleteObject(region);
			for (size_t i = 0; i < state->items->size(); ++i)
				if ((*state->items)[i].enabled)
				{
					SendMessageW(state->list, LB_SETCURSEL, i, 0);
					break;
				}
			return 0;
		}
		case WM_CTLCOLORLISTBOX:
			SetBkColor(reinterpret_cast<HDC>(w_param), THEME_BACKGROUND);
			return reinterpret_cast<LRESULT>(BackgroundBrush());
		case WM_MEASUREITEM:
			reinterpret_cast<MEASUREITEMSTRUCT*>(l_param)->itemHeight = ScaleForDpi(*state, 36);
			return TRUE;
		case WM_DRAWITEM:
		{
			const auto item = reinterpret_cast<DRAWITEMSTRUCT*>(l_param);
			if (item->itemID >= state->items->size())
				return TRUE;
			// Selection changes can send WM_DRAWITEM outside WM_PAINT. Buffer those
			// rows too, so their erase and content reach the screen together.
			const auto& entry = (*state->items)[item->itemID];
			RECT rect = item->rcItem;
			paint::Buffer row(state->painting_list ? nullptr : item->hDC, rect);
			if (!state->painting_list && (!row || !row.IsBuffered()))
				return TRUE;
			auto dc = state->painting_list ? item->hDC : row.DeviceContext();
			FillRect(dc, &rect, BackgroundBrush());
			if (entry.separator_before)
			{
				RECT line { rect.left + ScaleForDpi(*state, 6), rect.top + ScaleForDpi(*state, 3),
					        rect.right - ScaleForDpi(*state, 6), rect.top + ScaleForDpi(*state, 3) + 1 };
				HBRUSH brush = CreateSolidBrush(THEME_BORDER);
				FillRect(dc, &line, brush);
				DeleteObject(brush);
				rect.top += ScaleForDpi(*state, state->compact ? 6 : 8);
			}
			if ((item->itemState & ODS_SELECTED) && entry.enabled)
			{
				HPEN pen = CreatePen(PS_SOLID, 1, THEME_FOCUS_BORDER);
				auto old_brush = SelectObject(dc, GetStockObject(NULL_BRUSH)), old_pen = SelectObject(dc, pen);
				RoundRect(dc, rect.left, rect.top, rect.right, rect.bottom, ScaleForDpi(*state, 8),
				          ScaleForDpi(*state, 8));
				SelectObject(dc, old_brush);
				SelectObject(dc, old_pen);
				DeleteObject(pen);
			}
			SetBkMode(dc, TRANSPARENT);
			SetTextColor(dc, entry.enabled ? THEME_TEXT : THEME_MUTED);
			auto old_font = SelectObject(dc, state->font);
			RECT label = rect;
			label.left += ScaleForDpi(*state, state->compact ? 6 : 10);
			label.right -= ScaleForDpi(*state, state->compact ? 6 : 10);
			RECT hint = label;
			if (entry.glyph != Glyph::None)
			{
				const int size = ScaleForDpi(*state, state->compact ? 16 : 20), mid = (rect.top + rect.bottom) / 2;
				RECT icon { label.left, mid - size / 2, label.left + size, mid + (size + 1) / 2 };
				PaintMenuGlyph(state->window, dc, icon, entry.glyph, entry.checked, entry.enabled);
				label.left += ScaleForDpi(*state, 24);
			}
			if (!entry.shortcut.empty())
			{
				SIZE size {};
				GetTextExtentPoint32W(dc, entry.shortcut.c_str(), static_cast<int>(entry.shortcut.size()), &size);
				label.right -= size.cx + ScaleForDpi(*state, 12);
			}
			DrawTextW(dc, entry.text.c_str(), -1, &label, DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX | DT_END_ELLIPSIS);
			if (!entry.shortcut.empty())
			{
				SetTextColor(dc, THEME_MUTED);
				DrawTextW(dc, entry.shortcut.c_str(), -1, &hint, DT_SINGLELINE | DT_VCENTER | DT_RIGHT | DT_NOPREFIX);
			}
			SelectObject(dc, old_font);
			return TRUE;
		}
		case WM_ERASEBKGND:
			return 1;
		case WM_PRINTCLIENT:
			PaintPopup(*state, reinterpret_cast<HDC>(w_param));
			return 0;
		case WM_PAINT:
		{
			PAINTSTRUCT p {};
			HDC dc = BeginPaint(window, &p);
			RECT bounds {};
			GetClientRect(window, &bounds);
			{
				paint::Buffer frame(dc, bounds);
				if (frame && frame.IsBuffered())
					PaintPopup(*state, frame.DeviceContext());
			}
			EndPaint(window, &p);
			return 0;
		}
		case WM_MOUSEMOVE:
		{
			const int row = PointerRow(*state, l_param);
			if (row != SendMessageW(state->list, LB_GETCURSEL, 0, 0))
				SendMessageW(state->list, LB_SETCURSEL, row, 0);
			return 0;
		}
		case WM_LBUTTONDOWN:
			if (PointerRow(*state, l_param) < 0)
				DestroyWindow(window);
			return 0;
		case WM_LBUTTONUP:
		{
			const int row = PointerRow(*state, l_param);
			const bool opening = state->ignore_opening_release;
			state->ignore_opening_release = false;
			if (row >= 0)
			{
				SendMessageW(state->list, LB_SETCURSEL, row, 0);
				ChooseSelectedRow(*state);
			}
			else if (!opening)
				DestroyWindow(window);
			return 0;
		}
		case WM_RBUTTONDOWN:
		case WM_CANCELMODE:
			DestroyWindow(window);
			return 0;
		case WM_ACTIVATE:
			if (LOWORD(w_param) == WA_INACTIVE)
				DestroyWindow(window);
			return 0;
		case WM_COMMAND:
			if (LOWORD(w_param) == IDOK)
				ChooseSelectedRow(*state);
			else if (LOWORD(w_param) == IDCANCEL)
				DestroyWindow(window);
			return 0;
		case MESSAGE_ITEM_CHOSEN:
			ChooseSelectedRow(*state);
			return 0;
		case WM_CLOSE:
			DestroyWindow(window);
			return 0;
		case WM_DESTROY:
			if (GetCapture() == window)
				ReleaseCapture();
			if (state->font)
			{
				DeleteObject(state->font);
				state->font = nullptr;
			}
			return 0;
	}
	return DefWindowProcW(window, message, w_param, l_param);
}

HWND CreatePopupWindow(HWND owner, Popup& state, POINT point)
{
	WNDCLASSW window_class {};
	window_class.hInstance = GetModuleHandleW(nullptr);
	window_class.lpfnWndProc = TrayPopupWindowProc;
	window_class.lpszClassName = L"MonitorMageTrayPopup";
	window_class.hCursor = LoadCursorW(nullptr, IDC_ARROW);
	window_class.hbrBackground = BackgroundBrush();
	if (!RegisterClassW(&window_class) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
		return nullptr;
	HWND popup = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_TOPMOST, window_class.lpszClassName, L"MonitorMage tray menu",
	                             WS_POPUP | WS_CLIPCHILDREN, point.x, point.y, 1, 1, owner, nullptr,
	                             window_class.hInstance, &state);
	if (!popup)
		return nullptr;
	RECT rect {};
	GetWindowRect(popup, &rect);
	MONITORINFO monitor { sizeof(monitor) };
	GetMonitorInfoW(MonitorFromPoint(point, MONITOR_DEFAULTTONEAREST), &monitor);
	const LONG x = std::clamp(point.x, monitor.rcWork.left,
	                          std::max(monitor.rcWork.left, monitor.rcWork.right - (rect.right - rect.left)));
	const LONG y = std::clamp(state.below ? point.y : point.y - (rect.bottom - rect.top), monitor.rcWork.top,
	                          std::max(monitor.rcWork.top, monitor.rcWork.bottom - (rect.bottom - rect.top)));
	SetWindowPos(popup, nullptr, x, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
	return popup;
}
}

UINT ShowTrayPopup(HWND owner, HICON icon, const std::vector<MenuItem>& items, POINT anchor, bool below)
{
	Popup state;
	state.icon = icon;
	state.items = &items;
	state.below = below;
	state.compact = below;
	state.ignore_opening_release = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
	HWND window = CreatePopupWindow(owner, state, anchor);
	if (!window)
		return 0;
	if (below)
	{
		SetWindowTextW(window, L"MonitorMage window menu");
		SetWindowTextW(state.list, L"Window actions");
	}
	SetForegroundWindow(window);
	ShowWindow(window, SW_SHOWNORMAL);
	SetFocus(state.list);
	SetCapture(window);
	MSG message {};
	while (IsWindow(window))
	{
		const int status = GetMessageW(&message, nullptr, 0, 0);
		if (status <= 0)
		{
			if (IsWindow(window))
				DestroyWindow(window);
			if (status == 0)
				PostQuitMessage(static_cast<int>(message.wParam));
			break;
		}
		if (!IsDialogMessageW(window, &message))
		{
			TranslateMessage(&message);
			DispatchMessageW(&message);
		}
	}
	PostMessageW(owner, WM_NULL, 0, 0);
	return state.command;
}

bool PreviewPopup(HWND owner, HICON icon, const std::vector<MenuItem>& items, std::function<bool(HWND)> render,
                  bool compact)
{
	Popup state;
	state.icon = icon;
	state.items = &items;
	state.compact = compact;
	POINT point { 100, 100 };
	auto window = CreatePopupWindow(owner, state, point);
	if (!window)
		return false;
	const bool result = render(window);
	if (IsWindow(window))
		DestroyWindow(window);
	return result;
}

// Replay hover damage into an existing image; compare against a complete native
// list repaint. Erase requests must leave the previous image untouched.
bool PopupPaintTest(Popup& state)
{
	RECT bounds {};
	GetClientRect(state.list, &bounds);
	HDC screen = GetDC(state.list);
	HDC partial = CreateCompatibleDC(screen), full = CreateCompatibleDC(screen);
	BITMAPINFO info {};
	info.bmiHeader = { sizeof(BITMAPINFOHEADER), bounds.right, -bounds.bottom, 1, 32, BI_RGB };
	void *a = nullptr, *b = nullptr;
	HBITMAP first = CreateDIBSection(screen, &info, DIB_RGB_COLORS, &a, nullptr, 0),
			second = CreateDIBSection(screen, &info, DIB_RGB_COLORS, &b, nullptr, 0);
	ReleaseDC(state.list, screen);
	auto old_a = SelectObject(partial, first), old_b = SelectObject(full, second);
	bool ok = partial && full && first && second;
	if (ok)
	{
		PaintList(state, partial);
		int previous = static_cast<int>(SendMessageW(state.list, LB_GETCURSEL, 0, 0));
		const size_t bytes = static_cast<size_t>(bounds.right) * bounds.bottom * 4;
		for (int selected : { 3, 1, 0, 2, 1 })
		{
			RECT old_row {}, new_row {}, damage {};
			SendMessageW(state.list, LB_GETITEMRECT, previous, reinterpret_cast<LPARAM>(&old_row));
			SendMessageW(state.list, LB_SETCURSEL, selected, 0);
			SendMessageW(state.list, LB_GETITEMRECT, selected, reinterpret_cast<LPARAM>(&new_row));
			UnionRect(&damage, &old_row, &new_row);
			const int saved = SaveDC(partial);
			IntersectClipRect(partial, damage.left, damage.top, damage.right, damage.bottom);
			PaintList(state, partial);
			RestoreDC(partial, saved);
			state.painting_list = true;
			SendMessageW(state.list, WM_PRINTCLIENT, reinterpret_cast<WPARAM>(full), PRF_CLIENT);
			state.painting_list = false;
			GdiFlush();
			if (std::memcmp(a, b, bytes) != 0)
			{
				ok = false;
				break;
			}
			SendMessageW(state.list, WM_ERASEBKGND, reinterpret_cast<WPARAM>(partial), 0);
			SendMessageW(state.window, WM_ERASEBKGND, reinterpret_cast<WPARAM>(partial), 0);
			GdiFlush();
			if (std::memcmp(a, b, bytes) != 0)
			{
				ok = false;
				break;
			}
			previous = selected;
		}
	}
	SelectObject(partial, old_a);
	SelectObject(full, old_b);
	DeleteObject(first);
	DeleteObject(second);
	DeleteDC(partial);
	DeleteDC(full);
	return ok;
}

int PopupSelfTest(HWND owner)
{
	const std::vector<MenuItem> items { { 10, L"Unavailable", L"", false, false, false },
		                                { 11, L"Open editor", L"" },
		                                { 13, L"Unavailable", L"", false, false, false },
		                                { 12, L"Enable corrections", L"", true } };
	Popup state;
	state.items = &items;
	POINT point { 100, 100 };
	HWND window = CreatePopupWindow(owner, state, point);
	if (!window)
		return 1;
	if (!PopupPaintTest(state))
	{
		DestroyWindow(window);
		return 11;
	}
	state.compact = true;
	DestroyWindow(window);
	window = CreatePopupWindow(owner, state, point);
	if (!window)
		return 12;
	if (!PopupPaintTest(state))
	{
		DestroyWindow(window);
		return 13;
	}
	SendMessageW(state.list, LB_SETCURSEL, 0, 0);
	SendMessageW(state.list, WM_KEYDOWN, VK_RETURN, 0);
	if (!IsWindow(window) || state.command)
		return 7;
	SendMessageW(state.list, WM_KEYDOWN, VK_HOME, 0);
	if (SendMessageW(state.list, LB_GETCURSEL, 0, 0) != 1)
		return 8;
	SendMessageW(state.list, WM_KEYDOWN, VK_DOWN, 0);
	if (SendMessageW(state.list, LB_GETCURSEL, 0, 0) != 3)
		return 9;
	SendMessageW(state.list, WM_KEYDOWN, VK_RETURN, 0);
	if (IsWindow(window) || state.command != 12)
		return 2;
	state.command = 0;
	window = CreatePopupWindow(owner, state, point);
	if (!window)
		return 3;
	SendMessageW(window, WM_COMMAND, IDCANCEL, 0);
	if (IsWindow(window) || state.command)
		return 4;
	window = CreatePopupWindow(owner, state, point);
	if (!window)
		return 5;
	state.ignore_opening_release = true;
	SendMessageW(window, WM_LBUTTONUP, 0, MAKELPARAM(-1, -1));
	if (!IsWindow(window) || state.command)
		return 10;
	SendMessageW(window, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(-1, -1));
	return !IsWindow(window) && state.command == 0 ? 0 : 6;
}
}
