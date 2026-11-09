#pragma once
#include <windows.h>
#include <algorithm>

namespace paint
{
// Preserve the caller's logical coordinates, but allocate only the visible
// dirty rectangle. Shared WM_PRINT DCs and native BeginPaint DCs both work.
class Buffer
{
	HDC _target = nullptr, _memory = nullptr;
	HBITMAP _bitmap = nullptr;
	HGDIOBJ _previous = nullptr;
	RECT _area {};
	int _saved = 0;
	bool _active = false, _committed = false;

public:
	Buffer(HDC target, const RECT& bounds) : _target(target)
	{
		if (!_target || (_saved = SaveDC(_target)) == 0)
			return;
		IntersectClipRect(_target, bounds.left, bounds.top, bounds.right, bounds.bottom);
		RECT clip {};
		const int type = GetClipBox(_target, &clip);
		if (type == NULLREGION)
			return;
		if (type == ERROR)
			clip = bounds;
		if (!IntersectRect(&_area, &bounds, &clip))
			return;
		_active = true;
		const int w = _area.right - _area.left, h = _area.bottom - _area.top;
		_memory = CreateCompatibleDC(_target);
		if (_memory)
			_bitmap = CreateCompatibleBitmap(_target, w, h);
		if (_bitmap)
		{
			_previous = SelectObject(_memory, _bitmap);
			if (_previous && _previous != HGDI_ERROR)
			{
				SetViewportOrgEx(_memory, -_area.left, -_area.top, nullptr);
				IntersectClipRect(_memory, _area.left, _area.top, _area.right, _area.bottom);
				return;
			}
			DeleteObject(_bitmap);
			_bitmap = nullptr;
		}
		if (_memory)
		{
			DeleteDC(_memory);
			_memory = nullptr;
		}
	}
	Buffer(const Buffer&) = delete;
	Buffer& operator=(const Buffer&) = delete;
	explicit operator bool() const { return _active; }
	bool IsBuffered() const { return _memory != nullptr; }
	HDC DeviceContext() const { return _memory ? _memory : _target; }
	SIZE BitmapSize() const
	{
		BITMAP info {};
		if (_bitmap)
			GetObjectW(_bitmap, sizeof(info), &info);
		return { info.bmWidth, info.bmHeight };
	}
	size_t ByteCount() const
	{
		BITMAP info {};
		if (_bitmap)
			GetObjectW(_bitmap, sizeof(info), &info);
		return static_cast<size_t>(info.bmWidthBytes) * info.bmHeight;
	}
	void Commit()
	{
		if (_committed)
			return;
		_committed = true;
		if (_active && _memory)
			BitBlt(_target, _area.left, _area.top, _area.right - _area.left, _area.bottom - _area.top, _memory,
			       _area.left, _area.top, SRCCOPY);
	}
	void Finish()
	{
		Commit();
		if (_bitmap)
		{
			SelectObject(_memory, _previous);
			DeleteObject(_bitmap);
			_bitmap = nullptr;
		}
		if (_memory)
		{
			DeleteDC(_memory);
			_memory = nullptr;
		}
		if (_saved)
		{
			RestoreDC(_target, _saved);
			_saved = 0;
		}
	}
	~Buffer() { Finish(); }
};
}
