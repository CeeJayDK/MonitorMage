#pragma once
#include <windows.h>
namespace ui
{
// Overflow only; normal-size windows have no bars or extra painting buffers.
void EnableViewport(HWND, int width, int height);
POINT ViewportOffset(HWND);
void RevealControl(HWND, HWND);
}
