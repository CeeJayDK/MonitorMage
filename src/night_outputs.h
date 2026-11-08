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
		double lastLevel = -1;
		bool carry = false;
	};
	std::vector<Slot> slots_;

public:
	bool IsActive() const
	{
		for (const auto& slot : slots_)
			if (slot.session->IsActive())
				return true;
		return false;
	}
	void Release(const live::Output& output, Session* primary = nullptr)
	{
		for (auto& slot : slots_)
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
		slots_.erase(std::remove_if(slots_.begin(), slots_.end(),
		                            [](const Slot& slot)
		                            { return !slot.keep && !slot.carry && !slot.session->IsActive(); }),
		             slots_.end());
	}
	int Stop()
	{
		int result = 0;
		for (auto& slot : slots_)
		{
			const bool wasActive = slot.session->IsActive();
			const int code = slot.session->Stop();
			if (wasActive)
				slot.carry = code == 11;
			if (code != 0 && code != 10 && code != 11)
				result = code;
		}
		slots_.erase(std::remove_if(slots_.begin(), slots_.end(),
		                            [](const Slot& slot) { return !slot.carry && !slot.session->IsActive(); }),
		             slots_.end());
		return result;
	}
	void Update(const std::vector<live::Output>& outputs, const live::Output& primary, const Settings& config,
	            double level)
	{
		for (auto& slot : slots_)
			slot.keep = false;
		for (const auto& output : outputs)
		{
			if (output.name == primary.name || !output.supported || level <= 0 ||
			    !IsDisplayIncluded(config, output.identity))
				continue;
			auto at = std::find_if(slots_.begin(), slots_.end(),
			                       [&](const Slot& slot) { return live::SameDisplays(slot.output, output); });
			if (at == slots_.end())
			{
				slots_.push_back({ output, std::make_unique<Session>(), true });
				at = slots_.end() - 1;
			}
			at->keep = true;
			if (at->session->IsActive() && !at->session->HasSameDisplayMode(output))
			{
				const int code = at->session->Stop();
				if (code != 0 && code != 10 && code != 11)
					throw std::runtime_error("Night light could not restore a display after its mode changed");
			}
			live::Settings settings;
			settings.hdrFix = false;
			settings.nightWarmth = level;
			settings.bypass = true;
			if (at->session->IsActive())
			{
				if (at->lastLevel != level)
					at->session->Update(output, settings);
			}
			else
				at->session->Start(output, settings);
			at->output = output;
			at->lastLevel = level;
			at->carry = false;
		}
		for (auto& slot : slots_)
			if (!slot.keep)
			{
				const bool wasActive = slot.session->IsActive();
				const int code = slot.session->Stop();
				if (wasActive)
					slot.carry = code == 11;
				if (code != 0 && code != 10 && code != 11)
					throw std::runtime_error("Night light could not restore a display");
			}
		slots_.erase(std::remove_if(slots_.begin(), slots_.end(),
		                            [](const Slot& slot)
		                            { return !slot.keep && !slot.carry && !slot.session->IsActive(); }),
		             slots_.end());
	}
};

using Outputs = OutputSessions<>;
}
