#pragma once
#include <array>
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include "color_math.h"

namespace color
{
// Positions in Adjustments::Balance.
inline constexpr size_t BALANCE_RED = 0, BALANCE_GREEN = 1, BALANCE_BLUE = 2, BALANCE_WARMTH = 3, BALANCE_TINT = 4;

// Positions in Adjustments::Levels.
inline constexpr size_t INPUT_BLACK = 0, INPUT_WHITE = 1, OUTPUT_BLACK = 2, OUTPUT_WHITE = 3;

struct Adjustments
{
	std::array<int, 5> Balance { 100, 100, 100, 0, 0 }; // RGB percent, warmth, tint
	int Midtone = 100;
	std::array<int, 4> Levels { 0, 100, 0, 100 }; // input black/white, output black/white
	int Shadows = 0, Highlights = 0;
	bool BalanceEnabled = true, GammaEnabled = true, LevelsEnabled = true, ToneEnabled = true, CurvesEnabled = true,
		 ImportEnabled = true;
	std::array<std::vector<double>, 4> Curves;
	std::array<std::vector<double>, 3> Imported;
};

inline bool operator==(const Adjustments& a, const Adjustments& b)
{
	return a.Balance == b.Balance && a.Midtone == b.Midtone && a.Levels == b.Levels && a.Shadows == b.Shadows &&
	       a.Highlights == b.Highlights && a.BalanceEnabled == b.BalanceEnabled && a.GammaEnabled == b.GammaEnabled &&
	       a.LevelsEnabled == b.LevelsEnabled && a.ToneEnabled == b.ToneEnabled && a.CurvesEnabled == b.CurvesEnabled &&
	       a.ImportEnabled == b.ImportEnabled && a.Curves == b.Curves && a.Imported == b.Imported;
}

inline bool operator!=(const Adjustments& a, const Adjustments& b)
{
	return !(a == b);
}

inline void Validate(const Adjustments& a)
{
	for (int i = 0; i < 3; ++i)
		RequireRange(a.Balance[i], 50, 150);
	RequireRange(a.Balance[BALANCE_WARMTH], -50, 50);
	RequireRange(a.Balance[BALANCE_TINT], -50, 50);
	RequireRange(a.Midtone, 50, 200);
	for (int value : a.Levels)
		RequireRange(value, 0, 100);
	if (a.Levels[INPUT_WHITE] - a.Levels[INPUT_BLACK] < 5 || a.Levels[OUTPUT_WHITE] - a.Levels[OUTPUT_BLACK] < 5)
		throw std::invalid_argument("Black and white points must be separated by at least 5 percent");
	RequireRange(a.Shadows, -50, 50);
	RequireRange(a.Highlights, -50, 50);
	const auto curve = [](const std::vector<double>& points)
	{
		if (points.empty())
			return;
		if (points.size() < 2 || points.size() > 4096)
			throw std::invalid_argument("Curves need 2 to 4096 samples");
		double last = 0;
		for (double value : points)
		{
			RequireRange(value, 0, 1);
			if (value < last)
				throw std::invalid_argument("Curves must be nondecreasing");
			last = value;
		}
	};
	for (const auto& points : a.Curves)
	{
		if (!points.empty() && points.size() != 9)
			throw std::invalid_argument("Editable curves require nine points");
		curve(points);
	}
	for (const auto& points : a.Imported)
		curve(points);
	if (a.Imported[0].size() != a.Imported[1].size() || a.Imported[0].size() != a.Imported[2].size())
		throw std::invalid_argument("Imported channels must have equal sample counts");
}

inline std::array<double, 3> BalanceGains(const Adjustments& a)
{
	const double warm = a.Balance[BALANCE_WARMTH] / 100.0, tint = a.Balance[BALANCE_TINT] / 100.0;
	std::array<double, 3> gains { a.Balance[BALANCE_RED] / 100.0 * std::exp2(warm + tint * .5),
		                          a.Balance[BALANCE_GREEN] / 100.0 * std::exp2(-tint),
		                          a.Balance[BALANCE_BLUE] / 100.0 * std::exp2(-warm + tint * .5) };
	const double maximum = std::max({ 1.0, gains[0], gains[1], gains[2] });
	for (auto& gain : gains)
		gain /= maximum;
	return gains;
}

inline double ExtendedSrgbDecode(double x)
{
	return x <= SRGB_CODE_KNEE ? x / 12.92 : std::pow((x + .055) / 1.055, 2.4);
}

inline double ExtendedSrgbEncode(double x)
{
	return x <= SRGB_LINEAR_KNEE ? 12.92 * x : 1.055 * std::pow(x, 1 / 2.4) - .055;
}

inline double SampleCurve(const std::vector<double>& points, double x)
{
	if (points.empty())
		return x;
	if (x > 1)
		return points.back() + (x - 1);
	const double at = std::clamp(x, 0.0, 1.0) * (points.size() - 1);
	const auto lower = static_cast<size_t>(at), upper = std::min(lower + 1, points.size() - 1);
	return points[lower] + (points[upper] - points[lower]) * (at - lower);
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
	const auto lower = static_cast<size_t>(at), upper = lower + 1;
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
	const double t = at - lower, t2 = t * t, t3 = t2 * t;
	const double value = (2 * t3 - 3 * t2 + 1) * points[lower] + (t3 - 2 * t2 + t) * slope(lower) +
	                     (-2 * t3 + 3 * t2) * points[upper] + (t3 - t2) * slope(upper);
	return std::clamp(value, points[lower], points[upper]);
}

// Stage IDs match the editor: 2 RGB balance, 3 gamma, 4 levels, 5 tonal shaping.
inline double ApplyStageToSignal(double x, int channel, int stage, const Adjustments& a)
{
	if (stage == 2 && a.BalanceEnabled)
		return ExtendedSrgbEncode(ExtendedSrgbDecode(x) * BalanceGains(a)[channel]);
	if (stage == 3 && a.GammaEnabled && x <= 1)
		return std::pow(x, 100.0 / a.Midtone);
	if (stage == 4 && a.LevelsEnabled)
	{
		const double black = a.Levels[INPUT_BLACK] / 100.0, white = a.Levels[INPUT_WHITE] / 100.0,
					 low = a.Levels[OUTPUT_BLACK] / 100.0, high = a.Levels[OUTPUT_WHITE] / 100.0;
		// HDR values above reference white continue from the mapped endpoint;
		// no discontinuous jump back to identity at the SDR/HDR boundary.
		if (x > 1)
			return high + (x - 1);
		return low + (high - low) * std::clamp((x - black) / (white - black), 0.0, 1.0);
	}
	if (stage == 5 && a.ToneEnabled && x <= 1)
	{
		// Smooth endpoint-preserving curve; bounds keep the derivative positive.
		return x + (a.Shadows / 100.0) * (x * std::pow(1 - x, 3) * 1.8) +
		       (a.Highlights / 100.0) * (std::pow(x, 3) * (1 - x) * 1.8);
	}
	if (stage == 6 && a.CurvesEnabled)
		return SampleSpline(a.Curves[channel + 1], SampleSpline(a.Curves[0], x));
	if (stage == 7 && a.ImportEnabled)
		return SampleCurve(a.Imported[channel], x);
	return x;
}

inline double ApplyStageToCode(double code, int channel, int stage, const Adjustments& a, bool hdr, double white)
{
	const double signal = hdr ? ExtendedSrgbEncode(PqToNits(code) / white) : code;
	const double result = ApplyStageToSignal(signal, channel, stage, a);
	return hdr ? NitsToPq(std::clamp(white * ExtendedSrgbDecode(result), 0.0, 10000.0)) : std::clamp(result, 0.0, 1.0);
}
}
