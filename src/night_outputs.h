#pragma once
#include "live_lut.h"
#include "night_light.h"
#include <memory>
namespace night
{
// The selected output uses the editor's existing combined session. These
// sessions cover only the remaining independent output groups.
template <class Session = live::Session>
class OutputSessions
{
	struct Slot
	{
		live::Output output;
		std::unique_ptr<Session> session;
		bool keep = false;
		double last_level = -1;
		bool carry = false;
	};
	std::vector<Slot> _slots;

public:
	bool IsActive() const
	{
		for (const auto& slot : _slots)
			if (slot.session->IsActive())
				return true;
		return false;
	}
	void Release(const live::Output& output, Session* primary = nullptr)
	{
		for (auto& slot : _slots)
			if (slot.output.name == output.name || live::SameDisplays(slot.output, output))
			{
				const int code = slot.session->Stop();
				if (code != 0 && code != 10 && code != 11)
					throw std::runtime_error("Night light could not release the selected display");
				if (primary)
					primary->InheritCarryOver(*slot.session);
				slot.keep = false;
				slot.carry = false;
			}
		_slots.erase(std::remove_if(_slots.begin(), _slots.end(),
		                            [](const Slot& slot)
		                            { return !slot.keep && !slot.carry && !slot.session->IsActive(); }),
		             _slots.end());
	}
	int Stop()
	{
		int result = 0;
		for (auto& slot : _slots)
		{
			const bool was_active = slot.session->IsActive();
			const int code = slot.session->Stop();
			if (was_active)
				slot.carry = code == 11;
			if (code != 0 && code != 10 && code != 11)
				result = code;
		}
		_slots.erase(std::remove_if(_slots.begin(), _slots.end(),
		                            [](const Slot& slot) { return !slot.carry && !slot.session->IsActive(); }),
		             _slots.end());
		return result;
	}
	void Update(const std::vector<live::Output>& outputs, const live::Output& primary, const Settings& config,
	            double level)
	{
		for (auto& slot : _slots)
			slot.keep = false;
		for (const auto& output : outputs)
		{
			if (output.name == primary.name || !output.supported || level <= 0 ||
			    !IsDisplayIncluded(config, output.identity))
				continue;
			auto at = std::find_if(_slots.begin(), _slots.end(),
			                       [&](const Slot& slot) { return live::SameDisplays(slot.output, output); });
			if (at == _slots.end())
			{
				_slots.push_back({ output, std::make_unique<Session>(), true });
				at = _slots.end() - 1;
			}
			at->keep = true;
			if (at->session->IsActive() && !at->session->HasSameDisplayMode(output))
			{
				const int code = at->session->Stop();
				if (code != 0 && code != 10 && code != 11)
					throw std::runtime_error("Night light could not restore a display after its mode changed");
			}
			live::Settings settings;
			settings.HdrFix = false;
			settings.NightWarmth = level;
			settings.Bypass = true;
			if (at->session->IsActive())
			{
				if (at->last_level != level)
					at->session->Update(output, settings);
			}
			else
				at->session->Start(output, settings);
			at->output = output;
			at->last_level = level;
			at->carry = false;
		}
		for (auto& slot : _slots)
			if (!slot.keep)
			{
				const bool was_active = slot.session->IsActive();
				const int code = slot.session->Stop();
				if (was_active)
					slot.carry = code == 11;
				if (code != 0 && code != 10 && code != 11)
					throw std::runtime_error("Night light could not restore a display");
			}
		_slots.erase(std::remove_if(_slots.begin(), _slots.end(),
		                            [](const Slot& slot)
		                            { return !slot.keep && !slot.carry && !slot.session->IsActive(); }),
		             _slots.end());
	}
};

using Outputs = OutputSessions<>;
}
