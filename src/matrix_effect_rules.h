#pragma once
#include "matrix_effect.h"
#include "matrix_math.h"
namespace matrix_effect
{
// Ownership checks are shared by the real worker and its fake-API tests.
// Never compose onto an unknown matrix or overwrite another color tool.
class Engine
{
	matrix_color::Api& api_;
	matrix_color::Matrix baseline_ = matrix_color::IdentityMatrix(), ours_ = baseline_, attempted_ = baseline_;
	bool owns_ = false, pending_ = false;

public:
	explicit Engine(matrix_color::Api& api) : api_(api) {}
	State Stop()
	{
		if (!owns_)
			return State::Off;
		matrix_color::Matrix current {};
		const bool known = api_.Read(current);
		if (known && matrix_color::MaxDifference(current, baseline_) < 1e-5f)
		{
			owns_ = pending_ = false;
			return State::Off;
		}
		if (known && matrix_color::MaxDifference(current, ours_) > 1e-5f &&
		    (!pending_ || matrix_color::MaxDifference(current, attempted_) > 1e-5f))
		{
			owns_ = pending_ = false;
			return State::ExternalChange;
		}
		api_.Write(baseline_);
		if (api_.Read(current) && matrix_color::MaxDifference(current, baseline_) < 1e-5f)
		{
			owns_ = pending_ = false;
			return State::Off;
		}
		// Retain both known owned values after a transient failure. A later
		// pause/exit can retry instead of mistaking our own matrix for a rival.
		return State::Failed;
	}
	State Apply(int amount)
	{
		if (amount < 0 || amount == 100)
			return Stop();
		if (amount > color::SaturationMaximum)
			return State::Failed;
		return Apply(matrix_color::SaturationMatrix(amount / 100.f));
	}
	State Apply(const matrix_color::Matrix& target)
	{
		if (pending_)
		{
			const auto restored = Stop();
			if (restored != State::Off)
				return restored;
		}
		matrix_color::Matrix current {};
		if (!api_.Read(current))
			return State::Failed;
		if (owns_ && matrix_color::MaxDifference(current, ours_) > 1e-5f &&
		    matrix_color::MaxDifference(current, baseline_) > 1e-5f)
		{
			owns_ = pending_ = false;
			return State::ExternalChange;
		}
		if (!owns_)
		{
			if (matrix_color::MaxDifference(current, matrix_color::IdentityMatrix()) > 1e-5f)
				return State::OtherTool;
			baseline_ = current;
			ours_ = baseline_;
		}
		if (owns_ && matrix_color::MaxDifference(current, target) < 1e-5f)
		{
			ours_ = target;
			return State::Active;
		}
		// Track both old and attempted values until readback verifies the new
		// one, so a failed read or write cannot lose recovery ownership.
		attempted_ = target;
		pending_ = owns_ = true;
		api_.Write(target);
		matrix_color::Matrix readback {};
		if (!api_.Read(readback) || matrix_color::MaxDifference(readback, target) > 1e-5f)
		{
			const auto restored = Stop();
			return restored == State::ExternalChange ? restored : State::Failed;
		}
		ours_ = target;
		pending_ = false;
		return State::Active;
	}
};
}
