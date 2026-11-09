#include "effect_list.h"
#include "ui_theme.h"
#include "paint_buffer.h"
#include <commctrl.h>
#include <windowsx.h>
#include <array>
#include <algorithm>
#include <string>

namespace effects
{
namespace
{
struct State
{
	std::function<void(int)> toggle;
	std::function<void(int, int)> move;
	bool initialized = false, paused = false;
	bool powered = true, tracking = false;
	bool buffering = false;
	std::array<bool, effect::Count> enabled {};
	std::vector<int> order;
	int dragging = -1, target = -1;
	int hover = -1;
};

using ui::ScaleForDpi;

int RowAtPoint(HWND list, LPARAM point)
{
	const auto at = SendMessageW(list, LB_ITEMFROMPOINT, 0, point);
	return HIWORD(at) ? -1 : static_cast<int>(LOWORD(at));
}

int RoleOfRow(HWND list, int row)
{
	return static_cast<int>(SendMessageW(list, LB_GETITEMDATA, row, 0));
}

void SelectRow(HWND list, int row)
{
	SendMessageW(list, LB_SETCURSEL, row, 0);
	SendMessageW(GetParent(list), WM_COMMAND, MAKEWPARAM(GetDlgCtrlID(list), LBN_SELCHANGE),
	             reinterpret_cast<LPARAM>(list));
}

void RefreshHelp(HWND list, State& state)
{
	for (size_t row = 0; row < state.order.size(); ++row)
	{
		RECT rect {};
		if (SendMessageW(list, LB_GETITEMRECT, row, reinterpret_cast<LPARAM>(&rect)) == LB_ERR)
			continue;
		const int effect = state.order[row];
		const std::wstring name = EFFECT_NAMES[effect];
		RECT label = rect;
		label.right -= ScaleForDpi(list, 39);
		ui::HelpRegion(list, static_cast<UINT_PTR>(effect + 1), label, (name + L" settings").c_str());
		RECT toggle = rect;
		toggle.left = label.right;
		ui::HelpRegion(list, static_cast<UINT_PTR>(100 + effect), toggle, (L"Turn " + name + L" on/off").c_str());
	}
}

void RedrawRow(HWND list, int row)
{
	RECT rect {};
	if (row >= 0 && SendMessageW(list, LB_GETITEMRECT, row, reinterpret_cast<LPARAM>(&rect)) != LB_ERR)
		InvalidateRect(list, &rect, FALSE);
}

void PaintList(HWND list, HDC dc, State& state)
{
	RECT client {};
	GetClientRect(list, &client);
	paint::Buffer buffer(dc, client);
	if (!buffer)
		return;
	HDC target = buffer.DeviceContext();
	HBRUSH background = CreateSolidBrush(ui::THEME_SIDEBAR);
	FillRect(target, &client, background);
	state.buffering = true;
	DefSubclassProc(list, WM_PRINTCLIENT, reinterpret_cast<WPARAM>(target), PRF_CLIENT);
	state.buffering = false;
	// Owner drawing covers rows only. Native list boxes can leave the unused
	// tail white, so finish that area in the same buffer before showing it.
	const auto count = SendMessageW(list, LB_GETCOUNT, 0, 0);
	RECT last {};
	if (count > 0)
		SendMessageW(list, LB_GETITEMRECT, count - 1, reinterpret_cast<LPARAM>(&last));
	RECT tail = client;
	tail.top = std::clamp(last.bottom, 0L, client.bottom);
	FillRect(target, &tail, background);
	DeleteObject(background);
}

LRESULT CALLBACK EffectListWindowProc(HWND list, UINT message, WPARAM w_param, LPARAM l_param, UINT_PTR, DWORD_PTR ref)
{
	auto& state = *reinterpret_cast<State*>(ref);
	switch (message)
	{
		case WM_SIZE:
			RefreshHelp(list, state);
			break;
		case WM_ERASEBKGND:
			return 1;
		case WM_PAINT:
		{
			PAINTSTRUCT paint {};
			HDC dc = BeginPaint(list, &paint);
			PaintList(list, dc, state);
			EndPaint(list, &paint);
			return 0;
		}
		case WM_PRINTCLIENT:
			PaintList(list, reinterpret_cast<HDC>(w_param), state);
			return 0;
		case WM_SETCURSOR:
			if (LOWORD(l_param) == HTCLIENT && IsWindowEnabled(list))
			{
				POINT point {};
				GetCursorPos(&point);
				ScreenToClient(list, &point);
				const int row = RowAtPoint(list, MAKELPARAM(point.x, point.y));
				if (row >= 0 && row < static_cast<int>(state.order.size()))
				{
					SetCursor(LoadCursorW(nullptr, (RoleOfRow(list, row) != 1 && RoleOfRow(list, row) < 8) &&
					                                       point.x < ScaleForDpi(list, 28)
					                                   ? IDC_SIZEALL
					                                   : IDC_HAND));
					return TRUE;
				}
			}
			break;
		case WM_LBUTTONDOWN:
		case WM_LBUTTONDBLCLK:
		{
			const int row = RowAtPoint(list, l_param);
			if (row < 0)
				break;
			RECT rect {};
			GetClientRect(list, &rect);
			SetFocus(list);
			if (GET_X_LPARAM(l_param) >= rect.right - ScaleForDpi(list, 39))
			{
				const int effect = RoleOfRow(list, row);
				SelectRow(list, row);
				state.toggle(effect);
				return 0;
			}
			// Windows replaces the second DOWN in a rapid pair with DBLCLK.
			// Each switch press still toggles once; elsewhere retain native
			// list-box double-click behavior rather than starting another drag.
			if (message == WM_LBUTTONDBLCLK)
				break;
			if ((RoleOfRow(list, row) != 1 && RoleOfRow(list, row) < 8) &&
			    GET_X_LPARAM(l_param) < ScaleForDpi(list, 28))
			{
				SelectRow(list, row);
				state.dragging = row;
				state.target = row;
				SetCapture(list);
				return 0;
			}
			break;
		}
		case WM_MOUSEMOVE:
			if (!state.tracking)
			{
				TRACKMOUSEEVENT tracking { sizeof(tracking), TME_LEAVE, list, 0 };
				TrackMouseEvent(&tracking);
				state.tracking = true;
			}
			{
				const int at = RowAtPoint(list, l_param);
				const int hover = at >= 0 && at < static_cast<int>(state.order.size()) ? at : -1;
				if (hover != state.hover)
				{
					const int previous = state.hover;
					state.hover = hover;
					RedrawRow(list, previous);
					RedrawRow(list, hover);
				}
			}
			if (state.dragging >= 0)
			{
				const int at = RowAtPoint(list, l_param);
				if (at != state.target)
				{
					state.target = at;
					InvalidateRect(list, nullptr, FALSE);
				}
				return 0;
			}
			break;
		case WM_MOUSELEAVE:
			state.tracking = false;
			RedrawRow(list, state.hover);
			state.hover = -1;
			break;
		case WM_LBUTTONUP:
			if (state.dragging >= 0)
			{
				const int from = state.dragging, to = RowAtPoint(list, l_param), source_role = RoleOfRow(list, from);
				state.dragging = state.target = -1;
				ReleaseCapture();
				InvalidateRect(list, nullptr, FALSE);
				if (to >= 0 && to != from)
					state.move(source_role, to);
				return 0;
			}
			break;
		case WM_CANCELMODE:
		case WM_CAPTURECHANGED:
			state.dragging = state.target = -1;
			InvalidateRect(list, nullptr, FALSE);
			break;
		case WM_KEYDOWN:
			if (w_param == VK_ESCAPE && state.dragging >= 0)
			{
				state.dragging = state.target = -1;
				ReleaseCapture();
				InvalidateRect(list, nullptr, FALSE);
				return 0;
			}
			if (w_param == VK_SPACE)
			{
				const int at = static_cast<int>(SendMessageW(list, LB_GETCURSEL, 0, 0));
				if (at >= 0)
					state.toggle(RoleOfRow(list, at));
				return 0;
			}
			break;
		case WM_SYSKEYDOWN:
			if (w_param == VK_UP || w_param == VK_DOWN)
			{
				const int at = static_cast<int>(SendMessageW(list, LB_GETCURSEL, 0, 0)),
						  to = at + (w_param == VK_UP ? -1 : 1);
				if (at >= 0 && to >= 0 && to < static_cast<int>(state.order.size()))
					state.move(RoleOfRow(list, at), to);
				return 0;
			}
			break;
		case WM_NCDESTROY:
			RemoveWindowSubclass(list, EffectListWindowProc, 2);
			delete &state;
			return DefSubclassProc(list, message, w_param, l_param);
	}
	return DefSubclassProc(list, message, w_param, l_param);
}

State* GetListState(HWND list)
{
	DWORD_PTR data = 0;
	return GetWindowSubclass(list, EffectListWindowProc, 2, &data) ? reinterpret_cast<State*>(data) : nullptr;
}
}

void Attach(HWND list, std::function<void(int)> toggle, std::function<void(int, int)> move)
{
	auto state = new State;
	state->toggle = std::move(toggle);
	state->move = std::move(move);
	if (!SetWindowSubclass(list, EffectListWindowProc, 2, reinterpret_cast<DWORD_PTR>(state)))
		delete state;
}

void Update(HWND list, const std::vector<int>& order, const std::array<bool, effect::Count>& enabled, bool paused,
            int selected_role, bool powered)
{
	auto state = GetListState(list);
	if (!state)
		return;
	if (state->powered != powered)
	{
		state->powered = powered;
		InvalidateRect(list, nullptr, FALSE);
	}
	const bool rebuild = !state->initialized || state->order != order;
	if (!state->initialized || state->order != order || state->enabled != enabled || state->paused != paused)
	{
		const int top = static_cast<int>(SendMessageW(list, LB_GETTOPINDEX, 0, 0));
		SendMessageW(list, WM_SETREDRAW, FALSE, 0);
		if (rebuild)
			SendMessageW(list, LB_RESETCONTENT, 0, 0);
		for (size_t i = 0; i < order.size(); ++i)
		{
			const int effect = order[i];
			if (!rebuild && state->enabled[effect] == enabled[effect] &&
			    (effect != effect::HdrGamma || state->paused == paused))
				continue;
			const std::wstring text =
				std::wstring(EFFECT_NAMES[effect]) + (!enabled[effect]                       ? L" (off)"
			                                          : effect == effect::HdrGamma && paused ? L" (paused)"
			                                                                                 : L" (on)");
			if (!rebuild)
				SendMessageW(list, LB_DELETESTRING, i, 0);
			const auto row = SendMessageW(list, rebuild ? LB_ADDSTRING : LB_INSERTSTRING, rebuild ? 0 : i,
			                              reinterpret_cast<LPARAM>(text.c_str()));
			SendMessageW(list, LB_SETITEMDATA, row, effect);
		}
		state->initialized = true;
		state->order = order;
		state->enabled = enabled;
		state->paused = paused;
		const auto selected = std::find(order.begin(), order.end(), selected_role);
		SendMessageW(list, LB_SETCURSEL, selected == order.end() ? -1 : static_cast<int>(selected - order.begin()), 0);
		SendMessageW(list, LB_SETTOPINDEX, top, 0);
		SendMessageW(list, WM_SETREDRAW, TRUE, 0);
		InvalidateRect(list, nullptr, FALSE);
	}
	const auto selected = std::find(order.begin(), order.end(), selected_role);
	const int height = ScaleForDpi(list, ROW_HEIGHT),
			  row = selected == order.end() ? -1 : static_cast<int>(selected - order.begin());
	const bool height_changed = SendMessageW(list, LB_GETITEMHEIGHT, 0, 0) != height;
	if (height_changed)
		SendMessageW(list, LB_SETITEMHEIGHT, 0, height);
	if (SendMessageW(list, LB_GETCURSEL, 0, 0) != row)
		SendMessageW(list, LB_SETCURSEL, row, 0);
	if (rebuild || height_changed)
		RefreshHelp(list, *state);
}

static bool DrawRow(const DRAWITEMSTRUCT& item)
{
	auto state = GetListState(item.hwndItem);
	if (!state || item.itemID == static_cast<UINT>(-1))
		return false;
	const int effect = static_cast<int>(item.itemData);
	const bool enabled = state->enabled[effect];
	const bool selected = (item.itemState & ODS_SELECTED) != 0;
	const auto rect = item.rcItem;
	auto dc = item.hDC;
	auto list = item.hwndItem;
	HBRUSH brush = CreateSolidBrush(selected ? ui::THEME_SELECTED_TAB : ui::THEME_SIDEBAR);
	FillRect(dc, &rect, brush);
	DeleteObject(brush);
	SetBkMode(dc, TRANSPARENT);
	if (selected)
	{
		RECT bar { rect.left, rect.top, rect.left + ScaleForDpi(list, 2), rect.bottom };
		brush = CreateSolidBrush(ui::THEME_SELECTION_LIGHT);
		FillRect(dc, &bar, brush);
		DeleteObject(brush);
	}
	// Drag grip, rendered only on demand.
	brush = CreateSolidBrush(ui::THEME_MUTED);
	if (effect != effect::HdrGamma && effect < effect::FirstWindows)
		for (int x : { 13, 18 })
			for (int y : { 12, 18, 24 })
			{
				RECT dot { rect.left + ScaleForDpi(list, x), rect.top + ScaleForDpi(list, y),
					       rect.left + ScaleForDpi(list, x + 2), rect.top + ScaleForDpi(list, y + 2) };
				FillRect(dc, &dot, brush);
			}
	DeleteObject(brush);
	auto font = reinterpret_cast<HFONT>(SendMessageW(list, WM_GETFONT, 0, 0));
	auto old_font = SelectObject(dc, font);
	SetTextColor(dc, enabled && (effect != effect::HdrGamma || state->powered) ? ui::THEME_TEXT : ui::THEME_MUTED);
	RECT label { rect.left + ScaleForDpi(list, 29), rect.top + ScaleForDpi(list, 2), rect.right - ScaleForDpi(list, 37),
		         rect.top + ScaleForDpi(list, 20) };
	DrawTextW(dc, EFFECT_NAMES[effect], -1, &label, DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS);
	LOGFONTW small {};
	GetObjectW(font, sizeof(small), &small);
	small.lfHeight = -ScaleForDpi(list, ui::style::SECONDARY_TEXT_SIZE);
	HFONT sub_font = CreateFontIndirectW(&small);
	SelectObject(dc, sub_font);
	SetTextColor(dc, ui::THEME_MUTED);
	label.top = rect.top + ScaleForDpi(list, 20);
	label.bottom = rect.bottom - ScaleForDpi(list, 3);
	DrawTextW(dc,
	          effect == effect::HdrGamma && state->paused ? L"Paused by app"
	          : effect >= effect::FirstWindows            ? L"All displays"
	          : effect != effect::HdrGamma                ? L"Creative look"
	                                                      : L"Monitor correction",
	          -1, &label, DT_SINGLELINE | DT_END_ELLIPSIS);
	SelectObject(dc, old_font);
	DeleteObject(sub_font);
	RECT toggle { rect.right - ScaleForDpi(list, 38), rect.top, rect.right, rect.bottom };
	ui::PaintToggle(list, dc, toggle, enabled, effect == effect::ColorFilters ? true : state->powered);
	// One physical pixel at group edges and between rows, without doubling
	// the shared boundary of adjacent items. Corrections use the same dark ink.
	HBRUSH line_ink = CreateSolidBrush(RGB(29, 31, 35));
	if (item.itemID == 0)
	{
		RECT edge { rect.left, rect.top, rect.right, rect.top + 1 };
		FillRect(dc, &edge, line_ink);
	}
	RECT edge { rect.left, rect.bottom - 1, rect.right, rect.bottom };
	FillRect(dc, &edge, line_ink);
	DeleteObject(line_ink);
	if (state->hover == static_cast<int>(item.itemID))
	{
		HBRUSH border = CreateSolidBrush(ui::THEME_SELECTION_LIGHT);
		FrameRect(dc, &rect, border);
		DeleteObject(border);
	}
	if (state->dragging >= 0 && state->target == static_cast<int>(item.itemID) && state->target != state->dragging)
	{
		RECT mark { rect.left, rect.top, rect.right, rect.top + ScaleForDpi(list, 2) };
		brush = CreateSolidBrush(ui::THEME_SELECTION_LIGHT);
		FillRect(dc, &mark, brush);
		DeleteObject(brush);
	}
	if (item.itemState & ODS_FOCUS)
	{
		RECT focus = rect;
		InflateRect(&focus, -2, -2);
		DrawFocusRect(dc, &focus);
	}
	return true;
}

bool Draw(const DRAWITEMSTRUCT& item)
{
	auto state = GetListState(item.hwndItem);
	if (!state || item.itemID == static_cast<UINT>(-1))
		return false;
	if (state->buffering)
		return DrawRow(item);
	paint::Buffer buffer(item.hDC, item.rcItem);
	if (!buffer)
		return true;
	auto local = item;
	local.hDC = buffer.DeviceContext();
	return DrawRow(local);
}
}
