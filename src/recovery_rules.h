#pragma once
#include <array>
#include <cstdint>
#include <algorithm>
#include <cstdlib>

namespace live
{
using Ramp = std::array<std::array<std::uint16_t, 256>, 3>;
inline int MaxRampDifference(const Ramp& a, const Ramp& b)
{
	int maximum = 0;
	for (std::size_t c = 0; c < 3; ++c)
		for (std::size_t i = 0; i < 256; ++i)
			maximum = std::max(maximum, std::abs(static_cast<int>(a[c][i]) - b[c][i]));
	return maximum;
}

inline bool IsRampOurs(const Ramp& current, const Ramp& baseline, const Ramp& last, const Ramp& candidate, bool applied,
                       bool pending)
{
	return MaxRampDifference(current, baseline) <= 2 || (applied && MaxRampDifference(current, last) <= 2) ||
	       (pending && MaxRampDifference(current, candidate) <= 2);
}
} // namespace live
