// Keeping the window's controls laid out and showing the current state.
#include "app_state.h"

namespace app
{
std::vector<HWND> TabStopControls()
{
	// Sidebar, then the right pane in reading order. The graph is an input
	// only for curve editing, between the monitor's upper and lower buttons.
	std::vector<HWND> order { correction_list, windows_effect_list, effect_list,
		                      preset_choice,   autosave_control,    save_preset_button,
		                      save_as_button,  export_button,       revert_preset_button };
	if (settings_page)
		order.insert(order.end(),
		             { font_choice, shortcut_control, shortcut_button, startup_control, bypass_button,
		               start_minimized_control, diagnostics_control, open_preset_button, saturation_report_button });
	else
	{
		order.insert(order.end(), { selected_effect_toggle, reset_button });
		order.insert(order.end(), { contrast_slider, pivot_slider, white_slider,
		                            gamma_position == 0 ? gamma_choice : gamma_other, hdr_fix_control });
		order.insert(order.end(), adjustment_sliders.begin(), adjustment_sliders.end());
		order.insert(order.end(), { curve_channel, import_button, export_1d_button });
		if (selected_effect == effect::ColorFilters)
			order.insert(order.end(), { filter_choice, filter_settings_button });
		if (selected_effect == effect::NightLight)
			order.insert(order.end(), { night_schedule, night_start, night_end, night_display, night_include });
	}
	order.insert(order.end(), { hdr_button, info_button, power_button });
	if (!settings_page && selected_effect == effect::CustomCurves)
		order.push_back(curve_canvas);
	order.insert(order.end(),
	             { monitor_settings_button, profiles_button, source_choice, website_link, monitor_profile });
	order.erase(std::remove_if(order.begin(), order.end(),
	                           [](HWND control)
	                           {
								   if (!control)
									   return true;
								   for (auto at = control; at && at != main_window; at = GetParent(at))
									   if (!(GetWindowLongPtrW(at, GWL_STYLE) & WS_VISIBLE) || !IsWindowEnabled(at))
										   return true;
								   return false;
							   }),
	            order.end());
	return order;
}

HWND NextTabStop(HWND from, bool reverse)
{
	const auto order = TabStopControls();
	if (order.empty())
		return nullptr;
	const auto found = std::find(order.begin(), order.end(), from);
	if (found == order.end())
		return reverse ? order.back() : order.front();
	const auto index = static_cast<size_t>(found - order.begin());
	return order[(index + order.size() + (reverse ? -1 : 1)) % order.size()];
}

bool NavigateTab(MSG& message)
{
	if (message.message != WM_KEYDOWN || message.wParam != VK_TAB ||
	    GetAncestor(message.hwnd, GA_ROOT) != main_window || (GetKeyState(VK_CONTROL) & 0x8000) ||
	    (GetKeyState(VK_MENU) & 0x8000))
		return false;
	const auto from = GetFocus(), next = NextTabStop(from, (GetKeyState(VK_SHIFT) & 0x8000) != 0);
	if (!next)
		return false;
	wchar_t class_name[32] {};
	if (from)
		GetClassNameW(from, class_name, 32);
	if (wcscmp(class_name, L"ComboBox") == 0)
		SendMessageW(from, CB_SHOWDROPDOWN, FALSE, 0);
	SendMessageW(main_window, WM_UPDATEUISTATE, MAKEWPARAM(UIS_CLEAR, UISF_HIDEFOCUS), 0);
	SetFocus(next);
	ui::RevealControl(main_window, next);
	return true;
}

static void LayoutEffectControls()
{
	if (!ui_font || !effect_description)
		return;
	static int previous_page = -1;
	static bool previous_settings = false;
	static std::wstring previous_text;
	wchar_t text[1024] {};
	GetWindowTextW(effect_description, text, 1024);
	if (effect_layout_valid && previous_page == selected_effect && previous_settings == settings_page &&
	    previous_text == text)
		return;
	effect_layout_valid = true;
	previous_page = selected_effect;
	previous_settings = settings_page;
	previous_text = text;
	if (settings_page)
	{
		PlaceControl(curve_canvas, 198, 262, 558, 268);
		LayoutMonitorControls();
		InvalidateRect(curve_canvas, nullptr, FALSE);
		return;
	}
	HDC dc = GetDC(effect_description);
	const auto old = SelectObject(dc, ui_font);
	TEXTMETRICW metrics {};
	GetTextMetricsW(dc, &metrics);
	RECT measure { 0, 0, ScaleForDpi(558), 0 };
	DrawTextW(dc, text, -1, &measure, DT_CALCRECT | DT_WORDBREAK | DT_NOPREFIX);
	SelectObject(dc, old);
	ReleaseDC(effect_description, dc);
	// Reserve at least two font lines, then an explicit eight-DIP gap. The
	// native static's actual line metrics vary with font and display scaling.
	const int text_height = std::max(measure.bottom, 2 * metrics.tmHeight) + ScaleForDpi(2);
	const int height = (text_height * 96 + dpi - 1) / dpi, first = 108 + height + 8;
	PlaceControl(effect_description, 198, 108, 558, height);
	const auto row = [&](HWND label, HWND value, HWND slider, int index)
	{
		const int top = first + index * 24;
		PlaceControl(label, 198, top, 106, 22);
		PlaceControl(value, 306, top, 56, 22);
		PlaceControl(slider, 368, top, 388, 22);
	};
	row(amount_label, amount_value, contrast_slider, 0);
	row(pivot_label, pivot_value, pivot_slider, 1);
	row(white_label, white_value, white_slider, 0);
	for (size_t i = 0; i < adjustment_sliders.size(); ++i)
		row(adjustment_labels[i], adjustment_values[i], adjustment_sliders[i], static_cast<int>(i));
	const auto width = [&](HWND control, int minimum = 64, int maximum = 240)
	{
		return ui::ContentWidth(control, ui_font, minimum, maximum);
	};
	const int gamma_width = width(gamma_choice, 56, 100);
	PlaceControl(gamma_label, 198, first + 24, 130, 22);
	PlaceControl(gamma_choice, 331, first + 24, gamma_width, 22);
	PlaceControl(gamma_other, 331 + gamma_width + 12, first + 24, width(gamma_other, 56, 100), 22);
	PlaceControl(hdr_fix_control, 198, first + 48, 550, 22);
	PlaceControl(curve_channel, 198, first, width(curve_channel, 84, 200), 180);
	const int import_width = width(import_button);
	PlaceControl(import_button, 198, first, import_width, 28);
	PlaceControl(export_1d_button, 198 + import_width + 12, first, width(export_1d_button), 28);
	if (selected_effect == effect::ColorFilters)
	{
		const int choice_width = width(filter_choice, 160, 350);
		PlaceControl(filter_choice, 198, first, choice_width, 180);
		PlaceControl(filter_settings_button, 198 + choice_width + 12, first, width(filter_settings_button), 28);
		PlaceControl(correction_note, 198, first + 40, 558, 42);
	}
	if (selected_effect == effect::NightLight)
	{
		PlaceControl(night_schedule, 198, first + 51, 166, 22);
		PlaceControl(night_start_label, 376, first + 51, 40, 22);
		PlaceControl(night_start, 418, first + 50, 72, 24);
		PlaceControl(night_end_label, 514, first + 51, 40, 22);
		PlaceControl(night_end, 556, first + 50, 72, 24);
		PlaceControl(night_display, 198, first + 82, 256, 180);
		PlaceControl(night_include, 466, first + 82, 290, 22);
		PlaceControl(correction_note, 198, first + 108, 558, 22);
	}
	const auto align_track = [&](HWND slider)
	{
		RECT channel {}, bounds {};
		SendMessageW(slider, TBM_GETCHANNELRECT, 0, reinterpret_cast<LPARAM>(&channel));
		GetWindowRect(slider, &bounds);
		MapWindowPoints(nullptr, main_window, reinterpret_cast<POINT*>(&bounds), 2);
		const int right = bounds.right - bounds.left - channel.right, h = bounds.bottom - bounds.top;
		MoveWindow(slider, ScaleForDpi(376 + PANE_OFFSET) - ui::ViewportOffset(main_window).x - channel.left,
		           bounds.top + h / 2 - (channel.top + channel.bottom) / 2, ScaleForDpi(380) + channel.left + right, h,
		           TRUE);
	};
	for (HWND slider : { contrast_slider, pivot_slider, white_slider })
		align_track(slider);
	for (HWND slider : adjustment_sliders)
		align_track(slider);
	const int rows = selected_effect == effect::NightLight     ? 6
	                 : selected_effect == effect::ColorBalance ? 5
	                 : selected_effect == effect::Levels       ? 4
	                 : selected_effect == effect::HdrGamma     ? 3
	                                                           : 2;
	const int bottom = first + (rows - 1) * 24 + 22;
	const int graph_top = std::max(262, bottom + 8);
	PlaceControl(curve_canvas, 198, graph_top, 558, 530 - graph_top);
	LayoutMonitorControls();
	InvalidateRect(curve_canvas, nullptr, FALSE);
}

static void RefreshEffectLists(int source_index, bool valid)
{
	const bool correction_powered =
		corrections_requested && !power_suspended && !hdr_pending && profile_action != applications::Action::Disable &&
		profile_action != applications::Action::PauseHdr && valid && display_sources[source_index].supported &&
		(display_sources[source_index].hdr || CurrentPreset(preset_role::Correction).current.ApplyInSdr);
	std::array<bool, effect::Count> enabled { contrast_enabled, hdr_fix_enabled };
	for (int i = 2; i < 8; ++i)
		enabled[i] = AdjustmentEnabled(i);
	enabled[effect::Saturation] = CurrentPreset(preset_role::Look).current.SaturationEnabled;
	enabled[effect::ChannelMixer] = CurrentPreset(preset_role::Look).current.MonochromeEnabled;
	effects::Update(effect_list, CreativeOrder(), enabled, false, selected_effect, corrections_requested);
	effects::Update(correction_list, { 1 }, enabled, profile_action == applications::Action::PauseHdr, selected_effect,
	                correction_powered);
	const bool night_powered =
		corrections_requested && night_level > 0 && !power_suspended && !hdr_pending &&
		profile_action != applications::Action::Disable &&
		std::any_of(display_sources.begin(), display_sources.end(),
	                [](const Source& output)
	                { return output.supported && night::IsDisplayIncluded(night_config, output.identity); });
	enabled[effect::ColorFilters] = filter_state.on;
	enabled[effect::NightLight] = night_config.Enabled;
	effects::Update(windows_effect_list, { 8, 9 }, enabled, false, selected_effect, night_powered);
	SetTextIfChanged(effect_title, effects::EFFECT_NAMES[selected_effect]);
	SetTextIfChanged(selected_effect_toggle,
	                 (std::wstring(effects::EFFECT_NAMES[selected_effect]) + L" on/off").c_str());
	ui::ToggleButton(selected_effect_toggle,
	                 selected_effect == effect::ColorFilters ? filter_state.on : enabled[selected_effect],
	                 selected_effect == effect::ColorFilters ? true
	                 : selected_effect == effect::NightLight ? night_powered
	                 : selected_effect == effect::HdrGamma   ? correction_powered
	                                                         : corrections_requested);
	SetControlVisible(selected_effect_toggle, !settings_page);
	SetControlVisible(reset_button, !settings_page && (selected_effect < effect::FirstWindows ||
	                                                   selected_effect == effect::NightLight ||
	                                                   selected_effect >= effect::Saturation));
	SetControlEnabled(selected_effect_toggle,
	                  selected_effect != effect::ColorFilters ||
	                      (filter_state.available && !filter_controller.IsBusy() && !render_only));
}

static void RefreshEffectDescription()
{
	if (selected_effect < effect::FirstCreative)
		SetTextIfChanged(
			effect_description,
			selected_effect == effect::ContrastCurves
				? L"Make dark and bright tones stand apart. Pivot sets the tone that stays unchanged. Zero has no effect."
				: L"Adjust shadows in ordinary content when Windows HDR is on. Gamma changes the shadow brightness.");
	const wchar_t* explanations[] {
		L"",
		L"",
		L"Balance red, green and blue, or make the picture warmer, cooler or tinted. Brightness is kept within range.",
		L"Brighten or darken the middle tones while keeping black and white unchanged. 1.00 has no effect.",
		L"Input sets which tones become black or white. Output sets how dark or bright they can be.",
		L"Adjust dark and bright areas separately while keeping pure black and white unchanged.",
		L"Drag points to change the curve. Double-click resets a point; right-click resets the whole curve.",
		L"Load color curves from a file. Supports 1D cube, spi1d, CAL, CSV and PNG/TIFF ramps, but not 3D LUTs."
	};
	if (selected_effect >= effect::FirstCreative && selected_effect < effect::FirstWindows)
		SetTextIfChanged(effect_description, explanations[selected_effect]);
	if (selected_effect == effect::ColorFilters)
		SetTextIfChanged(
			effect_description,
			L"Windows color filters apply to all displays for your account. They stay on when MonitorMage is off or closed.");
	if (selected_effect == effect::Saturation)
		SetTextIfChanged(effect_description, L"All displays: 0% grayscale, 100% unchanged. Strong boosts may clip.");
	if (selected_effect == effect::ChannelMixer)
		SetTextIfChanged(
			effect_description,
			L"Make a black-and-white look by mixing red, green and blue. White stays white. All zeros use the standard mix. Applies to all displays.");
	if (selected_effect == effect::NightLight)
		SetTextIfChanged(
			effect_description,
			L"Warmer colors in the evening. Applies to all included displays, independently of your look. Linked displays change together.");
}

static COLORREF AdjustmentSliderColor(size_t i, const Parameter& p)
{
	COLORREF colour = ui::THEME_ACCENT;
	if (selected_effect == effect::NightLight && i == 0)
		colour = RGB(255, 165, 82);
	if (selected_effect == effect::ChannelMixer)
	{
		constexpr COLORREF CHANNEL_COLORS[] { RGB(245, 95, 104), RGB(92, 210, 126), RGB(90, 155, 248) };
		colour = CHANNEL_COLORS[i];
	}
	if (selected_effect == effect::ColorBalance)
	{
		constexpr COLORREF CHANNEL_COLORS[] { RGB(245, 95, 104), RGB(92, 210, 126), RGB(90, 155, 248) };
		if (i < 3)
			colour = CHANNEL_COLORS[i];
		else if (*p.value)
		{
			const auto target = i == 3 ? (*p.value > 0 ? RGB(255, 165, 82) : RGB(113, 173, 255))
			                           : (*p.value > 0 ? RGB(228, 112, 220) : RGB(104, 215, 139));
			const double mix = .45 + .55 * std::abs(*p.value) / 50.0;
			const auto blend = [&](int a, int b)
			{
				return static_cast<BYTE>(std::lround(a + (b - a) * mix));
			};
			colour = RGB(blend(GetRValue(ui::THEME_MUTED), GetRValue(target)),
			             blend(((ui::THEME_MUTED >> 8) & 255), GetGValue(target)),
			             blend(GetBValue(ui::THEME_MUTED), GetBValue(target)));
		}
		else
			colour = ui::THEME_MUTED;
	}
	return colour;
}

static void RefreshAdjustmentSliders(const std::vector<Parameter>& controls, bool changing_adjustment_page)
{
	for (size_t i = 0; i < adjustment_sliders.size(); ++i)
	{
		const bool visible = i < controls.size() && !settings_page;
		SetControlVisible(adjustment_sliders[i], visible && !changing_adjustment_page);
		SetControlVisible(adjustment_labels[i], visible);
		SetControlVisible(adjustment_values[i], visible);
		if (visible)
		{
			const auto& p = controls[i];
			const COLORREF colour = AdjustmentSliderColor(i, p);
			ui::SliderColour(adjustment_sliders[i], colour);
			ui::ConfigureSlider(adjustment_sliders[i], p.default_value,
			                    (selected_effect == effect::ColorBalance || selected_effect == effect::ChannelMixer) &&
			                            i < 3
			                        ? L"RGB"[i]
			                        : 0);
			if (adjustment_page != selected_effect)
				SendMessageW(adjustment_sliders[i], TBM_SETRANGE, TRUE, MAKELPARAM(p.minimum, p.maximum));
			if (SendMessageW(adjustment_sliders[i], TBM_GETPOS, 0, 0) != *p.value)
				SendMessageW(adjustment_sliders[i], TBM_SETPOS, TRUE, *p.value);
			SetTextIfChanged(adjustment_labels[i], p.label);
			SetTextIfChanged(adjustment_values[i],
			                 (selected_effect >= effect::Saturation || (selected_effect == effect::NightLight && i == 0)
			                      ? std::to_wstring(*p.value) + L"%"
			                  : selected_effect == effect::NightLight ? std::to_wstring(*p.value)
			                                                          : FormatDecimal(*p.value / 100.0))
			                     .c_str());
		}
	}
}

static void RefreshAdjustmentHelp(const std::vector<Parameter>& controls)
{
	static constexpr const wchar_t* PARAMETER_HELP[][5] {
		{ L"Red balance", L"Green balance", L"Blue balance", L"Warmth", L"Tint" },
		{ L"Midtone brightness" },
		{ L"Input black level", L"Input white level", L"Output black level", L"Output white level" },
		{ L"Shadow brightness", L"Highlight brightness" }
	};
	if (selected_effect >= effect::FirstCreative && selected_effect <= effect::ShadowsHighlights)
		for (size_t i = 0; i < controls.size(); ++i)
			ui::SetHelpText(adjustment_sliders[i], PARAMETER_HELP[selected_effect - effect::FirstCreative][i]);
	if (selected_effect == effect::Saturation)
		ui::SetHelpText(adjustment_sliders[0], L"Color strength");
	if (selected_effect == effect::ChannelMixer)
		for (int i = 0; i < 3; ++i)
		{
			const wchar_t* help[] { L"Red contribution", L"Green contribution", L"Blue contribution" };
			ui::SetHelpText(adjustment_sliders[i], help[i]);
		}
	if (selected_effect == effect::NightLight)
	{
		ui::SetHelpText(adjustment_sliders[0], L"Night light warmth");
		ui::SetHelpText(adjustment_sliders[1], L"Fade duration");
	}
}

// Shows only the controls that belong on the current page.
static void ShowControlsForCurrentPage()
{
	SetControlVisible(saturation_report_button, settings_page);
	SetControlVisible(curve_channel, selected_effect == effect::CustomCurves && !settings_page);
	SetControlVisible(import_button, selected_effect == effect::Imported1dLut && !settings_page);
	SetControlVisible(export_1d_button, selected_effect == effect::Imported1dLut && !settings_page);
	for (HWND control :
	     { effect_list, correction_list, windows_effect_list, corrections_heading, effects_label, order_hint })
		SetControlVisible(control, true);
	SetControlVisible(preset_label, false);
	SetControlVisible(effect_title, !settings_page);
	SetControlVisible(effect_description, !settings_page);
	SetControlVisible(active_look_label, false);
	SetControlVisible(open_preset_button, settings_page);
	SetControlVisible(settings_button, false);
	for (HWND control : { shortcut_label, shortcut_control, shortcut_button, font_choice, font_label, bypass_button })
		SetControlVisible(control, settings_page);
	// Personal corrections have their own settings, outside LUT presets.
	for (HWND control :
	     { preset_choice, autosave_control, save_preset_button, save_as_button, export_button, revert_preset_button })
		SetControlVisible(control, settings_page || selected_effect < effect::FirstWindows ||
		                               selected_effect >= effect::Saturation);
	SetControlVisible(version_label, settings_page);
	SetControlVisible(startup_control, settings_page);
	SetControlVisible(start_minimized_control, settings_page);
	SetControlVisible(diagnostics_control, settings_page);
	SetControlVisible(open_preset_button, settings_page);
	for (HWND control : { filter_choice, filter_settings_button, correction_note })
		SetControlVisible(control, false);
	for (HWND control :
	     { night_schedule, night_start, night_end, night_start_label, night_end_label, night_include, night_display })
		SetControlVisible(control, !settings_page && selected_effect == effect::NightLight);
}

static void RefreshNightLightControls(int source_index, bool valid)
{
	if (SendMessageW(night_display, CB_GETCOUNT, 0, 0) != static_cast<LRESULT>(display_sources.size()))
	{
		SendMessageW(night_display, CB_RESETCONTENT, 0, 0);
		for (const auto& output : display_sources)
			SendMessageW(night_display, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(output.label.c_str()));
		SendMessageW(night_display, CB_SETCURSEL, valid ? source_index : 0, 0);
	}
	const auto night_index = SendMessageW(night_display, CB_GETCURSEL, 0, 0);
	const bool night_valid = night_index >= 0 && static_cast<size_t>(night_index) < display_sources.size();
	SetControlVisible(correction_note, true);
	SetTextIfChanged(correction_note, night_message.empty()
	                                      ? (night_config.Scheduled
	                                             ? (night_level > 0 ? L"Schedule active" : L"Waiting for the schedule")
	                                             : L"Manual mode")
	                                      : night_message.c_str());
	for (const auto& entry :
	     { std::pair<HWND, bool> { night_schedule, night_config.Scheduled },
	       { night_include,
	         night_valid && night::IsDisplayIncluded(night_config, display_sources[night_index].identity) } })
		if (SendMessageW(entry.first, BM_GETCHECK, 0, 0) != (entry.second ? BST_CHECKED : BST_UNCHECKED))
			SendMessageW(entry.first, BM_SETCHECK, entry.second ? BST_CHECKED : BST_UNCHECKED, 0);
	if (GetFocus() != night_start)
		SetTextIfChanged(night_start, night::FormatTime(night_config.Start).c_str());
	if (GetFocus() != night_end)
		SetTextIfChanged(night_end, night::FormatTime(night_config.End).c_str());
	SetControlEnabled(night_start, night_config.Scheduled);
	SetControlEnabled(night_end, night_config.Scheduled);
	SetControlEnabled(night_include, night_valid && display_sources[night_index].supported);
}

static void RefreshFilterAndStartupControls()
{
	if (!settings_page && selected_effect == effect::ColorFilters)
	{
		SetControlVisible(filter_choice, true);
		SetControlVisible(filter_settings_button, true);
		SetControlVisible(correction_note, !filter_message.empty() || !filter_state.available);
	}
	SetTextIfChanged(version_label, L"MonitorMage " MONITORMAGE_VERSION_W);
	SetControlEnabled(filter_choice, filter_state.available && !filter_controller.IsBusy() && !render_only);
	if (SendMessageW(filter_choice, CB_GETCURSEL, 0, 0) != filter_state.filter)
		SendMessageW(filter_choice, CB_SETCURSEL, filter_state.filter, 0);
	if (selected_effect != effect::NightLight || settings_page)
		SetTextIfChanged(correction_note, (!filter_message.empty() ? filter_message
		                                   : !filter_state.available
		                                       ? filter_state.reason
		                                       : L"Windows color filters stay on when MonitorMage is off or closed.")
		                                      .c_str());
	const auto startup_check = startup_enabled ? BST_CHECKED : BST_UNCHECKED;
	if (SendMessageW(startup_control, BM_GETCHECK, 0, 0) != startup_check)
		SendMessageW(startup_control, BM_SETCHECK, startup_check, 0);
	SetControlEnabled(startup_control, !render_only);
	const auto minimized_check = start_minimized ? BST_CHECKED : BST_UNCHECKED;
	if (SendMessageW(start_minimized_control, BM_GETCHECK, 0, 0) != minimized_check)
		SendMessageW(start_minimized_control, BM_SETCHECK, minimized_check, 0);
	const auto diagnostics_check = save_diagnostics ? BST_CHECKED : BST_UNCHECKED;
	if (SendMessageW(diagnostics_control, BM_GETCHECK, 0, 0) != diagnostics_check)
		SendMessageW(diagnostics_control, BM_SETCHECK, diagnostics_check, 0);
	SetTextIfChanged(settings_button, settings_page ? L"Back to effects" : ui::wording::LABEL_SETTINGS);
	SetTextIfChanged(amount_label, L"Contrast");
	SetTextIfChanged(amount_value, FormatDecimal(ContrastAmount()).c_str());
	SetTextIfChanged(pivot_label, L"Pivot");
	SetTextIfChanged(pivot_value, FormatDecimal(ContrastPivot()).c_str());
}

// Chooses the one message to show. Later checks are more important and replace earlier ones.
static std::wstring BuildStatusText(bool applying)
{
	std::wstring status_text = live_message;
	status_error = live_error;
	if (!live_error && live_session.IsActive() && !applying)
		status_text = L"Ready. No adjustments are changing the picture.";
	if (!render_only && !hotkey_message.empty())
		status_text += L" " + hotkey_message;
	if (!live_error && !preset_message.empty())
	{
		status_text = preset_message;
		status_error = preset_message == last_preset_error;
	}
	else if (!live_error && !profile_message.empty())
	{
		status_text = profile_message;
		status_error = false;
	}
	if (IsColorMatrixNeeded())
	{
		const auto state = render_only ? matrix_effect::State::Active : matrix_session.CurrentState();
		if (!matrix_message.empty())
		{
			status_text = matrix_message;
			status_error = true;
		}
		else if (state == matrix_effect::State::OtherTool || state == matrix_effect::State::ExternalChange)
		{
			status_text =
				L"Color effects are paused because another color tool is active. Your color curves can still work.";
			status_error = true;
		}
		else if (state == matrix_effect::State::Failed)
		{
			status_text = L"Color effects could not be applied. Your color curves can still work.";
			status_error = true;
		}
		else if (state == matrix_effect::State::Active && !live_error)
		{
			status_text =
				L"Your color effects are on. They apply to all displays; some fullscreen games may skip them. The graph shows color curves only.";
			status_error = false;
		}
	}
	if (!render_only && !IsColorMatrixNeeded() && matrix_session.CurrentState() == matrix_effect::State::Failed)
	{
		status_text = L"Your previous colors could not be confirmed. See the color effect report in Settings.";
		status_error = true;
	}
	if (!matrix_message.empty())
	{
		status_text = matrix_message;
		status_error = true;
	}
	if (filter_error)
	{
		status_text = filter_message;
		status_error = true;
	}
	if (!night_message.empty() && !render_only)
	{
		status_text = night_message;
		status_error = true;
	}
	return status_text;
}

static void RefreshLookLabels(bool profile_active)
{
	const auto& look = CurrentPreset(preset_role::Look);
	std::wstring profile_text = L"Desktop profile";
	if (profile_active)
	{
		profile_text = L"Profile: " + programs::DisplayName(foreground_program.FocusedProgram()) + L" | ";
		profile_text += profile_action == applications::Action::PauseHdr  ? L"HDR gamma paused"
		                : profile_action == applications::Action::Disable ? L"All adjustments paused"
		                : profile_action == applications::Action::Bypass  ? L"Look bypassed"
		                : look.path.empty()                               ? L"Unsaved look"
		                                                                  : look.path.stem().wstring();
	}
	SetTextIfChanged(monitor_profile, profile_text.c_str());
	const auto look_name = (look.path.empty() ? std::wstring(L"Unsaved look") : look.path.stem().wstring()) +
	                       ((slider_dragging ? look_dirty_before_drag : look.IsDirty()) ? L" *" : L"");
	std::wstring origin =
		application_context_active
			? (foreground_program.IsManualOverride() ? L"Manual override: " : L"Application profile: ") +
				  programs::DisplayName(foreground_program.FocusedProgram())
			: L"Manual look";
	std::wstring look_state = look_name;
	if (profile_action == applications::Action::Disable)
		look_state += L" (all adjustments paused)";
	else
	{
		if (profile_action == applications::Action::PauseHdr)
			look_state += L" (HDR gamma paused)";
		if (IsLookBypassed())
			look_state += L" (look bypassed)";
	}
	SetTextIfChanged(hdr_fix_control, L"Also apply when Windows HDR is off (SDR)");
	const auto active_text = origin + L" | " + look_state;
	SetTextIfChanged(active_look_label, active_text.c_str());
	const auto caption = L"MonitorMage - " + active_text;
	SetTextIfChanged(main_window, caption.c_str());
}

static void RefreshWindowIcons(bool applying)
{
	const auto state = CurrentIconState(applying);
	const HICON small_icon = IconForState(SM_CXSMICON, state), large_icon = IconForState(SM_CXICON, state);
	if (small_icon && reinterpret_cast<HICON>(SendMessageW(main_window, WM_GETICON, ICON_SMALL, 0)) != small_icon)
		SendMessageW(main_window, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(small_icon));
	if (large_icon && reinterpret_cast<HICON>(SendMessageW(main_window, WM_GETICON, ICON_BIG, 0)) != large_icon)
		SendMessageW(main_window, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(large_icon));
}

static void ShowStatusText(const std::wstring& status_text)
{
	const auto shortcut_text =
		hotkey_registered ? L"Bypass: " + HotkeyName(active_shortcut) : std::wstring(L"Bypass shortcut");
	SetTextIfChanged(shortcut_label, shortcut_text.c_str());
	SetTextIfChanged(status_label, status_text.c_str());
	graph_status_visible = status_error || hdr_pending || display_retry_pending;
	SetControlVisible(info_button, !graph_status_visible);
	SetTextIfChanged(info_button, status_text.c_str());
	// Expose the painted status as the keyboard-focusable graph's accessible name.
	SetTextIfChanged(curve_canvas, status_text.c_str());
}

static void ShowContrastAndHdrControls()
{
	for (HWND control : { amount_label, amount_value, contrast_slider, pivot_label, pivot_value, pivot_slider })
		SetControlVisible(control, selected_effect == effect::ContrastCurves && !settings_page);
	for (HWND control :
	     { white_label, white_value, white_slider, gamma_label, gamma_choice, gamma_other, hdr_fix_control })
		SetControlVisible(control, selected_effect == effect::HdrGamma && !settings_page);
}

static void RefreshToolbarButtons(int source_index, bool valid, bool profile_active)
{
	ui::SetCaptionSettingsButton(main_window, ID_SETTINGS_PAGE, settings_page);
	const bool can_power =
		!hdr_pending && !render_only && (corrections_requested || (valid && display_sources[source_index].supported));
	ui::SetIconButton(power_button, ui::Glyph::Power, corrections_requested, can_power);
	ui::SetIconButton(hdr_button, ui::Glyph::Sun, valid && display_sources[source_index].hdr,
	                  !render_only && !hdr_pending && hdr_state.available);
	ui::SetIconButton(profiles_button, ui::Glyph::Diamond, profile_active, true);
	ui::SetIconButton(monitor_settings_button, ui::Glyph::Settings, settings_page, true);
	ui::SetIconButton(info_button, ui::Glyph::Info, false, true);
	ui::CaptionActions(main_window, { { { ID_TOGGLE_WINDOWS_HDR, valid && display_sources[source_index].hdr,
	                                      !render_only && !hdr_pending && hdr_state.available },
	                                    { ID_APPLICATION_PROFILES, profile_active, true },
	                                    { ID_TEST_CONTRAST, corrections_requested, can_power } } });
}

static void RefreshDisplayControls(int source_index, bool valid)
{
	SetControlEnabled(source_choice, !live_session.IsActive() && !hdr_pending);
	SetControlEnabled(white_slider, !(valid && display_sources[source_index].hdr));
	SetTextIfChanged(bypass_button, IsLookBypassed() ? L"Resume look" : L"Bypass look");
	std::wstring note = L"No active display group available.";
	if (valid)
	{
		const auto& source = display_sources[source_index];
		note = source.label + L"\n";
		if (settings_page && std::count(source.identity.begin(), source.identity.end(), L'\n') > 1)
			note += L"Linked displays change together.\n";
		note += source.hdr ? L"HDR: SDR white follows Windows." : L"SDR: hardware colour curves.";
		if (!source.supported)
			note += L" Mode not supported.";
	}
	SetTextIfChanged(test_note, note.c_str());
	SetControlVisible(test_note, false);
	SetControlVisible(source_choice, true);
	std::wstring mode = valid ? (display_sources[source_index].hdr ? L"HDR" : L"SDR") : L"";
	SetTextIfChanged(monitor_mode, mode.c_str());
	if (valid && display_sources[source_index].hdr)
	{
		white_position = static_cast<int>(std::lround(display_sources[source_index].white));
		if (SendMessageW(white_slider, TBM_GETPOS, 0, 0) != white_position)
			SendMessageW(white_slider, TBM_SETPOS, TRUE, white_position);
		const auto detected = FormatDecimal(display_sources[source_index].white, 0) + L" nits";
		SetTextIfChanged(white_value, detected.c_str());
	}
	else
	{
		const auto text = std::to_wstring(white_position) + L" nits";
		SetTextIfChanged(white_value, text.c_str());
	}
}

// Makes every control match the program's current state. Called after anything changes.
void RefreshInterface()
{
	if (!editor_ready || (!render_only && !editor_sync_depth && !IsWindowVisible(main_window)))
	{
		if (runtime_ready)
			UpdateTray();
		return;
	}
	const auto source_index = SelectedSourceIndex();
	const bool valid = source_index >= 0 && static_cast<size_t>(source_index) < display_sources.size();
	RefreshEffectLists(source_index, valid);
	RefreshEffectDescription();
	const auto controls = SliderParameters();
	const bool changing_adjustment_page = adjustment_page != (settings_page ? -1 : selected_effect);
	RefreshAdjustmentSliders(controls, changing_adjustment_page);
	RefreshAdjustmentHelp(controls);
	adjustment_page = settings_page ? -1 : selected_effect;
	ShowControlsForCurrentPage();
	if (!settings_page && selected_effect == effect::NightLight)
		RefreshNightLightControls(source_index, valid);
	RefreshFilterAndStartupControls();
	const bool applying = ProcessingActive(),
			   profile_active = application_context_active && !foreground_program.IsManualOverride();
	const auto status_text = BuildStatusText(applying);
	RefreshLookLabels(profile_active);
	RefreshWindowIcons(applying);
	ShowStatusText(status_text);
	ShowContrastAndHdrControls();
	RefreshToolbarButtons(source_index, valid, profile_active);
	RefreshDisplayControls(source_index, valid);
	UpdateTray();
	LayoutEffectControls();
	if (changing_adjustment_page)
		for (size_t i = 0; i < adjustment_sliders.size(); ++i)
			SetControlVisible(adjustment_sliders[i], i < controls.size() && !settings_page);
	InvalidateGraph();
}

void LayoutEditorControls()
{
	if (!editor_ready)
		return;
	++graph_layout_version;
	effect_layout_valid = false;
	if (ui_font)
		DeleteObject(ui_font);
	ui_font = ui::CreateUiFont(static_cast<UINT>(dpi));
	if (hint_font)
		DeleteObject(hint_font);
	LOGFONTW hint_style {};
	GetObjectW(ui_font, sizeof(hint_style), &hint_style);
	hint_style.lfHeight = -ScaleForDpi(ui::style::SECONDARY_TEXT_SIZE);
	hint_font = CreateFontIndirectW(&hint_style);
	SendMessageW(selected_effect_toggle, WM_SETFONT, reinterpret_cast<WPARAM>(ui_font), TRUE);
	for (HWND slider : adjustment_sliders)
		SendMessageW(slider, WM_SETFONT, reinterpret_cast<WPARAM>(ui_font), TRUE);
	for (HWND slider : { contrast_slider, pivot_slider, white_slider })
		SendMessageW(slider, WM_SETFONT, reinterpret_cast<WPARAM>(ui_font), TRUE);
	ui::StyleTooltip(monitor_tips);
	for (HWND control :
	     { effect_list,       effect_title,       effect_description, amount_label,         pivot_label,
	       white_label,       gamma_label,        gamma_choice,       gamma_other,          status_label,
	       monitor_mode,      monitor_profile,    reset_button,       export_button,        corrections_heading,
	       source_choice,     test_note,          bypass_button,      hdr_fix_control,      shortcut_control,
	       shortcut_button,   shortcut_label,     preset_label,       preset_choice,        save_preset_button,
	       save_as_button,    open_preset_button, autosave_control,   revert_preset_button, profiles_button,
	       active_look_label, order_hint })
		SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(ui_font), TRUE);
	for (HWND control : { correction_list, windows_effect_list, effects_label })
		SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(ui_font), TRUE);
	// Presets and their actions belong to the right pane; the sidebar is the stack.
	PlaceControl(preset_choice, 198, 21, 244, 240);
	PlaceControl(save_preset_button, 558, 16, 32, 42);
	PlaceControl(save_as_button, 598, 16, 32, 42);
	PlaceControl(autosave_control, 454, 16, 92, 28);
	PlaceControl(export_button, 644, 16, 32, 42);
	PlaceControl(active_look_label, 20, 48, 444, 24);
	PlaceControl(revert_preset_button, 724, 16, 32, 42);
	PlaceControl(test_note, 20, 480, 180, 82);
	PlaceControl(effect_title, 198, 64, 470, 24);
	PlaceControl(selected_effect_toggle, 676, 62, 40, 28);
	PlaceControl(reset_button, 724, 62, 32, 42);
	SendMessageW(saturation_report_button, WM_SETFONT, reinterpret_cast<WPARAM>(ui_font), TRUE);
	for (HWND control : { amount_value, pivot_value, white_value })
		SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(ui_font), TRUE);
	PlaceControl(corrections_heading, 13, 16, 160, 28);
	PlaceControl(correction_list, 0, 48, SIDEBAR_WIDTH, 36);
	PlaceControl(windows_effect_list, 0, 84, SIDEBAR_WIDTH, 72);
	const int effect_rows = std::min(static_cast<int>(CreativeOrder().size()), 10),
			  effect_height = effect_rows * effects::ROW_HEIGHT;
	PlaceControl(effects_label, 13, 168, 160, 24);
	PlaceControl(effect_list, 0, 196, SIDEBAR_WIDTH, effect_height);
	PlaceControl(order_hint, 0, 196 + effect_height + 6, SIDEBAR_WIDTH, 18);
	PlaceControl(test_note, 20, settings_page ? 480 : 500, 180, settings_page ? 82 : 62);
	const auto width = [&](HWND control, int minimum = 64, int maximum = 240)
	{
		return ui::ContentWidth(control, ui_font, minimum, maximum);
	};
	PlaceControl(font_label, 198, 98, 220, 20);
	PlaceControl(font_choice, 198, 125, width(font_choice, 128, 220), 180);
	PlaceControl(open_preset_button, 198, 228, width(open_preset_button), 28);
	PlaceControl(bypass_button, 456, 164, width(bypass_button, 100), 28);
	PlaceControl(saturation_report_button, 456, 228, width(saturation_report_button), 28);
	PlaceControl(version_label, 198, 64, 220, 24);
	PlaceControl(startup_control, 198, 164, 220, 26);
	PlaceControl(start_minimized_control, 198, 196, 220, 26);
	PlaceControl(diagnostics_control, 456, 196, 220, 26);
	PlaceControl(shortcut_label, 456, 98, 300, 20);
	PlaceControl(shortcut_control, 456, 120, 200, 28);
	PlaceControl(shortcut_button, 664, 120, 92, 28);

	for (size_t i = 0; i < adjustment_sliders.size(); ++i)
	{
		SendMessageW(adjustment_labels[i], WM_SETFONT, reinterpret_cast<WPARAM>(ui_font), TRUE);
		SendMessageW(adjustment_values[i], WM_SETFONT, reinterpret_cast<WPARAM>(ui_font), TRUE);
	}
	for (HWND control : { settings_button, font_choice, font_label, curve_channel, import_button, export_1d_button })
		SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(ui_font), TRUE);
	for (HWND control : { version_label, startup_control, start_minimized_control, diagnostics_control })
		SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(ui_font), TRUE);
	for (HWND control : { filter_choice, filter_settings_button, correction_note })
		SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(ui_font), TRUE);
	for (HWND control :
	     { night_schedule, night_start, night_end, night_start_label, night_end_label, night_include, night_display })
		SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(ui_font), TRUE);
	SendMessageW(order_hint, WM_SETFONT, reinterpret_cast<WPARAM>(hint_font ? hint_font : ui_font), TRUE);
	ui::RefreshCaptionFont(main_window);
	RefreshPresetChoice();
	RefreshInterface();
	for (HWND control : { monitor_mode, monitor_profile })
		InvalidateRect(control, nullptr, FALSE);
	InvalidateRect(main_window, nullptr, TRUE);
}

