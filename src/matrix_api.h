#pragma once
#include <windows.h>
#include <magnification.h>
#include "matrix_math.h"

namespace matrix_color
{
// Only worker processes load Magnification.dll. No static DLL dependency or
// initialization cost in the tray/editor process when this effect is unused.
class WindowsApi final : public Api
{
	HMODULE dll_ = nullptr;
	using Init = BOOL(WINAPI*)();
	using ReadFunction = BOOL(WINAPI*)(PMAGCOLOREFFECT);
	using WriteFunction = BOOL(WINAPI*)(PMAGCOLOREFFECT);
	using Zoom = BOOL(WINAPI*)(float*, int*, int*);
	Init initialize_ = nullptr, uninitialize_ = nullptr;
	ReadFunction get_ = nullptr;
	WriteFunction set_ = nullptr;
	Zoom zoom_ = nullptr;
	bool initialized_ = false;

public:
	bool Open()
	{
		if (initialized_)
			return true;
		if (!dll_)
			dll_ = LoadLibraryExW(L"Magnification.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
		if (!dll_)
			return false;
		initialize_ = reinterpret_cast<Init>(GetProcAddress(dll_, "MagInitialize"));
		uninitialize_ = reinterpret_cast<Init>(GetProcAddress(dll_, "MagUninitialize"));
		get_ = reinterpret_cast<ReadFunction>(GetProcAddress(dll_, "MagGetFullscreenColorEffect"));
		set_ = reinterpret_cast<WriteFunction>(GetProcAddress(dll_, "MagSetFullscreenColorEffect"));
		zoom_ = reinterpret_cast<Zoom>(GetProcAddress(dll_, "MagGetFullscreenTransform"));
		return initialize_ && uninitialize_ && get_ && set_ && zoom_ && (initialized_ = initialize_() != FALSE);
	}
	bool ReadZoom(float* scale, int* x, int* y) { return initialized_ && zoom_(scale, x, y) != FALSE; }
	bool Read(Matrix& m) override
	{
		MAGCOLOREFFECT value {};
		if (!initialized_ || !get_(&value))
			return false;
		for (int r = 0; r < 5; ++r)
			for (int c = 0; c < 5; ++c)
				m[r][c] = value.transform[r][c];
		return true;
	}
	bool Write(const Matrix& m) override
	{
		MAGCOLOREFFECT value {};
		for (int r = 0; r < 5; ++r)
			for (int c = 0; c < 5; ++c)
				value.transform[r][c] = m[r][c];
		return initialized_ && set_(&value) != FALSE;
	}
	bool Close()
	{
		if (initialized_ && !uninitialize_())
			return false;
		initialized_ = false;
		if (dll_)
		{
			FreeLibrary(dll_);
			dll_ = nullptr;
		}
		return true;
	}
	~WindowsApi()
	{
		Close();
		if (dll_)
			FreeLibrary(dll_);
	}
};
}
