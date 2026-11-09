#pragma once
#include "live_lut.h"

namespace live
{
// Key every transform input so preset, bypass, order and HDR changes cannot
// leave a stale active/paused indication. The expensive scan runs on changes.
class ProcessingState
{
	Settings _settings;
	bool _valid = false, _hdr = false, _result = false;
	double _white = 0;

public:
	bool IsProcessing(bool session, const Output& output, const Settings& settings)
	{
		if (!session)
		{
			_valid = false;
			return false;
		}
		if (_valid && _hdr == output.hdr && _white == output.white && _settings.Contrast == settings.Contrast &&
		    _settings.Pivot == settings.Pivot && _settings.Gamma == settings.Gamma &&
		    _settings.HdrFix == settings.HdrFix && _settings.Bypass == settings.Bypass &&
		    _settings.CorrectionFirst == settings.CorrectionFirst && _settings.ApplyInSdr == settings.ApplyInSdr &&
		    _settings.Order == settings.Order && _settings.NightWarmth == settings.NightWarmth &&
		    _settings.Adjustments == settings.Adjustments)
			return _result;
		_settings = settings;
		_hdr = output.hdr;
		_white = output.white;
		_valid = true;
		_result = false;
		for (int c = 0; c < 3 && !_result; ++c)
			for (int i = 0; i < 256; ++i)
			{
				const double x = i / 255.0;
				if (std::abs(EvaluateOutputCode(x, c, output, settings) - x) > 1e-7)
				{
					_result = true;
					break;
				}
			}
		return _result;
	}
};
}
