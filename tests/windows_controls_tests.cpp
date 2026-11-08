#include "windows_controls.h"
#include <iostream>
#include <stdexcept>
int main()
{
	try
	{
		const auto before = windows_controls::ReadColorFilterState();
		windows_controls::Controller controller;
		if (controller.IsBusy() || !controller.Poll())
			throw std::runtime_error("Idle controller must have no process or work");
		bool invalid = false;
		try
		{
			controller.Set({}, true, 6);
		}
		catch (const std::invalid_argument&)
		{
			invalid = true;
		}
		if (!invalid || controller.IsBusy())
			throw std::runtime_error("Invalid filter must not start a Windows process");
		bool unavailable = false;
		try
		{
			controller.Set({}, true, 0);
		}
		catch (const std::runtime_error&)
		{
			unavailable = true;
		}
		if (!unavailable || controller.IsBusy())
			throw std::runtime_error("Unavailable expected state must prevent mutations");
		const auto after = windows_controls::ReadColorFilterState();
		windows_controls::State observed;
		observed.available = true;
		observed.on = true;
		observed.filter = 2;
		using C = windows_controls::Completion;
		if (windows_controls::CheckCompletion(observed, true, 2, true, 0) != C::ConfirmedRequest)
			throw std::runtime_error("Successful broker plus matching saved settings must complete normally");
		if (windows_controls::CheckCompletion(observed, true, 2, true, 1) != C::SettingsSaved ||
		    windows_controls::CheckCompletion(observed, true, 2, false, STILL_ACTIVE) != C::SettingsSaved)
			throw std::runtime_error("A broker failure must not erase matching saved settings");
		if (windows_controls::CheckCompletion(observed, false, 2, true, 0) != C::DifferentSettings ||
		    windows_controls::CheckCompletion(observed, true, 3, true, 0) != C::DifferentSettings)
			throw std::runtime_error("Different current settings must refresh the UI rather than claim a cause");
		if (windows_controls::CheckCompletion({}, true, 2, true, 0) != C::Unreadable)
			throw std::runtime_error("Unreadable settings must stay distinct from different settings");
		if (before.available != after.available || before.on != after.on || before.filter != after.filter)
			throw std::runtime_error("Rejected requests must leave Windows settings unchanged");
		std::cout
			<< "Windows controls validation and read-only inspection passed; no filters or settings were changed.\n";
		return 0;
	}
	catch (const std::exception& error)
	{
		std::cerr << error.what() << '\n';
		return 1;
	}
}
