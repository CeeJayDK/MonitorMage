#pragma once
#include <windows.h>
#include <array>
#include <functional>
#include "window_geometry.h"
namespace ui
{
inline constexpr COLORREF THEME_BACKGROUND = RGB(37, 39, 43);
inline constexpr COLORREF THEME_SURFACE = RGB(46, 49, 54);
inline constexpr COLORREF THEME_SIDEBAR = RGB(43, 46, 51);
inline constexpr COLORREF THEME_TEXT = RGB(231, 233, 237);
inline constexpr COLORREF THEME_MUTED = RGB(158, 164, 174);
inline constexpr COLORREF THEME_BORDER = RGB(85, 91, 101);
inline constexpr COLORREF THEME_FOCUS_BORDER = RGB(158, 164, 174);
inline constexpr COLORREF THEME_ACCENT = RGB(85, 182, 233);
inline constexpr COLORREF THEME_ERROR = RGB(245, 95, 104);
inline constexpr COLORREF THEME_CLOSE_RED = RGB(151, 46, 66);
inline constexpr COLORREF THEME_SELECTED_TAB = RGB(62, 62, 62);
inline constexpr COLORREF THEME_SELECTION_LIGHT = RGB(164, 164, 164);
namespace wording
{
inline constexpr wchar_t LABEL_SETTINGS[] = L"Settings";
inline constexpr wchar_t LABEL_APPLICATION_PROFILES[] = L"Application profiles";
}

namespace style
{
inline constexpr int TEXT_SIZE = 13, SECONDARY_TEXT_SIZE = 11, CAPTION_TEXT_SIZE = 8, CORNER_RADIUS = 4;
// A named style is the default; callers can pass an explicit variant when
// a control intentionally needs a different palette or geometry.
struct Toggle
{
	int width, height, knob, inset;
	COLORREF off_track, on_track, unpowered_track, off_knob, on_knob, border, highlight;
};

inline constexpr Toggle EFFECT_TOGGLE { 26,
	                                    14,
	                                    10,
	                                    2,
	                                    THEME_BACKGROUND,
	                                    THEME_ACCENT,
	                                    THEME_BORDER,
	                                    THEME_MUTED,
	                                    THEME_BACKGROUND,
	                                    THEME_BORDER,
	                                    THEME_FOCUS_BORDER };
}

inline int ScaleForDpi(HWND window, int value)
{
	return MulDiv(value, GetDpiForWindow(window), 96);
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

void PaintMenuGlyph(HWND, HDC, RECT, Glyph, bool on, bool enabled, COLORREF base = THEME_BACKGROUND);
void SetIconButton(HWND, Glyph, bool on, bool enabled = true);
void ToggleButton(HWND, bool on, bool powered = true);
void PaintToggle(HWND, HDC, RECT, bool on, bool powered, bool highlighted = false,
                 const style::Toggle& theme = style::EFFECT_TOGGLE);
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
void ConfigureSlider(HWND, int default_value, wchar_t thumb_letter = 0);
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
void FixedWindowSize(HWND, int client_width, int client_height);
bool WindowNeedsSizeRepair(HWND);
void RepairWindowSize(HWND);
bool HandleThemeMessage(HWND, UINT, WPARAM, LPARAM, LRESULT&);
}
