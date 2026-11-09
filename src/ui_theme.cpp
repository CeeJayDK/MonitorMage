#include "ui_theme.h"
#include "tray_popup.h"
#include "paint_buffer.h"
#include <commctrl.h>
#include <dwmapi.h>
#include <uxtheme.h>
#include <string>
#include <windowsx.h>
#include <cmath>
#include <vector>
#include <algorithm>
#include <memory>

namespace ui
{
namespace
{
HRGN TooltipShape(HWND tip, int inset = 0)
{
	RECT bounds {};
	GetWindowRect(tip, &bounds);
	const int diameter = MulDiv(style::CORNER_RADIUS * 2, GetDpiForWindow(tip), 96);
	return CreateRoundRectRgn(inset, inset, bounds.right - bounds.left - inset, bounds.bottom - bounds.top - inset,
	                          std::max(1, diameter - inset * 2), std::max(1, diameter - inset * 2));
}

void TooltipBounds(HWND tip)
{
	HRGN region = TooltipShape(tip);
	HRGN previous = CreateRectRgn(0, 0, 0, 0);
	const bool same = GetWindowRgn(tip, previous) != ERROR && EqualRgn(region, previous);
	DeleteObject(previous);
	if (same || !SetWindowRgn(tip, region, TRUE))
		DeleteObject(region);
}

void TooltipBorder(HWND tip, HDC supplied = nullptr)
{
	HDC dc = supplied ? supplied : GetWindowDC(tip);
	RECT bounds {};
	GetWindowRect(tip, &bounds);
	if (!dc)
		return;
	const int width = bounds.right - bounds.left, height = bounds.bottom - bounds.top;
	const int diameter = MulDiv(style::CORNER_RADIUS * 2, GetDpiForWindow(tip), 96);
	// Remove the native bevel, including its corner remnants. The padding
	// keeps text outside these small corner squares.
	HBRUSH fill = CreateSolidBrush(THEME_SURFACE);
	const int corner = diameter / 2 + 1;
	const RECT clear[] { { 0, 0, width, 2 },
		                 { 0, height - 2, width, height },
		                 { 0, 0, 2, height },
		                 { width - 2, 0, width, height },
		                 { 0, 0, corner, corner },
		                 { width - corner, 0, width, corner },
		                 { 0, height - corner, corner, height },
		                 { width - corner, height - corner, width, height } };
	for (const auto& rect : clear)
		FillRect(dc, &rect, fill);
	DeleteObject(fill);
	// Fill the difference between two masks rather than stroking another
	// rounded rectangle. Window clipping and the edge now share exactly
	// the same rasterized contour, including the bottom/right pixels.
	HRGN edge = TooltipShape(tip), inside = TooltipShape(tip, 1);
	CombineRgn(edge, edge, inside, RGN_DIFF);
	fill = CreateSolidBrush(THEME_BORDER);
	FillRgn(dc, edge, fill);
	DeleteObject(fill);
	DeleteObject(inside);
	DeleteObject(edge);
	if (!supplied)
		ReleaseDC(tip, dc);
}

LRESULT CALLBACK TooltipStyleProc(HWND tip, UINT message, WPARAM w_param, LPARAM l_param, UINT_PTR, DWORD_PTR)
{
	if (message == WM_NCDESTROY)
	{
		auto font = reinterpret_cast<HFONT>(RemovePropW(tip, L"MonitorMageTipFont"));
		const auto result = DefSubclassProc(tip, message, w_param, l_param);
		if (font)
			DeleteObject(font);
		return result;
	}
	if (message == WM_NCPAINT)
	{
		TooltipBorder(tip);
		return 0;
	}
	const auto result = DefSubclassProc(tip, message, w_param, l_param);
	if (message == WM_PAINT)
		TooltipBorder(tip);
	if (message == WM_WINDOWPOSCHANGED)
		TooltipBounds(tip);
	if ((message == WM_SHOWWINDOW && w_param) || message == WM_THEMECHANGED)
		StyleTooltip(tip);
	return result;
}

struct SliderClicks
{
	POINT point {};
	DWORD time = 0;
	bool candidate = false, tracking = false, suppress_up = false;
};

LRESULT CALLBACK SliderResetProc(HWND slider, UINT message, WPARAM w_param, LPARAM l_param, UINT_PTR, DWORD_PTR ref)
{
	auto& clicks = *reinterpret_cast<SliderClicks*>(ref);
	if (message == WM_NCDESTROY)
	{
		delete &clicks;
		return DefSubclassProc(slider, message, w_param, l_param);
	}
	const POINT point { GET_X_LPARAM(l_param), GET_Y_LPARAM(l_param) };
	const auto close = [&]()
	{
		return std::abs(point.x - clicks.point.x) <= GetSystemMetrics(SM_CXDOUBLECLK) / 2 &&
		       std::abs(point.y - clicks.point.y) <= GetSystemMetrics(SM_CYDOUBLECLK) / 2;
	};
	bool double_click = message == WM_LBUTTONDBLCLK;
	if (message == WM_LBUTTONDOWN)
	{
		double_click = clicks.candidate && GetTickCount() - clicks.time <= GetDoubleClickTime() && close();
		clicks.candidate = false;
		clicks.point = point;
		clicks.time = GetTickCount();
		clicks.tracking = !double_click;
	}
	if (message == WM_MOUSEMOVE && clicks.tracking && !close())
		clicks.tracking = false;
	if (message == WM_LBUTTONUP)
	{
		if (clicks.suppress_up)
		{
			clicks.suppress_up = false;
			return 0;
		}
		clicks.candidate = clicks.tracking;
		clicks.tracking = false;
		clicks.point = point;
	}
	if (message == WM_CANCELMODE || message == WM_KILLFOCUS)
		clicks.candidate = clicks.tracking = false;
	if (message == WM_RBUTTONDOWN)
		return 0;
	if (double_click || message == WM_RBUTTONUP || message == WM_CONTEXTMENU)
	{
		clicks.candidate = clicks.tracking = false;
		clicks.suppress_up = double_click;
		if (IsWindowEnabled(slider))
			if (const auto marker = reinterpret_cast<INT_PTR>(GetPropW(slider, L"LutSliderDefault")))
			{
				SendMessageW(slider, TBM_SETPOS, TRUE, marker - 32769);
				SendMessageW(GetParent(slider), WM_HSCROLL, TB_ENDTRACK, reinterpret_cast<LPARAM>(slider));
			}
		return 0;
	}
	return DefSubclassProc(slider, message, w_param, l_param);
}
}

void StyleTooltip(HWND tip)
{
	if (!tip)
		return;
	auto old = reinterpret_cast<HFONT>(GetPropW(tip, L"MonitorMageTipFont"));
	if (!old)
	{
		SetWindowTheme(tip, L"", L"");
		SetWindowSubclass(tip, TooltipStyleProc, 10, 0);
	}
	const UINT dpi = GetDpiForWindow(tip);
	auto base = CreateUiFont(dpi);
	LOGFONTW desired {}, current {};
	GetObjectW(base, sizeof(desired), &desired);
	DeleteObject(base);
	desired.lfHeight = -MulDiv(style::SECONDARY_TEXT_SIZE, dpi, 96);
	if (old)
		GetObjectW(old, sizeof(current), &current);
	if (!old || desired.lfHeight != current.lfHeight || wcscmp(desired.lfFaceName, current.lfFaceName))
	{
		if (auto font = CreateFontIndirectW(&desired))
		{
			SendMessageW(tip, WM_SETFONT, reinterpret_cast<WPARAM>(font), FALSE);
			SetPropW(tip, L"MonitorMageTipFont", font);
			if (old)
				DeleteObject(old);
			old = font;
		}
	}
	if (old)
		SendMessageW(tip, WM_SETFONT, reinterpret_cast<WPARAM>(old), FALSE);
	const int horizontal = MulDiv(6, dpi, 96), vertical = MulDiv(3, dpi, 96);
	RECT margin { horizontal, vertical, horizontal, vertical };
	SendMessageW(tip, TTM_SETMARGIN, 0, reinterpret_cast<LPARAM>(&margin));
	SendMessageW(tip, TTM_SETTIPBKCOLOR, THEME_SURFACE, 0);
	SendMessageW(tip, TTM_SETTIPTEXTCOLOR, THEME_TEXT, 0);
}

bool TooltipEdgeSelfTest(HWND tip)
{
	RECT bounds {};
	GetWindowRect(tip, &bounds);
	const int w = bounds.right - bounds.left, h = bounds.bottom - bounds.top;
	HDC screen = GetDC(tip), dc = CreateCompatibleDC(screen);
	HBITMAP bitmap = CreateCompatibleBitmap(screen, w, h);
	ReleaseDC(tip, screen);
	if (!dc || !bitmap)
	{
		if (dc)
			DeleteDC(dc);
		if (bitmap)
			DeleteObject(bitmap);
		return false;
	}
	auto old = SelectObject(dc, bitmap);
	RECT all { 0, 0, w, h }, bottom { 0, h / 2, w, h };
	FillRect(dc, &all, reinterpret_cast<HBRUSH>(GetStockObject(WHITE_BRUSH)));
	FillRect(dc, &bottom, reinterpret_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));
	HRGN shape = TooltipShape(tip);
	SelectClipRgn(dc, shape);
	TooltipBorder(tip, dc);
	GdiFlush();
	bool ok = true;
	// Every visible outermost pixel must have the same edge color, regardless
	// of a bright or dark native frame underneath. Check all four directions.
	for (int y = 0; y < h; ++y)
	{
		int left = 0, right = w - 1;
		while (left < w && !PtInRegion(shape, left, y))
			++left;
		while (right >= left && !PtInRegion(shape, right, y))
			--right;
		if (left <= right && (GetPixel(dc, left, y) != THEME_BORDER || GetPixel(dc, right, y) != THEME_BORDER))
			ok = false;
	}
	for (int x = 0; x < w; ++x)
	{
		int top = 0, last = h - 1;
		while (top < h && !PtInRegion(shape, x, top))
			++top;
		while (last >= top && !PtInRegion(shape, x, last))
			--last;
		if (top <= last && (GetPixel(dc, x, top) != THEME_BORDER || GetPixel(dc, x, last) != THEME_BORDER))
			ok = false;
	}
	DeleteObject(shape);
	SelectObject(dc, old);
	DeleteObject(bitmap);
	DeleteDC(dc);
	return ok;
}

