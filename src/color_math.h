#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <stdexcept>
#include <vector>

// Platform-independent math. No display access, profile installation, or shaders.
namespace color
{
inline void RequireRange(double value, double low, double high)
{
	if (!std::isfinite(value) || value < low || value > high)
		throw std::invalid_argument("Color parameter outside supported range");
}

// Endpoint-preserving S curve. Zero amount is identity; pivot is fixed.
inline double ApplyContrast(double x, double amount, double pivot)
{
	if (x <= 0 || x >= 1 || amount == 0 || x == pivot)
		return x;
	const double relativeOdds = (x / (1 - x)) / (pivot / (1 - pivot));
	return 1 / (1 + ((1 - pivot) / pivot) * std::pow(relativeOdds, -(1 + amount)));
}

// SMPTE ST 2084 / ITU-R BT.2100 PQ, with luminance in cd/m2 (nits).
inline constexpr double pqM1 = 2610.0 / 16384.0;
inline constexpr double pqM2 = 2523.0 / 32.0;
inline constexpr double pqC1 = 3424.0 / 4096.0;
inline constexpr double pqC2 = 2413.0 / 128.0;
inline constexpr double pqC3 = 2392.0 / 128.0;

inline double PqToNits(double code)
{
	RequireRange(code, 0, 1);
	const double power = std::pow(code, 1 / pqM2);
	return 10000 * std::pow(std::max(power - pqC1, 0.0) / (pqC2 - pqC3 * power), 1 / pqM1);
}

inline double NitsToPq(double nits)
{
	RequireRange(nits, 0, 10000);
	// Canonicalize black to exact code zero rather than the formula's tiny pedestal.
	if (nits == 0)
		return 0;
	const double power = std::pow(nits / 10000, pqM1);
	return std::pow((pqC1 + pqC2 * power) / (1 + pqC3 * power), pqM2);
}

// Intersection of the two sRGB branches, avoiding rounding discontinuity at
// the commonly rounded 0.04045/0.0031308 breakpoints.
inline constexpr double srgbCodeKnee = 0.0404482362771082;
inline constexpr double srgbLinearKnee = 0.00313066844250063;
inline double SrgbDecode(double code)
{
	RequireRange(code, 0, 1);
	return code <= srgbCodeKnee ? code / 12.92 : std::pow((code + 0.055) / 1.055, 2.4);
}

inline double SrgbEncode(double linear)
{
	RequireRange(linear, 0, 1);
	return linear <= srgbLinearKnee ? linear * 12.92 : 1.055 * std::pow(linear, 1 / 2.4) - 0.055;
}

class HdrGammaCurve
{
	double whiteNits_, gamma_, whiteCode_;

public:
	HdrGammaCurve(double whiteNits, double gamma) : whiteNits_(whiteNits), gamma_(gamma), whiteCode_(0)
	{
		RequireRange(whiteNits, 1, 10000);
		RequireRange(gamma, 1, 4);
		whiteCode_ = NitsToPq(whiteNits);
	}
	double WhiteNits() const { return whiteNits_; }
	double Gamma() const { return gamma_; }
	double WhiteCode() const { return whiteCode_; }
	double Map(double input) const
	{
		RequireRange(input, 0, 1);
		if (input == 0 || input >= whiteCode_)
			return input;
		const double relativeLight = std::clamp(PqToNits(input) / whiteNits_, 0.0, 1.0);
		const double signal = SrgbEncode(relativeLight);
		const double correctedNits = whiteNits_ * std::pow(signal, gamma_);
		return NitsToPq(std::clamp(correctedNits, 0.0, 10000.0));
	}
	std::vector<double> SampleLut(std::size_t entries) const
	{
		if (entries < 2 || entries > 4096)
			throw std::invalid_argument("LUT entries must be in [2,4096]");
		std::vector<double> output(entries);
		for (std::size_t i = 0; i < entries; ++i)
			output[i] = Map(static_cast<double>(i) / (entries - 1));
		return output;
	}
};

// Creative contrast is evaluated in the reconstructed SDR signal, before the
// target gamma. HDR highlights above SDR white remain unchanged in this version.
inline double ApplyHdrContrastAndGamma(double code, double white, double gamma, double amount, double pivot,
                                       bool correctGamma)
{
	RequireRange(code, 0, 1);
	RequireRange(white, 1, 10000);
	RequireRange(gamma, 1, 4);
	RequireRange(amount, -0.25, 0.25);
	RequireRange(pivot, 0.2, 0.8);
	if (!correctGamma && amount == 0)
		return code;
	if (amount == 0 && correctGamma)
		return HdrGammaCurve(white, gamma).Map(code);
	if (code == 0 || code >= NitsToPq(white))
		return code;
	const double signal = SrgbEncode(std::clamp(PqToNits(code) / white, 0.0, 1.0));
	const double creative = ApplyContrast(signal, amount, pivot);
	const double relative = correctGamma ? std::pow(creative, gamma) : SrgbDecode(creative);
	return NitsToPq(std::clamp(white * relative, 0.0, 10000.0));
}

// Compile the selected two-effect order into one point transform. Captured
// per-channel monitor calibration is composed later by the output backend.
inline double ApplyHdrEffectsInOrder(double code, double white, double gamma, double amount, double pivot,
                                     bool correctGamma, bool correctionFirst)
{
	if (!correctionFirst)
		return ApplyHdrContrastAndGamma(code, white, gamma, amount, pivot, correctGamma);
	const double corrected = correctGamma ? HdrGammaCurve(white, gamma).Map(code) : code;
	return ApplyHdrContrastAndGamma(corrected, white, gamma, amount, pivot, false);
}
} // namespace color
