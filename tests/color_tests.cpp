#include "color_math.h"
#include "recovery_rules.h"
#include <iostream>
#include <limits>
#include <string>

namespace
{
void Check(bool value, const char* name)
{
	if (!value)
		throw std::runtime_error(name);
}

void Near(double actual, double expected, double tolerance, const char* name)
{
	Check(std::isfinite(actual) && std::abs(actual - expected) <= tolerance, name);
}

template <class F>
void Rejects(F action, const char* name)
{
	bool rejected = false;
	try
	{
		action();
	}
	catch (const std::invalid_argument&)
	{
		rejected = true;
	}
	Check(rejected, name);
}

void Tests()
{
	// Published PQ anchors, independent of the correction implementation.
	Near(color::NitsToPq(100), 0.508078421517399, 1e-12, "PQ 100-nit anchor");
	Near(color::NitsToPq(1000), 0.751827096247041, 1e-12, "PQ 1000-nit anchor");
	Near(color::PqToNits(1), 10000, 1e-8, "PQ maximum");
	Check(color::NitsToPq(0) == 0 && color::PqToNits(0) == 0, "Exact black");
	for (double nits : { 0.001, 0.01, 0.1, 1.0, 10.0, 100.0, 1000.0, 10000.0 })
		Near(color::PqToNits(color::NitsToPq(nits)), nits, std::max(1e-8, nits * 1e-10), "PQ roundtrip");
	for (double code : { 0.0, 0.01, color::SRGB_CODE_KNEE, 0.1, 0.5, 1.0 })
		Near(color::SrgbEncode(color::SrgbDecode(code)), code, 1e-12, "sRGB roundtrip");

	// Work from source SDR values to expected luminance rather than only checking
	// that two implementations of the LUT formula agree.
	for (double white : { 80.0, 100.0, 200.0, 300.0, 480.0 })
		for (double gamma : { 2.2, 2.4 })
		{
			const color::HdrGammaCurve curve(white, gamma);
			for (double signal : { 0.01, 0.04, 0.1, 0.25, 0.5, 0.75 })
			{
				const double incoming = color::NitsToPq(white * color::SrgbDecode(signal));
				const double corrected = color::PqToNits(curve.Map(incoming));
				Near(corrected, white * std::pow(signal, gamma), 1e-7, "SDR-in-HDR target luminance");
			}
			Check(curve.Map(0) == 0, "Correction black endpoint");
			Check(curve.Map(curve.WhiteCode()) == curve.WhiteCode(), "Fixed SDR white");
			Check(curve.Map(1) == 1, "HDR peak unchanged");
			Near(curve.Map(curve.WhiteCode() - 1e-9), curve.WhiteCode(), 1e-8, "SDR-white continuity");
			for (std::size_t entries : { std::size_t { 256 }, std::size_t { 1024 }, std::size_t { 4096 } })
			{
				const auto lut = curve.SampleLut(entries);
				double previous = 0;
				for (std::size_t i = 0; i < entries; ++i)
				{
					const double x = static_cast<double>(i) / (entries - 1);
					Check(std::isfinite(lut[i]) && lut[i] >= previous && lut[i] >= 0 && lut[i] <= 1,
					      "Monotonic bounded LUT");
					if (x >= curve.WhiteCode())
						Check(lut[i] == x, "Exact HDR highlight passthrough");
					previous = lut[i];
				}
			}
		}
	const color::HdrGammaCurve reference(200, 2.2);
	const double low = color::NitsToPq(200 * color::SrgbDecode(0.1));
	Check(reference.Map(low) < low, "Expected shadow darkening");
	Rejects([] { color::HdrGammaCurve invalid(0, 2.2); }, "Invalid white rejected");
	Rejects([] { color::HdrGammaCurve invalid(200, std::numeric_limits<double>::quiet_NaN()); }, "NaN rejected");
	Rejects([&] { reference.Map(-0.1); }, "Invalid PQ input rejected");
	Rejects([&] { reference.SampleLut(1); }, "Short LUT rejected");
	Rejects([&] { reference.SampleLut(4097); }, "Oversized LUT rejected");

	// Exercise creative contrast in source SDR code space, before HDR gamma.
	for (double contrast : { -0.25, 0.0, 0.25 })
		for (bool fix : { false, true })
		{
			double previous = 0;
			for (int i = 0; i <= 4096; ++i)
			{
				const double x = i / 4096.0;
				const double y = color::ApplyHdrContrastAndGamma(x, 280, 2.2, contrast, 0.5, fix);
				Check(std::isfinite(y) && y >= previous && y <= 1, "Combined HDR curve monotonicity");
				if (x >= color::NitsToPq(280))
					Check(y == x, "Combined HDR highlight passthrough");
				if (!fix && contrast == 0)
					Check(y == x, "Disabled HDR operations preserve identity");
				if (fix && contrast == 0)
					Near(y, color::HdrGammaCurve(280, 2.2).Map(x), 1e-15, "Zero-contrast HDR regression");
				previous = y;
			}
			for (double signal : { 0.1, 0.25, 0.5, 0.75 })
			{
				const double input = color::NitsToPq(280 * color::SrgbDecode(signal));
				const double adjusted = color::ApplyContrast(signal, contrast, 0.5);
				const double expected = 280 * (fix ? std::pow(adjusted, 2.2) : color::SrgbDecode(adjusted));
				Near(color::PqToNits(color::ApplyHdrContrastAndGamma(input, 280, 2.2, contrast, 0.5, fix)), expected,
				     1e-7, "Combined HDR luminance semantics");
			}
		}
	live::Ramp baseline {}, last {}, candidate {}, external {};
	for (size_t c = 0; c < 3; ++c)
		for (size_t i = 0; i < 256; ++i)
		{
			baseline[c][i] = static_cast<std::uint16_t>(i * 257);
			last[c][i] = static_cast<std::uint16_t>(color::ApplyContrast(i / 255.0, 0.2, 0.5) * 65535);
			candidate[c][i] = static_cast<std::uint16_t>(color::ApplyContrast(i / 255.0, -0.2, 0.5) * 65535);
			external[c][i] = static_cast<std::uint16_t>(i * 200);
		}
	Check(live::IsRampOurs(baseline, baseline, last, candidate, false, false), "Untouched baseline recoverable");
	Check(live::IsRampOurs(last, baseline, last, candidate, true, false), "Committed correction recoverable");
	Check(live::IsRampOurs(candidate, baseline, last, candidate, true, true), "Crash during write recoverable");
	Check(!live::IsRampOurs(candidate, baseline, last, candidate, true, false), "Unpublished candidate not owned");
	Check(!live::IsRampOurs(external, baseline, last, candidate, true, true), "External calibration preserved");
}
}

int main()
{
	try
	{
		Tests();
		std::cout << "HDR curve tests passed. No display API calls.\n";
		return 0;
	}
	catch (const std::exception& error)
	{
		std::cerr << error.what() << '\n';
		return 1;
	}
}
