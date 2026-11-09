// Turning effects on and off, ordering them, and describing their sliders.
#include "app_state.h"

namespace app
{
bool& AdjustmentEnabled(int effect)
{
	auto& adjustments = CurrentPreset(preset_role::Look).current.Adjustments;
	switch (effect)
	{
		case 2:
			return adjustments.BalanceEnabled;
		case 3:
			return adjustments.GammaEnabled;
		case 4:
			return adjustments.LevelsEnabled;
		case 5:
			return adjustments.ToneEnabled;
		case 6:
			return adjustments.CurvesEnabled;
		default:
			return adjustments.ImportEnabled;
	}
}

void ToggleEffect(int effect)
{
	if (effect == effect::ColorFilters)
	{
		ChangeWindowsFilter(!filter_state.on, filter_state.filter);
		return;
	}
	if (effect == effect::NightLight)
	{
		night_config.Enabled = !night_config.Enabled;
		PersistPresetChoices();
		ApplyLiveCorrections();
		RefreshInterface();
		return;
	}
	if (effect >= effect::Saturation)
	{
		auto& look = CurrentPreset(preset_role::Look).current;
		auto& enabled = effect == effect::Saturation ? look.SaturationEnabled : look.MonochromeEnabled;
		enabled = !enabled;
		OnPresetEdited(preset_role::Look);
		SyncColorMatrix();
		RefreshInterface();
		return;
	}
	if (effect == effect::ContrastCurves)
		contrast_enabled = !contrast_enabled;
	else if (effect == effect::HdrGamma)
		hdr_fix_enabled = !hdr_fix_enabled;
	else
		AdjustmentEnabled(effect) = !AdjustmentEnabled(effect);
	OnPresetEdited(effect == effect::HdrGamma ? 1 : 0);
	ApplyLiveCorrections();
	RefreshInterface();
}

void MoveEffect(int effect, int target)
{
	if (target < 0 || target >= static_cast<int>(effect_order.size()))
		return;
	const auto before = effect_order;
	const auto position = std::find(effect_order.begin(), effect_order.end(), effect);
	effect_order.erase(position);
	effect_order.insert(effect_order.begin() + target, effect);
	correction_first =
		std::find(effect_order.begin(), effect_order.end(), 1) < std::find(effect_order.begin(), effect_order.end(), 0);
	if (!render_only && !PersistPresetChoices())
	{
		effect_order = before;
		correction_first = std::find(before.begin(), before.end(), 1) < std::find(before.begin(), before.end(), 0);
		RefreshInterface();
		return;
	}
	ApplyLiveCorrections();
	RefreshInterface();
}

void MoveCreative(int effect, int target)
{
	auto order = CreativeOrder();
	if (effect == effect::HdrGamma || effect >= effect::Saturation || target < 0 ||
	    target >= static_cast<int>(order.size()) - 2)
		return;
	const auto before = effect_order;
	const auto found = std::find(order.begin(), order.end(), effect);
	if (found == order.end())
		return;
	order.erase(found);
	order.insert(order.begin() + target, effect);
	size_t index = 0;
	for (auto& slot : effect_order)
		if (slot != effect::HdrGamma)
			slot = order[index++];
	correction_first = ProcessingOrderIndex(effect::HdrGamma) < ProcessingOrderIndex(effect::ContrastCurves);
	if (!render_only && !PersistPresetChoices())
	{
		effect_order = before;
		correction_first = ProcessingOrderIndex(effect::HdrGamma) < ProcessingOrderIndex(effect::ContrastCurves);
		RefreshInterface();
		return;
	}
	ApplyLiveCorrections();
	RefreshInterface();
}

std::vector<Parameter> SliderParameters()
{
	auto& adjustments = CurrentPreset(preset_role::Look).current.Adjustments;
	if (selected_effect == effect::ColorBalance)
		return { { L"Red", 50, 150, &adjustments.Balance[color::BALANCE_RED], 100 },
			     { L"Green", 50, 150, &adjustments.Balance[color::BALANCE_GREEN], 100 },
			     { L"Blue", 50, 150, &adjustments.Balance[color::BALANCE_BLUE], 100 },
			     { L"Warmth", -50, 50, &adjustments.Balance[color::BALANCE_WARMTH] },
			     { L"Tint", -50, 50, &adjustments.Balance[color::BALANCE_TINT] } };
	if (selected_effect == effect::MidtoneGamma)
		return { { L"Midtone gamma", 50, 200, &adjustments.Midtone, 100 } };
	if (selected_effect == effect::Levels)
		return { { L"Input black", 0, 95, &adjustments.Levels[color::INPUT_BLACK] },
			     { L"Input white", 5, 100, &adjustments.Levels[color::INPUT_WHITE], 100 },
			     { L"Output black", 0, 95, &adjustments.Levels[color::OUTPUT_BLACK] },
			     { L"Output white", 5, 100, &adjustments.Levels[color::OUTPUT_WHITE], 100 } };
	if (selected_effect == effect::ShadowsHighlights)
		return { { L"Shadows", -50, 50, &adjustments.Shadows }, { L"Highlights", -50, 50, &adjustments.Highlights } };
	if (selected_effect == effect::Saturation)
		return { { L"Saturation", 0, color::SATURATION_MAXIMUM, &CurrentPreset(preset_role::Look).current.Saturation,
			       100 } };
	if (selected_effect == effect::ChannelMixer)
	{
		auto& mix = CurrentPreset(preset_role::Look).current.Monochrome;
		return { { L"Red", 0, 200, &mix[0], 21 }, { L"Green", 0, 200, &mix[1], 72 }, { L"Blue", 0, 200, &mix[2], 7 } };
	}
	if (selected_effect == effect::NightLight)
		return { { L"Warmth", 0, 100, &night_config.Strength, 35 },
			     { L"Fade minutes", 0, 60, &night_config.Fade, 10 } };
	return {};
}
}
