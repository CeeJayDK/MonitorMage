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
	std::array<bool, 12> enabled {};
	std::vector<int> order;
	int dragging = -1, target = -1;
	int hover = -1;
};

int ScaleForDpi(HWND window, int n)
{
	return MulDiv(n, GetDpiForWindow(window), 96);
}

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
		const std::wstring name = Names[effect];
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
	HBRUSH background = CreateSolidBrush(ui::Sidebar);
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

LRESULT CALLBACK EffectListWindowProc(HWND list, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR, DWORD_PTR ref)
{
	auto& state = *reinterpret_cast<State*>(ref);
	switch (msg)
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
			PaintList(list, reinterpret_cast<HDC>(wp), state);
			return 0;
		case WM_SETCURSOR:
			if (LOWORD(lp) == HTCLIENT && IsWindowEnabled(list))
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
			const int row = RowAtPoint(list, lp);
			if (row < 0)
				break;
			RECT rect {};
			GetClientRect(list, &rect);
			SetFocus(list);
			if (GET_X_LPARAM(lp) >= rect.right - ScaleForDpi(list, 39))
			{
				const int effect = RoleOfRow(list, row);
				SelectRow(list, row);
				state.toggle(effect);
				return 0;
			}
			// Windows replaces the second DOWN in a rapid pair with DBLCLK.
			// Each switch press still toggles once; elsewhere retain native
			// list-box double-click behavior rather than starting another drag.
			if (msg == WM_LBUTTONDBLCLK)
				break;
			if ((RoleOfRow(list, row) != 1 && RoleOfRow(list, row) < 8) && GET_X_LPARAM(lp) < ScaleForDpi(list, 28))
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
				const int at = RowAtPoint(list, lp);
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
				const int at = RowAtPoint(list, lp);
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
				const int from = state.dragging, to = RowAtPoint(list, lp), sourceRole = RoleOfRow(list, from);
				state.dragging = state.target = -1;
				ReleaseCapture();
				InvalidateRect(list, nullptr, FALSE);
				if (to >= 0 && to != from)
					state.move(sourceRole, to);
				return 0;
			}
			break;
		case WM_CANCELMODE:
		case WM_CAPTURECHANGED:
			state.dragging = state.target = -1;
			InvalidateRect(list, nullptr, FALSE);
			break;
		case WM_KEYDOWN:
			if (wp == VK_ESCAPE && state.dragging >= 0)
			{
				state.dragging = state.target = -1;
				ReleaseCapture();
				InvalidateRect(list, nullptr, FALSE);
				return 0;
			}
			if (wp == VK_SPACE)
			{
				const int at = static_cast<int>(SendMessageW(list, LB_GETCURSEL, 0, 0));
				if (at >= 0)
					state.toggle(RoleOfRow(list, at));
				return 0;
			}
			break;
		case WM_SYSKEYDOWN:
			if (wp == VK_UP || wp == VK_DOWN)
			{
				const int at = static_cast<int>(SendMessageW(list, LB_GETCURSEL, 0, 0)),
						  to = at + (wp == VK_UP ? -1 : 1);
				if (at >= 0 && to >= 0 && to < static_cast<int>(state.order.size()))
					state.move(RoleOfRow(list, at), to);
				return 0;
			}
			break;
		case WM_NCDESTROY:
			RemoveWindowSubclass(list, EffectListWindowProc, 2);
			delete &state;
			return DefSubclassProc(list, msg, wp, lp);
	}
	return DefSubclassProc(list, msg, wp, lp);
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

void Update(HWND list, const std::vector<int>& order, const std::array<bool, 12>& enabled, bool paused,
            int selectedRole, bool powered)
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
			const int role = order[i];
			if (!rebuild && state->enabled[role] == enabled[role] && (role != 1 || state->paused == paused))
				continue;
			const std::wstring text = std::wstring(Names[role]) + (!enabled[role]        ? L" (off)"
			                                                       : role == 1 && paused ? L" (paused)"
			                                                                             : L" (on)");
			if (!rebuild)
				SendMessageW(list, LB_DELETESTRING, i, 0);
			const auto row = SendMessageW(list, rebuild ? LB_ADDSTRING : LB_INSERTSTRING, rebuild ? 0 : i,
			                              reinterpret_cast<LPARAM>(text.c_str()));
			SendMessageW(list, LB_SETITEMDATA, row, role);
		}
		state->initialized = true;
		state->order = order;
		state->enabled = enabled;
		state->paused = paused;
		const auto selected = std::find(order.begin(), order.end(), selectedRole);
		SendMessageW(list, LB_SETCURSEL, selected == order.end() ? -1 : static_cast<int>(selected - order.begin()), 0);
		SendMessageW(list, LB_SETTOPINDEX, top, 0);
		SendMessageW(list, WM_SETREDRAW, TRUE, 0);
		InvalidateRect(list, nullptr, FALSE);
	}
	const auto selected = std::find(order.begin(), order.end(), selectedRole);
	const int height = ScaleForDpi(list, RowHeight),
			  row = selected == order.end() ? -1 : static_cast<int>(selected - order.begin());
	const bool heightChanged = SendMessageW(list, LB_GETITEMHEIGHT, 0, 0) != height;
	if (heightChanged)
		SendMessageW(list, LB_SETITEMHEIGHT, 0, height);
	if (SendMessageW(list, LB_GETCURSEL, 0, 0) != row)
		SendMessageW(list, LB_SETCURSEL, row, 0);
	if (rebuild || heightChanged)
		RefreshHelp(list, *state);
}

