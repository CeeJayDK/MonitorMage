#pragma once
#include <windows.h>
#include <functional>
#include <vector>
#include <array>
// Effect numbers, in the same order as effects::EFFECT_NAMES.
namespace effect
{
enum Id : int
{
	ContrastCurves,
	HdrGamma,
	ColorBalance,
	MidtoneGamma,
	Levels,
	ShadowsHighlights,
	CustomCurves,
	Imported1dLut,
	ColorFilters,
	NightLight,
	Saturation,
	ChannelMixer,
	Count,
	FirstCreative = ColorBalance,
	FirstWindows = ColorFilters,
};
}

namespace effects
{
inline constexpr int ROW_HEIGHT = 36;
void Attach(HWND list, std::function<void(int)> toggle, std::function<void(int, int)> move);
inline constexpr const wchar_t* EFFECT_NAMES[] { L"Contrast curves", L"HDR gamma",       L"Color balance",
	                                             L"Midtone gamma",   L"Levels",          L"Shadows / highlights",
	                                             L"Custom curves",   L"Imported 1D LUT", L"Color filters",
	                                             L"Night light",     L"Saturation",      L"Channel mixer" };
void Update(HWND list, const std::vector<int>& order, const std::array<bool, 12>& enabled, bool hdr_paused,
            int selected_role, bool powered);
bool Draw(const DRAWITEMSTRUCT&);
}
