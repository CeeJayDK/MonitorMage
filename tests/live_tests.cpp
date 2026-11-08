#include "live_lut.h"
#include "processing_state.h"
#include "night_outputs.h"
#include <iostream>
#include <stdexcept>

namespace
{
void Check(bool condition, const char* message)
{
	if (!condition)
		throw std::runtime_error(message);
}

struct NightProbe
{
	inline static int starts = 0, stops = 0;
	bool running = false;
	live::Output output;
	~NightProbe() { Stop(); }
	bool IsActive() const { return running; }
	bool HasSameDisplayMode(const live::Output& next) const { return running && output.identity == next.identity; }
	void InheritCarryOver(const NightProbe&) {}
	void Start(const live::Output& next, const live::Settings& settings)
	{
		Check(!running, "Night light must restore before recapturing");
		Check(settings.bypass && !settings.hdrFix && settings.nightWarmth > 0,
		      "Additional displays must receive only Night light");
		output = next;
		running = true;
		++starts;
	}
	void Update(const live::Output& next, const live::Settings&)
	{
		Check(HasSameDisplayMode(next), "A changed mode needs a new capture");
	}
	int Stop()
	{
		if (running)
		{
			running = false;
			++stops;
		}
		return 0;
	}
};

void NightTests()
{
	night::Settings config;
	config.enabled = true;
	config.strength = 100;
	Check(night::CurrentPhase(config, 12 * 3600).level == 1 && night::CurrentPhase(config, 0).nextMs == 0,
	      "Manual Night light must not need a polling timer");
	config.scheduled = true;
	config.fade = 10;
	Check(night::CurrentPhase(config, 12 * 3600).level == 0, "Daytime must be neutral");
	Check(night::CurrentPhase(config, 12 * 3600).nextMs == 9 * 3600000u,
	      "Daytime must sleep until the evening boundary");
	Check(night::CurrentPhase(config, 21 * 3600).level == 0 && night::CurrentPhase(config, 21 * 3600).nextMs == 5000,
	      "Fade starts at zero and requests bounded updates");
	Check(std::abs(night::CurrentPhase(config, 21 * 3600 + 300).level - .5) < 1e-9, "Evening fade midpoint");
	Check(night::CurrentPhase(config, 0).level == 1 && night::CurrentPhase(config, 0).nextMs > 3600000,
	      "Overnight plateau must sleep");
	Check(std::abs(night::CurrentPhase(config, 7 * 3600 - 300).level - .5) < 1e-9 &&
	          night::CurrentPhase(config, 7 * 3600).level == 0,
	      "Morning fade and exact end");
	config.start = 8 * 60;
	config.end = 9 * 60;
	config.fade = 60;
	Check(night::CurrentPhase(config, 8.5 * 3600).level == 1, "Short windows must clamp overlapping fades");
	config.enabled = false;
	Check(night::CurrentPhase(config, 8.5 * 3600).nextMs == 0, "Disabled schedule must have no wakeups");
	Check(night::ParseTime(L"23:59") == 1439 && night::ParseTime(L"24:00") == -1 && night::ParseTime(L"9:00") == -1,
	      "Time validation");
	const auto keyA = std::wstring(L"panel-A:0:0\n"), keyB = std::wstring(L"panel-B:0:0\n");
	night::SetDisplayIncluded(config, keyA, false);
	Check(!night::IsDisplayIncluded(config, L"panel-A:1:1\n") && night::IsDisplayIncluded(config, keyB),
	      "Display exclusions must survive HDR changes");
	Check(!night::IsDisplayIncluded(config, keyB + keyA), "Clone groups must honor excluded physical members");
	night::SetDisplayIncluded(config, keyB + keyA, true);
	Check(config.excluded.empty(), "Reincluding a clone group includes its members");
	live::Output a, b;
	a.name = L"A";
	a.identity = keyA;
	b.name = L"B";
	b.identity = keyB;
	night::OutputSessions<NightProbe> sessions;
	sessions.Update({ a, b }, a, config, .5);
	Check(NightProbe::starts == 1, "Do not apply a second ramp to the selected display");
	b.identity = L"panel-B:1:1\n";
	sessions.Update({ a, b }, a, config, .6);
	Check(NightProbe::stops == 1 && NightProbe::starts == 2, "Mode transition must restore before recapturing");
	sessions.Release(b);
	Check(!sessions.IsActive(), "Release auxiliary ramp before the editor takes ownership");
	sessions.Update({ a, b }, b, config, .5);
	Check(sessions.IsActive(), "Switching the selected group must keep other displays warm");
	night::SetDisplayIncluded(config, a.identity, false);
	sessions.Update({ a, b }, b, config, .5);
	Check(!sessions.IsActive(), "Excluding a display restores its captured ramp");
}
}

