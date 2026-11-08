#pragma once
#include <array>
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include "color_math.h"

namespace color
{
struct Adjustments
{
	std::array<int, 5> balance { 100, 100, 100, 0, 0 }; // RGB percent, warmth, tint
	int midtone = 100;
	std::array<int, 4> levels { 0, 100, 0, 100 }; // input black/white, output black/white
	int shadows = 0, highlights = 0;
	bool balanceEnabled = true, gammaEnabled = true, levelsEnabled = true, toneEnabled = true, curvesEnabled = true,
		 importEnabled = true;
	std::array<std::vector<double>, 4> curves;
	std::array<std::vector<double>, 3> imported;
};

inline bool operator==(const Adjustments& a, const Adjustments& b)
{
	return a.balance == b.balance && a.midtone == b.midtone && a.levels == b.levels && a.shadows == b.shadows &&
	       a.highlights == b.highlights && a.balanceEnabled == b.balanceEnabled && a.gammaEnabled == b.gammaEnabled &&
	       a.levelsEnabled == b.levelsEnabled && a.toneEnabled == b.toneEnabled && a.curvesEnabled == b.curvesEnabled &&
	       a.importEnabled == b.importEnabled && a.curves == b.curves && a.imported == b.imported;
}

inline bool operator!=(const Adjustments& a, const Adjustments& b)
{
	return !(a == b);
}

inline void Validate(const Adjustments& a)
{
	for (int i = 0; i < 3; ++i)
		RequireRange(a.balance[i], 50, 150);
	RequireRange(a.balance[3], -50, 50);
	RequireRange(a.balance[4], -50, 50);
	RequireRange(a.midtone, 50, 200);
	for (int v : a.levels)
		RequireRange(v, 0, 100);
	if (a.levels[1] - a.levels[0] < 5 || a.levels[3] - a.levels[2] < 5)
		throw std::invalid_argument("Black and white points must be separated by at least 5 percent");
	RequireRange(a.shadows, -50, 50);
	RequireRange(a.highlights, -50, 50);
	const auto curve = [](const std::vector<double>& points)
	{
		if (points.empty())
			return;
		if (points.size() < 2 || points.size() > 4096)
			throw std::invalid_argument("Curves need 2 to 4096 samples");
		double last = 0;
		for (double v : points)
		{
			RequireRange(v, 0, 1);
			if (v < last)
				throw std::invalid_argument("Curves must be nondecreasing");
			last = v;
		}
	};
	for (const auto& points : a.curves)
	{
		if (!points.empty() && points.size() != 9)
			throw std::invalid_argument("Editable curves require nine points");
		curve(points);
	}
	for (const auto& points : a.imported)
		curve(points);
	if (a.imported[0].size() != a.imported[1].size() || a.imported[0].size() != a.imported[2].size())
		throw std::invalid_argument("Imported channels must have equal sample counts");
}

inline std::array<double, 3> BalanceGains(const Adjustments& a)
{
	const double warm = a.balance[3] / 100.0, tint = a.balance[4] / 100.0;
	std::array<double, 3> gains { a.balance[0] / 100.0 * std::exp2(warm + tint * .5),
		                          a.balance[1] / 100.0 * std::exp2(-tint),
		                          a.balance[2] / 100.0 * std::exp2(-warm + tint * .5) };
	const double maximum = std::max({ 1.0, gains[0], gains[1], gains[2] });
	for (auto& gain : gains)
		gain /= maximum;
	return gains;
}

inline double ExtendedSrgbDecode(double x)
{
	return x <= srgbCodeKnee ? x / 12.92 : std::pow((x + .055) / 1.055, 2.4);
}

inline double ExtendedSrgbEncode(double x)
{
	return x <= srgbLinearKnee ? 12.92 * x : 1.055 * std::pow(x, 1 / 2.4) - .055;
}

inline double SampleCurve(const std::vector<double>& points, double x)
{
	if (points.empty())
		return x;
	if (x > 1)
		return points.back() + (x - 1);
	const double at = std::clamp(x, 0.0, 1.0) * (points.size() - 1);
	const auto lo = static_cast<size_t>(at), hi = std::min(lo + 1, points.size() - 1);
	return points[lo] + (points[hi] - points[lo]) * (at - lo);
}

// Uniform-knot PCHIP (shape-preserving cubic Hermite). Harmonic interior
// slopes and limited one-sided endpoint slopes avoid overshoot, including
// beside flat segments. Slopes are in output units per knot interval.
inline double SampleSpline(const std::vector<double>& points, double x)
{
	if (points.size() < 3)
		return SampleCurve(points, x);
	if (x >= 1)
		return points.back() + (x - 1);
	const double at = std::clamp(x, 0.0, 1.0) * (points.size() - 1);
	const auto lo = static_cast<size_t>(at), hi = lo + 1;
	const auto delta = [&](size_t i)
	{
		return points[i + 1] - points[i];
	};
	const auto slope = [&](size_t i)
	{
		if (i == 0 || i == points.size() - 1)
		{
			const double edge = delta(i == 0 ? 0 : points.size() - 2);
			const double next = delta(i == 0 ? 1 : points.size() - 3);
			return std::clamp((3 * edge - next) / 2, 0.0, 3 * edge);
		}
		const double left = delta(i - 1), right = delta(i);
		return left <= 0 || right <= 0 ? 0.0 : 2 * left * right / (left + right);
	};
	const double t = at - lo, t2 = t * t, t3 = t2 * t;
	const double value = (2 * t3 - 3 * t2 + 1) * points[lo] + (t3 - 2 * t2 + t) * slope(lo) +
	                     (-2 * t3 + 3 * t2) * points[hi] + (t3 - t2) * slope(hi);
	return std::clamp(value, points[lo], points[hi]);
}

// Stage IDs match the editor: 2 RGB balance, 3 gamma, 4 levels, 5 tonal shaping.
inline double ApplyStageToSignal(double x, int channel, int stage, const Adjustments& a)
{
	if (stage == 2 && a.balanceEnabled)
		return ExtendedSrgbEncode(ExtendedSrgbDecode(x) * BalanceGains(a)[channel]);
	if (stage == 3 && a.gammaEnabled && x <= 1)
		return std::pow(x, 100.0 / a.midtone);
	if (stage == 4 && a.levelsEnabled)
	{
		const double black = a.levels[0] / 100.0, white = a.levels[1] / 100.0, low = a.levels[2] / 100.0,
					 high = a.levels[3] / 100.0;
		// HDR values above reference white continue from the mapped endpoint;
		// no discontinuous jump back to identity at the SDR/HDR boundary.
		if (x > 1)
			return high + (x - 1);
		return low + (high - low) * std::clamp((x - black) / (white - black), 0.0, 1.0);
	}
	if (stage == 5 && a.toneEnabled && x <= 1)
	{
		// Smooth endpoint-preserving curve; bounds keep the derivative positive.
		return x + (a.shadows / 100.0) * (x * std::pow(1 - x, 3) * 1.8) +
		       (a.highlights / 100.0) * (std::pow(x, 3) * (1 - x) * 1.8);
	}
	if (stage == 6 && a.curvesEnabled)
		return SampleSpline(a.curves[channel + 1], SampleSpline(a.curves[0], x));
	if (stage == 7 && a.importEnabled)
		return SampleCurve(a.imported[channel], x);
	return x;
}

inline double ApplyStageToCode(double code, int channel, int stage, const Adjustments& a, bool hdr, double white)
{
	const double signal = hdr ? ExtendedSrgbEncode(PqToNits(code) / white) : code;
	const double result = ApplyStageToSignal(signal, channel, stage, a);
	return hdr ? NitsToPq(std::clamp(white * ExtendedSrgbDecode(result), 0.0, 10000.0)) : std::clamp(result, 0.0, 1.0);
}
}
