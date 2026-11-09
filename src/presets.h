#pragma once
#include <filesystem>
#include <string>
#include "adjustments.h"
#include "color_limits.h"

// Which preset slot is meant: the creative look or the display correction.
namespace preset_role
{
enum Id : int
{
	Look,
	Correction,
	Count,
};
}

namespace presets
{
enum class Kind
{
	Look,
	Correction
};

struct Data
{
	Kind kind = Kind::Look;
	int Contrast = 0, Pivot = 50; // Slider units, one hundredth of signal range.
	int Gamma = 22;
	bool HdrFix = true;
	bool ApplyInSdr = false;
	bool ContrastEnabled = true;
	int Saturation = 100;           // Percent; 0 grayscale, 100 unchanged, up to 300 boosted.
	bool SaturationEnabled = false; // Old presets never enable a global matrix.
	bool MonochromeEnabled = false;
	std::array<int, 3> Monochrome { 21, 72, 7 }; // Relative RGB contributions; normalized when applied.
	color::Adjustments Adjustments;
};

// First-run defaults are separate from the historical file-format defaults.
// Older presets without adjustment flags must retain their original behavior.
inline Data InactivePreset(Kind kind)
{
	Data data;
	data.kind = kind;
	data.ContrastEnabled = false;
	data.HdrFix = false;
	auto& adjustments = data.Adjustments;
	adjustments.BalanceEnabled = adjustments.GammaEnabled = adjustments.LevelsEnabled = adjustments.ToneEnabled =
		adjustments.CurvesEnabled = adjustments.ImportEnabled = false;
	return data;
}

bool operator==(const Data& a, const Data& b);
inline bool operator!=(const Data& a, const Data& b)
{
	return !(a == b);
}

struct Draft
{
	std::filesystem::path path;
	Data current, saved;
	bool loaded = true; // Unopened library entries retain only their filename.
	bool IsDirty() const { return loaded && current != saved; }
	bool CanAutosave(bool enabled) const { return enabled && !path.empty() && IsDirty(); }
	void MarkSaved() { saved = current; }
};

std::string Encode(const Data& data);
Data Decode(const std::string& text);
Data Read(const std::filesystem::path& path);
// Also used for the app's UTF-16 settings file; bytes are published unchanged.
void WriteBytes(const std::filesystem::path& path, const std::string& bytes, bool replace);
// Publish a complete, flushed file through a same-directory atomic replacement.
void Write(const std::filesystem::path& path, const Data& data, bool replace);
}
