#pragma once
#include <array>
namespace matrix_effect
{
// A short settling window after focus/mode changes, never an idle poll.
class Recovery
{
	unsigned _next = 0;

public:
	void Restart() { _next = 0; }
	unsigned NextDelay()
	{
		constexpr std::array<unsigned, 3> RECOVERY_DELAYS { 250, 750, 1500 };
		return _next < RECOVERY_DELAYS.size() ? RECOVERY_DELAYS[_next++] : 0;
	}
};
}
