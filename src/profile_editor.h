#pragma once
#include <windows.h>
#include <functional>
#include "application_profiles.h"
#include "programs.h"

namespace profile_editor
{
struct Look
{
	std::wstring name;
	std::filesystem::path path;
};

HWND ShowProfileEditor(HWND owner, HINSTANCE instance, const applications::Configuration& config,
                       const std::vector<Look>& looks, const std::filesystem::path& last_application,
                       std::function<bool(const applications::Configuration&)> apply, bool hidden = false);
void RefreshAppearance();
HWND ProfileEditorHandle();
void PreviewPrograms(const std::vector<programs::Program>&);
}
