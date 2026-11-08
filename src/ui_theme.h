#pragma once
#include <windows.h>
#include <array>
#include <functional>
#include "window_geometry.h"
namespace ui
{
inline constexpr COLORREF Background = RGB(37, 39, 43);
inline constexpr COLORREF Surface = RGB(46, 49, 54);
inline constexpr COLORREF Sidebar = RGB(43, 46, 51);
inline constexpr COLORREF Text = RGB(231, 233, 237);
inline constexpr COLORREF Muted = RGB(158, 164, 174);
inline constexpr COLORREF Border = RGB(85, 91, 101);
inline constexpr COLORREF FocusBorder = RGB(158, 164, 174);
inline constexpr COLORREF Accent = RGB(85, 182, 233);
inline constexpr COLORREF Error = RGB(245, 95, 104);
inline constexpr COLORREF CloseRed = RGB(151, 46, 66);
inline constexpr COLORREF SelectedTab = RGB(62, 62, 62);
inline constexpr COLORREF SelectionLight = RGB(164, 164, 164);
namespace wording
{
inline constexpr wchar_t Settings[] = L"Settings";
inline constexpr wchar_t ApplicationProfiles[] = L"Application profiles";
}

namespace style
{
inline constexpr int TextSize = 13, SecondaryTextSize = 11, CaptionTextSize = 8, CornerRadius = 4;
// A named style is the default; callers can pass an explicit variant when
// a control intentionally needs a different palette or geometry.
struct Toggle
{
	int width, height, knob, inset;
	COLORREF offTrack, onTrack, unpoweredTrack, offKnob, onKnob, border, highlight;
};

inline constexpr Toggle EffectToggle {
	26, 14, 10, 2, Background, Accent, Border, Muted, Background, Border, FocusBorder
};
}

HBRUSH BackgroundBrush();
void SetFontFamily(const wchar_t*);
const wchar_t* FontFamily();
void RefreshCaptionFont(HWND);
void SetCaptionPowerButton(HWND, int command, bool on, bool enabled);
void SetCaptionSettingsButton(HWND, int command, bool selected);
enum class Glyph
{
	None = 0,
	Sun = 1,
	Power = 2,
	Diamond = 3,
	Save = 4,
	SaveAs = 5,
	Export = 6,
	Reset = 7,
	Revert = 8,
	Settings = 9,
	Info = 10,
	Editor = 11,
	Move = 12,
	Resize = 13,
	Minimize = 14,
	Maximize = 15,
	Close = 16,
	Bypass = 17,
	Restore = 18,
	ColorFilter = 19
};

void PaintMenuGlyph(HWND, HDC, RECT, Glyph, bool on, bool enabled, COLORREF base = Background);
void SetIconButton(HWND, Glyph, bool on, bool enabled = true);
void ToggleButton(HWND, bool on, bool powered = true);
void PaintToggle(HWND, HDC, RECT, bool on, bool powered, bool highlighted = false,
                 const style::Toggle& theme = style::EffectToggle);
struct CaptionAction
{
	int command;
	bool on, enabled;
};

void CaptionActions(HWND, const std::array<CaptionAction, 3>&);
const wchar_t* HdrTooltip(bool on, bool available);
const wchar_t* CaptionTooltip(HWND, int hit);
bool PreviewCaptionMenu(HWND, std::function<bool(HWND)>);
void SliderColour(HWND, COLORREF);
void ConfigureSlider(HWND, int defaultValue, wchar_t thumbLetter = 0);
HFONT CreateUiFont(UINT dpi);
// Content-sized actions and fixed-choice dropdowns, in DPI-independent units.
int ContentWidth(HWND, HFONT, int minimum = 64, int maximum = 240);
void StyleTooltip(HWND);
bool TooltipEdgeSelfTest(HWND);
void AdjustThemedWindowRect(RECT&, UINT dpi);
void ApplyControlTheme(HWND);
// Shared tooltip labels; keep them brief. Explanations belong on the page.
// Repeated calls only update changed text.
void SetHelpText(HWND, const wchar_t*);
void HelpRegion(HWND, UINT_PTR, const RECT&, const wchar_t*);
const wchar_t* HelpTextForRegion(HWND, UINT_PTR region = 0);
void ApplyWindowFrameTheme(HWND);
void FixedWindowSize(HWND, int clientWidth, int clientHeight);
bool WindowNeedsSizeRepair(HWND);
void RepairWindowSize(HWND);
bool HandleThemeMessage(HWND, UINT, WPARAM, LPARAM, LRESULT&);
}
