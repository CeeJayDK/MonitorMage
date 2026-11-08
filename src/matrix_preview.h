#pragma once
#include <windows.h>
namespace matrix_preview
{
void ShowPreviewWindow(HWND owner, bool hidden = false);
HWND PreviewWindowHandle();
bool HandleDialogMessage(MSG&);
int RunStandalone(HINSTANCE);
int RunWorkerProcess(DWORD parent, const wchar_t* token, bool fake = false);
int SelfTest(const wchar_t* executable);
}
