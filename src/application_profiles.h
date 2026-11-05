#pragma once
#include <filesystem>
#include <string>
#include <vector>

namespace applications {
enum class Action { Look, Bypass, Disable, PauseHdr };
struct Rule { std::filesystem::path executable, look; Action action = Action::Look; };
struct Configuration { bool enabled = false; std::vector<Rule> rules; };
bool samePath(const std::filesystem::path& a, const std::filesystem::path& b);
void validate(const Configuration& config);
Configuration read(const std::filesystem::path& settings);
std::wstring encode(const Configuration& config, const std::filesystem::path& settings);
struct Decision { Action action = Action::Look; std::filesystem::path look; bool matched = false; };
Decision resolve(const Configuration& config, const std::filesystem::path& executable);
class Focus {
    std::filesystem::path current_;
    bool manual_ = false;
public:
    // Own windows and transient shell focus are filtered before entering this state machine.
    bool changed(const std::filesystem::path& executable);
    void overrideLook() { manual_ = true; }
    void resetOverride() { manual_ = false; }
    bool manual() const { return manual_; }
    const std::filesystem::path& executable() const { return current_; }
};
}
