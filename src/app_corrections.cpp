// Applying corrections to the displays: live LUTs, the color matrix, night light and HDR.
#include "app_state.h"

namespace app
{
void RefreshColorFilters()
{
	filter_state = windows_controls::ReadColorFilterState();
}

void ChangeWindowsFilter(bool on, DWORD filter, bool selecting_filter)
{
	if (render_only || filter_controller.IsBusy())
		return;
	try
	{
		filter_controller.Set(filter_state, on, filter, selecting_filter);
		filter_error = false;
		filter_message = L"Windows is applying the color filter...";
		if (!SetTimer(main_window, TIMER_COLOR_FILTER, 150, nullptr))
		{
			filter_controller.StopWaiting();
			throw std::runtime_error("The Windows request was sent but could not be monitored; check Windows settings");
		}
	}
	catch (const std::exception& error)
	{
		filter_message = ui::messages::FriendlyError(error.what());
		filter_error = true;
	}
	RefreshColorFilters();
	RefreshInterface();
}

Source OutputForPreview()
{
	if (live_session.IsActive())
		return live_output;
	Source output;
	const auto source_index = SelectedSourceIndex();
	if (source_index >= 0 && static_cast<size_t>(source_index) < display_sources.size())
		output = display_sources[source_index];
	if (selected_effect == effect::HdrGamma && !CurrentPreset(preset_role::Correction).current.ApplyInSdr)
		output.hdr = true;
	output.white = white_position;
	return output;
}

static std::vector<Source> ActiveSources(bool fresh = false)
{
	try
	{
		if (SourceQueryBatch::depth)
		{
			if (fresh || !SourceQueryBatch::valid)
			{
				SourceQueryBatch::snapshot = live::ActiveOutputs();
				SourceQueryBatch::valid = true;
			}
			return SourceQueryBatch::snapshot;
		}
		return live::ActiveOutputs();
	}
	catch (const std::exception& error)
	{
		live_message = ui::messages::FriendlyError(error.what());
		live_error = true;
		return {};
	}
}

void RefreshSources()
{
	SourceQueryBatch query_batch;
	if (night_display)
		SendMessageW(night_display, CB_RESETCONTENT, 0, 0);
	const auto old_index = SelectedSourceIndex();
	const std::wstring previous = old_index >= 0 && static_cast<size_t>(old_index) < display_sources.size()
	                                  ? display_sources[old_index].name
	                                  : L"";
	display_sources = ActiveSources(true);
	SendMessageW(source_choice, CB_RESETCONTENT, 0, 0);
	int select = 0;
	for (size_t i = 0; i < display_sources.size(); ++i)
	{
		SendMessageW(source_choice, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(display_sources[i].label.c_str()));
		if ((live_session.IsActive() && display_sources[i].identity == live_output.identity) ||
		    display_sources[i].name == previous)
			select = static_cast<int>(i);
	}
	SelectSource(static_cast<int>(select));
	hdr_state = display_sources.empty() ? hdr::State {} : hdr::ReadHdrState(display_sources[select].name);
}

bool IsLookBypassed()
{
	return bypassed || profile_action == applications::Action::Bypass;
}

bool IsHdrFixActive()
{
	return hdr_fix_enabled && profile_action != applications::Action::PauseHdr;
}

static bool LutProcessingActive()
{
	static live::ProcessingState cache;
	if (!live_session.IsActive())
		return cache.IsProcessing(false, live_output, {});
	return cache.IsProcessing(true, live_output, CurrentSettings());
}

bool IsColorMatrixNeeded()
{
	const auto& look = CurrentPreset(preset_role::Look).current;
	return corrections_requested && !power_suspended && !hdr_pending &&
	       profile_action != applications::Action::Disable && !IsLookBypassed() &&
	       ((look.SaturationEnabled && look.Saturation != 100) || look.MonochromeEnabled);
}

void SyncColorMatrix(bool refresh)
{
	if (render_only)
		return;
	if (!IsColorMatrixNeeded())
		KillTimer(main_window, TIMER_MATRIX_RECOVERY);
	try
	{
		const auto& look = CurrentPreset(preset_role::Look).current;
		const bool wanted = IsColorMatrixNeeded();
		matrix_session.Update(main_window, wanted ? (look.SaturationEnabled ? look.Saturation : 100) : -1, refresh,
		                      false, wanted && look.MonochromeEnabled ? &look.Monochrome : nullptr);
		matrix_message.clear();
	}
	catch (const std::exception&)
	{
		matrix_message = L"The color matrix could not start. Your color curves can still work.";
	}
}

void ScheduleColorMatrixRecovery()
{
	KillTimer(main_window, TIMER_MATRIX_RECOVERY);
	if (render_only || !IsColorMatrixNeeded())
		return;
	matrix_recovery.Restart();
	SetTimer(main_window, TIMER_MATRIX_RECOVERY, matrix_recovery.NextDelay(), nullptr);
}

bool ProcessingActive()
{
	return LutProcessingActive() || night_outputs.IsActive() ||
	       (!render_only && IsColorMatrixNeeded() && matrix_session.CurrentState() == matrix_effect::State::Active);
}

live::Settings CurrentSettings()
{
	live::Settings settings { contrast_enabled ? ContrastAmount() : 0,
		                      ContrastPivot(),
		                      HdrGamma(),
		                      IsHdrFixActive(),
		                      IsLookBypassed(),
		                      correction_first };
	settings.Adjustments = CurrentPreset(preset_role::Look).current.Adjustments;
	settings.Order = effect_order;
	settings.ApplyInSdr = CurrentPreset(preset_role::Correction).current.ApplyInSdr;
	const auto source_index = SelectedSourceIndex();
	const auto output = live_session.IsActive() ? live_output
	                    : source_index >= 0 && static_cast<size_t>(source_index) < display_sources.size()
	                        ? display_sources[source_index]
	                        : Source {};
	if (night::IsDisplayIncluded(night_config, output.identity))
		settings.NightWarmth = night_level;
	return settings;
}

void UpdateNightLightSchedule()
{
	KillTimer(main_window, TIMER_NIGHT);
	SYSTEMTIME now {};
	GetLocalTime(&now);
	const auto phase = night::CurrentPhase(night_config, now.wHour * 3600. + now.wMinute * 60. + now.wSecond);
	night_level = phase.level;
	if (!render_only && !is_closing && corrections_requested && !power_suspended && !hdr_pending &&
	    profile_action != applications::Action::Disable && phase.next_ms)
		SetTimer(main_window, TIMER_NIGHT, phase.next_ms, nullptr);
}

static void SyncNightOutputs()
{
	if (render_only)
		return;
	try
	{
		if (is_closing || !corrections_requested || power_suspended || hdr_pending ||
		    profile_action == applications::Action::Disable || night_level <= 0)
		{
			if (night_outputs.Stop() != 0)
				throw std::runtime_error("Night light could not restore all displays");
		}
		else
		{
			const auto source_index = SelectedSourceIndex();
			const auto primary = live_session.IsActive() ? live_output
			                     : source_index >= 0 && static_cast<size_t>(source_index) < display_sources.size()
			                         ? display_sources[source_index]
			                         : Source {};
			night_outputs.Update(ActiveSources(), primary, night_config, night_level);
		}
		night_message.clear();
	}
	catch (const std::exception& error)
	{
		night_message = ui::messages::FriendlyError(error.what());
	}
}

struct NightSync
{
	static int& Depth()
	{
		static int count = 0;
		return count;
	}
	NightSync() { ++Depth(); }
	~NightSync()
	{
		if (--Depth() == 0 && !is_closing)
		{
			SyncNightOutputs();
			RefreshInterface();
		}
	}
};

int DisableLiveCorrections(bool retain_request)
{
	KillTimer(main_window, TIMER_APPLY);
	if (!retain_request)
		KillTimer(main_window, TIMER_NIGHT);
	const int night_stopped = night_outputs.Stop();
	if (!render_only && (!retain_request || !IsColorMatrixNeeded()))
		matrix_session.Stop();
	const int primary_stopped = live_session.Stop();
	const int result = night_stopped ? night_stopped : primary_stopped;
	live_error = result != 0 && result != 10 && result != 11;
	if (!retain_request || (result != 0 && result != 10 && result != 11))
	{
		corrections_requested = false;
		profile_paused_identity.clear();
		display_retry_pending = false;
		mode_reset_pending = false;
		display_attempts = 0;
	}
	live_message =
		result == 0    ? L"Off. Your previous display colors have been restored."
		: result == 10 ? L"Off. Color changes made by another app have been kept."
		: result == 11
			? L"Paused after a display change. Check the picture, then turn MonitorMage on again."
			: L"MonitorMage could not confirm that your previous display colors were restored. Recovery details are in the temporary folder.";
	return result;
}

bool WaitForDisplay()
{
	if (!corrections_requested || ++display_attempts > 10)
		return false;
	display_retry_pending = true;
	live_error = false;
	live_message = L"Updating your adjustments after a display change...";
	if (!render_only)
		SetTimer(main_window, TIMER_DISPLAY, 500, nullptr);
	RefreshInterface();
	return true;
}

void ApplyLiveCorrections()
{
	SourceQueryBatch query_batch;
	UpdateNightLightSchedule();
	NightSync night_sync;
	SyncColorMatrix();
	if (!live_session.IsActive())
	{
		if (corrections_requested && !render_only && !power_suspended && !hdr_pending &&
		    profile_action != applications::Action::Disable)
			EnableLiveCorrections();
		return;
	}
	try
	{
		const auto current = ActiveSources();
		const auto at = std::find_if(current.begin(), current.end(),
		                             [&](const Source& output) { return output.identity == live_output.identity; });
		if (at == current.end())
		{
			const auto replacement =
				std::find_if(current.begin(), current.end(),
			                 [&](const Source& output) { return live::SameDisplays(output, live_output); });
			if (replacement == current.end() || !replacement->supported)
			{
				if (WaitForDisplay())
					return;
				throw std::runtime_error("The selected display did not become available after the mode change");
			}
			const auto next = *replacement;
			if (++display_attempts > 10)
				throw std::runtime_error("The display mode did not settle after repeated changes");
			mode_reset_pending = next.hdr != live_output.hdr;
			display_retry_pending = true;
			const int stopped = DisableLiveCorrections(true);
			if (stopped != 0 && stopped != 10 && stopped != 11)
				throw std::runtime_error("Display restoration could not be verified during the mode change");
			RefreshSources();
			for (size_t i = 0; i < display_sources.size(); ++i)
				if (display_sources[i].identity == next.identity)
					SelectSource(static_cast<int>(i));
			EnableLiveCorrections();
			return;
		}
		live_output = *at;
		live_session.Update(live_output, CurrentSettings());
		live_error = false;
		display_retry_pending = false;
		mode_reset_pending = false;
		display_attempts = 0;
		if (!LutProcessingActive())
		{
			const int stopped = live_session.Stop();
			if (stopped != 0 && stopped != 10 && stopped != 11)
				throw std::runtime_error("Your previous display colors could not be restored");
			live_message = L"Ready. Turn on an effect or correction to change the picture.";
			RefreshInterface();
			return;
		}
		live_message =
			live_output.hdr && profile_action == applications::Action::PauseHdr
				? L"This application profile pauses HDR gamma. Your other adjustments stay on."
			: IsLookBypassed() ? L"Effects paused. Monitor corrections can still be active."
			: live_output.hdr
				? L"Your adjustments are on. In HDR, they affect ordinary content while preserving bright HDR highlights."
			: CurrentSettings().HdrFix && CurrentSettings().ApplyInSdr
				? L"Your adjustments are on, including HDR gamma because Also apply in SDR mode is selected."
				: L"Your adjustments are on. HDR gamma is skipped while Windows HDR is off.";
	}
	catch (const std::exception& error)
	{
		// A second mode change can race the query/read/write sequence. Retry
		// only transition failures, with a bounded, event-driven timer.
		const auto latest = ActiveSources(true);
		const bool mode_gone =
			std::none_of(latest.begin(), latest.end(),
		                 [&](const Source& output) { return output.identity == live_output.identity; });
		if ((display_retry_pending || mode_gone) && WaitForDisplay())
			return;
		DisableLiveCorrections();
		live_message = ui::messages::FriendlyError(error.what());
		live_error = true;
	}
	RefreshInterface();
}

void EnableLiveCorrections()
{
	SourceQueryBatch query_batch;
	UpdateNightLightSchedule();
	NightSync night_sync;
	SyncColorMatrix();
	if (render_only || live_session.IsActive())
		return;
	try
	{
		const auto source_index = SelectedSourceIndex();
		if (source_index < 0 || static_cast<size_t>(source_index) >= display_sources.size())
			throw std::runtime_error("Select an active display group");
		if (active_test && WaitForSingleObject(active_test, 0) == WAIT_TIMEOUT)
			throw std::runtime_error("Close the timed diagnostic before enabling live correction");
		live_output = display_sources[source_index];
		static live::ProcessingState pending;
		const auto settings = CurrentSettings();
		if (!mode_reset_pending && !pending.IsProcessing(true, live_output, settings))
		{
			display_retry_pending = false;
			display_attempts = 0;
			live_error = false;
			live_message = L"Ready. Turn on an effect or correction to change the picture.";
			RefreshInterface();
			return;
		}
		night_outputs.Release(live_output, &live_session);
		live_session.Start(live_output, settings, mode_reset_pending);
		mode_reset_pending = false;
		ApplyLiveCorrections();
	}
	catch (const std::exception& error)
	{
		if (display_retry_pending)
		{
			DisableLiveCorrections(true);
			if (WaitForDisplay())
				return;
		}
		DisableLiveCorrections();
		live_message = ui::messages::FriendlyError(error.what());
		live_error = true;
	}
	RefreshInterface();
}

void ToggleBypass()
{
	const bool previous = IsLookBypassed();
	if (profile_action == applications::Action::Bypass)
	{
		foreground_program.SetManualOverride();
		profile_action = applications::Action::Look;
	}
	bypassed = !previous;
	ApplyLiveCorrections();
	RefreshInterface();
}

void ReconcileLiveCorrections()
{
	SourceQueryBatch query_batch;
	SyncColorMatrix();
	if (power_suspended)
	{
		RefreshInterface();
		return;
	}
	if (profile_action == applications::Action::Disable)
	{
		if (live_session.IsActive() || night_outputs.IsActive())
		{
			profile_paused_identity = live_output.identity;
			DisableLiveCorrections(true);
		}
		if (corrections_requested)
		{
			const auto source_index = SelectedSourceIndex();
			if (profile_paused_identity.empty() && source_index >= 0 &&
			    static_cast<size_t>(source_index) < display_sources.size())
				profile_paused_identity = display_sources[source_index].identity;
			live_message =
				L"This application profile pauses MonitorMage. Your adjustments resume when you switch to another program.";
		}
	}
	else if (corrections_requested)
	{
		if (live_session.IsActive())
			ApplyLiveCorrections();
		else
		{
			profile_paused_identity.clear();
			EnableLiveCorrections();
		}
	}
	RefreshInterface();
}

void ToggleCorrections()
{
	if (hdr_pending || (render_only && !preset_ui_test))
		return;
	corrections_requested = !corrections_requested;
	if (!corrections_requested)
		DisableLiveCorrections();
	else
		ReconcileLiveCorrections();
	RefreshInterface();
}

void TurnOnCorrections()
{
	corrections_requested = true;
	live_error = false;
	live_message = L"Ready. Turn on an effect or correction to change the picture.";
	ReconcileLiveCorrections();
}

void ToggleWindowsHdrMode()
{
	if (render_only || hdr_pending)
		return;
	try
	{
		const auto source_index = SelectedSourceIndex();
		if (source_index < 0 || static_cast<size_t>(source_index) >= display_sources.size())
			throw std::runtime_error("Select a display before switching HDR");
		const auto state = hdr::ReadHdrState(display_sources[source_index].name);
		if (!state.available)
		{
			live_message = state.reason;
			RefreshInterface();
			return;
		}
		hdr_resume = corrections_requested;
		hdr_source = state.source;
		hdr_desired = !state.on;
		hdr_transition = state;
		if (DisableLiveCorrections() != 0)
			throw std::runtime_error("Restore the display baseline before changing HDR mode");
		mode_reset_pending = hdr_resume;
		hdr_pending = true;
		hdr_attempts = 0;
		hdr::SetHdrEnabled(state, hdr_desired);
		live_message = L"Switching Windows HDR...";
		if (!SetTimer(main_window, TIMER_HDR, 600, nullptr))
			throw std::runtime_error(
				"HDR was requested but its completion timer could not start; corrections remain off");
	}
	catch (const std::exception& error)
	{
		hdr_pending = false;
		hdr_resume = false;
		mode_reset_pending = false;
		live_message = ui::messages::FriendlyError(error.what());
		live_error = true;
	}
	RefreshInterface();
}
}
