#pragma once
#include <windows.h>
namespace unsaved
{
bool Confirm(HWND owner);
HWND PreviewDialog(HWND owner);
int SelfTest(HWND owner);
}