namespace
{
struct Brush
{
	HBRUSH value;
	explicit Brush(COLORREF color) : value(CreateSolidBrush(color)) {}
	~Brush() { DeleteObject(value); }
};

Brush bg(THEME_BACKGROUND), surface(THEME_SURFACE);
std::wstring family = L"Bahnschrift";
void FillRoundedRect(HDC dc, RECT rect, COLORREF fill, COLORREF border, int radius)
{
	HBRUSH brush = CreateSolidBrush(fill);
	HPEN pen = CreatePen(PS_SOLID, 1, border);
	const auto old_brush = SelectObject(dc, brush), old_pen = SelectObject(dc, pen);
	RoundRect(dc, rect.left, rect.top, rect.right, rect.bottom, radius * 2, radius * 2);
	SelectObject(dc, old_brush);
	SelectObject(dc, old_pen);
	DeleteObject(brush);
	DeleteObject(pen);
}

void RoundBounds(HWND window, int radius = style::CORNER_RADIUS)
{
	RECT rect {};
	GetWindowRect(window, &rect);
	const int w = rect.right - rect.left, h = rect.bottom - rect.top;
	if (w <= 0 || h <= 0)
		return;
	HRGN shape =
		CreateRoundRectRgn(0, 0, w + 1, h + 1, ScaleForDpi(window, radius * 2), ScaleForDpi(window, radius * 2));
	if (!shape)
		return;
	HRGN previous = CreateRectRgn(0, 0, 0, 0);
	const bool same = GetWindowRgn(window, previous) != ERROR && EqualRgn(shape, previous);
	DeleteObject(previous);
	if (same || !SetWindowRgn(window, shape, TRUE))
		DeleteObject(shape);
}

void DrawFocusOutline(HWND window, HDC dc, RECT rect)
{
	HPEN pen = CreatePen(
		PS_SOLID, 1, (GetFocus() == window || GetPropW(window, L"LutThemeHover")) ? THEME_FOCUS_BORDER : THEME_BORDER);
	const auto old_pen = SelectObject(dc, pen), old_brush = SelectObject(dc, GetStockObject(NULL_BRUSH));
	RoundRect(dc, rect.left, rect.top, rect.right, rect.bottom, ScaleForDpi(window, 8), ScaleForDpi(window, 8));
	SelectObject(dc, old_brush);
	SelectObject(dc, old_pen);
	DeleteObject(pen);
}

void DrawGear(HDC dc, int x, int y, int radius, COLORREF colour, int stroke)
{
	POINT points[32] {};
	for (int i = 0; i < 32; ++i)
	{
		const double angle = i * 6.283185307179586 / 32;
		const double spoke_radius = (i % 4 < 2 ? radius : radius * .77);
		points[i] = { x + static_cast<LONG>(std::lround(std::cos(angle) * spoke_radius)),
			          y + static_cast<LONG>(std::lround(std::sin(angle) * spoke_radius)) };
	}
	HPEN pen = CreatePen(PS_SOLID, stroke, colour);
	auto old_pen = SelectObject(dc, pen), old_brush = SelectObject(dc, GetStockObject(NULL_BRUSH));
	Polygon(dc, points, 32);
	Ellipse(dc, x - radius / 3, y - radius / 3, x + radius / 3 + 1, y + radius / 3 + 1);
	SelectObject(dc, old_brush);
	SelectObject(dc, old_pen);
	DeleteObject(pen);
}

bool IsToolbarGlyph(Glyph glyph)
{
	return glyph >= Glyph::Save && glyph <= Glyph::Revert;
}

// Keep the one-pixel button edge out of the icon's supersampling pass.
// The fill and border share one native raster contour, like popup edges.
void GlyphFrame(HWND window, HDC dc, RECT rect, Glyph glyph, bool hover, bool focus, COLORREF base, bool menu)
{
	const bool toolbar = !menu && IsToolbarGlyph(glyph);
	if (!toolbar && !hover && !focus)
		return;
	const int diameter = ScaleForDpi(window, 8);
	HRGN shape = CreateRoundRectRgn(rect.left, rect.top, rect.right, rect.bottom, diameter, diameter);
	HRGN outside = CreateRectRgn(rect.left, rect.top, rect.right, rect.bottom);
	if (shape && outside)
	{
		CombineRgn(outside, outside, shape, RGN_DIFF);
		HBRUSH fill = CreateSolidBrush(base);
		FillRgn(dc, outside, fill);
		DeleteObject(fill);
		const COLORREF edge_color = hover || focus ? THEME_FOCUS_BORDER : THEME_BORDER;
		HBRUSH edge = CreateSolidBrush(edge_color);
		FrameRgn(dc, shape, edge, 1, 1);
		DeleteObject(edge);
		// Approved middle variant: keep the rounded corners bright, then
		// smoothly darken each straight edge to 76.5% of its corner color.
		// This runs only when generating a cached icon/button image.
		const int radius = diameter / 2;
		const double transition = std::max(1.0, ScaleForDpi(window, 16) / 3.0);
		const auto color_at = [&](int distance)
		{
			double t = std::clamp(distance / transition, 0.0, 1.0);
			t = t * t * (3 - 2 * t);
			const double gain = 1.0 - .235 * t;
			return RGB(static_cast<int>(std::lround(GetRValue(edge_color) * gain)),
			           static_cast<int>(std::lround(GetGValue(edge_color) * gain)),
			           static_cast<int>(std::lround(GetBValue(edge_color) * gain)));
		};
		const int left = rect.left + radius, right = rect.right - radius - 2;
		for (int x = left; x <= right; ++x)
		{
			const auto color = color_at(std::min(x - left, right - x));
			SetPixelV(dc, x, rect.top, color);
			SetPixelV(dc, x, rect.bottom - 2, color);
		}
		const int top = rect.top + radius, bottom = rect.bottom - radius - 2;
		for (int y = top; y <= bottom; ++y)
		{
			const auto color = color_at(std::min(y - top, bottom - y));
			SetPixelV(dc, rect.left, y, color);
			SetPixelV(dc, rect.right - 2, y, color);
		}
	}
	if (outside)
		DeleteObject(outside);
	if (shape)
		DeleteObject(shape);
}

void GlyphRaster(HWND window, HDC dc, RECT rect, Glyph glyph, bool on, bool enabled, bool /*hover*/, bool /*focus*/,
                 COLORREF base, bool menu = false)
{
	const bool toolbar = !menu && IsToolbarGlyph(glyph);
	HBRUSH back = CreateSolidBrush(toolbar ? THEME_SURFACE : base);
	FillRect(dc, &rect, back);
	DeleteObject(back);
	const COLORREF ink = menu                  ? (glyph == Glyph::Close ? THEME_CLOSE_RED
	                                              : glyph == Glyph::Sun ? (on ? RGB(255, 207, 69) : THEME_MUTED)
	                                              : glyph == Glyph::Power || glyph == Glyph::Diamond || glyph == Glyph::ColorFilter
	                                                  ? (on ? THEME_ACCENT : THEME_MUTED)
	                                              : enabled ? THEME_TEXT
	                                                        : THEME_BORDER)
	                     : toolbar             ? (enabled ? THEME_TEXT : THEME_BORDER)
	                     : glyph == Glyph::Sun ? (on ? RGB(255, 207, 69) : THEME_MUTED)
	                     : on                  ? THEME_ACCENT
	                                           : THEME_MUTED;
	// Small line icons share the caption controls' one-DIP stroke weight.
	// Keep their button bounds unchanged so pointer and keyboard targets stay roomy.
	const auto unit = [&](int value)
	{
		return MulDiv(ScaleForDpi(window, value), 3, 4);
	};
	const int x = (rect.left + rect.right) / 2, y = (rect.top + rect.bottom) / 2, r = unit(8);
	HPEN pen = CreatePen(PS_SOLID, ScaleForDpi(window, 1), !enabled && !on ? THEME_BORDER : ink);
	auto old_pen = SelectObject(dc, pen), old_brush = SelectObject(dc, GetStockObject(NULL_BRUSH));
	if (glyph == Glyph::Sun)
	{
		Ellipse(dc, x - r / 2, y - r / 2, x + r / 2 + 1, y + r / 2 + 1);
		for (int i = 0; i < 8; ++i)
		{
			const double angle = i * 6.283185307179586 / 8;
			MoveToEx(dc, x + static_cast<int>(std::lround(std::cos(angle) * r * .7)),
			         y + static_cast<int>(std::lround(std::sin(angle) * r * .7)), nullptr);
			LineTo(dc, x + static_cast<int>(std::lround(std::cos(angle) * r)),
			       y + static_cast<int>(std::lround(std::sin(angle) * r)));
		}
	}
	else if (glyph == Glyph::Power)
	{
		Arc(dc, x - r, y - r, x + r, y + r, x - r, y - r, x + r, y - r);
		MoveToEx(dc, x, y - r, nullptr);
		LineTo(dc, x, y - unit(1));
	}
	else if (glyph == Glyph::Diamond)
	{
		POINT points[] { { x, y - r }, { x + r, y }, { x, y + r }, { x - r, y } };
		Polygon(dc, points, 4);
	}
	else if (glyph == Glyph::Settings)
	{
		DrawGear(dc, x, y, r, ink, ScaleForDpi(window, 1));
	}
	else if (glyph == Glyph::Info)
	{
		Ellipse(dc, x - r, y - r, x + r + 1, y + r + 1);
		MoveToEx(dc, x, y - unit(1), nullptr);
		LineTo(dc, x, y + unit(4));
		// A device pixel disappears when the supersampled icon is reduced.
		// Draw the dot in logical units, with a visible gap above the stem.
		const int diameter = std::max(2, ScaleForDpi(window, 1)), left = x - diameter / 2,
				  top = y - unit(4) - diameter / 2;
		HBRUSH dot = CreateSolidBrush(!enabled && !on ? THEME_BORDER : ink);
		const auto prior_brush = SelectObject(dc, dot), prior_pen = SelectObject(dc, GetStockObject(NULL_PEN));
		Ellipse(dc, left, top, left + diameter, top + diameter);
		SelectObject(dc, prior_pen);
		SelectObject(dc, prior_brush);
		DeleteObject(dot);
	}
	else if (glyph >= Glyph::Editor)
	{
		const auto line = [&](int x1, int y1, int x2, int y2)
		{
			MoveToEx(dc, x + unit(x1), y + unit(y1), nullptr);
			LineTo(dc, x + unit(x2), y + unit(y2));
		};
		if (glyph == Glyph::Editor)
		{
			Rectangle(dc, x - r, y - r, x + r + 1, y + unit(4));
			line(0, 4, 0, 8);
			line(-4, 8, 4, 8);
			line(-5, 1, 5, -4);
		}
		else if (glyph == Glyph::Move)
		{
			line(-8, 0, 8, 0);
			line(0, -8, 0, 8);
			line(-8, 0, -4, -4);
			line(-8, 0, -4, 4);
			line(8, 0, 4, -4);
			line(8, 0, 4, 4);
			line(0, -8, -4, -4);
			line(0, -8, 4, -4);
			line(0, 8, -4, 4);
			line(0, 8, 4, 4);
		}
		else if (glyph == Glyph::Resize)
		{
			line(-7, 7, 7, -7);
			line(-7, 1, -7, 7);
			line(-7, 7, -1, 7);
			line(1, -7, 7, -7);
			line(7, -7, 7, -1);
		}
		else if (glyph == Glyph::Minimize)
			line(-7, 4, 7, 4);
		else if (glyph == Glyph::Maximize)
			Rectangle(dc, x - r, y - r, x + r + 1, y + r + 1);
		else if (glyph == Glyph::Close)
		{
			line(-6, -6, 6, 6);
			line(-6, 6, 6, -6);
		}
		else if (glyph == Glyph::Bypass)
		{
			Ellipse(dc, x - r, y - r, x + r + 1, y + r + 1);
			line(-6, 6, 6, -6);
		}
		else if (glyph == Glyph::Restore)
		{
			Rectangle(dc, x - unit(4), y - r, x + r + 1, y + unit(4));
			Rectangle(dc, x - r, y - unit(4), x + unit(4), y + r + 1);
		}
		else if (glyph == Glyph::ColorFilter)
		{
			POINT filter[] { { x - r, y - r },       { x + r, y - r },       { x + unit(2), y + unit(1) },
				             { x + unit(2), y + r }, { x - unit(2), y + r }, { x - unit(2), y + unit(1) },
				             { x - r, y - r } };
			Polyline(dc, filter, 7);
		}
	}
	else
	{
		const auto line = [&](int x1, int y1, int x2, int y2)
		{
			MoveToEx(dc, x + unit(x1), y + unit(y1), nullptr);
			LineTo(dc, x + unit(x2), y + unit(y2));
		};
		if (glyph == Glyph::Save || glyph == Glyph::SaveAs)
		{
			POINT edge[] { { x - r, y - r }, { x + unit(5), y - r }, { x + r, y - unit(5) },
				           { x + r, y + r }, { x - r, y + r },       { x - r, y - r } };
			Polyline(dc, edge, 6);
			Rectangle(dc, x - unit(4), y - r, x + unit(4), y - unit(1));
			Rectangle(dc, x - unit(4), y + unit(3), x + unit(4), y + r);
			if (glyph == Glyph::SaveAs)
			{
				RECT badge { x + unit(2), y + unit(1), x + unit(11), y + unit(11) };
				HBRUSH brush = CreateSolidBrush(THEME_SURFACE);
				FillRect(dc, &badge, brush);
				DeleteObject(brush);
				line(3, 6, 10, 6);
				line(6, 3, 6, 10);
			}
		}
		else if (glyph == Glyph::Export)
		{
			line(-7, -3, -7, 8);
			line(-7, 8, 5, 8);
			line(5, 8, 5, 3);
			line(-3, 2, 8, -8);
			line(1, -8, 8, -8);
			line(8, -8, 8, -1);
		}
		else if (glyph == Glyph::Reset)
		{
			POINT arc[25] {};
			for (int i = 0; i < 25; ++i)
			{
				const double angle = -.7853981633974483 + i * 4.71238898038469 / 24;
				arc[i] = { x + static_cast<LONG>(std::lround(std::cos(angle) * r)),
					       y + static_cast<LONG>(std::lround(std::sin(angle) * r)) };
			}
			Polyline(dc, arc, 25);
			const auto tip = arc[24];
			MoveToEx(dc, tip.x - unit(5), tip.y, nullptr);
			LineTo(dc, tip.x, tip.y);
			LineTo(dc, tip.x, tip.y + unit(5));
		}
		else
		{
			line(-2, -8, 7, -8);
			line(7, -8, 7, 8);
			line(7, 8, -2, 8);
			line(-8, 0, 3, 0);
			line(-8, 0, -3, -5);
			line(-8, 0, -3, 5);
		}
	}
	SelectObject(dc, old_brush);
	SelectObject(dc, old_pen);
	DeleteObject(pen);
}

struct GlyphCache
{
	struct Entry
	{
		int width, height;
		Glyph glyph;
		UINT dpi;
		unsigned state;
		COLORREF base;
		HBITMAP bitmap;
	};
	std::vector<Entry> entries;
	~GlyphCache()
	{
		for (const auto& entry : entries)
			DeleteObject(entry.bitmap);
	}
} glyph_cache;
void DrawGlyph(HWND window, HDC dc, RECT rect, Glyph glyph, bool on, bool enabled, bool hover, bool focus,
               COLORREF base, bool menu = false)
{
	// A normal symbol needs no cached padding. Keep full-size images only
	// when they include a toolbar frame or hover/focus outline.
	if (!hover && !focus && (menu || !IsToolbarGlyph(glyph)))
	{
		HBRUSH background = CreateSolidBrush(base);
		FillRect(dc, &rect, background);
		DeleteObject(background);
		const int cx = (rect.left + rect.right) / 2, cy = (rect.top + rect.bottom) / 2;
		const int w = std::min<int>(rect.right - rect.left, ScaleForDpi(window, 20)),
				  h = std::min<int>(rect.bottom - rect.top, ScaleForDpi(window, 20));
		rect = { cx - w / 2, cy - h / 2, cx - w / 2 + w, cy - h / 2 + h };
	}
	const int w = rect.right - rect.left, h = rect.bottom - rect.top;
	if (w <= 0 || h <= 0)
		return;
	const UINT dpi = GetDpiForWindow(window);
	const unsigned state = (on ? 1 : 0) | (enabled ? 2 : 0) | (hover ? 4 : 0) | (focus ? 8 : 0) | (menu ? 16 : 0);
	HBITMAP image = nullptr;
	for (const auto& entry : glyph_cache.entries)
		if (entry.width == w && entry.height == h && entry.glyph == glyph && entry.dpi == dpi && entry.state == state &&
		    entry.base == base)
		{
			image = entry.bitmap;
			break;
		}
	HDC memory = CreateCompatibleDC(dc);
	if (!memory)
	{
		GlyphRaster(window, dc, rect, glyph, on, enabled, hover, focus, base, menu);
		GlyphFrame(window, dc, rect, glyph, hover, focus, base, menu);
		return;
	}
	if (!image)
	{
		// Supersample once per size/state. Cached opaque images include the
		// exact surface colour, preserving hover/focus borders and thin strokes.
		HBITMAP high = CreateCompatibleBitmap(dc, w * 4, h * 4), low = CreateCompatibleBitmap(dc, w, h);
		if (high && low)
		{
			const auto original = SelectObject(memory, high);
			const int saved = SaveDC(memory);
			SetMapMode(memory, MM_ANISOTROPIC);
			SetWindowExtEx(memory, w, h, nullptr);
			SetViewportExtEx(memory, w * 4, h * 4, nullptr);
			GlyphRaster(window, memory, { 0, 0, w, h }, glyph, on, enabled, hover, focus, base, menu);
			RestoreDC(memory, saved);
			HDC reduced = CreateCompatibleDC(dc);
			if (reduced)
			{
				const auto old = SelectObject(reduced, low);
				SetStretchBltMode(reduced, HALFTONE);
				SetBrushOrgEx(reduced, 0, 0, nullptr);
				if (StretchBlt(reduced, 0, 0, w, h, memory, 0, 0, w * 4, h * 4, SRCCOPY))
				{
					GlyphFrame(window, reduced, { 0, 0, w, h }, glyph, hover, focus, base, menu);
					image = low;
				}
				SelectObject(reduced, old);
				DeleteDC(reduced);
			}
			SelectObject(memory, original);
		}
		if (high)
			DeleteObject(high);
		if (image)
		{
			// Bound retained GDI/memory use even after many DPI/state changes.
			if (glyph_cache.entries.size() == 32)
			{
				DeleteObject(glyph_cache.entries.front().bitmap);
				glyph_cache.entries.erase(glyph_cache.entries.begin());
			}
			glyph_cache.entries.push_back({ w, h, glyph, dpi, state, base, image });
		}
		else if (low)
			DeleteObject(low);
	}
	if (image)
	{
		const auto old = SelectObject(memory, image);
		BitBlt(dc, rect.left, rect.top, w, h, memory, 0, 0, SRCCOPY);
		SelectObject(memory, old);
	}
	else
	{
		GlyphRaster(window, dc, rect, glyph, on, enabled, hover, focus, base, menu);
		GlyphFrame(window, dc, rect, glyph, hover, focus, base, menu);
	}
	DeleteDC(memory);
}

using Actions = std::array<CaptionAction, 3>;
Actions* GetCaptionActions(HWND window)
{
	return reinterpret_cast<Actions*>(GetPropW(window, L"LutCaptionActions"));
}

CaptionAction* CaptionActionAtHit(HWND window, int hit)
{
	auto list = GetCaptionActions(window);
	return list && hit >= 100 && hit < 103 ? &(*list)[hit - 100] : nullptr;
}

RECT ActionRect(HWND window, int width, int i)
{
	return { width - ScaleForDpi(window, i == 2 ? 140 : 260 - i * 40), ScaleForDpi(window, 3),
		     width - ScaleForDpi(window, i == 2 ? 96 : 228 - i * 40), ScaleForDpi(window, 33) };
}

RECT PrimaryRect(HWND window, int width)
{
	const bool settings = GetPropW(window, L"LutCaptionSettings") != nullptr,
			   swapped = settings && GetCaptionActions(window);
	return { width - ScaleForDpi(window, swapped    ? 180
		                                 : settings ? 140
		                                            : 184),
		     ScaleForDpi(window, 3), width - ScaleForDpi(window, swapped ? 148 : 96), ScaleForDpi(window, 33) };
}

std::vector<MenuItem> CaptionItems(HWND window)
{
	HMENU menu = GetSystemMenu(window, FALSE);
	const auto style = GetWindowLongPtrW(window, GWL_STYLE);
	const bool iconic = IsIconic(window) != FALSE, zoomed = IsZoomed(window) != FALSE;
	const auto enable = [&](UINT command, bool on)
	{
		EnableMenuItem(menu, command, MF_BYCOMMAND | (on ? MF_ENABLED : MF_GRAYED));
	};
	enable(SC_RESTORE, iconic || zoomed || WindowNeedsSizeRepair(window));
	enable(SC_MOVE, !zoomed);
	enable(SC_SIZE, !iconic && !zoomed && (style & WS_THICKFRAME));
	enable(SC_MINIMIZE, !iconic && (style & WS_MINIMIZEBOX));
	enable(SC_MAXIMIZE, !zoomed && (style & WS_MAXIMIZEBOX));
	std::vector<MenuItem> items;
	bool separator = false;
	for (int i = 0; i < GetMenuItemCount(menu); ++i)
	{
		wchar_t text[512] {};
		MENUITEMINFOW item { sizeof(item) };
		item.fMask = MIIM_FTYPE | MIIM_STATE | MIIM_ID | MIIM_STRING;
		item.dwTypeData = text;
		item.cch = static_cast<UINT>(std::size(text));
		if (!GetMenuItemInfoW(menu, i, TRUE, &item))
			continue;
		if (item.fType & MFT_SEPARATOR)
		{
			separator = !items.empty();
			continue;
		}
		std::wstring label;
		for (size_t j = 0; text[j]; ++j)
		{
			if (text[j] == L'&')
			{
				if (text[j + 1] == L'&')
					++j;
				else
					continue;
			}
			label += text[j];
		}
		std::wstring shortcut;
		const auto tab = label.find(L'\t');
		if (tab != std::wstring::npos)
		{
			shortcut = label.substr(tab + 1);
			label.resize(tab);
		}
		const bool checked = (item.fState & MFS_CHECKED) != 0;
		Glyph icon = Glyph::None;
		switch (item.wID)
		{
			case SC_RESTORE:
				icon = Glyph::Restore;
				break;
			case SC_MOVE:
				icon = Glyph::Move;
				break;
			case SC_SIZE:
				icon = Glyph::Resize;
				break;
			case SC_MINIMIZE:
				icon = Glyph::Minimize;
				break;
			case SC_MAXIMIZE:
				icon = Glyph::Maximize;
				break;
			case SC_CLOSE:
				icon = Glyph::Close;
				break;
			case 0x1F00:
				icon = Glyph::Settings;
				break;
			case 0x1F10:
				icon = Glyph::Sun;
				break;
			case 0x1F20:
				icon = Glyph::Diamond;
				break;
			case 0x1F30:
				icon = Glyph::Power;
				break;
		}
		items.push_back(
			{ item.wID, label, shortcut, checked, separator, (item.fState & (MFS_DISABLED | MFS_GRAYED)) == 0, icon });
		separator = false;
	}
	return items;
}

void ShowCaptionMenu(HWND window, POINT anchor)
{
	const HWND focus = GetFocus();
	const auto items = CaptionItems(window);
	const auto icon = reinterpret_cast<HICON>(SendMessageW(window, WM_GETICON, ICON_SMALL, 0));
	const auto command = ShowTrayPopup(window, icon, items, anchor, true);
	if (IsWindow(window) && IsWindowVisible(window))
	{
		SetForegroundWindow(window);
		if (IsWindow(focus) && (focus == window || IsChild(window, focus)))
			SetFocus(focus);
	}
	if (command)
		PostMessageW(window, WM_SYSCOMMAND, command, 0);
}

constexpr UINT_PTR TIMER_GEOMETRY = 0x434d4752;
constexpr UINT_PTR TIMER_CAPTION_TIP = 0x43546970;
HWND CaptionTip(HWND window)
{
	return reinterpret_cast<HWND>(GetPropW(window, L"LutCaptionTooltip"));
}

void HideCaptionTip(HWND window)
{
	KillTimer(window, TIMER_CAPTION_TIP);
	RemovePropW(window, L"LutCaptionTipHit");
	if (auto tip = CaptionTip(window))
	{
		TOOLINFOW item { sizeof(item) };
		item.hwnd = window;
		item.uId = 1;
		SendMessageW(tip, TTM_TRACKACTIVATE, FALSE, reinterpret_cast<LPARAM>(&item));
	}
}

void TrackCaptionTip(HWND window, int hit)
{
	const auto text = CaptionTooltip(window, hit);
	if (!text)
	{
		HideCaptionTip(window);
		return;
	}
	if (reinterpret_cast<INT_PTR>(GetPropW(window, L"LutCaptionTipHit")) == hit)
		return;
	HideCaptionTip(window);
	auto tip = CaptionTip(window);
	if (!tip)
	{
		tip = CreateWindowExW(WS_EX_TOPMOST, TOOLTIPS_CLASSW, nullptr, WS_POPUP | TTS_NOPREFIX | TTS_ALWAYSTIP,
		                      CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, window, nullptr,
		                      GetModuleHandleW(nullptr), nullptr);
		if (!tip)
			return;
		SetPropW(window, L"LutCaptionTooltip", tip);
		StyleTooltip(tip);
		TOOLINFOW item { sizeof(item) };
		item.uFlags = TTF_TRACK | TTF_ABSOLUTE;
		item.hwnd = window;
		item.uId = 1;
		item.lpszText = const_cast<wchar_t*>(text);
		SendMessageW(tip, TTM_ADDTOOLW, 0, reinterpret_cast<LPARAM>(&item));
	}
	SetPropW(window, L"LutCaptionTipHit", reinterpret_cast<HANDLE>(static_cast<INT_PTR>(hit)));
	SetTimer(window, TIMER_CAPTION_TIP, GetDoubleClickTime(), nullptr);
}

void ShowCaptionTip(HWND window)
{
	KillTimer(window, TIMER_CAPTION_TIP);
	const auto hit = static_cast<int>(reinterpret_cast<INT_PTR>(GetPropW(window, L"LutCaptionTipHit")));
	const auto text = CaptionTooltip(window, hit);
	auto tip = CaptionTip(window);
	if (!tip || !text || !IsWindowVisible(window))
		return;
	TOOLINFOW item { sizeof(item) };
	item.hwnd = window;
	item.uId = 1;
	item.lpszText = const_cast<wchar_t*>(text);
	SendMessageW(tip, TTM_UPDATETIPTEXTW, 0, reinterpret_cast<LPARAM>(&item));
	StyleTooltip(tip);
	POINT cursor {};
	GetCursorPos(&cursor);
	SendMessageW(tip, TTM_TRACKPOSITION, 0,
	             MAKELPARAM(cursor.x + ScaleForDpi(window, 8), cursor.y + ScaleForDpi(window, 18)));
	SendMessageW(tip, TTM_TRACKACTIVATE, TRUE, reinterpret_cast<LPARAM>(&item));
}

void PopupBorder(HWND list, HDC supplied = nullptr)
{
	RECT rect {};
	GetWindowRect(list, &rect);
	OffsetRect(&rect, -rect.left, -rect.top);
	HDC dc = supplied ? supplied : GetWindowDC(list);
	if (!dc)
		return;
	// Use the same silhouette as the window region, rather than mixing a
	// native rectangular frame with a differently rasterized rounded stroke.
	HRGN edge = CreateRectRgn(0, 0, 0, 0);
	if (GetWindowRgn(list, edge) == ERROR)
	{
		DeleteObject(edge);
		const int diameter = ScaleForDpi(list, style::CORNER_RADIUS * 2);
		edge = CreateRoundRectRgn(0, 0, rect.right + 1, rect.bottom + 1, diameter, diameter);
	}
	if (edge)
	{
		HBRUSH ink = CreateSolidBrush(THEME_FOCUS_BORDER);
		FrameRgn(dc, edge, ink, 1, 1);
		DeleteObject(ink);
		DeleteObject(edge);
	}
	if (!supplied)
		ReleaseDC(list, dc);
}

LRESULT CALLBACK PopupListProc(HWND list, UINT message, WPARAM w_param, LPARAM l_param, UINT_PTR, DWORD_PTR)
{
	if (message == WM_NCDESTROY)
	{
		RemoveWindowSubclass(list, PopupListProc, 4);
		return DefSubclassProc(list, message, w_param, l_param);
	}
	if (message == WM_NCPAINT)
	{
		const auto result = DefSubclassProc(list, message, w_param, l_param);
		PopupBorder(list);
		return result;
	}
	const bool track_selection = message == WM_MOUSEMOVE || message == WM_KEYDOWN || message == WM_LBUTTONDOWN ||
	                             message == WM_LBUTTONUP || message == WM_VSCROLL;
	const auto selection = track_selection ? DefSubclassProc(list, LB_GETCURSEL, 0, 0) : LB_ERR;
	int saved = 0;
	if (message == WM_PRINT)
	{
		HDC dc = reinterpret_cast<HDC>(w_param);
		saved = SaveDC(dc);
		HRGN shape = CreateRectRgn(0, 0, 0, 0);
		if (GetWindowRgn(list, shape) != ERROR)
		{
			POINT origin {};
			GetViewportOrgEx(dc, &origin);
			OffsetRgn(shape, origin.x, origin.y);
			ExtSelectClipRgn(dc, shape, RGN_AND);
		}
		DeleteObject(shape);
	}
	const auto result = DefSubclassProc(list, message, w_param, l_param);
	if (message == WM_WINDOWPOSCHANGED || message == WM_SHOWWINDOW || message == WM_DPICHANGED)
		RoundBounds(list);
	// Native item drawing reaches into the rounded corners. Restore the
	// complete border after painting or changing the selected row.
	if (message == WM_PAINT || message == WM_NCACTIVATE || message == LB_SETCURSEL ||
	    (track_selection && selection != DefSubclassProc(list, LB_GETCURSEL, 0, 0)))
		PopupBorder(list);
	if (message == WM_PRINT && (l_param & PRF_NONCLIENT))
		PopupBorder(list, reinterpret_cast<HDC>(w_param));
	if (saved)
		RestoreDC(reinterpret_cast<HDC>(w_param), saved);
	return result;
}

// Keep native edit behavior (selection, caret and accessibility) and draw only
// its frame. Removing the native square frame avoids a second border.
bool SingleLineEdit(HWND window)
{
	wchar_t class_name[32] {};
	GetClassNameW(window, class_name, 32);
	return wcscmp(class_name, L"Edit") == 0 && !(GetWindowLongPtrW(window, GWL_STYLE) & ES_MULTILINE);
}

RECT InputBounds(HWND window)
{
	RECT rect {};
	GetWindowRect(window, &rect);
	return { 0, 0, rect.right - rect.left, rect.bottom - rect.top };
}

POINT InputOrigin(HWND window)
{
	RECT outer {};
	GetWindowRect(window, &outer);
	POINT origin {};
	ClientToScreen(window, &origin);
	return { origin.x - outer.left, origin.y - outer.top };
}

void PaintInputFrame(HWND window)
{
	HDC dc = GetWindowDC(window);
	if (!dc)
		return;
	const auto bounds = InputBounds(window);
	const auto origin = InputOrigin(window);
	RECT client {};
	GetClientRect(window, &client);
	const int saved = SaveDC(dc);
	ExcludeClipRect(dc, origin.x, origin.y, origin.x + client.right, origin.y + client.bottom);
	FillRect(dc, &bounds, surface.value);
	RestoreDC(dc, saved);
	DrawFocusOutline(window, dc, bounds);
	ReleaseDC(window, dc);
}

LRESULT CALLBACK InputControlProc(HWND window, UINT message, WPARAM w_param, LPARAM l_param, UINT_PTR, DWORD_PTR)
{
	if (message == WM_NCDESTROY)
	{
		RemovePropW(window, L"LutThemeHover");
		RemoveWindowSubclass(window, InputControlProc, 5);
		return DefSubclassProc(window, message, w_param, l_param);
	}
	const bool centered = SingleLineEdit(window);
	bool hover_changed = false;
	if (centered && message == WM_NCHITTEST)
		return HTCLIENT;
	if (centered && message == WM_NCCALCSIZE)
	{
		const auto result = DefSubclassProc(window, message, w_param, l_param);
		auto rect =
			w_param ? &reinterpret_cast<NCCALCSIZE_PARAMS*>(l_param)->rgrc[0] : reinterpret_cast<RECT*>(l_param);
		HDC dc = GetDC(window);
		if (!dc)
			return result;
		auto old = SelectObject(dc, reinterpret_cast<HFONT>(SendMessageW(window, WM_GETFONT, 0, 0)));
		TEXTMETRICW metrics {};
		GetTextMetricsW(dc, &metrics);
		SelectObject(dc, old);
		ReleaseDC(window, dc);
		const int spare = std::max(0, static_cast<int>(rect->bottom - rect->top - metrics.tmHeight));
		rect->top += spare / 2;
		rect->bottom -= spare - spare / 2;
		return result;
	}
	if (centered && message == WM_NCPAINT)
	{
		PaintInputFrame(window);
		return 0;
	}
	if (centered && message == WM_PRINT)
	{
		const auto bounds = InputBounds(window);
		HDC dc = reinterpret_cast<HDC>(w_param);
		const int saved = SaveDC(dc);
		IntersectClipRect(dc, 0, 0, bounds.right, bounds.bottom);
		FillRect(dc, &bounds, surface.value);
		const auto origin = InputOrigin(window);
		RECT client {};
		GetClientRect(window, &client);
		const int content = SaveDC(dc);
		IntersectClipRect(dc, origin.x, origin.y, origin.x + client.right, origin.y + client.bottom);
		OffsetViewportOrgEx(dc, origin.x, origin.y, nullptr);
		SelectObject(dc, reinterpret_cast<HFONT>(SendMessageW(window, WM_GETFONT, 0, 0)));
		DefSubclassProc(window, WM_PRINTCLIENT, w_param, PRF_CLIENT);
		RestoreDC(dc, content);
		DrawFocusOutline(window, dc, bounds);
		RestoreDC(dc, saved);
		return 0;
	}
	if (message == WM_MOUSEMOVE && !GetPropW(window, L"LutThemeHover"))
	{
		hover_changed = true;
		SetPropW(window, L"LutThemeHover", reinterpret_cast<HANDLE>(1));
		TRACKMOUSEEVENT track { sizeof(track), TME_LEAVE, window, 0 };
		TrackMouseEvent(&track);
		InvalidateRect(window, nullptr, FALSE);
	}
	if (message == WM_MOUSELEAVE)
	{
		hover_changed = true;
		RemovePropW(window, L"LutThemeHover");
		InvalidateRect(window, nullptr, FALSE);
	}
	const auto result = DefSubclassProc(window, message, w_param, l_param);
	if (centered && message == WM_SETFONT)
		SetWindowPos(window, nullptr, 0, 0, 0, 0,
		             SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
	if (message == WM_SIZE || message == WM_DPICHANGED)
		RoundBounds(window);
	if (message == WM_SETFOCUS || message == WM_KILLFOCUS || message == WM_ENABLE)
		InvalidateRect(window, nullptr, FALSE);
	if (centered)
	{
		if (message == WM_PAINT || message == WM_SETFOCUS || message == WM_KILLFOCUS || message == WM_ENABLE ||
		    hover_changed)
			PaintInputFrame(window);
		return result;
	}
	if (message == WM_PAINT || message == WM_PRINT || message == WM_PRINTCLIENT)
	{
		HDC dc = message == WM_PAINT ? GetDC(window) : reinterpret_cast<HDC>(w_param);
		RECT rect {};
		GetClientRect(window, &rect);
		if (dc)
			DrawFocusOutline(window, dc, rect);
		if (message == WM_PAINT && dc)
			ReleaseDC(window, dc);
	}
	return result;
}

int CaptionHitTest(HWND window, POINT point)
{
	RECT rect {};
	GetWindowRect(window, &rect);
	point.x -= rect.left;
	point.y -= rect.top;
	if (point.y < 0 || point.y >= ScaleForDpi(window, 36))
		return HTCLIENT;
	if (point.x >= rect.right - rect.left - ScaleForDpi(window, 44))
		return HTCLOSE;
	if ((GetWindowLongPtrW(window, GWL_STYLE) & WS_MINIMIZEBOX) &&
	    point.x >= rect.right - rect.left - ScaleForDpi(window, 88))
		return HTMINBUTTON;
	if (GetCaptionActions(window))
		for (int i = 0; i < 3; ++i)
		{
			const auto button = ActionRect(window, rect.right - rect.left, i);
			if (PtInRect(&button, point))
				return 100 + i;
		}
	if (GetPropW(window, L"LutPowerCommand"))
	{
		const auto button = PrimaryRect(window, rect.right - rect.left);
		if (PtInRect(&button, point))
			return HTHELP;
	}
	if (point.x < ScaleForDpi(window, 34))
		return HTSYSMENU;
	return HTCAPTION;
}

void PaintCaption(HWND window, HDC dc)
{
	RECT outer {};
	GetWindowRect(window, &outer);
	RECT cap { 0, 0, outer.right - outer.left, ScaleForDpi(window, 36) };
	FillRect(dc, &cap, bg.value);
	SetBkMode(dc, TRANSPARENT);
	auto font = reinterpret_cast<HFONT>(GetPropW(window, L"LutCaptionFont"));
	const auto old_font = SelectObject(dc, font ? font : GetStockObject(DEFAULT_GUI_FONT));
	SetTextColor(dc, GetForegroundWindow() == window ? THEME_TEXT : THEME_MUTED);
	const auto icon = reinterpret_cast<HICON>(SendMessageW(window, WM_GETICON, ICON_SMALL, 0));
	if (icon)
		DrawIconEx(dc, ScaleForDpi(window, 13), ScaleForDpi(window, 10), icon, ScaleForDpi(window, 16),
		           ScaleForDpi(window, 16), 0, nullptr, DI_NORMAL);
	wchar_t text[1024] {};
	GetWindowTextW(window, text, static_cast<int>(std::size(text)));
	RECT label { ScaleForDpi(window, 38), 0,
		         cap.right - ScaleForDpi(window, (GetWindowLongPtrW(window, GWL_STYLE) & WS_MINIMIZEBOX) ? 98 : 54),
		         cap.bottom };
	if (GetPropW(window, L"LutPowerCommand"))
		label.right = cap.right - ScaleForDpi(window, GetPropW(window, L"LutCaptionSettings") ? 150 : 194);
	if (GetCaptionActions(window))
		label.right = cap.right - ScaleForDpi(window, 270);
	DrawTextW(dc, text, -1, &label, DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX);
	const auto hover = reinterpret_cast<INT_PTR>(GetPropW(window, L"LutCaptionHover"));
	if (auto list = GetCaptionActions(window))
		for (int i = 0; i < 3; ++i)
		{
			const auto& entry = (*list)[i];
			DrawGlyph(window, dc, ActionRect(window, cap.right, i),
			          i == 0   ? Glyph::Sun
			          : i == 1 ? Glyph::Diamond
			                   : Glyph::Power,
			          entry.on, entry.enabled, hover == 100 + i, false, THEME_BACKGROUND);
		}
	if (GetPropW(window, L"LutPowerCommand"))
	{
		const auto state = reinterpret_cast<INT_PTR>(GetPropW(window, L"LutPowerState")) - 1;
		const bool on = (state & 1) != 0, enabled = (state & 2) != 0;
		const bool settings = GetPropW(window, L"LutCaptionSettings") != nullptr;
		RECT button = PrimaryRect(window, cap.right);
		const COLORREF ink = !enabled ? THEME_BORDER : on ? THEME_ACCENT : THEME_TEXT;
		const bool pressed = GetPropW(window, L"LutCaptionPressed") == reinterpret_cast<HANDLE>(HTHELP);
		if (settings)
		{
			DrawGlyph(window, dc, button, Glyph::Settings, on, enabled, hover == HTHELP || pressed, false,
			          THEME_BACKGROUND);
		}
		else
		{
			FillRoundedRect(dc, button,
			                enabled && pressed           ? RGB(57, 80, 94)
			                : enabled && hover == HTHELP ? RGB(66, 71, 79)
			                                             : THEME_SURFACE,
			                on ? THEME_ACCENT : THEME_BORDER, ScaleForDpi(window, 6));
			const int cx = settings ? (button.left + button.right) / 2 : button.left + ScaleForDpi(window, 20),
					  cy = (button.top + button.bottom) / 2, r = ScaleForDpi(window, 9);
			HPEN pen = CreatePen(PS_SOLID, ScaleForDpi(window, 2), ink);
			const auto previous = SelectObject(dc, pen);
			Arc(dc, cx - r, cy - r, cx + r, cy + r, cx - r, cy - r, cx + r, cy - r);
			MoveToEx(dc, cx, cy - ScaleForDpi(window, 11), nullptr);
			LineTo(dc, cx, cy - ScaleForDpi(window, 1));
			SelectObject(dc, previous);
			DeleteObject(pen);
			RECT power_label = button;
			power_label.left += ScaleForDpi(window, 37);
			SetTextColor(dc, ink);
			DrawTextW(dc, on ? L"ON" : L"OFF", -1, &power_label, DT_SINGLELINE | DT_VCENTER | DT_CENTER | DT_NOPREFIX);
		}
	}
	for (int hit : { HTMINBUTTON, HTCLOSE })
	{
		if (hit == HTMINBUTTON && !(GetWindowLongPtrW(window, GWL_STYLE) & WS_MINIMIZEBOX))
			continue;
		RECT button { cap.right - ScaleForDpi(window, hit == HTCLOSE ? 44 : 88), ScaleForDpi(window, 4),
			          cap.right - ScaleForDpi(window, hit == HTCLOSE ? 5 : 49), cap.bottom - ScaleForDpi(window, 4) };
		if (hover == hit)
			FillRoundedRect(dc, button, hit == HTCLOSE ? THEME_CLOSE_RED : THEME_SURFACE,
			                hit == HTCLOSE ? THEME_CLOSE_RED : THEME_SURFACE, ScaleForDpi(window, 4));
		const int cx = (button.left + button.right) / 2, cy = (button.top + button.bottom) / 2,
				  arm = ScaleForDpi(window, 5);
		HPEN pen = CreatePen(PS_SOLID, ScaleForDpi(window, 1),
		                     hit == HTCLOSE && hover != HTCLOSE ? THEME_CLOSE_RED : THEME_TEXT);
		const auto old_pen = SelectObject(dc, pen);
		if (hit == HTCLOSE)
		{
			MoveToEx(dc, cx - arm, cy - arm, nullptr);
			LineTo(dc, cx + arm, cy + arm);
			MoveToEx(dc, cx + arm, cy - arm, nullptr);
			LineTo(dc, cx - arm, cy + arm);
		}
		else
		{
			MoveToEx(dc, cx - arm, cy + ScaleForDpi(window, 3), nullptr);
			LineTo(dc, cx + arm, cy + ScaleForDpi(window, 3));
		}
		SelectObject(dc, old_pen);
		DeleteObject(pen);
	}
	HPEN pen = CreatePen(PS_SOLID, 1, RGB(60, 64, 71));
	const auto old_pen = SelectObject(dc, pen);
	const auto old_brush = SelectObject(dc, GetStockObject(NULL_BRUSH));
	RoundRect(dc, 0, 0, cap.right, outer.bottom - outer.top, ScaleForDpi(window, 16), ScaleForDpi(window, 16));
	SelectObject(dc, old_brush);
	SelectObject(dc, old_pen);
	DeleteObject(pen);
	SelectObject(dc, old_font);
}

void RepaintCaption(HWND window)
{
	HDC dc = GetWindowDC(window);
	if (!dc)
		return;
	RECT outer {};
	GetWindowRect(window, &outer);
	const int width = outer.right - outer.left, height = ScaleForDpi(window, 36);
	HDC buffer = CreateCompatibleDC(dc);
	HBITMAP bitmap = CreateCompatibleBitmap(dc, width, height);
	if (buffer && bitmap)
	{
		const auto old = SelectObject(buffer, bitmap);
		PaintCaption(window, buffer);
		BitBlt(dc, 0, 0, width, height, buffer, 0, 0, SRCCOPY);
		SelectObject(buffer, old);
	}
	else
		PaintCaption(window, dc);
	if (bitmap)
		DeleteObject(bitmap);
	if (buffer)
		DeleteDC(buffer);
	// Preserve the frame outside the short caption buffer without clearing it.
	HPEN pen = CreatePen(PS_SOLID, 1, RGB(60, 64, 71));
	auto old_pen = SelectObject(dc, pen), old_brush = SelectObject(dc, GetStockObject(NULL_BRUSH));
	RoundRect(dc, 0, 0, width, outer.bottom - outer.top, ScaleForDpi(window, 16), ScaleForDpi(window, 16));
	SelectObject(dc, old_brush);
	SelectObject(dc, old_pen);
	DeleteObject(pen);
	ReleaseDC(window, dc);
}

void PaintWindowToDc(HWND window, HDC dc, LPARAM flags)
{
	RECT outer {};
	GetWindowRect(window, &outer);
	RECT whole { 0, 0, outer.right - outer.left, outer.bottom - outer.top };
	FillRect(dc, &whole, bg.value);
	if (flags & PRF_NONCLIENT)
		PaintCaption(window, dc);
	POINT origin {};
	ClientToScreen(window, &origin);
	origin.x -= outer.left;
	origin.y -= outer.top;
	const int saved = SaveDC(dc);
	OffsetViewportOrgEx(dc, origin.x, origin.y, nullptr);
	// Scrolled children can have negative coordinates. Keep their print path
	// inside the client area, matching native window clipping at runtime.
	RECT client_clip {};
	GetClientRect(window, &client_clip);
	IntersectClipRect(dc, client_clip.left, client_clip.top, client_clip.right, client_clip.bottom);
	if (flags & PRF_CLIENT)
	{
		RECT client {};
		GetClientRect(window, &client);
		FillRect(dc, &client, bg.value);
		SendMessageW(window, WM_PRINTCLIENT, reinterpret_cast<WPARAM>(dc), PRF_CLIENT);
	}
	if (flags & PRF_CHILDREN)
		for (HWND child = GetWindow(window, GW_CHILD); child; child = GetWindow(child, GW_HWNDNEXT))
		{
			if (!(GetWindowLongPtrW(child, GWL_STYLE) & WS_VISIBLE))
				continue;
			RECT rect {};
			GetWindowRect(child, &rect);
			POINT at { rect.left, rect.top };
			ScreenToClient(window, &at);
			const int child_saved = SaveDC(dc);
			OffsetViewportOrgEx(dc, at.x, at.y, nullptr);
			IntersectClipRect(dc, 0, 0, rect.right - rect.left, rect.bottom - rect.top);
			HRGN shape = CreateRectRgn(0, 0, 0, 0);
			if (GetWindowRgn(child, shape) != ERROR)
			{
				POINT viewport {};
				GetViewportOrgEx(dc, &viewport);
				OffsetRgn(shape, viewport.x, viewport.y);
				ExtSelectClipRgn(dc, shape, RGN_AND);
			}
			DeleteObject(shape);
			SendMessageW(child, WM_PRINT, reinterpret_cast<WPARAM>(dc),
			             PRF_NONCLIENT | PRF_CLIENT | PRF_CHILDREN | PRF_ERASEBKGND);
			RestoreDC(dc, child_saved);
		}
	RestoreDC(dc, saved);
}

void PaintControl(HWND window, HDC dc)
{
	RECT rect {};
	GetClientRect(window, &rect);
	// WM_PRINTCLIENT can supply a shared parent DC. Keep borders and focus
	// outlines inside this control just as WM_PAINT's window DC does.
	struct Clip
	{
		HDC dc;
		int saved;
		Clip(HDC value, const RECT& r) : dc(value), saved(SaveDC(value))
		{
			if (saved)
				IntersectClipRect(dc, r.left, r.top, r.right, r.bottom);
		}
		~Clip()
		{
			if (saved)
				RestoreDC(dc, saved);
		}
	} clip(dc, rect);
	FillRect(dc, &rect, bg.value);
	auto font = reinterpret_cast<HFONT>(SendMessageW(window, WM_GETFONT, 0, 0));
	const auto old_font = SelectObject(dc, font);
	SetBkMode(dc, TRANSPARENT);
	const bool enabled = IsWindowEnabled(window) != FALSE, focused = GetFocus() == window;
	const bool hover = GetPropW(window, L"LutThemeHover") != nullptr;
	SetTextColor(dc, enabled ? THEME_TEXT : THEME_MUTED);
	if (GetPropW(window, L"LutToggle"))
	{
		const bool on = SendMessageW(window, BM_GETCHECK, 0, 0) == BST_CHECKED;
		const bool powered = GetPropW(window, L"LutToggleUnpowered") == nullptr;
		PaintToggle(window, dc, rect, on, powered, hover || focused);
		if (focused && !(SendMessageW(window, WM_QUERYUISTATE, 0, 0) & UISF_HIDEFOCUS))
		{
			RECT focus = rect;
			InflateRect(&focus, -1, -1);
			DrawFocusRect(dc, &focus);
		}
		SelectObject(dc, old_font);
		return;
	}
	if (const auto glyph = static_cast<Glyph>(reinterpret_cast<INT_PTR>(GetPropW(window, L"LutMonitorGlyph")));
	    glyph != Glyph::None)
	{
		const bool captioned = IsToolbarGlyph(glyph);
		RECT icon = rect;
		if (captioned)
			icon.bottom = std::min(rect.bottom, rect.top + ScaleForDpi(window, 28));
		DrawGlyph(window, dc, icon, glyph, GetPropW(window, L"LutMonitorOn") != nullptr, enabled, hover, focused,
		          captioned ? THEME_BACKGROUND : RGB(7, 9, 12));
		if (captioned && rect.bottom > icon.bottom)
		{
			const wchar_t* captions[] { L"Save", L"Save as", L"Export", L"Reset", L"Revert" };
			LOGFONTW label_style {};
			GetObjectW(font, sizeof(label_style), &label_style);
			label_style.lfHeight = -ScaleForDpi(window, style::CAPTION_TEXT_SIZE);
			HFONT label_font = CreateFontIndirectW(&label_style);
			SelectObject(dc, label_font ? label_font : font);
			RECT label = rect;
			label.top = icon.bottom;
			SetTextColor(dc, enabled ? THEME_MUTED : THEME_BORDER);
			DrawTextW(dc, captions[static_cast<int>(glyph) - static_cast<int>(Glyph::Save)], -1, &label,
			          DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
			SelectObject(dc, font);
			if (label_font)
				DeleteObject(label_font);
		}
		SelectObject(dc, old_font);
		return;
	}
	wchar_t class_name[40] {};
	GetClassNameW(window, class_name, static_cast<int>(std::size(class_name)));
	if (wcscmp(class_name, HOTKEY_CLASSW) == 0)
	{
		FillRoundedRect(dc, rect, THEME_SURFACE, (focused || hover) ? THEME_FOCUS_BORDER : THEME_BORDER,
		                ScaleForDpi(window, 4));
		const WORD shortcut = static_cast<WORD>(SendMessageW(window, HKM_GETHOTKEY, 0, 0));
		std::wstring text;
		if (HIBYTE(shortcut) & HOTKEYF_CONTROL)
			text += L"Ctrl+";
		if (HIBYTE(shortcut) & HOTKEYF_ALT)
			text += L"Alt+";
		if (HIBYTE(shortcut) & HOTKEYF_SHIFT)
			text += L"Shift+";
		if (LOBYTE(shortcut))
		{
			wchar_t key[128] {};
			LONG scan = static_cast<LONG>(MapVirtualKeyW(LOBYTE(shortcut), MAPVK_VK_TO_VSC) << 16);
			if (HIBYTE(shortcut) & HOTKEYF_EXT)
				scan |= 1 << 24;
			GetKeyNameTextW(scan, key, 128);
			text += key;
		}
		else
			text = L"Press shortcut";
		RECT label = rect;
		label.left += ScaleForDpi(window, 8);
		DrawTextW(dc, text.c_str(), -1, &label, DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX);
	}
	else if (wcscmp(class_name, L"ComboBox") == 0)
	{
		FillRoundedRect(dc, rect, enabled ? THEME_SURFACE : THEME_BACKGROUND,
		                (focused || hover) ? THEME_FOCUS_BORDER : THEME_BORDER, ScaleForDpi(window, 4));
		wchar_t text[1024] {};
		GetWindowTextW(window, text, static_cast<int>(std::size(text)));
		RECT label { rect.left + ScaleForDpi(window, 9), rect.top, rect.right - ScaleForDpi(window, 25), rect.bottom };
		DrawTextW(dc, text, -1, &label, DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX);
		const int x = rect.right - ScaleForDpi(window, 13), y = (rect.top + rect.bottom) / 2;
		HPEN pen = CreatePen(PS_SOLID, ScaleForDpi(window, 1), enabled ? THEME_TEXT : THEME_MUTED);
		const auto old_pen = SelectObject(dc, pen);
		MoveToEx(dc, x - ScaleForDpi(window, 4), y - ScaleForDpi(window, 2), nullptr);
		LineTo(dc, x, y + ScaleForDpi(window, 2));
		LineTo(dc, x + ScaleForDpi(window, 4), y - ScaleForDpi(window, 2));
		SelectObject(dc, old_pen);
		DeleteObject(pen);
	}
	else
	{
		const auto style = GetWindowLongPtrW(window, GWL_STYLE) & BS_TYPEMASK;
		const bool radio = style == BS_RADIOBUTTON || style == BS_AUTORADIOBUTTON;
		const bool check = radio || style == BS_CHECKBOX || style == BS_AUTOCHECKBOX;
		wchar_t text[1024] {};
		GetWindowTextW(window, text, static_cast<int>(std::size(text)));
		RECT label = rect;
		if (check)
		{
			const int size = ScaleForDpi(window, 14), top = (rect.bottom - size) / 2;
			RECT box { rect.left + 1, top, rect.left + 1 + size, top + size };
			const bool checked = SendMessageW(window, BM_GETCHECK, 0, 0) == BST_CHECKED;
			const COLORREF ink = enabled ? THEME_ACCENT : THEME_MUTED;
			if (radio)
			{
				HBRUSH brush = CreateSolidBrush(THEME_SURFACE);
				HPEN pen = CreatePen(PS_SOLID, 1, (focused || hover) ? THEME_FOCUS_BORDER : THEME_BORDER);
				auto old_brush = SelectObject(dc, brush), old_pen = SelectObject(dc, pen);
				Ellipse(dc, box.left, box.top, box.right, box.bottom);
				SelectObject(dc, old_brush);
				SelectObject(dc, old_pen);
				DeleteObject(brush);
				DeleteObject(pen);
				if (checked)
				{
					InflateRect(&box, -ScaleForDpi(window, 4), -ScaleForDpi(window, 4));
					brush = CreateSolidBrush(ink);
					old_brush = SelectObject(dc, brush);
					old_pen = SelectObject(dc, GetStockObject(NULL_PEN));
					Ellipse(dc, box.left, box.top, box.right, box.bottom);
					SelectObject(dc, old_brush);
					SelectObject(dc, old_pen);
					DeleteObject(brush);
				}
			}
			else
			{
				FillRoundedRect(dc, box, checked ? ink : THEME_SURFACE,
				                (focused || hover) ? THEME_FOCUS_BORDER : THEME_BORDER, ScaleForDpi(window, 2));
				if (checked)
				{
					HPEN pen = CreatePen(PS_SOLID, ScaleForDpi(window, 2), THEME_BACKGROUND);
					const auto old_pen = SelectObject(dc, pen);
					MoveToEx(dc, box.left + ScaleForDpi(window, 3), box.top + ScaleForDpi(window, 7), nullptr);
					LineTo(dc, box.left + ScaleForDpi(window, 6), box.top + ScaleForDpi(window, 10));
					LineTo(dc, box.left + ScaleForDpi(window, 11), box.top + ScaleForDpi(window, 4));
					SelectObject(dc, old_pen);
					DeleteObject(pen);
				}
			}
			label.left += size + ScaleForDpi(window, 7);
			DrawTextW(dc, text, -1, &label, DT_SINGLELINE | DT_VCENTER);
		}
		else
		{
			const auto fill = enabled ? THEME_SURFACE : THEME_BACKGROUND;
			FillRoundedRect(dc, rect, fill, (focused || hover) ? THEME_FOCUS_BORDER : THEME_BORDER,
			                ScaleForDpi(window, 4));
			DrawTextW(dc, text, -1, &label, DT_SINGLELINE | DT_CENTER | DT_VCENTER);
		}
		if (focused && !(SendMessageW(window, WM_QUERYUISTATE, 0, 0) & UISF_HIDEFOCUS))
		{
			RECT focus = label;
			InflateRect(&focus, -ScaleForDpi(window, 4), -ScaleForDpi(window, 4));
			DrawFocusRect(dc, &focus);
		}
	}
	SelectObject(dc, old_font);
}

LRESULT CALLBACK SliderPaintProc(HWND window, UINT message, WPARAM w_param, LPARAM l_param, UINT_PTR, DWORD_PTR)
{
	if (message == WM_ERASEBKGND)
		return 1;
	if (message == WM_PAINT)
	{
		PAINTSTRUCT paint {};
		HDC target = BeginPaint(window, &paint);
		RECT bounds {};
		GetClientRect(window, &bounds);
		// Native custom draw clears the track before drawing its thumb.
		// Present the completed slider together, with only a transient dirty buffer.
		paint::Buffer frame(target, bounds);
		if (frame && frame.IsBuffered())
			DefSubclassProc(window, WM_PRINTCLIENT, reinterpret_cast<WPARAM>(frame.DeviceContext()), PRF_CLIENT);
		frame.Finish();
		EndPaint(window, &paint);
		return 0;
	}
	if (message == WM_NCDESTROY)
		RemoveWindowSubclass(window, SliderPaintProc, 82);
	return DefSubclassProc(window, message, w_param, l_param);
}

LRESULT CALLBACK ThemedControlProc(HWND window, UINT message, WPARAM w_param, LPARAM l_param, UINT_PTR, DWORD_PTR)
{
	switch (message)
	{
		case WM_SIZE:
		{
			wchar_t cls[40] {};
			GetClassNameW(window, cls, 40);
			if (wcscmp(cls, HOTKEY_CLASSW) == 0)
				RoundBounds(window);
			break;
		}
		case WM_SETCURSOR:
			if (LOWORD(l_param) == HTCLIENT && IsWindowEnabled(window))
			{
				wchar_t class_name[40] {};
				GetClassNameW(window, class_name, 40);
				if (wcscmp(class_name, L"Button") == 0 || wcscmp(class_name, L"ComboBox") == 0)
				{
					SetCursor(LoadCursorW(nullptr, IDC_HAND));
					return TRUE;
				}
			}
			break;
		case WM_PAINT:
		{
			PAINTSTRUCT paint {};
			auto dc = BeginPaint(window, &paint);
			RECT rect {};
			GetClientRect(window, &rect);
			{
				paint::Buffer buffer(dc, rect);
				if (buffer)
					PaintControl(window, buffer.DeviceContext());
			}
			EndPaint(window, &paint);
			return 0;
		}
		case WM_PRINT:
		case WM_PRINTCLIENT:
			PaintControl(window, reinterpret_cast<HDC>(w_param));
			return 0;
		case WM_ERASEBKGND:
			return 1;
		case WM_MOUSEMOVE:
			if (!GetPropW(window, L"LutThemeHover"))
			{
				SetPropW(window, L"LutThemeHover", reinterpret_cast<HANDLE>(1));
				TRACKMOUSEEVENT tracking { sizeof(tracking), TME_LEAVE, window, 0 };
				TrackMouseEvent(&tracking);
				InvalidateRect(window, nullptr, FALSE);
			}
			break;
		case WM_MOUSELEAVE:
			RemovePropW(window, L"LutThemeHover");
			InvalidateRect(window, nullptr, FALSE);
			break;
		case WM_NCDESTROY:
			RemovePropW(window, L"LutThemeHover");
			RemovePropW(window, L"LutMonitorGlyph");
			RemovePropW(window, L"LutMonitorOn");
			RemovePropW(window, L"LutToggle");
			RemovePropW(window, L"LutToggleUnpowered");
			RemoveWindowSubclass(window, ThemedControlProc, 1);
			break;
	}
	const auto result = DefSubclassProc(window, message, w_param, l_param);
	if (message == WM_SETFONT)
	{
		wchar_t class_name[32] {};
		GetClassNameW(window, class_name, 32);
		if (wcscmp(class_name, L"ComboBox") == 0)
		{
			SendMessageW(window, CB_SETITEMHEIGHT, static_cast<WPARAM>(-1), ScaleForDpi(window, 18));
			SendMessageW(window, CB_SETITEMHEIGHT, 0, ScaleForDpi(window, 18));
		}
	}
	if (message == BM_SETCHECK || message == BM_SETSTATE || message == CB_SETCURSEL || message == HKM_SETHOTKEY ||
	    message == WM_KEYDOWN || message == WM_KEYUP || message == WM_CHAR || message == WM_ENABLE ||
	    message == WM_SETTEXT || message == WM_SETFOCUS || message == WM_KILLFOCUS || message == WM_UPDATEUISTATE)
		InvalidateRect(window, nullptr, FALSE);
	return result;
}
}

namespace
{
struct HelpItem
{
	HWND control;
	UINT_PTR id;
	bool whole;
	std::wstring text;
};

struct HelpWindow
{
	HWND tip = nullptr;
	std::wstring family;
	UINT dpi = 0;
	std::vector<std::unique_ptr<HelpItem>> items;
};

void UpdateHelpFont(HWND owner, HelpWindow& state)
{
	const UINT dpi = GetDpiForWindow(owner);
	if (state.family == FontFamily() && state.dpi == dpi)
		return;
	StyleTooltip(state.tip);
	state.family = FontFamily();
	state.dpi = dpi;
	SendMessageW(state.tip, TTM_SETMAXTIPWIDTH, 0, ScaleForDpi(owner, 360));
}

LRESULT CALLBACK HelpOwnerProc(HWND window, UINT message, WPARAM w_param, LPARAM l_param, UINT_PTR, DWORD_PTR data)
{
	if (message == WM_NCDESTROY)
	{
		auto state = reinterpret_cast<HelpWindow*>(data);
		RemovePropW(window, L"MonitorMageHelp");
		if (IsWindow(state->tip))
			DestroyWindow(state->tip);
		RemoveWindowSubclass(window, HelpOwnerProc, 9);
		delete state;
	}
	return DefSubclassProc(window, message, w_param, l_param);
}

void SetHelpRegion(HWND control, UINT_PTR id, const RECT* region, const wchar_t* text)
{
	if (!control)
		return;
	const HWND owner = GetAncestor(control, GA_ROOT);
	auto state = reinterpret_cast<HelpWindow*>(GetPropW(owner, L"MonitorMageHelp"));
	if (!state)
	{
		auto created = std::make_unique<HelpWindow>();
		created->tip = CreateWindowExW(WS_EX_TOPMOST, TOOLTIPS_CLASSW, nullptr, WS_POPUP | TTS_ALWAYSTIP | TTS_NOPREFIX,
		                               0, 0, 0, 0, owner, nullptr, GetModuleHandleW(nullptr), nullptr);
		if (!created->tip)
			return;
		if (!SetWindowSubclass(owner, HelpOwnerProc, 9, reinterpret_cast<DWORD_PTR>(created.get())))
		{
			DestroyWindow(created->tip);
			return;
		}
		state = created.release();
		SetPropW(owner, L"MonitorMageHelp", state);
		StyleTooltip(state->tip);
		SendMessageW(state->tip, TTM_SETMAXTIPWIDTH, 0, ScaleForDpi(owner, 340));
		SendMessageW(state->tip, TTM_SETDELAYTIME, TTDT_AUTOPOP, 15000);
	}
	UpdateHelpFont(owner, *state);
	HelpItem* item = nullptr;
	for (const auto& candidate : state->items)
		if (candidate->control == control && candidate->id == id && candidate->whole == !region)
		{
			item = candidate.get();
			break;
		}
	TOOLINFOW tool { sizeof(tool) };
	tool.hwnd = region ? control : GetParent(control);
	tool.uId = region ? id : reinterpret_cast<UINT_PTR>(control);
	tool.uFlags = TTF_SUBCLASS | (region ? 0 : TTF_IDISHWND);
	if (region)
		tool.rect = *region;
	if (!item)
	{
		auto created = std::make_unique<HelpItem>(HelpItem { control, id, !region, text });
		item = created.get();
		state->items.push_back(std::move(created));
		tool.lpszText = item->text.data();
		if (!SendMessageW(state->tip, TTM_ADDTOOLW, 0, reinterpret_cast<LPARAM>(&tool)))
		{
			state->items.pop_back();
			return;
		}
	}
	else if (item->text != text)
	{
		item->text = text;
		tool.lpszText = item->text.data();
		SendMessageW(state->tip, TTM_UPDATETIPTEXTW, 0, reinterpret_cast<LPARAM>(&tool));
	}
	if (region)
		SendMessageW(state->tip, TTM_NEWTOOLRECTW, 0, reinterpret_cast<LPARAM>(&tool));
}
}

void SetHelpText(HWND control, const wchar_t* text)
{
	SetHelpRegion(control, 0, nullptr, text);
}

void HelpRegion(HWND control, UINT_PTR id, const RECT& rect, const wchar_t* text)
{
	SetHelpRegion(control, id, &rect, text);
}

const wchar_t* HelpTextForRegion(HWND control, UINT_PTR region)
{
	auto state = reinterpret_cast<HelpWindow*>(GetPropW(GetAncestor(control, GA_ROOT), L"MonitorMageHelp"));
	if (state)
		for (const auto& item : state->items)
			if (item->whole == !region && item->id == region && item->control == control)
				return item->text.c_str();
	return L"";
}

void FixedWindowSize(HWND window, int width, int height)
{
	auto size = reinterpret_cast<SIZE*>(GetPropW(window, L"MonitorMagePreferredSize"));
	if (!size)
	{
		size = new SIZE { width, height };
		if (!SetPropW(window, L"MonitorMagePreferredSize", size))
			delete size;
	}
	else
		*size = { width, height };
}

namespace
{
bool RepairedBounds(HWND window, RECT& current, RECT& target)
{
	const auto size = reinterpret_cast<SIZE*>(GetPropW(window, L"MonitorMagePreferredSize"));
	if (!size || IsIconic(window) || IsZoomed(window) || !GetWindowRect(window, &current))
		return false;
	const UINT dpi = GetDpiForWindow(window);
	RECT preferred { 0, 0, MulDiv(size->cx, dpi, 96), MulDiv(size->cy, dpi, 96) };
	AdjustThemedWindowRect(preferred, dpi);
	MONITORINFO monitor { sizeof(monitor) };
	if (!GetMonitorInfoW(MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST), &monitor))
		return false;
	target =
		FitWindowRect(current, { preferred.right - preferred.left, preferred.bottom - preferred.top }, monitor.rcWork);
	return true;
}
}

bool WindowNeedsSizeRepair(HWND window)
{
	RECT current {}, target {};
	return RepairedBounds(window, current, target) && !EqualRect(&current, &target);
}

void RepairWindowSize(HWND window)
{
	RECT current {}, target {};
	if (RepairedBounds(window, current, target) && !EqualRect(&current, &target))
		SetWindowPos(window, nullptr, target.left, target.top, target.right - target.left, target.bottom - target.top,
		             SWP_NOZORDER | SWP_NOACTIVATE);
	ApplyWindowFrameTheme(window);
}

HBRUSH BackgroundBrush()
{
	return bg.value;
}

const wchar_t* HdrTooltip(bool on, bool available)
{
	return !available ? L"Windows HDR unavailable" : on ? L"Turn Windows HDR off" : L"Turn Windows HDR on";
}

const wchar_t* CaptionTooltip(HWND window, int hit)
{
	if (hit == 100 && GetCaptionActions(window))
		return HdrTooltip((*GetCaptionActions(window))[0].on, (*GetCaptionActions(window))[0].enabled);
	if (hit == 101 && GetCaptionActions(window))
		return wording::LABEL_APPLICATION_PROFILES;
	if (hit == 102 && GetCaptionActions(window))
		return (*GetCaptionActions(window))[2].on ? L"Turn off (Alt+P)" : L"Turn on (Alt+P)";
	if (hit == HTHELP && GetPropW(window, L"LutPowerCommand"))
		return GetPropW(window, L"LutCaptionSettings") ? wording::LABEL_SETTINGS : L"Master power on/off";
	if (hit == HTMINBUTTON && (GetWindowLongPtrW(window, GWL_STYLE) & WS_MINIMIZEBOX))
		return L"Minimize";
	if (hit == HTCLOSE)
		return L"Close";
	if (hit == HTSYSMENU)
		return L"Window menu";
	return nullptr;
}

bool PreviewCaptionMenu(HWND window, std::function<bool(HWND)> render)
{
	return PreviewPopup(window, reinterpret_cast<HICON>(SendMessageW(window, WM_GETICON, ICON_SMALL, 0)),
	                    CaptionItems(window), std::move(render), true);
}

void SetFontFamily(const wchar_t* value)
{
	family = value;
}

const wchar_t* FontFamily()
{
	return family.c_str();
}

void RefreshCaptionFont(HWND window)
{
	if (auto help = reinterpret_cast<HelpWindow*>(GetPropW(window, L"MonitorMageHelp")))
		UpdateHelpFont(window, *help);
	HideCaptionTip(window);
	auto font = reinterpret_cast<HFONT>(RemovePropW(window, L"LutCaptionFont"));
	if (font)
		DeleteObject(font);
	SetPropW(window, L"LutCaptionFont", CreateUiFont(GetDpiForWindow(window)));
	RepaintCaption(window);
}

void SetCaptionPowerButton(HWND window, int command, bool on, bool enabled)
{
	const auto state = static_cast<INT_PTR>(1 + (on ? 1 : 0) + (enabled ? 2 : 0));
	if (GetPropW(window, L"LutPowerCommand") == reinterpret_cast<HANDLE>(static_cast<INT_PTR>(command)) &&
	    GetPropW(window, L"LutPowerState") == reinterpret_cast<HANDLE>(state))
		return;
	const bool first = !GetPropW(window, L"LutPowerCommand");
	SetPropW(window, L"LutPowerCommand", reinterpret_cast<HANDLE>(static_cast<INT_PTR>(command)));
	SetPropW(window, L"LutPowerState", reinterpret_cast<HANDLE>(state));
	// The standard system menu exposes the custom caption action to keyboard
	// users and accessibility clients as well as the visible pointer target.
	if (first)
	{
		HMENU menu = GetSystemMenu(window, FALSE);
		AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
		const auto label = GetPropW(window, L"LutCaptionSettings") ? std::wstring(wording::LABEL_SETTINGS) + L"\tAlt+S"
		                                                           : L"Power on/off\tAlt+P";
		AppendMenuW(menu, MF_STRING, 0x1F00, label.c_str());
	}
	EnableMenuItem(GetSystemMenu(window, FALSE), 0x1F00, MF_BYCOMMAND | (enabled ? MF_ENABLED : MF_GRAYED));
	RepaintCaption(window);
}

void SetCaptionSettingsButton(HWND window, int command, bool selected)
{
	SetPropW(window, L"LutCaptionSettings", reinterpret_cast<HANDLE>(1));
	SetCaptionPowerButton(window, command, selected, true);
}

void SetIconButton(HWND window, Glyph glyph, bool on, bool enabled)
{
	const auto value = reinterpret_cast<HANDLE>(static_cast<INT_PTR>(glyph));
	const bool changed =
		GetPropW(window, L"LutMonitorGlyph") != value || (GetPropW(window, L"LutMonitorOn") != nullptr) != on;
	SetPropW(window, L"LutMonitorGlyph", value);
	if (on)
		SetPropW(window, L"LutMonitorOn", reinterpret_cast<HANDLE>(1));
	else
		RemovePropW(window, L"LutMonitorOn");
	if ((IsWindowEnabled(window) != FALSE) != enabled)
		EnableWindow(window, enabled);
	if (changed)
		InvalidateRect(window, nullptr, FALSE);
}

void PaintMenuGlyph(HWND window, HDC dc, RECT rect, Glyph icon, bool on, bool enabled, COLORREF base)
{
	DrawGlyph(window, dc, rect, icon, on, enabled, false, false, base, true);
}

void ConfigureSlider(HWND window, int default_value, wchar_t thumb_letter)
{
	DWORD_PTR clicks = 0;
	if (!GetWindowSubclass(window, SliderResetProc, 11, &clicks))
	{
		auto state = std::make_unique<SliderClicks>();
		if (SetWindowSubclass(window, SliderResetProc, 11, reinterpret_cast<DWORD_PTR>(state.get())))
			state.release();
	}
	const auto marker = reinterpret_cast<HANDLE>(static_cast<INT_PTR>(default_value) + 32769);
	const auto letter = reinterpret_cast<HANDLE>(static_cast<INT_PTR>(thumb_letter));
	if (GetPropW(window, L"LutSliderDefault") == marker && GetPropW(window, L"LutSliderLetter") == letter)
		return;
	SetPropW(window, L"LutSliderDefault", marker);
	if (letter)
		SetPropW(window, L"LutSliderLetter", letter);
	else
		RemovePropW(window, L"LutSliderLetter");
	InvalidateRect(window, nullptr, FALSE);
}

void CaptionActions(HWND window, const std::array<CaptionAction, 3>& value)
{
	auto list = GetCaptionActions(window);
	bool changed = !list;
	if (!list)
	{
		list = new Actions;
		SetPropW(window, L"LutCaptionActions", list);
		const wchar_t* names[] { L"Windows HDR", wording::LABEL_APPLICATION_PROFILES, L"Master power" };
		for (int i = 0; i < 3; ++i)
			AppendMenuW(GetSystemMenu(window, FALSE), MF_STRING, 0x1F10 + i * 16, names[i]);
	}
	else
		for (int i = 0; i < 3; ++i)
			if ((*list)[i].command != value[i].command || (*list)[i].on != value[i].on ||
			    (*list)[i].enabled != value[i].enabled)
				changed = true;
	*list = value;
	if (changed)
	{
		for (int i = 0; i < 3; ++i)
		{
			EnableMenuItem(GetSystemMenu(window, FALSE), 0x1F10 + i * 16,
			               MF_BYCOMMAND | (value[i].enabled ? MF_ENABLED : MF_GRAYED));
			CheckMenuItem(GetSystemMenu(window, FALSE), 0x1F10 + i * 16,
			              MF_BYCOMMAND | (value[i].on ? MF_CHECKED : MF_UNCHECKED));
		}
		RepaintCaption(window);
	}
}

void PaintToggle(HWND window, HDC dc, RECT bounds, bool on, bool powered, bool highlighted, const style::Toggle& theme)
{
	const int width = ScaleForDpi(window, theme.width), height = ScaleForDpi(window, theme.height);
	RECT pill { bounds.left + (bounds.right - bounds.left - width) / 2,
		        bounds.top + (bounds.bottom - bounds.top - height) / 2, 0, 0 };
	pill.right = pill.left + width;
	pill.bottom = pill.top + height;
	const auto fill = on ? (powered ? theme.on_track : theme.unpowered_track) : theme.off_track;
	FillRoundedRect(dc, pill, fill,
	                highlighted     ? theme.highlight
	                : on && powered ? theme.on_track
	                                : theme.border,
	                height / 2);
	const int inset = ScaleForDpi(window, theme.inset), knob = ScaleForDpi(window, theme.knob),
			  x = on ? pill.right - inset - knob : pill.left + inset;
	HBRUSH brush = CreateSolidBrush(on ? theme.on_knob : theme.off_knob);
	auto old_brush = SelectObject(dc, brush), old_pen = SelectObject(dc, GetStockObject(NULL_PEN));
	Ellipse(dc, x, pill.top + inset, x + knob, pill.top + inset + knob);
	SelectObject(dc, old_pen);
	SelectObject(dc, old_brush);
	DeleteObject(brush);
}

void ToggleButton(HWND window, bool on, bool powered)
{
	SetPropW(window, L"LutToggle", reinterpret_cast<HANDLE>(1));
	const bool changed = (GetPropW(window, L"LutToggleUnpowered") == nullptr) != powered;
	if (powered)
		RemovePropW(window, L"LutToggleUnpowered");
	else
		SetPropW(window, L"LutToggleUnpowered", reinterpret_cast<HANDLE>(1));
	const auto check = on ? BST_CHECKED : BST_UNCHECKED;
	if (SendMessageW(window, BM_GETCHECK, 0, 0) != check)
		SendMessageW(window, BM_SETCHECK, check, 0);
	if (changed)
		InvalidateRect(window, nullptr, FALSE);
}

void SliderColour(HWND window, COLORREF colour)
{
	const auto value = reinterpret_cast<HANDLE>(static_cast<UINT_PTR>(colour) + 1);
	if (GetPropW(window, L"LutSliderColour") != value)
	{
		SetPropW(window, L"LutSliderColour", value);
		InvalidateRect(window, nullptr, FALSE);
	}
}

HFONT CreateUiFont(UINT dpi)
{
	return CreateFontW(-MulDiv(style::TEXT_SIZE, dpi, 96), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
	                   OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, family.c_str());
}

int ContentWidth(HWND window, HFONT font, int minimum, int maximum)
{
	if (!window)
		return minimum;
	wchar_t class_name[32] {};
	GetClassNameW(window, class_name, 32);
	const bool combo = wcscmp(class_name, L"ComboBox") == 0;
	HDC dc = GetDC(window);
	if (!dc)
		return minimum;
	auto old = SelectObject(dc, font);
	int widest = 0;
	const int count = combo ? static_cast<int>(SendMessageW(window, CB_GETCOUNT, 0, 0)) : 1;
	for (int i = 0; i < count; ++i)
	{
		const int length =
			combo ? static_cast<int>(SendMessageW(window, CB_GETLBTEXTLEN, i, 0)) : GetWindowTextLengthW(window);
		if (length < 0 || length > 32767)
			continue;
		std::wstring text(static_cast<size_t>(length) + 1, L'\0');
		if (combo)
			SendMessageW(window, CB_GETLBTEXT, i, reinterpret_cast<LPARAM>(text.data()));
		else
			GetWindowTextW(window, text.data(), length + 1);
		SIZE size {};
		GetTextExtentPoint32W(dc, text.c_str(), length, &size);
		widest = std::max(widest, static_cast<int>(size.cx));
	}
	SelectObject(dc, old);
	ReleaseDC(window, dc);
	const UINT dpi = std::max(96u, GetDpiForWindow(window));
	// Dropdown painting reserves nine DIPs on the left and 25 for the arrow.
	return std::clamp(static_cast<int>((widest * 96 + dpi - 1) / dpi) + (combo ? 36 : 24), minimum, maximum);
}

void AdjustThemedWindowRect(RECT& rect, UINT dpi)
{
	rect.left -= 1;
	rect.right += 1;
	rect.top -= MulDiv(36, dpi, 96);
	rect.bottom += 1;
}

void ApplyControlTheme(HWND window)
{
	wchar_t class_name[40] {};
	GetClassNameW(window, class_name, static_cast<int>(std::size(class_name)));
	if (wcscmp(class_name, TRACKBAR_CLASSW) == 0)
		SetWindowSubclass(window, SliderPaintProc, 82, 0);
	if (wcscmp(class_name, L"Button") == 0 || wcscmp(class_name, L"ComboBox") == 0 ||
	    wcscmp(class_name, HOTKEY_CLASSW) == 0)
		SetWindowSubclass(window, ThemedControlProc, 1, 0);
	const bool framed_list = wcscmp(class_name, L"ListBox") == 0 && (GetWindowLongPtrW(window, GWL_STYLE) & WS_BORDER);
	if (wcscmp(class_name, L"Edit") == 0 || wcscmp(class_name, HOTKEY_CLASSW) == 0 || framed_list)
	{
		SetWindowLongPtrW(window, GWL_STYLE, GetWindowLongPtrW(window, GWL_STYLE) & ~WS_BORDER);
		SetWindowLongPtrW(window, GWL_EXSTYLE, GetWindowLongPtrW(window, GWL_EXSTYLE) & ~WS_EX_CLIENTEDGE);
		SetWindowPos(window, nullptr, 0, 0, 0, 0,
		             SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
		if (wcscmp(class_name, L"Edit") == 0 || framed_list)
			SetWindowSubclass(window, InputControlProc, 5, 0);
		if (wcscmp(class_name, L"Edit") == 0)
			SendMessageW(window, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN,
			             MAKELPARAM(ScaleForDpi(window, 8), ScaleForDpi(window, 8)));
		RoundBounds(window);
	}
	if (wcscmp(class_name, L"ComboBox") == 0 || wcscmp(class_name, L"ListBox") == 0 || wcscmp(class_name, L"Edit") == 0)
		SetWindowTheme(window, L"", L"");
	if (wcscmp(class_name, L"ComboBox") == 0)
	{
		COMBOBOXINFO info { sizeof(info) };
		if (GetComboBoxInfo(window, &info))
		{
			SetWindowTheme(info.hwndList, L"", L"");
			SetWindowSubclass(info.hwndList, PopupListProc, 4, 0);
		}
	}
}

void ApplyWindowFrameTheme(HWND window)
{
	// Best effort: unsupported DWM title attributes leave the system frame intact.
	const BOOL dark = TRUE;
	DwmSetWindowAttribute(window, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark, sizeof(dark));
	if (IsIconic(window))
		return;
	if (IsZoomed(window))
	{
		SetWindowRgn(window, nullptr, TRUE);
		return;
	}
	RECT outer {};
	GetWindowRect(window, &outer);
	RECT visible { 0, 0, outer.right - outer.left, outer.bottom - outer.top };
	const int diameter = ScaleForDpi(window, 16); // 8 DIP radius: restrained, nearly square corners.
	HRGN shape =
		CreateRoundRectRgn(visible.left, visible.top, visible.right + 1, visible.bottom + 1, diameter, diameter);
	if (!shape)
		return;
	HRGN previous = CreateRectRgn(0, 0, 0, 0);
	const bool same = GetWindowRgn(window, previous) != ERROR && EqualRgn(shape, previous);
	DeleteObject(previous);
	if (same || !SetWindowRgn(window, shape, TRUE))
		DeleteObject(shape); // Windows owns a successful region.
}

static bool ShowCaptionContextMenu(HWND window, WPARAM w_param, LPARAM l_param, LRESULT& result)
{
	if (reinterpret_cast<HWND>(w_param) == window)
	{
		POINT at { GET_X_LPARAM(l_param), GET_Y_LPARAM(l_param) };
		if (at.x == -1 && at.y == -1)
		{
			RECT bounds {};
			GetWindowRect(window, &bounds);
			at = { bounds.left + ScaleForDpi(window, 8), bounds.top + ScaleForDpi(window, 36) };
		}
		else if (CaptionHitTest(window, at) != HTCAPTION && CaptionHitTest(window, at) != HTSYSMENU)
			return false;
		HideCaptionTip(window);
		ShowCaptionMenu(window, at);
		result = 0;
		return true;
	}
	return false;
}

static bool SetThemedCursor(HWND window, WPARAM w_param, LPARAM l_param, LRESULT& result)
{
	const int hit = LOWORD(l_param);
	auto entry = CaptionActionAtHit(window, hit);
	if (IsWindowEnabled(window) && ((entry && entry->enabled) || hit == HTCLOSE || hit == HTMINBUTTON ||
	                                hit == HTSYSMENU || (hit == HTHELP && GetPropW(window, L"LutPowerCommand"))))
	{
		SetCursor(LoadCursorW(nullptr, IDC_HAND));
		result = TRUE;
		return true;
	}
	if (hit == HTCLIENT)
	{
		wchar_t class_name[40] {};
		auto target = reinterpret_cast<HWND>(w_param);
		GetClassNameW(target, class_name, 40);
		if (IsWindowEnabled(target) && wcscmp(class_name, TRACKBAR_CLASSW) == 0)
		{
			SetCursor(LoadCursorW(nullptr, IDC_SIZEWE));
			result = TRUE;
			return true;
		}
	}
	return false;
}

static void DrawThemedComboItem(HWND window, const DRAWITEMSTRUCT* item)
{
	const auto combo = item->hwndItem;
	HDC dc = item->hDC;
	const bool selected = (item->itemState & ODS_SELECTED) != 0;
	HBRUSH fill = CreateSolidBrush(selected ? RGB(66, 71, 79) : THEME_SURFACE);
	FillRect(dc, &item->rcItem, fill);
	DeleteObject(fill);
	const auto old = SelectObject(dc, reinterpret_cast<HFONT>(SendMessageW(combo, WM_GETFONT, 0, 0)));
	SetBkMode(dc, TRANSPARENT);
	SetTextColor(dc, IsWindowEnabled(combo) ? THEME_TEXT : THEME_MUTED);
	if (item->itemID != static_cast<UINT>(-1))
	{
		const auto length = SendMessageW(combo, CB_GETLBTEXTLEN, item->itemID, 0);
		if (length >= 0)
		{
			std::wstring text(static_cast<size_t>(length) + 1, L'\0');
			SendMessageW(combo, CB_GETLBTEXT, item->itemID, reinterpret_cast<LPARAM>(text.data()));
			RECT area = item->rcItem;
			area.left += ScaleForDpi(window, 5);
			area.right -= ScaleForDpi(window, 5);
			DrawTextW(dc, text.c_str(), -1, &area, DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX);
		}
	}
	if ((item->itemState & ODS_FOCUS) && !(SendMessageW(combo, WM_QUERYUISTATE, 0, 0) & UISF_HIDEFOCUS))
	{
		RECT focus = item->rcItem;
		InflateRect(&focus, -1, -1);
		DrawFocusRect(dc, &focus);
	}
	SelectObject(dc, old);
}

static bool PressCaptionButton(HWND window, WPARAM w_param, LRESULT& result)
{
	HideCaptionTip(window);
	if (w_param == HTSYSMENU)
	{
		RECT bounds {};
		GetWindowRect(window, &bounds);
		ShowCaptionMenu(window, { bounds.left + ScaleForDpi(window, 8), bounds.top + ScaleForDpi(window, 36) });
		result = 0;
		return true;
	}
	if (auto entry = CaptionActionAtHit(window, static_cast<int>(w_param)))
	{
		if (entry->enabled)
		{
			SetPropW(window, L"LutCaptionPressed", reinterpret_cast<HANDLE>(w_param));
			SetCapture(window);
			RepaintCaption(window);
		}
		result = 0;
		return true;
	}
	if (w_param == HTHELP && GetPropW(window, L"LutPowerCommand"))
	{
		if ((reinterpret_cast<INT_PTR>(GetPropW(window, L"LutPowerState")) - 1) & 2)
		{
			SetPropW(window, L"LutCaptionPressed", reinterpret_cast<HANDLE>(w_param));
			SetCapture(window);
			RepaintCaption(window);
		}
		result = 0;
		return true;
	}
	if (w_param == HTCLOSE || w_param == HTMINBUTTON)
	{
		SetPropW(window, L"LutCaptionPressed", reinterpret_cast<HANDLE>(w_param));
		SetCapture(window);
		result = 0;
		return true;
	}
	return false;
}

static bool ReleaseCaptionButton(HWND window, LPARAM l_param, LRESULT& result)
{
	if (const auto hit = reinterpret_cast<INT_PTR>(RemovePropW(window, L"LutCaptionPressed")))
	{
		POINT point { GET_X_LPARAM(l_param), GET_Y_LPARAM(l_param) };
		ClientToScreen(window, &point);
		ReleaseCapture();
		if (CaptionHitTest(window, point) == hit)
		{
			if (auto entry = CaptionActionAtHit(window, static_cast<int>(hit)))
			{
				if (entry->enabled)
					PostMessageW(window, WM_COMMAND, entry->command, 0);
			}
			else if (hit == HTHELP)
			{
				if ((reinterpret_cast<INT_PTR>(GetPropW(window, L"LutPowerState")) - 1) & 2)
					PostMessageW(window, WM_COMMAND, reinterpret_cast<WPARAM>(GetPropW(window, L"LutPowerCommand")), 0);
			}
			else
				PostMessageW(window, WM_SYSCOMMAND, hit == HTCLOSE ? SC_CLOSE : SC_MINIMIZE, 0);
		}
		result = 0;
		return true;
	}
	return false;
}

static bool HandleCaptionSystemCommand(HWND window, UINT message, WPARAM w_param, LPARAM l_param, LRESULT& result)
{
	if (message == WM_SYSCOMMAND && (w_param & 0xFFF0) == SC_RESTORE && GetPropW(window, L"MonitorMagePreferredSize"))
	{
		DefWindowProcW(window, message, w_param, l_param);
		RepairWindowSize(window);
		result = 0;
		return true;
	}
	if (message == WM_SYSCOMMAND && ((w_param & 0xFFF0) == SC_KEYMENU || (w_param & 0xFFF0) == SC_MOUSEMENU))
	{
		// Alt+Space and the window icon use the same menu as caption right-click.
		if ((w_param & 0xFFF0) == SC_MOUSEMENU || l_param == VK_SPACE || l_param == 0)
		{
			RECT bounds {};
			GetWindowRect(window, &bounds);
			HideCaptionTip(window);
			ShowCaptionMenu(window, { bounds.left + ScaleForDpi(window, 8), bounds.top + ScaleForDpi(window, 36) });
			result = 0;
			return true;
		}
	}
	if (message == WM_SYSCOMMAND && (w_param & 0xFFF0) >= 0x1F10 && (w_param & 0xFFF0) <= 0x1F30)
	{
		if (auto entry = CaptionActionAtHit(window, 100 + static_cast<int>(((w_param & 0xFFF0) - 0x1F10) / 16)))
		{
			if (entry->enabled)
				PostMessageW(window, WM_COMMAND, entry->command, 0);
			result = 0;
			return true;
		}
	}
	if (GetPropW(window, L"LutPowerCommand") &&
	    ((message == WM_SYSCOMMAND && (w_param & 0xFFF0) == 0x1F00) ||
	     (message == WM_SYSCHAR && (GetPropW(window, L"LutCaptionSettings") ? (w_param == L's' || w_param == L'S')
	                                                                        : (w_param == L'p' || w_param == L'P')))))
	{
		if ((reinterpret_cast<INT_PTR>(GetPropW(window, L"LutPowerState")) - 1) & 2)
			PostMessageW(window, WM_COMMAND, reinterpret_cast<WPARAM>(GetPropW(window, L"LutPowerCommand")), 0);
		result = 0;
		return true;
	}
	return false;
}

static void ReleaseCaptionResources(HWND window)
{
	KillTimer(window, TIMER_GEOMETRY);
	delete reinterpret_cast<SIZE*>(RemovePropW(window, L"MonitorMagePreferredSize"));
	HideCaptionTip(window);
	if (auto tip = reinterpret_cast<HWND>(RemovePropW(window, L"LutCaptionTooltip")))
		DestroyWindow(tip);
	auto font = reinterpret_cast<HFONT>(RemovePropW(window, L"LutCaptionFont"));
	if (font)
		DeleteObject(font);
	RemovePropW(window, L"LutCaptionHover");
	RemovePropW(window, L"LutCaptionPressed");
	RemovePropW(window, L"LutPowerCommand");
	RemovePropW(window, L"LutPowerState");
	RemovePropW(window, L"LutCaptionSettings");
	delete reinterpret_cast<Actions*>(RemovePropW(window, L"LutCaptionActions"));
}

static void ColorThemedControl(UINT message, WPARAM w_param, LPARAM l_param, LRESULT& result)
{
	auto dc = reinterpret_cast<HDC>(w_param);
	const auto child = reinterpret_cast<HWND>(l_param);
	wchar_t class_name[40] {};
	GetClassNameW(child, class_name, 40);
	const bool field = message == WM_CTLCOLOREDIT || message == WM_CTLCOLORLISTBOX || wcscmp(class_name, L"Edit") == 0;
	SetTextColor(dc, IsWindowEnabled(child) ? THEME_TEXT : THEME_MUTED);
	SetBkColor(dc, field ? THEME_SURFACE : THEME_BACKGROUND);
	result = reinterpret_cast<LRESULT>(field ? surface.value : bg.value);
}

static void DrawSliderChannel(HWND window, const NMCUSTOMDRAW* draw, COLORREF colour, bool tinted)
{
	RECT rect = draw->rc;
	const int mid = (rect.top + rect.bottom) / 2;
	rect.top = mid - ScaleForDpi(window, 2);
	rect.bottom = mid + ScaleForDpi(window, 2);
	FillRoundedRect(draw->hdc, rect, THEME_BORDER, THEME_BORDER, ScaleForDpi(window, 2));
	if (tinted)
	{
		const auto muted = RGB((GetRValue(colour) + 2 * GetRValue(THEME_BACKGROUND)) / 3,
		                       (GetGValue(colour) + 2 * ((THEME_BACKGROUND >> 8) & 255)) / 3,
		                       (GetBValue(colour) + 2 * GetBValue(THEME_BACKGROUND)) / 3);
		FillRoundedRect(draw->hdc, rect, muted, muted, ScaleForDpi(window, 2));
	}
	if (const auto value = reinterpret_cast<INT_PTR>(GetPropW(draw->hdr.hwndFrom, L"LutSliderDefault")))
	{
		const int low = static_cast<int>(SendMessageW(draw->hdr.hwndFrom, TBM_GETRANGEMIN, 0, 0)),
				  high = static_cast<int>(SendMessageW(draw->hdr.hwndFrom, TBM_GETRANGEMAX, 0, 0));
		if (high > low)
		{
			const int x = draw->rc.left +
			              MulDiv(static_cast<int>(value - 32769) - low, draw->rc.right - draw->rc.left - 1, high - low);
			HPEN tick = CreatePen(PS_SOLID, 1, THEME_MUTED);
			const auto old = SelectObject(draw->hdc, tick);
			MoveToEx(draw->hdc, x, mid - ScaleForDpi(window, 5), nullptr);
			LineTo(draw->hdc, x, mid + ScaleForDpi(window, 5) + 1);
			SelectObject(draw->hdc, old);
			DeleteObject(tick);
		}
	}
}

static void DrawSliderThumb(HWND window, const NMCUSTOMDRAW* draw, COLORREF colour)
{
	FillRoundedRect(draw->hdc, draw->rc, IsWindowEnabled(draw->hdr.hwndFrom) ? colour : THEME_MUTED, THEME_BORDER,
	                ScaleForDpi(window, 3));
	if (const wchar_t letter =
	        static_cast<wchar_t>(reinterpret_cast<INT_PTR>(GetPropW(draw->hdr.hwndFrom, L"LutSliderLetter"))))
	{
		LOGFONTW label_style {};
		GetObjectW(reinterpret_cast<HFONT>(SendMessageW(draw->hdr.hwndFrom, WM_GETFONT, 0, 0)), sizeof(label_style),
		           &label_style);
		label_style.lfHeight = -ScaleForDpi(window, style::CAPTION_TEXT_SIZE);
		HFONT label_font = CreateFontIndirectW(&label_style);
		const auto old = SelectObject(draw->hdc, label_font);
		SetBkMode(draw->hdc, TRANSPARENT);
		SetTextColor(draw->hdc, THEME_BACKGROUND);
		RECT label = draw->rc;
		DrawTextW(draw->hdc, &letter, 1, &label, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
		SelectObject(draw->hdc, old);
		DeleteObject(label_font);
	}
}

// Windows lets a window draw parts of its native buttons and sliders itself.
static bool HandleCustomDraw(HWND window, const NMCUSTOMDRAW* draw, LRESULT& result)
{
	if (draw->hdr.code != NM_CUSTOMDRAW)
		return false;
	wchar_t class_name[40] {};
	GetClassNameW(draw->hdr.hwndFrom, class_name, static_cast<int>(std::size(class_name)));
	// Native buttons also draw synchronously for text, enabled and pressed
	// state changes. Route that path through the same dark painter.
	if (wcscmp(class_name, L"Button") == 0 && draw->dwDrawStage == CDDS_PREPAINT)
	{
		PaintControl(draw->hdr.hwndFrom, draw->hdc);
		result = CDRF_SKIPDEFAULT;
		return true;
	}
	if (wcscmp(class_name, TRACKBAR_CLASSW) != 0)
		return false;
	if (draw->dwDrawStage == CDDS_PREPAINT)
	{
		FillRect(draw->hdc, &draw->rc, bg.value);
		result = CDRF_NOTIFYITEMDRAW;
		return true;
	}
	if (draw->dwDrawStage == CDDS_ITEMPREPAINT)
	{
		const auto stored = reinterpret_cast<UINT_PTR>(GetPropW(draw->hdr.hwndFrom, L"LutSliderColour"));
		const COLORREF colour = stored ? static_cast<COLORREF>(stored - 1) : THEME_ACCENT;
		if (draw->dwItemSpec == TBCD_CHANNEL)
			DrawSliderChannel(window, draw, colour, stored != 0);
		if (draw->dwItemSpec == TBCD_THUMB)
			DrawSliderThumb(window, draw, colour);
		result = CDRF_SKIPDEFAULT;
		return true;
	}
	return false;
}

bool HandleThemeMessage(HWND window, UINT message, WPARAM w_param, LPARAM l_param, LRESULT& result)
{
	switch (message)
	{
		case WM_NCRBUTTONUP:
			if (w_param == HTCAPTION || w_param == HTSYSMENU)
			{
				HideCaptionTip(window);
				ShowCaptionMenu(window, { GET_X_LPARAM(l_param), GET_Y_LPARAM(l_param) });
				result = 0;
				return true;
			}
			break;
		case WM_CONTEXTMENU:
			return ShowCaptionContextMenu(window, w_param, l_param, result);
		case WM_SETCURSOR:
			return SetThemedCursor(window, w_param, l_param, result);
		case WM_MEASUREITEM:
			if (auto item = reinterpret_cast<MEASUREITEMSTRUCT*>(l_param); item->CtlType == ODT_COMBOBOX)
			{
				item->itemHeight = ScaleForDpi(window, 18);
				result = TRUE;
				return true;
			}
			break;
		case WM_DRAWITEM:
			if (auto item = reinterpret_cast<DRAWITEMSTRUCT*>(l_param); item->CtlType == ODT_COMBOBOX)
			{
				DrawThemedComboItem(window, item);
				result = TRUE;
				return true;
			}
			break;
		case WM_NCCALCSIZE:
		{
			RECT* rect =
				w_param ? &reinterpret_cast<NCCALCSIZE_PARAMS*>(l_param)->rgrc[0] : reinterpret_cast<RECT*>(l_param);
			rect->left += 1;
			rect->right -= 1;
			rect->top += ScaleForDpi(window, 36);
			rect->bottom -= 1;
			result = 0;
			return true;
		}
		case WM_CREATE:
			SetPropW(window, L"LutCaptionFont", CreateUiFont(GetDpiForWindow(window)));
			break;
		case WM_NCPAINT:
			RepaintCaption(window);
			result = 0;
			return true;
		case WM_NCACTIVATE:
			if (!w_param)
				HideCaptionTip(window);
			RepaintCaption(window);
			result = TRUE;
			return true;
		case WM_NCHITTEST:
		{
			POINT point { GET_X_LPARAM(l_param), GET_Y_LPARAM(l_param) };
			result = CaptionHitTest(window, point);
			return true;
		}
		case WM_NCMOUSEMOVE:
		{
			TrackCaptionTip(window, static_cast<int>(w_param));
			const auto previous = GetPropW(window, L"LutCaptionHover");
			if (!previous)
			{
				TRACKMOUSEEVENT tracking { sizeof(tracking), TME_NONCLIENT | TME_LEAVE, window, 0 };
				TrackMouseEvent(&tracking);
			}
			if (previous != reinterpret_cast<HANDLE>(w_param))
			{
				SetPropW(window, L"LutCaptionHover", reinterpret_cast<HANDLE>(w_param));
				RepaintCaption(window);
			}
			result = 0;
			return true;
		}
		case WM_NCMOUSELEAVE:
			HideCaptionTip(window);
			RemovePropW(window, L"LutCaptionHover");
			RepaintCaption(window);
			result = 0;
			return true;
		case WM_DISPLAYCHANGE:
			if (GetPropW(window, L"MonitorMagePreferredSize"))
				SetTimer(window, TIMER_GEOMETRY, 250, nullptr);
			break;
		case WM_SETTINGCHANGE:
			if (w_param == SPI_SETWORKAREA && GetPropW(window, L"MonitorMagePreferredSize"))
				SetTimer(window, TIMER_GEOMETRY, 250, nullptr);
			break;
		case WM_TIMER:
			if (w_param == TIMER_GEOMETRY)
			{
				KillTimer(window, TIMER_GEOMETRY);
				RepairWindowSize(window);
				result = 0;
				return true;
			}
			if (w_param == TIMER_CAPTION_TIP)
			{
				ShowCaptionTip(window);
				result = 0;
				return true;
			}
			break;
		case WM_SHOWWINDOW:
			if (!w_param)
				HideCaptionTip(window);
			else if (GetPropW(window, L"MonitorMagePreferredSize"))
				SetTimer(window, TIMER_GEOMETRY, 250, nullptr);
			break;
		case WM_MOUSEMOVE:
			HideCaptionTip(window);
			if (GetPropW(window, L"LutCaptionPressed"))
			{
				POINT point { GET_X_LPARAM(l_param), GET_Y_LPARAM(l_param) };
				ClientToScreen(window, &point);
				const auto hit = reinterpret_cast<HANDLE>(static_cast<INT_PTR>(CaptionHitTest(window, point)));
				if (GetPropW(window, L"LutCaptionHover") != hit)
				{
					SetPropW(window, L"LutCaptionHover", hit);
					RepaintCaption(window);
				}
				result = 0;
				return true;
			}
			break;
		case WM_NCLBUTTONDBLCLK:
			if (!CaptionActionAtHit(window, static_cast<int>(w_param)) &&
			    (w_param != HTHELP || !GetPropW(window, L"LutPowerCommand")))
				break;
			[[fallthrough]];
		case WM_NCLBUTTONDOWN:
			return PressCaptionButton(window, w_param, result);
		case WM_LBUTTONUP:
			return ReleaseCaptionButton(window, l_param, result);
		case WM_SYSCOMMAND:
		case WM_SYSCHAR:
			return HandleCaptionSystemCommand(window, message, w_param, l_param, result);
		case WM_CAPTURECHANGED:
			RemovePropW(window, L"LutCaptionPressed");
			RemovePropW(window, L"LutCaptionHover");
			RepaintCaption(window);
			break;
		case WM_CANCELMODE:
			if (RemovePropW(window, L"LutCaptionPressed"))
			{
				if (GetCapture() == window)
					ReleaseCapture();
				RepaintCaption(window);
			}
			break;
		case WM_SETTEXT:
			result = DefWindowProcW(window, message, w_param, l_param);
			RepaintCaption(window);
			return true;
		case WM_SETICON:
			result = DefWindowProcW(window, message, w_param, l_param);
			RepaintCaption(window);
			return true;
		case WM_PRINT:
			PaintWindowToDc(window, reinterpret_cast<HDC>(w_param), l_param);
			result = 0;
			return true;
		case WM_DPICHANGED:
		{
			if (GetPropW(window, L"MonitorMagePreferredSize"))
				SetTimer(window, TIMER_GEOMETRY, 250, nullptr);
			HideCaptionTip(window);
			auto font = reinterpret_cast<HFONT>(RemovePropW(window, L"LutCaptionFont"));
			if (font)
				DeleteObject(font);
			SetPropW(window, L"LutCaptionFont", CreateUiFont(HIWORD(w_param)));
			break;
		}
		case WM_NCDESTROY:
			ReleaseCaptionResources(window);
			break;
	}
	if (message >= WM_CTLCOLORMSGBOX && message <= WM_CTLCOLORSTATIC)
	{
		ColorThemedControl(message, w_param, l_param, result);
		return true;
	}
	if (message == WM_ERASEBKGND)
	{
		RECT rect {};
		GetClientRect(window, &rect);
		FillRect(reinterpret_cast<HDC>(w_param), &rect, bg.value);
		result = 1;
		return true;
	}
	if (message == WM_NOTIFY)
		return HandleCustomDraw(window, reinterpret_cast<NMCUSTOMDRAW*>(l_param), result);
	return false;
}
}
