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
	HMODULE _dll = nullptr;
	using Init = BOOL(WINAPI*)();
	using ReadFunction = BOOL(WINAPI*)(PMAGCOLOREFFECT);
	using WriteFunction = BOOL(WINAPI*)(PMAGCOLOREFFECT);
	using Zoom = BOOL(WINAPI*)(float*, int*, int*);
	Init _initialize = nullptr, _uninitialize = nullptr;
	ReadFunction _get = nullptr;
	WriteFunction _set = nullptr;
	Zoom _zoom = nullptr;
	bool _initialized = false;

public:
	bool Open()
	{
		if (_initialized)
			return true;
		if (!_dll)
			_dll = LoadLibraryExW(L"Magnification.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
		if (!_dll)
			return false;
		_initialize = reinterpret_cast<Init>(GetProcAddress(_dll, "MagInitialize"));
		_uninitialize = reinterpret_cast<Init>(GetProcAddress(_dll, "MagUninitialize"));
		_get = reinterpret_cast<ReadFunction>(GetProcAddress(_dll, "MagGetFullscreenColorEffect"));
		_set = reinterpret_cast<WriteFunction>(GetProcAddress(_dll, "MagSetFullscreenColorEffect"));
		_zoom = reinterpret_cast<Zoom>(GetProcAddress(_dll, "MagGetFullscreenTransform"));
		return _initialize && _uninitialize && _get && _set && _zoom && (_initialized = _initialize() != FALSE);
	}
	bool ReadZoom(float* scale, int* x, int* y) { return _initialized && _zoom(scale, x, y) != FALSE; }
	bool Read(Matrix& matrix) override
	{
		MAGCOLOREFFECT value {};
		if (!_initialized || !_get(&value))
			return false;
		for (int r = 0; r < 5; ++r)
			for (int c = 0; c < 5; ++c)
				matrix[r][c] = value.transform[r][c];
		return true;
	}
	bool Write(const Matrix& matrix) override
	{
		MAGCOLOREFFECT value {};
		for (int r = 0; r < 5; ++r)
			for (int c = 0; c < 5; ++c)
				value.transform[r][c] = matrix[r][c];
		return _initialized && _set(&value) != FALSE;
	}
	bool Close()
	{
		if (_initialized && !_uninitialize())
			return false;
		_initialized = false;
		if (_dll)
		{
			FreeLibrary(_dll);
			_dll = nullptr;
		}
		return true;
	}
	~WindowsApi()
	{
		Close();
		if (_dll)
			FreeLibrary(_dll);
	}
};
}
