#pragma once
#include <windows.h>
#include <functional>
#include <vector>
#include <array>
namespace effects
{
inline constexpr int RowHeight = 36;
void Attach(HWND list, std::function<void(int)> toggle, std::function<void(int, int)> move);
inline constexpr const wchar_t* Names[] { L"Contrast curves", L"HDR gamma",       L"Color balance",
	                                      L"Midtone gamma",   L"Levels",          L"Shadows / highlights",
	                                      L"Custom curves",   L"Imported 1D LUT", L"Color filters",
	                                      L"Night light",     L"Saturation",      L"Channel mixer" };
void Update(HWND list, const std::vector<int>& order, const std::array<bool, 12>& enabled, bool hdrPaused,
            int selectedRole, bool powered);
bool Draw(const DRAWITEMSTRUCT&);
}
