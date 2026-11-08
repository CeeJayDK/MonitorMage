#pragma once
#include <windows.h>
#include <vector>
#include "icon_resources.h"

namespace app_icons
{
enum class State
{
	Disabled,
	DisabledHdr,
	Active,
	Hdr,
	Application,
	ApplicationHdr
};

inline State ChooseState(bool active, bool hdr, bool application)
{
	if (!active)
		return hdr ? State::DisabledHdr : State::Disabled;
	if (application)
		return hdr ? State::ApplicationHdr : State::Application;
	return hdr ? State::Hdr : State::Active;
}

inline int ResourceId(State state)
{
	switch (state)
	{
		case State::Disabled:
			return IDI_LUT_DISABLED;
		case State::DisabledHdr:
			return IDI_LUT_DISABLED_HDR;
		case State::Hdr:
			return IDI_LUT_HDR;
		case State::Application:
			return IDI_LUT_APPLICATION;
		case State::ApplicationHdr:
			return IDI_LUT_APPLICATION_HDR;
		default:
			return IDI_LUT_ACTIVE;
	}
}

class Cache
{
	struct Entry
	{
		State state;
		int size;
		HICON icon;
	};
	std::vector<Entry> entries_;

public:
	Cache() = default;
	Cache(const Cache&) = delete;
	Cache& operator=(const Cache&) = delete;
	~Cache()
	{
		for (const auto& entry : entries_)
			DestroyIcon(entry.icon);
	}
	HICON GetIcon(HINSTANCE instance, State state, int size)
	{
		for (const auto& entry : entries_)
			if (entry.state == state && entry.size == size)
				return entry.icon;
		HICON icon =
			static_cast<HICON>(LoadImageW(instance, MAKEINTRESOURCEW(ResourceId(state)), IMAGE_ICON, size, size, 0));
		if (icon)
			entries_.push_back({ state, size, icon });
		return icon;
	}
};
}
