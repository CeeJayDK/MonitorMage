#include "live_lut.h"
#include "processing_state.h"
#include "night_outputs.h"
#include <fstream>
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
		Check(settings.Bypass && !settings.HdrFix && settings.NightWarmth > 0,
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
	config.Enabled = true;
	config.Strength = 100;
	Check(night::CurrentPhase(config, 12 * 3600).level == 1 && night::CurrentPhase(config, 0).next_ms == 0,
	      "Manual Night light must not need a polling timer");
	config.Scheduled = true;
	config.Fade = 10;
	Check(night::CurrentPhase(config, 12 * 3600).level == 0, "Daytime must be neutral");
	Check(night::CurrentPhase(config, 12 * 3600).next_ms == 9 * 3600000u,
	      "Daytime must sleep until the evening boundary");
	Check(night::CurrentPhase(config, 21 * 3600).level == 0 && night::CurrentPhase(config, 21 * 3600).next_ms == 5000,
	      "Fade starts at zero and requests bounded updates");
	Check(std::abs(night::CurrentPhase(config, 21 * 3600 + 300).level - .5) < 1e-9, "Evening fade midpoint");
	Check(night::CurrentPhase(config, 0).level == 1 && night::CurrentPhase(config, 0).next_ms > 3600000,
	      "Overnight plateau must sleep");
	Check(std::abs(night::CurrentPhase(config, 7 * 3600 - 300).level - .5) < 1e-9 &&
	          night::CurrentPhase(config, 7 * 3600).level == 0,
	      "Morning fade and exact end");
	config.Start = 8 * 60;
	config.End = 9 * 60;
	config.Fade = 60;
	Check(night::CurrentPhase(config, 8.5 * 3600).level == 1, "Short windows must clamp overlapping fades");
	config.Enabled = false;
	Check(night::CurrentPhase(config, 8.5 * 3600).next_ms == 0, "Disabled schedule must have no wakeups");
	Check(night::ParseTime(L"23:59") == 1439 && night::ParseTime(L"24:00") == -1 && night::ParseTime(L"9:00") == -1,
	      "Time validation");
	const auto key_a = std::wstring(L"panel-A:0:0\n"), key_b = std::wstring(L"panel-B:0:0\n");
	night::SetDisplayIncluded(config, key_a, false);
	Check(!night::IsDisplayIncluded(config, L"panel-A:1:1\n") && night::IsDisplayIncluded(config, key_b),
	      "Display exclusions must survive HDR changes");
	Check(!night::IsDisplayIncluded(config, key_b + key_a), "Clone groups must honor excluded physical members");
	night::SetDisplayIncluded(config, key_b + key_a, true);
	Check(config.Excluded.empty(), "Reincluding a clone group includes its members");
	live::Output a, b;
	a.name = L"A";
	a.identity = key_a;
	b.name = L"B";
	b.identity = key_b;
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

void LayoutChangeTests()
{
	const std::wstring lg = L"\\\\?\\DISPLAY#GSM#1", samsung = L"\\\\?\\DISPLAY#SAM#2", other = L"\\\\?\\DISPLAY#DEL#3";
	const std::wstring lg_hdr = lg + L":3:2\n", lg_sdr = lg + L":0:0\n";
	const std::wstring clone = lg + L":0:0\n" + samsung + L":0:0\n";
	const auto outputs_named = [](std::initializer_list<std::wstring> identities)
	{
		std::vector<live::Output> outputs;
		for (const auto& identity : identities)
		{
			live::Output output;
			output.identity = identity;
			outputs.push_back(output);
		}
		return outputs;
	};
	live::Ramp neutral {};
	for (auto& channel : neutral)
		for (size_t i = 0; i < 256; ++i)
			channel[i] = static_cast<std::uint16_t>(i * 257);
	live::Ramp neutral_256 {};
	for (auto& channel : neutral_256)
		for (size_t i = 0; i < 256; ++i)
			channel[i] = static_cast<std::uint16_t>(i * 256);
	auto changed = neutral;
	changed[1][128] -= 3000;
	const auto found =
		[&](const std::wstring& session, const live::Ramp& baseline, std::initializer_list<std::wstring> identities)
	{
		return live::OutputsAfterLayoutChange(session, baseline, outputs_named(identities));
	};
	Check(found(lg_hdr, neutral_256, { clone }).size() == 1, "A neutral ramp written as i * 256 is also reset");
	const auto joined = found(lg_hdr, neutral, { clone });
	Check(joined.size() == 1 && joined[0].identity == clone, "A display that joins a clone group is reset");
	Check(found(clone, neutral, { lg_hdr }).size() == 1, "A clone group that loses a monitor is reset");
	Check(found(clone, neutral, { lg_sdr, samsung + L":0:0\n" }).size() == 2,
	      "Every part of a split clone group is reset");
	Check(found(lg_hdr, neutral, { lg_sdr }).empty(), "An HDR change on the same monitors keeps its own path");
	Check(found(lg_hdr, neutral, { lg_sdr, samsung + L":0:0\n" }).empty(),
	      "An added separate monitor is only a mode change");
	Check(found(lg_hdr, neutral, { other + L":0:0\n" }).empty(), "Unrelated monitors are left alone");
	Check(found(lg_hdr, changed, { clone }).empty(), "A non-neutral original is not carried to a new layout");
}

void SessionFileTests()
{
	wchar_t folder[MAX_PATH] {};
	Check(GetTempPathW(MAX_PATH, folder) != 0, "The temp folder is available");
	const std::wstring base = std::wstring(folder) + L"MonitorMageFileTest-" + std::to_wstring(GetCurrentProcessId());
	const std::wstring session = base + L".session.txt", result = base + L".result.txt";
	for (const auto& path : { session, result })
		std::ofstream(path) << "x";
	const auto exists = [](const std::wstring& path)
	{
		return GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES;
	};
	Check(exists(session) && exists(result), "The test log files were created");
	live::RemoveSessionLogs(base);
	Check(!exists(session) && !exists(result), "A clean stop removes the session logs");
	live::RemoveSessionLogs(base); // Files that are already gone are not an error.
}
}

