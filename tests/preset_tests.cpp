#include "presets.h"
#include <windows.h>
#include <iostream>
#include <stdexcept>
#include <fstream>

namespace
{
void Check(bool value, const char* message)
{
	if (!value)
		throw std::runtime_error(message);
}

template <class F>
void Rejects(F action, const char* message)
{
	try
	{
		action();
	}
	catch (const std::exception&)
	{
		return;
	}
	throw std::runtime_error(message);
}

std::string Changed(std::string text, const char* from, const char* to)
{
	const auto at = text.find(from);
	Check(at != std::string::npos, "Fixture field missing");
	text.replace(at, strlen(from), to);
	return text;
}
}

int main(int argc, char** argv)
{
	std::filesystem::path folder;
	try
	{
		presets::Data look;
		look.contrast = 18;
		look.pivot = 42;
		presets::Data correction;
		correction.kind = presets::Kind::Correction;
		correction.gamma = 24;
		correction.hdrFix = false;
		const auto lookText = presets::Encode(look), correctionText = presets::Encode(correction);
		Check(presets::Decode(lookText) == look, "Look must round-trip exactly");
		Check(!presets::Decode(lookText).saturationEnabled && presets::Decode(lookText).saturation == 100,
		      "Older looks must not change desktop saturation");
		Check(!presets::Decode(lookText).monochromeEnabled, "Old looks must keep Channel mixer off");
		auto mixerLook = look;
		mixerLook.monochromeEnabled = true;
		mixerLook.monochrome = { 95, 35, 0 };
		const auto mixerText = presets::Encode(mixerLook);
		Check(mixerText.find("Version=7") != std::string::npos && presets::Decode(mixerText) == mixerLook,
		      "Channel mixer must round-trip in a new format version");
		mixerLook.monochromeEnabled = false;
		Check(presets::Decode(presets::Encode(mixerLook)) == mixerLook, "Disabled channel mix must be retained");
		Rejects([&] { presets::Decode(Changed(mixerText, "MixRed=0.95", "MixRed=2.01")); },
		        "Invalid channel weight must be refused");
		Rejects([&] { presets::Decode(Changed(mixerText, "MonochromeEnabled=true", "MonochromeEnabled=maybe")); },
		        "Invalid mixer flag must be refused");
		for (int amount : { 0, 50, 100, 120, 200, 300 })
		{
			auto matrixLook = look;
			matrixLook.saturationEnabled = true;
			matrixLook.saturation = amount;
			Check(presets::Decode(presets::Encode(matrixLook)) == matrixLook,
			      "Saturation including grayscale must round-trip");
			matrixLook.saturationEnabled = false;
			Check(presets::Decode(presets::Encode(matrixLook)) == matrixLook,
			      "Disabled saturation must retain its setting");
		}
		auto matrixLook = look;
		matrixLook.saturationEnabled = true;
		matrixLook.saturation = 120;
		const auto matrixText = presets::Encode(matrixLook);
		Rejects([&] { presets::Decode(Changed(matrixText, "Saturation=1.20", "Saturation=1.21")); },
		        "Out-of-range saturation must be refused");
		Rejects([&] { presets::Decode(Changed(matrixText, "[Matrix]\r\nEnabled=true", "[Matrix]\r\nEnabled=maybe")); },
		        "Invalid saturation enabled flag must be refused");
		Rejects([&] { presets::Decode(Changed(matrixText, "Saturation=1.20", "Saturation=nan")); },
		        "Non-finite saturation must be refused");
		matrixLook.saturation = 300;
		const auto strongText = presets::Encode(matrixLook);
		Check(strongText.find("Version=6") != std::string::npos,
		      "Extended saturation needs a version older builds reject explicitly");
		Rejects([&] { presets::Decode(Changed(strongText, "Saturation=3.00", "Saturation=3.01")); },
		        "Saturation beyond the new range must be refused");
		auto disabledLook = look;
		disabledLook.contrastEnabled = false;
		Check(presets::Decode(presets::Encode(disabledLook)) == disabledLook,
		      "Disabled effect must round-trip with its parameters intact");
		const auto legacy = Changed(Changed(lookText, "Version=2", "Version=1"), "Enabled=true\r\n", "");
		Check(presets::Decode(legacy) == look, "Old look files must default to enabled contrast");
		Rejects([&] { presets::Decode(Changed(lookText, "Enabled=true", "Enabled=maybe")); },
		        "Invalid effect enabled flag must be refused");
		Check(presets::Decode(correctionText) == correction, "Correction must round-trip exactly");
		auto sdrCorrection = correction;
		sdrCorrection.applyInSdr = true;
		Check(presets::Decode(presets::Encode(sdrCorrection)) == sdrCorrection,
		      "SDR applicability must round-trip in correction presets");
		Check(!presets::Decode(correctionText).applyInSdr, "Existing correction presets must remain HDR-only");
		Check(presets::Decode("\xef\xbb\xbf" + lookText) == look, "UTF-8 BOM must be accepted");
		Check(lookText.find("Gamma=") == std::string::npos && correctionText.find("Contrast=") == std::string::npos,
		      "Files must isolate creative settings and correction settings");
		Rejects([&] { presets::Decode(Changed(lookText, "Version=2", "Version=99")); },
		        "Unknown versions must be refused");
		Rejects([&] { presets::Decode(Changed(lookText, "Contrast=0.18", "Contrast=nan")); },
		        "Non-finite numbers must be refused");
		Rejects([&] { presets::Decode(Changed(lookText, "Contrast=0.18", "Contrast=0.26")); },
		        "Out-of-range values must be refused");
		Rejects([&] { presets::Decode(Changed(lookText, "Contrast=0.18", "Contrast=0.123")); },
		        "Unsupported precision must not be silently rounded");
		Rejects([&] { presets::Decode(Changed(lookText, "Contrast=0.18", "Contrast=0,18")); },
		        "File numbers must be independent of system locale");
		Rejects([&] { presets::Decode(lookText + "Pivot=0.60\n"); }, "Duplicate fields must be refused");
		Rejects([&] { presets::Decode(lookText + "[Correction]\nGamma=2.4\n"); }, "Mixed-role fields must be refused");
		Rejects([&] { presets::Decode(Changed(correctionText, "Gamma=2.4", "Gamma=3.0")); },
		        "Unsupported gamma must be refused");
		Rejects([&] { presets::Decode(Changed(correctionText, "Enabled=false", "Enabled=maybe")); },
		        "Invalid booleans must be refused");
		Rejects([&] { presets::Decode(std::string(1048577, 'a')); }, "Oversized files must be refused");
		Rejects([&] { presets::Decode(lookText + std::string(1, '\0')); }, "Embedded NUL must be refused");
		wchar_t temp[32768] {};
		Check(GetTempPathW(static_cast<DWORD>(std::size(temp)), temp) != 0, "No temporary directory");
		folder = std::filesystem::path(temp) / (L"LutPresetTests-" + std::to_wstring(GetCurrentProcessId()) + L"-" +
		                                        std::to_wstring(GetTickCount64()));
		Check(std::filesystem::create_directory(folder), "Temporary test directory must be new");
		const auto path = folder / L"Farver-\u00e6\u00f8\u00e5.lutpreset";
		presets::Write(path, look, false);
		Check(presets::Read(path) == look, "Unicode paths must round-trip");
		presets::Draft draft { path, look, look };
		draft.current.contrast = -12;
		Check(draft.IsDirty() && !draft.CanAutosave(false),
		      "Manual mode must retain unsaved edits without scheduling a write");
		Check(presets::Read(path) == look, "Draft edits must leave the saved file unchanged");
		Check(draft.CanAutosave(true), "Opted-in autosave must recognize an edited named preset");
		presets::Draft unnamed = draft;
		unnamed.path.clear();
		Check(!unnamed.CanAutosave(true), "Autosave must not invent a name for an unnamed preset");
		Rejects([&] { presets::Write(path, draft.current, false); },
		        "Creating a new preset must not overwrite an existing file");
		Check(presets::Read(path) == look, "Failed Save As must preserve old contents");
		HANDLE lock = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
		                          FILE_ATTRIBUTE_NORMAL, nullptr);
		Check(lock != INVALID_HANDLE_VALUE, "Could not lock destination fixture");
		bool failed = false;
		try
		{
			presets::Write(path, draft.current, true);
		}
		catch (const std::exception&)
		{
			failed = true;
		}
		CloseHandle(lock);
		Check(failed && presets::Read(path) == look, "Failed replacement must preserve existing contents");
		presets::Write(path, draft.current, true);
		draft.MarkSaved();
		Check(!draft.IsDirty() && presets::Read(path) == draft.current, "Successful save must commit the entire draft");
		for (const auto& entry : std::filesystem::directory_iterator(folder))
			Check(entry.path() == path, "Temporary write files must be cleaned up");
		std::filesystem::remove(path);
		std::filesystem::remove(folder);
		if (argc == 2)
		{
			unsigned examples = 0;
			for (const auto& entry : std::filesystem::recursive_directory_iterator(argv[1]))
				if (entry.path().extension() == L".lutpreset")
				{
					const auto data = presets::Read(entry.path());
					Check(presets::Decode(presets::Encode(data)) == data, "Bundled example must parse and round-trip");
					++examples;
				}
			Check(examples >= 12, "Expected bundled correction and look examples");
		}
		std::cout
			<< "Preset parsing, role separation, drafts and atomic file replacement passed without display changes.\n";
		return 0;
	}
	catch (const std::exception& error)
	{
		std::cerr << error.what() << '\n';
		return 1;
	}
}
