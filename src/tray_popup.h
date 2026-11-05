#pragma once
#include <windows.h>
#include <string>
#include <vector>
#include <functional>
#include "ui_theme.h"
namespace ui {
struct MenuItem { UINT id; std::wstring text, shortcut; bool checked = false, separatorBefore = false, enabled = true; Glyph glyph=Glyph::None; };
UINT popup(HWND owner, HICON icon, const std::vector<MenuItem>& items, POINT anchor, bool below=false);
bool previewPopup(HWND owner, HICON icon, const std::vector<MenuItem>& items, std::function<bool(HWND)> render, bool compact=false);
int popupSelfTest(HWND owner);
}
