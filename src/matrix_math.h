#pragma once
#include <array>
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include "color_limits.h"
namespace matrix_color
{
using Matrix = std::array<std::array<float, 5>, 5>;
inline Matrix IdentityMatrix()
{
	Matrix matrix {};
	for (int i = 0; i < 5; ++i)
		matrix[i][i] = 1;
	return matrix;
}

// Row-vector convention used by MAGCOLOREFFECT. These are encoded-RGB
// experiments, not a claim about linear-light luminance or HDR colorimetry.
inline Matrix SaturationMatrix(float strength)
{
	if (!std::isfinite(strength) || strength < 0 || strength > color::SATURATION_MAXIMUM / 100.f)
		throw std::invalid_argument("Invalid saturation");
	constexpr float LUMINANCE_WEIGHTS[] { .2126f, .7152f, .0722f };
	auto m = IdentityMatrix();
	for (int input = 0; input < 3; ++input)
		for (int output = 0; output < 3; ++output)
			m[input][output] = LUMINANCE_WEIGHTS[input] * (1 - strength) + (input == output ? strength : 0);
	return m;
}

inline Matrix PreviewMatrix(int mode)
{
	if (mode < 0 || mode > 2)
		throw std::invalid_argument("Invalid preview mode");
	return SaturationMatrix(mode == 0 ? 0.f : mode == 1 ? .5f : 1.2f);
}

inline Matrix MonochromeMatrix(std::array<int, 3> weights)
{
	for (int value : weights)
		if (value < 0 || value > 200)
			throw std::invalid_argument("Invalid channel mix");
	int sum = weights[0] + weights[1] + weights[2];
	if (!sum)
	{
		weights = { 21, 72, 7 };
		sum = 100;
	}
	auto m = IdentityMatrix();
	for (int input = 0; input < 3; ++input)
		for (int output = 0; output < 3; ++output)
			m[input][output] = weights[input] / static_cast<float>(sum);
	return m;
}

inline Matrix MultiplyMatrices(const Matrix& first, const Matrix& second)
{
	Matrix result {};
	for (int r = 0; r < 5; ++r)
		for (int c = 0; c < 5; ++c)
			for (int k = 0; k < 5; ++k)
				result[r][c] += first[r][k] * second[k][c];
	return result;
}

inline float MaxDifference(const Matrix& a, const Matrix& b)
{
	float worst = 0;
	for (int r = 0; r < 5; ++r)
		for (int c = 0; c < 5; ++c)
		{
			if (!std::isfinite(a[r][c]) || !std::isfinite(b[r][c]))
				return INFINITY;
			worst = std::max(worst, std::abs(a[r][c] - b[r][c]));
		}
	return worst;
}

struct Api
{
	virtual bool Read(Matrix&) = 0;
	virtual bool Write(const Matrix&) = 0;
	virtual ~Api() = default;
};

enum class Restoration
{
	Restored,
	ExternalChange,
	Failed
};

inline Restoration RestoreBaseline(Api& api, const Matrix& baseline, const Matrix& ours)
{
	Matrix current {};
	const bool known = api.Read(current);
	if (known && MaxDifference(current, baseline) < 1e-5f)
		return Restoration::Restored;
	if (known && MaxDifference(current, ours) > 1e-5f)
		return Restoration::ExternalChange;
	// After a read failure, still attempt to remove our temporary adjustment.
	api.Write(baseline);
	return api.Read(current) && MaxDifference(current, baseline) < 1e-5f ? Restoration::Restored : Restoration::Failed;
}
}