int main()
{
	try
	{
		Check(live::WriterLeaseSelfTest() == 0,
		      "Independent output sessions share writer ownership and preserve recovery exclusion");
		NightTests();
		live::Ramp baseline {}, identity {};
		for (size_t i = 0; i < 256; ++i)
		{
			baseline[0][i] = static_cast<std::uint16_t>(i * 240);
			baseline[1][i] = static_cast<std::uint16_t>(100 + i * 230);
			baseline[2][i] = static_cast<std::uint16_t>(200 + i * 220);
			for (auto& channel : identity)
				channel[i] = static_cast<std::uint16_t>(i * 257);
		}
		live::Output sdr;
		live::Settings off;
		off.hdrFix = false;
		live::ProcessingState state;
		Check(!state.IsProcessing(false, sdr, off), "No session must appear paused");
		Check(!state.IsProcessing(true, sdr, off), "Neutral session must appear ready");
		Check(!state.IsProcessing(true, sdr, off), "Cached neutral result must be stable");
		auto changed = off;
		changed.contrast = .2;
		Check(state.IsProcessing(true, sdr, changed), "Changed contrast must invalidate neutral cache");
		changed.bypass = true;
		Check(!state.IsProcessing(true, sdr, changed), "Bypass must invalidate active cache");
		changed = off;
		changed.adjustments.balance[0] = 80;
		Check(state.IsProcessing(true, sdr, changed), "RGB settings must invalidate neutral cache");
		changed.adjustments.balanceEnabled = false;
		Check(!state.IsProcessing(true, sdr, changed), "Layer disable must invalidate active cache");
		auto hdrState = sdr;
		hdrState.hdr = true;
		changed = off;
		changed.hdrFix = true;
		Check(state.IsProcessing(true, hdrState, changed), "HDR transition must invalidate cache");
		Check(!state.IsProcessing(false, hdrState, changed), "Ending a session must clear active state");
		Check(live::BuildGammaRamp(baseline, sdr, off) == baseline,
		      "SDR identity must preserve captured calibration exactly");
		live::Output hdr;
		hdr.hdr = true;
		hdr.white = 280;
		for (double gamma : { 2.2, 2.4 })
		{
			live::Settings correction;
			correction.gamma = gamma;
			Check(live::BuildGammaRamp(baseline, sdr, correction) == baseline, "SDR gamma correction must be opt-in");
			const auto originalHdr = live::BuildGammaRamp(identity, hdr, correction);
			correction.applyInSdr = true;
			Check(live::BuildGammaRamp(identity, hdr, correction) == originalHdr,
			      "SDR opt-in must not change the HDR mapping");
			const auto adjusted = live::BuildGammaRamp(identity, sdr, correction);
			Check(adjusted != identity && adjusted[0][0] == 0 && adjusted[0][255] == 65535,
			      "SDR compensation must alter tones and retain endpoints");
			for (size_t i = 1; i < 256; ++i)
				Check(adjusted[0][i] >= adjusted[0][i - 1], "SDR compensation must remain monotone");
			const double x = .1, expected = color::SrgbEncode(std::pow(x, gamma));
			Check(std::abs(live::EvaluateOutputCode(x, 0, sdr, correction) - expected) < 1e-14,
			      "SDR correction must encode the selected power response through sRGB");
			correction.bypass = true;
			Check(live::BuildGammaRamp(identity, sdr, correction) == adjusted,
			      "Creative bypass must retain monitor correction in SDR");
			correction.hdrFix = false;
			Check(live::BuildGammaRamp(baseline, sdr, correction) == baseline,
			      "Correction's single enable switch must control both modes");
		}
		Check(live::BuildGammaRamp(baseline, hdr, off) == baseline,
		      "Disabled HDR effects must preserve baseline exactly");
		live::Settings contrast;
		contrast.contrast = 0.2;
		const auto creative = live::BuildGammaRamp(baseline, sdr, contrast);
		for (size_t c = 0; c < 3; ++c)
		{
			Check(creative[c][0] == baseline[c][0] && creative[c][255] == baseline[c][255],
			      "Calibration endpoints must remain intact");
			Check(creative[c][64] < baseline[c][64] && creative[c][192] > baseline[c][192],
			      "Creative curve must precede monitor calibration");
		}
		const auto gammaOnly = live::BuildGammaRamp(identity, hdr, live::Settings {});
		hdr.identity = L"LG-device:3:2\n";
		auto previousSdr = sdr;
		previousSdr.identity = L"LG-device:0:0\n";
		// An already-loaded fix is replaced, not used as the calibration into
		// which a second copy of the fix would be composed.
		for (double gamma : { 2.2, 2.4 })
		{
			live::Settings existing;
			existing.gamma = gamma;
			const auto loaded = live::BuildGammaRamp(identity, hdr, existing);
			const auto clean = live::CompositionBaseline(loaded, hdr);
			Check(clean == identity, "Known HDR correction must be removed from composition baseline");
			Check(live::BuildGammaRamp(clean, hdr, existing) == loaded, "Taking over the same fix must not double it");
			auto desired = existing;
			desired.contrast = .15;
			desired.gamma = 2.4;
			Check(live::BuildGammaRamp(clean, hdr, desired) == live::BuildGammaRamp(identity, hdr, desired),
			      "Taking over must support changing fix and adding effects");
			auto quantized = loaded;
			quantized[0][64] += 2;
			Check(live::CompositionBaseline(quantized, hdr) == identity, "Known fix must tolerate two readback units");
			auto unknown = loaded;
			unknown[1][64] += 32;
			Check(live::CompositionBaseline(unknown, hdr) == unknown, "Unknown channel calibration must be preserved");
			Check(live::CompositionBaseline(loaded, sdr) == loaded, "SDR curves must not be mistaken for an HDR fix");
		}
		auto staleSettings = off;
		staleSettings.contrast = .2;
		const auto stale = live::BuildGammaRamp(identity, previousSdr, staleSettings);
		Check(live::CompositionBaseline(stale, hdr, &stale, &identity, &previousSdr) == identity,
		      "Own stale correction must recover using known neutral calibration across modes");
		const auto calibratedStale = live::BuildGammaRamp(baseline, previousSdr, staleSettings);
		auto sameSdr = previousSdr;
		sameSdr.identity = L"LG-device:8:0\n";
		Check(live::CompositionBaseline(calibratedStale, sameSdr, &calibratedStale, &baseline, &previousSdr) ==
		          baseline,
		      "Same-mode recovery must preserve non-neutral calibration");
		auto unrelated = hdr;
		unrelated.identity = L"Samsung-device:0:0\n";
		Check(live::CompositionBaseline(stale, unrelated, &stale, &identity, &previousSdr) == stale,
		      "Coincidentally equal ramp on a different monitor must be preserved");
		auto external = stale;
		external[0][64] += 32;
		Check(live::CompositionBaseline(external, hdr, &stale, &identity, &previousSdr) == external,
		      "External changes must not be replaced with a saved baseline");
		bool ambiguous = false;
		try
		{
			live::CompositionBaseline(calibratedStale, hdr, &calibratedStale, &baseline, &previousSdr);
		}
		catch (const std::runtime_error&)
		{
			ambiguous = true;
		}
		Check(ambiguous, "Non-neutral calibration must not be guessed across SDR/HDR modes");
		// Explicit mode-transition policy: reset calibration, then compile the
		// same enabled effects for the new mode, without retaining the HDR fix.
		auto nextSdr = previousSdr;
		auto settingsAfterSwitch = live::Settings {};
		settingsAfterSwitch.contrast = .2;
		const auto staleHdr = live::BuildGammaRamp(baseline, hdr, settingsAfterSwitch);
		const auto reset = live::CompositionBaseline(staleHdr, nextSdr, nullptr, nullptr, nullptr, true);
		Check(reset == identity, "Automatic HDR-to-SDR transition must use identity even after nonneutral calibration");
		const auto resumedSdr = live::BuildGammaRamp(reset, nextSdr, settingsAfterSwitch);
		auto contrastOnly = settingsAfterSwitch;
		contrastOnly.hdrFix = false;
		Check(resumedSdr == live::BuildGammaRamp(identity, nextSdr, contrastOnly),
		      "SDR resume must skip HDR gamma and keep contrast");
		Check(resumedSdr != identity, "SDR contrast must remain active after the mode switch");
		settingsAfterSwitch.contrast = 0;
		Check(live::BuildGammaRamp(reset, nextSdr, settingsAfterSwitch) == identity,
		      "No SDR effects still requires clearing the stale HDR ramp");
		settingsAfterSwitch.applyInSdr = true;
		Check(live::BuildGammaRamp(reset, nextSdr, settingsAfterSwitch) != identity,
		      "An explicit SDR gamma opt-in must survive the transition");
		settingsAfterSwitch.applyInSdr = false;
		const auto resetHdr = live::CompositionBaseline(resumedSdr, hdr, nullptr, nullptr, nullptr, true);
		Check(live::BuildGammaRamp(resetHdr, hdr, settingsAfterSwitch) == gammaOnly,
		      "Returning to HDR must reapply its enabled gamma fix once");
		Check(live::SameDisplays(hdr, nextSdr), "Mode detection must match the same physical monitor across HDR/SDR");
		Check(!live::SameDisplays(hdr, unrelated), "Mode detection must not reset an unrelated monitor");
		auto clone = hdr;
		clone.identity += L"Samsung-device:0:0\n";
		Check(!live::SameDisplays(hdr, clone), "A changed clone group must not be confused with a simple mode switch");
		// Anchors observed in the verified LG HDR path, allowing one WORD of rounding.
		Check(std::abs(static_cast<int>(gammaOnly[0][64]) - 15286) <= 1,
		      "Live HDR curve must match verified shadow anchor");
		Check(std::abs(static_cast<int>(gammaOnly[0][128]) - 33028) <= 1,
		      "Live HDR curve must match verified midtone anchor");
		const auto combined = live::BuildGammaRamp(identity, hdr, contrast);
		Check(combined[0][32] < gammaOnly[0][32], "HDR creative contrast must deepen SDR-range shadows");
		auto paused = contrast;
		paused.hdrFix = false;
		const auto creativeHdr = live::BuildGammaRamp(baseline, hdr, paused);
		Check(creativeHdr != baseline && creativeHdr != live::BuildGammaRamp(baseline, hdr, contrast),
		      "HDR-only pause must retain contrast while removing gamma correction");
		// Highlights above SDR white and captured calibration stay intact during the pause.
		for (size_t c = 0; c < 3; ++c)
			for (size_t i = 192; i < 256; ++i)
				Check(creativeHdr[c][i] == baseline[c][i], "HDR-only pause must preserve highlights and calibration");
		paused.bypass = true;
		Check(live::BuildGammaRamp(baseline, hdr, paused) == baseline,
		      "HDR pause plus creative bypass must preserve captured calibration exactly");
		auto reversed = contrast;
		reversed.correctionFirst = true;
		const auto reverseRamp = live::BuildGammaRamp(baseline, hdr, reversed);
		Check(reverseRamp != live::BuildGammaRamp(baseline, hdr, contrast),
		      "Reordering must change the compiled HDR ramp, not just the list");
		for (size_t c = 0; c < 3; ++c)
			for (size_t i = 192; i < 256; ++i)
				Check(reverseRamp[c][i] == baseline[c][i],
				      "Reordered effects must preserve HDR highlights and captured calibration");
		reversed.hdrFix = false;
		Check(live::BuildGammaRamp(baseline, hdr, reversed) == creativeHdr,
		      "Order must have no effect when gamma correction is disabled");
		reversed.hdrFix = true;
		reversed.bypass = true;
		auto gammaCalibrated = live::Settings {};
		Check(live::BuildGammaRamp(baseline, hdr, reversed) == live::BuildGammaRamp(baseline, hdr, gammaCalibrated),
		      "Creative bypass must preserve gamma and calibration in either order");
		contrast.bypass = true;
		Check(live::BuildGammaRamp(identity, hdr, contrast) == gammaOnly, "Bypass must retain HDR correction");
		Check(live::BuildGammaRamp(baseline, sdr, contrast) == baseline,
		      "SDR bypass must restore captured calibration");
		auto warm = off;
		warm.nightWarmth = .7;
		warm.bypass = true;
		for (const auto& output : { sdr, hdr })
		{
			const auto result = live::BuildGammaRamp(identity, output, warm);
			Check(state.IsProcessing(true, output, warm), "Night light must update active-state cache");
			Check(result[0] == identity[0] && result[2][200] < result[1][200] && result[1][200] < result[0][200],
			      "Night light reduces blue and green without boosting or clipping red");
			for (const auto& channel : result)
				for (size_t i = 1; i < 256; ++i)
					Check(channel[i] >= channel[i - 1], "Night light must stay monotonic in SDR and HDR");
			warm.nightWarmth = 0;
			Check(live::BuildGammaRamp(baseline, output, warm) == baseline,
			      "Night light off preserves calibration during look bypass");
			warm.nightWarmth = .7;
		}
		std::cout << "Live ramp composition and Night light scheduling passed without display API calls.\n";
		return 0;
	}
	catch (const std::exception& error)
	{
		std::cerr << error.what() << '\n';
		return 1;
	}
}
