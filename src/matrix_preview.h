#pragma once
#include <windows.h>
namespace matrix_preview {
void show(HWND owner,bool hidden=false);
HWND window();
bool dialogMessage(MSG&);
int run(HINSTANCE);
int worker(DWORD parent,const wchar_t* token,bool fake=false);
int selfTest(const wchar_t* executable);
}