static bool DrawRow(const DRAWITEMSTRUCT& item)
{
	auto state = GetListState(item.hwndItem);
	if (!state || item.itemID == static_cast<UINT>(-1))
		return false;
	const int role = static_cast<int>(item.itemData);
	const bool enabled = state->enabled[role];
	const bool selected = (item.itemState & ODS_SELECTED) != 0;
	const auto rect = item.rcItem;
	auto dc = item.hDC;
	auto list = item.hwndItem;
	HBRUSH brush = CreateSolidBrush(selected ? ui::SelectedTab : ui::Sidebar);
	FillRect(dc, &rect, brush);
	DeleteObject(brush);
	SetBkMode(dc, TRANSPARENT);
	if (selected)
	{
		RECT bar { rect.left, rect.top, rect.left + ScaleForDpi(list, 2), rect.bottom };
		brush = CreateSolidBrush(ui::SelectionLight);
		FillRect(dc, &bar, brush);
		DeleteObject(brush);
	}
	// Drag grip, rendered only on demand.
	brush = CreateSolidBrush(ui::Muted);
	if (role != 1 && role < 8)
		for (int x : { 13, 18 })
			for (int y : { 12, 18, 24 })
			{
				RECT dot { rect.left + ScaleForDpi(list, x), rect.top + ScaleForDpi(list, y),
					       rect.left + ScaleForDpi(list, x + 2), rect.top + ScaleForDpi(list, y + 2) };
				FillRect(dc, &dot, brush);
			}
	DeleteObject(brush);
	auto font = reinterpret_cast<HFONT>(SendMessageW(list, WM_GETFONT, 0, 0));
	auto oldFont = SelectObject(dc, font);
	SetTextColor(dc, enabled && (role != 1 || state->powered) ? ui::Text : ui::Muted);
	RECT label { rect.left + ScaleForDpi(list, 29), rect.top + ScaleForDpi(list, 2), rect.right - ScaleForDpi(list, 37),
		         rect.top + ScaleForDpi(list, 20) };
	DrawTextW(dc, Names[role], -1, &label, DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS);
	LOGFONTW small {};
	GetObjectW(font, sizeof(small), &small);
	small.lfHeight = -ScaleForDpi(list, ui::style::SecondaryTextSize);
	HFONT subFont = CreateFontIndirectW(&small);
	SelectObject(dc, subFont);
	SetTextColor(dc, ui::Muted);
	label.top = rect.top + ScaleForDpi(list, 20);
	label.bottom = rect.bottom - ScaleForDpi(list, 3);
	DrawTextW(dc,
	          role == 1 && state->paused ? L"Paused by app"
	          : role >= 8                ? L"All displays"
	          : role != 1                ? L"Creative look"
	                                     : L"Monitor correction",
	          -1, &label, DT_SINGLELINE | DT_END_ELLIPSIS);
	SelectObject(dc, oldFont);
	DeleteObject(subFont);
	RECT toggle { rect.right - ScaleForDpi(list, 38), rect.top, rect.right, rect.bottom };
	ui::PaintToggle(list, dc, toggle, enabled, role == 8 ? true : state->powered);
	// One physical pixel at group edges and between rows, without doubling
	// the shared boundary of adjacent items. Corrections use the same dark ink.
	HBRUSH lineInk = CreateSolidBrush(RGB(29, 31, 35));
	if (item.itemID == 0)
	{
		RECT edge { rect.left, rect.top, rect.right, rect.top + 1 };
		FillRect(dc, &edge, lineInk);
	}
	RECT edge { rect.left, rect.bottom - 1, rect.right, rect.bottom };
	FillRect(dc, &edge, lineInk);
	DeleteObject(lineInk);
	if (state->hover == static_cast<int>(item.itemID))
	{
		HBRUSH border = CreateSolidBrush(ui::SelectionLight);
		FrameRect(dc, &rect, border);
		DeleteObject(border);
	}
	if (state->dragging >= 0 && state->target == static_cast<int>(item.itemID) && state->target != state->dragging)
	{
		RECT mark { rect.left, rect.top, rect.right, rect.top + ScaleForDpi(list, 2) };
		brush = CreateSolidBrush(ui::SelectionLight);
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
