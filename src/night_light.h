#pragma once
#include <algorithm>
#include <cmath>
#include <string>
#include <set>
#include <sstream>
#include <iomanip>
#include <cstdint>
namespace night
{
struct Settings
{
	bool Enabled = false, Scheduled = false;
	int Strength = 35, Start = 21 * 60, End = 7 * 60, Fade = 10;
	std::set<std::wstring> Excluded;
};

struct Phase
{
	double level = 0;
	unsigned next_ms = 0;
};

// Local clock seconds. Only fades need repeated updates; plateaus sleep until
// their next boundary. The OS time-change notification handles clock edits.
inline Phase CurrentPhase(const Settings& settings, double seconds)
{
	if (!settings.Enabled || settings.Strength <= 0)
		return {};
	if (!settings.Scheduled)
		return { settings.Strength / 100., 0 };
	constexpr double SECONDS_PER_DAY = 86400;
	const double duration = std::fmod((settings.End - settings.Start) * 60. + SECONDS_PER_DAY, SECONDS_PER_DAY);
	if (duration == 0)
		return {};
	const double elapsed = std::fmod(seconds - settings.Start * 60. + SECONDS_PER_DAY, SECONDS_PER_DAY);
	if (elapsed >= duration)
		return { 0, static_cast<unsigned>(std::max(1000., std::ceil((SECONDS_PER_DAY - elapsed) * 1000))) };
	const double fade = std::min(settings.Fade * 60., duration / 2);
	const double scale = fade > 0 ? std::min({ 1., elapsed / fade, (duration - elapsed) / fade }) : 1.;
	const bool fading = fade > 0 && (elapsed < fade || elapsed >= duration - fade);
	const double next = fading ? 5 : std::max(1., duration - fade - elapsed);
	return { settings.Strength / 100. * scale, static_cast<unsigned>(std::ceil(next * 1000)) };
}

inline std::wstring FormatTime(int minute)
{
	wchar_t text[6] {};
	swprintf_s(text, L"%02d:%02d", minute / 60, minute % 60);
	return text;
}

inline int ParseTime(const std::wstring& text)
{
	if (text.size() != 5 || text[2] != L':')
		return -1;
	for (int i : { 0, 1, 3, 4 })
		if (text[i] < L'0' || text[i] > L'9')
			return -1;
	const int h = (text[0] - L'0') * 10 + text[1] - L'0', m = (text[3] - L'0') * 10 + text[4] - L'0';
	return h < 24 && m < 60 ? h * 60 + m : -1;
}

// Stable physical target IDs, independent of HDR/SDR mode and clone membership.
inline std::set<std::wstring> DisplayKeys(const std::wstring& identity)
{
	std::set<std::wstring> keys;
	std::wistringstream lines(identity);
	std::wstring line;
	while (std::getline(lines, line))
	{
		const auto last = line.rfind(L':');
		const auto flags = last == std::wstring::npos ? last : line.rfind(L':', last - 1);
		if (flags == std::wstring::npos)
			continue;
		line.resize(flags);
		uint64_t hash = 14695981039346656037ull;
		for (wchar_t c : line)
		{
			hash ^= static_cast<uint16_t>(c);
			hash *= 1099511628211ull;
		}
		std::wostringstream out;
		out << std::hex << std::setw(16) << std::setfill(L'0') << hash;
		keys.insert(out.str());
	}
	return keys;
}

inline bool IsDisplayIncluded(const Settings& settings, const std::wstring& identity)
{
	for (const auto& key : DisplayKeys(identity))
		if (settings.Excluded.count(key))
			return false;
	return true;
}

inline void SetDisplayIncluded(Settings& settings, const std::wstring& identity, bool value)
{
	for (const auto& key : DisplayKeys(identity))
	{
		if (value)
			settings.Excluded.erase(key);
		else
			settings.Excluded.insert(key);
	}
}
}
