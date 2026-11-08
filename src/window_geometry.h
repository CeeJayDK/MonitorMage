#pragma once
#include <windows.h>
#include <algorithm>
namespace ui
{
// The preferred size is independent of temporary screen limits. Never replace
// it with the shrunken size Windows assigns during a game's mode switch.
inline RECT FitWindowRect(RECT current, SIZE preferred, RECT work)
{
	const LONG width = std::min(std::max(1L, preferred.cx), std::max(1L, work.right - work.left));
	const LONG height = std::min(std::max(1L, preferred.cy), std::max(1L, work.bottom - work.top));
	const LONG x = std::clamp(current.left, work.left, std::max(work.left, work.right - width));
	const LONG y = std::clamp(current.top, work.top, std::max(work.top, work.bottom - height));
	return { x, y, x + width, y + height };
}
}
