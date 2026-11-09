#pragma once
#include <windows.h>
#include <array>
#include <vector>
#include <algorithm>
#include <cstdint>

namespace plot
{
using Paths = std::array<std::vector<POINT>, 3>;
// One bounded supersampling tile, reusable while the editor is open. A full
// graph bitmap is never retained. The owner releases this on hide/minimize.
class Scratch
{
	HDC _dc = nullptr;
	HBITMAP _bitmap = nullptr;
	int _width = 0, _height = 0;
	void* _pixels = nullptr;

public:
	Paths points;
	Scratch() = default;
	Scratch(const Scratch&) = delete;
	Scratch& operator=(const Scratch&) = delete;
	void Reset()
	{
		if (_bitmap)
			DeleteObject(_bitmap);
		if (_dc)
			DeleteDC(_dc);
		_bitmap = nullptr;
		_dc = nullptr;
		_pixels = nullptr;
		_width = _height = 0;
		for (auto& p : points)
			std::vector<POINT>().swap(p);
	}
	~Scratch() { Reset(); }
	bool EnsureSize(HDC target, int width, int height)
	{
		if (_bitmap && _width == width && _height == height)
			return true;
		Reset();
		_dc = CreateCompatibleDC(target);
		if (!_dc)
			return false;
		BITMAPINFO info {};
		info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
		info.bmiHeader.biWidth = width;
		info.bmiHeader.biHeight = -height;
		info.bmiHeader.biPlanes = 1;
		info.bmiHeader.biBitCount = 32;
		info.bmiHeader.biCompression = BI_RGB;
		_bitmap = CreateDIBSection(target, &info, DIB_RGB_COLORS, &_pixels, nullptr, 0);
		if (!_bitmap)
		{
			Reset();
			return false;
		}
		_width = width;
		_height = height;
		SetROP2(_dc, R2_MERGEPEN);
		return true;
	}
	HDC DeviceContext() const { return _dc; }
	HBITMAP BitmapHandle() const { return _bitmap; }
	uint32_t* Pixels() const { return static_cast<uint32_t*>(_pixels); }
	size_t ByteCount() const { return static_cast<size_t>(_width) * _height * 4; }
};

// Draws only the parts of a line that can touch the band from top to bottom. A line is
// only ever broken at a point outside the band, so the break cannot show inside it.
inline bool DrawLineInBand(HDC dc, const std::vector<POINT>& line, LONG top, LONG bottom)
{
	bool ok = true;
	size_t run_start = line.size();
	for (size_t i = 0; i < line.size(); ++i)
	{
		const bool segment_in_band = i + 1 < line.size() && std::max(line[i].y, line[i + 1].y) >= top &&
		                             std::min(line[i].y, line[i + 1].y) <= bottom;
		if (segment_in_band && run_start == line.size())
			run_start = i;
		if (!segment_in_band && run_start != line.size())
		{
			ok = Polyline(dc, &line[run_start], static_cast<int>(i - run_start + 1)) && ok;
			run_start = line.size();
		}
	}
	return ok;
}

// Additive RGB coverage with unchanged 2x supersampling and no glow. Each
// tile is downsampled in place, then blended before its storage is reused.
inline bool DrawRgbCurves(HDC target, int width, int height, const Paths& paths, int stroke,
                          Scratch* reusable = nullptr)
{
	if (width <= 0 || height <= 0 || stroke <= 0)
		return false;
	RECT bounds { width, height, 0, 0 };
	bool any = false;
	for (const auto& path : paths)
		if (path.size() > 1)
			for (const auto& p : path)
			{
				any = true;
				bounds.left = std::min(bounds.left, p.x);
				bounds.top = std::min(bounds.top, p.y);
				bounds.right = std::max(bounds.right, p.x);
				bounds.bottom = std::max(bounds.bottom, p.y);
			}
	if (!any)
		return true;
	const int pad = stroke + 1;
	bounds.left = std::max(0L, bounds.left - pad);
	bounds.top = std::max(0L, bounds.top - pad);
	bounds.right = std::min(static_cast<LONG>(width), bounds.right + pad);
	bounds.bottom = std::min(static_cast<LONG>(height), bounds.bottom + pad);
	RECT clip {};
	const auto type = GetClipBox(target, &clip);
	if (type == NULLREGION)
		return true;
	if (type != ERROR)
	{
		RECT visible {};
		if (!IntersectRect(&visible, &bounds, &clip))
			return true;
		bounds = visible;
	}
	const int w = bounds.right - bounds.left, h = bounds.bottom - bounds.top;
	if (w <= 0 || h <= 0)
		return true;
	// Four supersamples * four bytes per output pixel. Keep the tile at
	// most 1 MiB for ordinary widths, rather than retaining a full 2x image.
	const int rows = std::min({ h, 64, std::max(1, 1048576 / (w * 16)) });
	Scratch transient;
	auto& scratch = reusable ? *reusable : transient;
	if (!scratch.EnsureSize(target, w * 2, rows * 2))
		return false;
	HDC layer = scratch.DeviceContext();
	auto& scaled = scratch.points;
	for (size_t c = 0; c < 3; ++c)
	{
		scaled[c].clear();
		scaled[c].reserve(paths[c].size());
		for (const auto& p : paths[c])
			scaled[c].push_back({ (p.x - bounds.left) * 2, (p.y - bounds.top) * 2 });
	}
	constexpr COLORREF CHANNEL_COLORS[] { RGB(255, 0, 0), RGB(0, 255, 0), RGB(0, 0, 255) };
	HPEN pens[3] {};
	for (int c = 0; c < 3; ++c)
		if (!(pens[c] = CreatePen(PS_SOLID, stroke * 2, CHANNEL_COLORS[c])))
		{
			for (auto p : pens)
				if (p)
					DeleteObject(p);
			return false;
		}
	bool ok = true;
	const auto original = SelectObject(layer, scratch.BitmapHandle());
	for (int start = 0; start < h && ok; start += rows)
	{
		const int count = std::min(rows, h - start);
		const LONG band_margin = stroke * 2 + 2; // Wider than the pen.
		const LONG band_top = start * 2 - band_margin, band_bottom = (start + count) * 2 + band_margin;
		SetViewportOrgEx(layer, 0, 0, nullptr);
		PatBlt(layer, 0, 0, w * 2, count * 2, BLACKNESS);
		SetViewportOrgEx(layer, 0, -start * 2, nullptr);
		for (size_t c = 0; c < 3; ++c)
			if (scaled[c].size() > 1)
			{
				const auto prior = SelectObject(layer, pens[c]);
				if (!DrawLineInBand(layer, scaled[c], band_top, band_bottom))
					ok = false;
				SelectObject(layer, prior);
			}
		SetViewportOrgEx(layer, 0, 0, nullptr);
		GdiFlush();
		auto output = scratch.Pixels();
		const auto input = output;
		for (int y = 0; y < count; ++y)
			for (int x = 0; x < w; ++x)
			{
				// Most of the graph is empty, so skip the averaging for blank pixels.
				const uint32_t* upper = &input[static_cast<size_t>(y * 2) * w * 2 + x * 2];
				const uint32_t* lower = upper + w * 2;
				if (!((upper[0] | upper[1] | lower[0] | lower[1]) & 0x00FFFFFF))
				{
					output[static_cast<size_t>(y) * w * 2 + x] = 0;
					continue;
				}
				uint32_t value = 0;
				int alpha = 0;
				for (int c = 0; c < 3; ++c)
				{
					int sum = 0;
					for (int dy = 0; dy < 2; ++dy)
						for (int dx = 0; dx < 2; ++dx)
							sum += (input[static_cast<size_t>(y * 2 + dy) * w * 2 + x * 2 + dx] >> (c * 8)) & 255;
					const int coverage = (sum + 2) / 4;
					alpha = std::max(alpha, coverage);
					value |= static_cast<uint32_t>(coverage) << (c * 8);
				}
				output[static_cast<size_t>(y) * w * 2 + x] = value | (static_cast<uint32_t>(alpha) << 24);
			}
		if (ok)
		{
			BLENDFUNCTION blend { AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };
			ok = AlphaBlend(target, bounds.left, bounds.top + start, w, count, layer, 0, 0, w, count, blend) != FALSE;
		}
		GdiFlush();
	}
	SelectObject(layer, original);
	for (auto pen : pens)
		DeleteObject(pen);
	return ok;
}
}