void ShowEditor()
{
	SourceQueryBatch query_batch;
	EditorSync sync;
	CreateEditorIfNeeded();
	RefreshColorFilters();
	RefreshSources();
	ApplyLiveCorrections();
	RefreshInterface();
	ShowWindow(main_window, SW_SHOW);
	ShowWindow(main_window, SW_RESTORE);
	SetForegroundWindow(main_window);
}

void LayoutMonitorControls()
{
	if (!curve_canvas)
		return;
	RECT client {};
	GetClientRect(curve_canvas, &client);
	const auto g = CalculateMonitorGeometry(curve_canvas);
	const auto label = [&](HWND control, int x, int y, int width, int height)
	{
		SetWindowPos(control, nullptr, x, y, width, height, SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOCOPYBITS);
		InvalidateRect(control, nullptr, FALSE);
	};
	label(website_link, g.foot.left + ScaleForDpi(2), g.foot.top + ScaleForDpi(1),
	      g.foot.right - g.foot.left - ScaleForDpi(4), g.foot.bottom - g.foot.top - ScaleForDpi(2));
	MoveWindow(hdr_button, g.left, g.top, g.size, g.size, TRUE);
	MoveWindow(power_button, g.right, g.top, g.size, g.size, TRUE);
	MoveWindow(info_button, (g.screen.left + g.screen.right - g.size) / 2, g.top, g.size, g.size, TRUE);
	MoveWindow(profiles_button, g.right, g.bottom, g.size, g.size, TRUE);
	MoveWindow(monitor_settings_button, g.left, g.bottom, g.size, g.size, TRUE);
	MoveWindow(status_label, g.left + g.size + ScaleForDpi(12), g.top, g.right - g.left - g.size - ScaleForDpi(24),
	           g.size, TRUE);
	label(monitor_mode, g.left + g.size + ScaleForDpi(32), g.bottom, g.right - g.left - g.size - ScaleForDpi(64),
	      g.size);
	const int center = client.right / 2;
	RECT choice {};
	GetWindowRect(source_choice, &choice);
	const int height = choice.bottom - choice.top;
	const int row_top = (g.body.bottom + g.foot.bottom - height) / 2;
	MoveWindow(source_choice, 0, row_top, center - ScaleForDpi(68), ScaleForDpi(180), TRUE);
	label(monitor_profile, center + ScaleForDpi(68), row_top, client.right - center - ScaleForDpi(68), height);
}
}
