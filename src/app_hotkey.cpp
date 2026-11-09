// The global keyboard shortcut that turns corrections on and off.
#include "app_state.h"

namespace app
{
UINT HotkeyModifiers(WORD shortcut)
{
	const BYTE flags = HIBYTE(shortcut);
	return MOD_NOREPEAT | ((flags & HOTKEYF_CONTROL) ? MOD_CONTROL : 0) | ((flags & HOTKEYF_ALT) ? MOD_ALT : 0) |
	       ((flags & HOTKEYF_SHIFT) ? MOD_SHIFT : 0);
}

static bool IsValidHotkey(WORD shortcut)
{
	const BYTE key = LOBYTE(shortcut), flags = HIBYTE(shortcut);
	return key && key != VK_F12 && key != VK_CONTROL && key != VK_MENU && key != VK_SHIFT &&
	       (flags & (HOTKEYF_CONTROL | HOTKEYF_ALT | HOTKEYF_SHIFT)) &&
	       !(flags & ~(HOTKEYF_CONTROL | HOTKEYF_ALT | HOTKEYF_SHIFT | HOTKEYF_EXT));
}

std::wstring HotkeyName(WORD shortcut)
{
	std::wstring name;
	if (HIBYTE(shortcut) & HOTKEYF_CONTROL)
		name += L"Ctrl+";
	if (HIBYTE(shortcut) & HOTKEYF_ALT)
		name += L"Alt+";
	if (HIBYTE(shortcut) & HOTKEYF_SHIFT)
		name += L"Shift+";
	wchar_t key_name[80] {};
	LONG scan = static_cast<LONG>(MapVirtualKeyW(LOBYTE(shortcut), MAPVK_VK_TO_VSC) << 16);
	if (HIBYTE(shortcut) & HOTKEYF_EXT)
		scan |= 1 << 24;
	if (GetKeyNameTextW(scan, key_name, static_cast<int>(std::size(key_name))))
		name += key_name;
	else
		name += L"key " + std::to_wstring(LOBYTE(shortcut));
	return name;
}

bool BindHotkey(WORD shortcut)
{
	if (!IsValidHotkey(shortcut))
	{
		hotkey_error = ERROR_INVALID_PARAMETER;
		return false;
	}
	if (hotkey_registered && shortcut == active_shortcut)
		return true;
	const int next_id = hotkey_registered ? (active_hotkey_id == 1 ? 2 : 1) : 1;
	if (!RegisterHotKey(main_window, next_id, HotkeyModifiers(shortcut), LOBYTE(shortcut)))
	{
		hotkey_error = GetLastError();
		return false;
	}
	if (hotkey_registered)
		UnregisterHotKey(main_window, active_hotkey_id);
	active_hotkey_id = next_id;
	active_shortcut = shortcut;
	hotkey_registered = true;
	hotkey_error = 0;
	return true;
}

void BindPreferredHotkey(WORD preferred)
{
	const WORD candidates[] = { preferred, MAKEWORD(VK_F8, HOTKEYF_CONTROL | HOTKEYF_ALT),
		                        MAKEWORD(VK_F8, HOTKEYF_CONTROL | HOTKEYF_SHIFT),
		                        MAKEWORD(VK_F9, HOTKEYF_CONTROL | HOTKEYF_ALT),
		                        MAKEWORD(VK_F9, HOTKEYF_CONTROL | HOTKEYF_SHIFT) };
	for (const WORD candidate : candidates)
	{
		if (BindHotkey(candidate))
		{
			if (candidate != preferred)
				hotkey_message = L"Your shortcut is unavailable. Using " + HotkeyName(candidate) + L".";
			return;
		}
	}
	hotkey_message = L"The shortcut could not be set. Choose another key combination in Settings.";
}

void LoadAndBindHotkey()
{
	const auto path = SettingsFilePath();
	preferred_shortcut = static_cast<WORD>(GetPrivateProfileIntW(L"Hotkey", L"Bypass", active_shortcut, path.c_str()));
	BindPreferredHotkey(preferred_shortcut);
}
}
