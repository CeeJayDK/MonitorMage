#pragma once
#include <filesystem>
#include <string>
#include <vector>

namespace applications
{
enum class Action
{
	Look,
	Bypass,
	Disable,
	PauseHdr
};

struct Rule
{
	std::filesystem::path Executable, Look;
	Action Action = Action::Look;
};

struct Configuration
{
	bool Enabled = false;
	std::vector<Rule> Rules;
};

bool SamePath(const std::filesystem::path& a, const std::filesystem::path& b);
void Validate(const Configuration& config);
Configuration Read(const std::filesystem::path& settings);
std::wstring Encode(const Configuration& config, const std::filesystem::path& settings);
struct Decision
{
	Action action = Action::Look;
	std::filesystem::path look;
	bool matched = false;
};

Decision ResolveForProgram(const Configuration& config, const std::filesystem::path& executable);
class Focus
{
	std::filesystem::path _current;
	bool _manual = false;

public:
	// Own windows and transient shell focus are filtered before entering this state machine.
	bool UpdateFocusedProgram(const std::filesystem::path& executable);
	void SetManualOverride() { _manual = true; }
	void ClearManualOverride() { _manual = false; }
	bool IsManualOverride() const { return _manual; }
	const std::filesystem::path& FocusedProgram() const { return _current; }
};
}