int main()
{
	try
	{
		Check(live::WriterLeaseSelfTest() == 0,
		      "Independent output sessions share writer ownership and preserve recovery exclusion");
		NightTests();
		SessionFileTests();
		LayoutChangeTests();
		Check(live::BaselineStorageSelfTest() == 0,
		      "Only ramps that can be rebuilt exactly are left out of recovery data");
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
		off.HdrFix = false;
		live::ProcessingState state;
		Check(!state.IsProcessing(false, sdr, off), "No session must appear paused");
		Check(!state.IsProcessing(true, sdr, off), "Neutral session must appear ready");
		Check(!state.IsProcessing(true, sdr, off), "Cached neutral result must be stable");
		auto changed = off;
		changed.Contrast = .2;
		Check(state.IsProcessing(true, sdr, changed), "Changed contrast must invalidate neutral cache");
		changed.Bypass = true;
		Check(!state.IsProcessing(true, sdr, changed), "Bypass must invalidate active cache");
		changed = off;
		changed.Adjustments.Balance[color::BALANCE_RED] = 80;
		Check(state.IsProcessing(true, sdr, changed), "RGB settings must invalidate neutral cache");
		changed.Adjustments.BalanceEnabled = false;
		Check(!state.IsProcessing(true, sdr, changed), "Layer disable must invalidate active cache");
		auto hdr_state = sdr;
		hdr_state.hdr = true;
		changed = off;
		changed.HdrFix = true;
		Check(state.IsProcessing(true, hdr_state, changed), "HDR transition must invalidate cache");
		Check(!state.IsProcessing(false, hdr_state, changed), "Ending a session must clear active state");
		Check(live::BuildGammaRamp(baseline, sdr, off) == baseline,
		      "SDR identity must preserve captured calibration exactly");
		live::Output hdr;
		hdr.hdr = true;
		hdr.white = 280;
		for (double gamma : { 2.2, 2.4 })
		{
			live::Settings correction;
			correction.Gamma = gamma;
			Check(live::BuildGammaRamp(baseline, sdr, correction) == baseline, "SDR gamma correction must be opt-in");
			const auto original_hdr = live::BuildGammaRamp(identity, hdr, correction);
			correction.ApplyInSdr = true;
			Check(live::BuildGammaRamp(identity, hdr, correction) == original_hdr,
			      "SDR opt-in must not change the HDR mapping");
			const auto adjusted = live::BuildGammaRamp(identity, sdr, correction);
			Check(adjusted != identity && adjusted[0][0] == 0 && adjusted[0][255] == 65535,
			      "SDR compensation must alter tones and retain endpoints");
			for (size_t i = 1; i < 256; ++i)
				Check(adjusted[0][i] >= adjusted[0][i - 1], "SDR compensation must remain monotone");
			const double x = .1, expected = color::SrgbEncode(std::pow(x, gamma));
			Check(std::abs(live::EvaluateOutputCode(x, 0, sdr, correction) - expected) < 1e-14,
			      "SDR correction must encode the selected power response through sRGB");
			correction.Bypass = true;
			Check(live::BuildGammaRamp(identity, sdr, correction) == adjusted,
			      "Creative bypass must retain monitor correction in SDR");
			correction.HdrFix = false;
			Check(live::BuildGammaRamp(baseline, sdr, correction) == baseline,
			      "Correction's single enable switch must control both modes");
		}
		Check(live::BuildGammaRamp(baseline, hdr, off) == baseline,
		      "Disabled HDR effects must preserve baseline exactly");
		live::Settings contrast;
		contrast.Contrast = 0.2;
		const auto creative = live::BuildGammaRamp(baseline, sdr, contrast);
		for (size_t c = 0; c < 3; ++c)
		{
			Check(creative[c][0] == baseline[c][0] && creative[c][255] == baseline[c][255],
			      "Calibration endpoints must remain intact");
			Check(creative[c][64] < baseline[c][64] && creative[c][192] > baseline[c][192],
			      "Creative curve must precede monitor calibration");
		}
		const auto gamma_only = live::BuildGammaRamp(identity, hdr, live::Settings {});
		hdr.identity = L"LG-device:3:2\n";
		auto previous_sdr = sdr;
		previous_sdr.identity = L"LG-device:0:0\n";
		// An already-loaded fix is replaced, not used as the calibration into
		// which a second copy of the fix would be composed.
		for (double gamma : { 2.2, 2.4 })
		{
			live::Settings existing;
			existing.Gamma = gamma;
			const auto loaded = live::BuildGammaRamp(identity, hdr, existing);
			const auto clean = live::CompositionBaseline(loaded, hdr);
			Check(clean == identity, "Known HDR correction must be removed from composition baseline");
			Check(live::BuildGammaRamp(clean, hdr, existing) == loaded, "Taking over the same fix must not double it");
			auto desired = existing;
			desired.Contrast = .15;
			desired.Gamma = 2.4;
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
		auto stale_settings = off;
		stale_settings.Contrast = .2;
		const auto stale = live::BuildGammaRamp(identity, previous_sdr, stale_settings);
		Check(live::CompositionBaseline(stale, hdr, &stale, &identity, &previous_sdr) == identity,
		      "Own stale correction must recover using known neutral calibration across modes");
		const auto calibrated_stale = live::BuildGammaRamp(baseline, previous_sdr, stale_settings);
		auto same_sdr = previous_sdr;
		same_sdr.identity = L"LG-device:8:0\n";
		Check(live::CompositionBaseline(calibrated_stale, same_sdr, &calibrated_stale, &baseline, &previous_sdr) ==
		          baseline,
		      "Same-mode recovery must preserve non-neutral calibration");
		auto unrelated = hdr;
		unrelated.identity = L"Samsung-device:0:0\n";
		Check(live::CompositionBaseline(stale, unrelated, &stale, &identity, &previous_sdr) == stale,
		      "Coincidentally equal ramp on a different monitor must be preserved");
		auto external = stale;
		external[0][64] += 32;
		Check(live::CompositionBaseline(external, hdr, &stale, &identity, &previous_sdr) == external,
		      "External changes must not be replaced with a saved baseline");
		bool ambiguous = false;
		try
		{
			live::CompositionBaseline(calibrated_stale, hdr, &calibrated_stale, &baseline, &previous_sdr);
		}
		catch (const std::runtime_error&)
		{
			ambiguous = true;
		}
		Check(ambiguous, "Non-neutral calibration must not be guessed across SDR/HDR modes");
		// Explicit mode-transition policy: reset calibration, then compile the
		// same enabled effects for the new mode, without retaining the HDR fix.
		auto next_sdr = previous_sdr;
		auto settings_after_switch = live::Settings {};
		settings_after_switch.Contrast = .2;
		const auto stale_hdr = live::BuildGammaRamp(baseline, hdr, settings_after_switch);
		const auto reset = live::CompositionBaseline(stale_hdr, next_sdr, nullptr, nullptr, nullptr, true);
		Check(reset == identity, "Automatic HDR-to-SDR transition must use identity even after nonneutral calibration");
		const auto resumed_sdr = live::BuildGammaRamp(reset, next_sdr, settings_after_switch);
		auto contrast_only = settings_after_switch;
		contrast_only.HdrFix = false;
		Check(resumed_sdr == live::BuildGammaRamp(identity, next_sdr, contrast_only),
		      "SDR resume must skip HDR gamma and keep contrast");
		Check(resumed_sdr != identity, "SDR contrast must remain active after the mode switch");
		settings_after_switch.Contrast = 0;
		Check(live::BuildGammaRamp(reset, next_sdr, settings_after_switch) == identity,
		      "No SDR effects still requires clearing the stale HDR ramp");
		settings_after_switch.ApplyInSdr = true;
		Check(live::BuildGammaRamp(reset, next_sdr, settings_after_switch) != identity,
		      "An explicit SDR gamma opt-in must survive the transition");
		settings_after_switch.ApplyInSdr = false;
		const auto reset_hdr = live::CompositionBaseline(resumed_sdr, hdr, nullptr, nullptr, nullptr, true);
		Check(live::BuildGammaRamp(reset_hdr, hdr, settings_after_switch) == gamma_only,
		      "Returning to HDR must reapply its enabled gamma fix once");
		Check(live::SameDisplays(hdr, next_sdr), "Mode detection must match the same physical monitor across HDR/SDR");
		Check(!live::SameDisplays(hdr, unrelated), "Mode detection must not reset an unrelated monitor");
		auto clone = hdr;
		clone.identity += L"Samsung-device:0:0\n";
		Check(!live::SameDisplays(hdr, clone), "A changed clone group must not be confused with a simple mode switch");
		// Anchors observed in the verified LG HDR path, allowing one WORD of rounding.
		Check(std::abs(static_cast<int>(gamma_only[0][64]) - 15286) <= 1,
		      "Live HDR curve must match verified shadow anchor");
		Check(std::abs(static_cast<int>(gamma_only[0][128]) - 33028) <= 1,
		      "Live HDR curve must match verified midtone anchor");
		const auto combined = live::BuildGammaRamp(identity, hdr, contrast);
		Check(combined[0][32] < gamma_only[0][32], "HDR creative contrast must deepen SDR-range shadows");
		auto paused = contrast;
		paused.HdrFix = false;
		const auto creative_hdr = live::BuildGammaRamp(baseline, hdr, paused);
		Check(creative_hdr != baseline && creative_hdr != live::BuildGammaRamp(baseline, hdr, contrast),
		      "HDR-only pause must retain contrast while removing gamma correction");
		// Highlights above SDR white and captured calibration stay intact during the pause.
		for (size_t c = 0; c < 3; ++c)
			for (size_t i = 192; i < 256; ++i)
				Check(creative_hdr[c][i] == baseline[c][i], "HDR-only pause must preserve highlights and calibration");
		paused.Bypass = true;
		Check(live::BuildGammaRamp(baseline, hdr, paused) == baseline,
		      "HDR pause plus creative bypass must preserve captured calibration exactly");
		auto reversed = contrast;
		reversed.CorrectionFirst = true;
		const auto reverse_ramp = live::BuildGammaRamp(baseline, hdr, reversed);
		Check(reverse_ramp != live::BuildGammaRamp(baseline, hdr, contrast),
		      "Reordering must change the compiled HDR ramp, not just the list");
		for (size_t c = 0; c < 3; ++c)
			for (size_t i = 192; i < 256; ++i)
				Check(reverse_ramp[c][i] == baseline[c][i],
				      "Reordered effects must preserve HDR highlights and captured calibration");
		reversed.HdrFix = false;
		Check(live::BuildGammaRamp(baseline, hdr, reversed) == creative_hdr,
		      "Order must have no effect when gamma correction is disabled");
		reversed.HdrFix = true;
		reversed.Bypass = true;
		auto gamma_calibrated = live::Settings {};
		Check(live::BuildGammaRamp(baseline, hdr, reversed) == live::BuildGammaRamp(baseline, hdr, gamma_calibrated),
		      "Creative bypass must preserve gamma and calibration in either order");
		contrast.Bypass = true;
		Check(live::BuildGammaRamp(identity, hdr, contrast) == gamma_only, "Bypass must retain HDR correction");
		Check(live::BuildGammaRamp(baseline, sdr, contrast) == baseline,
		      "SDR bypass must restore captured calibration");
		auto warm = off;
		warm.NightWarmth = .7;
		warm.Bypass = true;
		for (const auto& output : { sdr, hdr })
		{
			const auto result = live::BuildGammaRamp(identity, output, warm);
			Check(state.IsProcessing(true, output, warm), "Night light must update active-state cache");
			Check(result[0] == identity[0] && result[2][200] < result[1][200] && result[1][200] < result[0][200],
			      "Night light reduces blue and green without boosting or clipping red");
			for (const auto& channel : result)
				for (size_t i = 1; i < 256; ++i)
					Check(channel[i] >= channel[i - 1], "Night light must stay monotonic in SDR and HDR");
			warm.NightWarmth = 0;
			Check(live::BuildGammaRamp(baseline, output, warm) == baseline,
			      "Night light off preserves calibration during look bypass");
			warm.NightWarmth = .7;
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
