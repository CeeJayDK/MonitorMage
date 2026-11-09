#pragma once
#include "matrix_effect.h"
#include "matrix_math.h"
namespace matrix_effect
{
// Ownership checks are shared by the real worker and its fake-API tests.
// Never compose onto an unknown matrix or overwrite another color tool.
class Engine
{
	matrix_color::Api& _api;
	matrix_color::Matrix _baseline = matrix_color::IdentityMatrix(), _ours = _baseline, _attempted = _baseline;
	bool _owns = false, _pending = false;

public:
	explicit Engine(matrix_color::Api& api) : _api(api) {}
	State Stop()
	{
		if (!_owns)
			return State::Off;
		matrix_color::Matrix current {};
		const bool known = _api.Read(current);
		if (known && matrix_color::MaxDifference(current, _baseline) < 1e-5f)
		{
			_owns = _pending = false;
			return State::Off;
		}
		if (known && matrix_color::MaxDifference(current, _ours) > 1e-5f &&
		    (!_pending || matrix_color::MaxDifference(current, _attempted) > 1e-5f))
		{
			_owns = _pending = false;
			return State::ExternalChange;
		}
		_api.Write(_baseline);
		if (_api.Read(current) && matrix_color::MaxDifference(current, _baseline) < 1e-5f)
		{
			_owns = _pending = false;
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
		if (amount > color::SATURATION_MAXIMUM)
			return State::Failed;
		return Apply(matrix_color::SaturationMatrix(amount / 100.f));
	}
	State Apply(const matrix_color::Matrix& target)
	{
		if (_pending)
		{
			const auto restored = Stop();
			if (restored != State::Off)
				return restored;
		}
		matrix_color::Matrix current {};
		if (!_api.Read(current))
			return State::Failed;
		if (_owns && matrix_color::MaxDifference(current, _ours) > 1e-5f &&
		    matrix_color::MaxDifference(current, _baseline) > 1e-5f)
		{
			_owns = _pending = false;
			return State::ExternalChange;
		}
		if (!_owns)
		{
			if (matrix_color::MaxDifference(current, matrix_color::IdentityMatrix()) > 1e-5f)
				return State::OtherTool;
			_baseline = current;
			_ours = _baseline;
		}
		if (_owns && matrix_color::MaxDifference(current, target) < 1e-5f)
		{
			_ours = target;
			return State::Active;
		}
		// Track both old and attempted values until readback verifies the new
		// one, so a failed read or write cannot lose recovery ownership.
		_attempted = target;
		_pending = _owns = true;
		_api.Write(target);
		matrix_color::Matrix readback {};
		if (!_api.Read(readback) || matrix_color::MaxDifference(readback, target) > 1e-5f)
		{
			const auto restored = Stop();
			return restored == State::ExternalChange ? restored : State::Failed;
		}
		_ours = target;
		_pending = false;
		return State::Active;
	}
};
}
