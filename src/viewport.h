#pragma once
#include <windows.h>
namespace ui {
// Overflow only; normal-size windows have no bars or extra painting buffers.
void enableViewport(HWND,int width,int height);
POINT viewportOffset(HWND);
void revealControl(HWND,HWND);
}
