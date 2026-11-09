#include "viewport.h"
#include <commctrl.h>
#include <windowsx.h>
#include <uxtheme.h>
#include <algorithm>

namespace ui
{
namespace
{
constexpr wchar_t VIEWPORT_PROPERTY[] = L"MonitorMageViewport";
struct View
{
	SIZE content {};
	POINT offset {};
	HWND horizontal {}, vertical {};
	bool busy = false;
	int width = 0, height = 0;
};

View* GetViewState(HWND window)
{
	return reinterpret_cast<View*>(GetPropW(window, VIEWPORT_PROPERTY));
}

void ScrollTo(HWND window, View& view, int x, int y)
{
	x = std::clamp(x, 0, std::max(0, static_cast<int>(view.content.cx) - view.width));
	y = std::clamp(y, 0, std::max(0, static_cast<int>(view.content.cy) - view.height));
	const int dx = view.offset.x - x, dy = view.offset.y - y;
	if (!dx && !dy)
		return;
	view.offset = { x, y };
	// Do not copy desktop/graph pixels into a sibling control when scrolling.
	for (HWND child = GetWindow(window, GW_CHILD); child; child = GetWindow(child, GW_HWNDNEXT))
	{
		if (child == view.horizontal || child == view.vertical)
			continue;
		RECT bounds {};
		GetWindowRect(child, &bounds);
		MapWindowPoints(nullptr, window, reinterpret_cast<POINT*>(&bounds), 2);
		SetWindowPos(child, nullptr, bounds.left + dx, bounds.top + dy, 0, 0,
		             SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOCOPYBITS);
	}
	SetScrollPos(view.horizontal, SB_CTL, x, TRUE);
	SetScrollPos(view.vertical, SB_CTL, y, TRUE);
	RedrawWindow(window, nullptr, nullptr, RDW_INVALIDATE | RDW_ALLCHILDREN);
}

void UpdateScrollbars(HWND window, View& view)
{
	if (view.busy || IsIconic(window))
		return;
	view.busy = true;
	RECT client {};
	GetClientRect(window, &client);
	const UINT dpi = GetDpiForWindow(window);
	const int thickness = GetSystemMetricsForDpi(SM_CXVSCROLL, dpi);
	const auto preferred = reinterpret_cast<SIZE*>(GetPropW(window, L"MonitorMagePreferredSize"));
	if (preferred)
		view.content = { MulDiv(preferred->cx, dpi, 96), MulDiv(preferred->cy, dpi, 96) };
	bool needs_horizontal = client.right < view.content.cx, needs_vertical = client.bottom < view.content.cy;
	// One scrollbar can make the other necessary.
	for (int i = 0; i < 2; ++i)
	{
		needs_horizontal = client.right - (needs_vertical ? thickness : 0) < view.content.cx;
		needs_vertical = client.bottom - (needs_horizontal ? thickness : 0) < view.content.cy;
	}
	view.width = std::max(1, static_cast<int>(client.right) - (needs_vertical ? thickness : 0));
	view.height = std::max(1, static_cast<int>(client.bottom) - (needs_horizontal ? thickness : 0));
	SetWindowPos(view.horizontal, HWND_TOP, 0, view.height, view.width, thickness,
	             SWP_NOACTIVATE | (needs_horizontal ? SWP_SHOWWINDOW : SWP_HIDEWINDOW));
	SetWindowPos(view.vertical, HWND_TOP, view.width, 0, thickness, view.height,
	             SWP_NOACTIVATE | (needs_vertical ? SWP_SHOWWINDOW : SWP_HIDEWINDOW));
	SCROLLINFO info { sizeof(info),
		              SIF_RANGE | SIF_PAGE | SIF_POS,
		              0,
		              view.content.cx - 1,
		              static_cast<UINT>(view.width),
		              view.offset.x,
		              0 };
	SetScrollInfo(view.horizontal, SB_CTL, &info, TRUE);
	info.nMax = view.content.cy - 1;
	info.nPage = view.height;
	info.nPos = view.offset.y;
	SetScrollInfo(view.vertical, SB_CTL, &info, TRUE);
	ScrollTo(window, view, view.offset.x, view.offset.y);
	view.busy = false;
}

LRESULT CALLBACK ViewportProc(HWND window, UINT message, WPARAM w_param, LPARAM l_param, UINT_PTR, DWORD_PTR)
{
	auto* view = GetViewState(window);
	if (!view)
		return DefSubclassProc(window, message, w_param, l_param);
	if (message == WM_NCDESTROY)
	{
		RemovePropW(window, VIEWPORT_PROPERTY);
		RemoveWindowSubclass(window, ViewportProc, 83);
		delete view;
		return DefSubclassProc(window, message, w_param, l_param);
	}
	if ((message == WM_VSCROLL || message == WM_HSCROLL) &&
	    reinterpret_cast<HWND>(l_param) == (message == WM_VSCROLL ? view->vertical : view->horizontal))
	{
		const bool vertical = message == WM_VSCROLL;
		int value = vertical ? view->offset.y : view->offset.x;
		const int page = vertical ? view->height : view->width, line = MulDiv(32, GetDpiForWindow(window), 96);
		switch (LOWORD(w_param))
		{
			case SB_LINEUP:
				value -= line;
				break;
			case SB_LINEDOWN:
				value += line;
				break;
			case SB_PAGEUP:
				value -= page;
				break;
			case SB_PAGEDOWN:
				value += page;
				break;
			case SB_TOP:
				value = 0;
				break;
			case SB_BOTTOM:
				value = vertical ? view->content.cy : view->content.cx;
				break;
			case SB_THUMBPOSITION:
			case SB_THUMBTRACK:
			{
				SCROLLINFO info { sizeof(info), SIF_TRACKPOS };
				GetScrollInfo(reinterpret_cast<HWND>(l_param), SB_CTL, &info);
				value = info.nTrackPos;
				break;
			}
			default:
				return 0;
		}
		ScrollTo(window, *view, vertical ? view->offset.x : value, vertical ? value : view->offset.y);
		return 0;
	}
	if (message == WM_MOUSEWHEEL || message == WM_MOUSEHWHEEL)
	{
		const bool horizontal =
			message == WM_MOUSEHWHEEL || (GET_KEYSTATE_WPARAM(w_param) & MK_SHIFT) || view->content.cy <= view->height;
		if ((horizontal ? view->content.cx > view->width : view->content.cy > view->height))
		{
			const int delta =
				MulDiv(GET_WHEEL_DELTA_WPARAM(w_param), MulDiv(48, GetDpiForWindow(window), 96), WHEEL_DELTA) *
				(message == WM_MOUSEHWHEEL ? 1 : -1);
			ScrollTo(window, *view, view->offset.x + (horizontal ? delta : 0),
			         view->offset.y + (horizontal ? 0 : delta));
			return 0;
		}
	}
	const auto result = DefSubclassProc(window, message, w_param, l_param);
	if (message == WM_SIZE || message == WM_DPICHANGED || message == WM_SHOWWINDOW)
		UpdateScrollbars(window, *view);
	return result;
}
}

POINT ViewportOffset(HWND window)
{
	const auto* view = GetViewState(window);
	return view ? view->offset : POINT {};
}

void EnableViewport(HWND window, int width, int height)
{
	if (GetViewState(window))
		return;
	auto* view = new View;
	view->content = { width, height };
	if (!SetPropW(window, VIEWPORT_PROPERTY, view))
	{
		delete view;
		return;
	}
	view->horizontal = CreateWindowExW(0, L"SCROLLBAR", nullptr, WS_CHILD | SBS_HORZ, 0, 0, 1, 1, window, nullptr,
	                                   GetModuleHandleW(nullptr), nullptr);
	view->vertical = CreateWindowExW(0, L"SCROLLBAR", nullptr, WS_CHILD | SBS_VERT, 0, 0, 1, 1, window, nullptr,
	                                 GetModuleHandleW(nullptr), nullptr);
	SetWindowTheme(view->horizontal, L"DarkMode_Explorer", nullptr);
	SetWindowTheme(view->vertical, L"DarkMode_Explorer", nullptr);
	SetWindowSubclass(window, ViewportProc, 83, 0);
	UpdateScrollbars(window, *view);
}

void RevealControl(HWND window, HWND control)
{
	auto* view = GetViewState(window);
	if (!view || !control || !IsChild(window, control))
		return;
	RECT bounds {};
	GetWindowRect(control, &bounds);
	MapWindowPoints(nullptr, window, reinterpret_cast<POINT*>(&bounds), 2);
	const int x =
		view->offset.x + (bounds.right > view->width ? bounds.right - view->width : std::min(0L, bounds.left));
	const int y =
		view->offset.y + (bounds.bottom > view->height ? bounds.bottom - view->height : std::min(0L, bounds.top));
	ScrollTo(window, *view, x, y);
}
}
