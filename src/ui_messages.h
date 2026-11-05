#pragma once
#include <windows.h>
#include <string>
#include <string_view>

// Translate technical failures at the UI boundary. Keep the original exception
// for diagnostics; do not make display/recovery decisions from these messages.
namespace ui::messages {
inline std::wstring error(std::string_view detail) {
    const std::string diagnostic = "MonitorMage: " + std::string(detail) + "\n";
    OutputDebugStringA(diagnostic.c_str());
    struct Message { std::string_view detail; const wchar_t* text; };
    static constexpr Message messages[] = {
        {"Driver rejected the correction", L"The display driver could not apply these settings. Try a smaller adjustment."},
        {"Cannot verify the requested correction", L"MonitorMage could not check that your adjustments were applied. Try turning it on again."},
        {"Cannot capture baseline calibration", L"MonitorMage could not read your current display colors. Your adjustments have not been started."},
        {"Cannot read active calibration", L"MonitorMage could not read the current display colors. Try turning it on again."},
        {"Another calibration changed the ramp; its adjustment will be preserved", L"Another app changed the display colors. MonitorMage has paused to keep those changes."},
        {"This display mode is not supported by the live backend", L"MonitorMage cannot adjust this display with its current settings. Try another display or display mode."},
        {"Display mode changed with a non-neutral calibration. Its new calibration cannot be inferred from the retained correction", L"The display changed, but MonitorMage could not separate its adjustments from your existing calibration. Restore your calibration before turning MonitorMage on again."},
        {"Another LUT session or timed test is active", L"Another MonitorMage session or display test is running. Close it and try again."},
        {"The previous correction or its restoration is still running", L"MonitorMage is still finishing the previous display change. Please try again shortly."},
        {"Cannot save recovery baseline", L"MonitorMage could not save the display colors needed for crash recovery. Check free space and folder permissions."},
        {"Settings path unavailable", L"MonitorMage could not find its settings file. Run it from a folder you can save files in."},
        {"Select an active display group", L"Choose a connected display first."},
        {"Close the timed diagnostic before enabling live correction", L"Close the display test before turning MonitorMage on."},
        {"Select a display before switching HDR", L"Choose a display before switching Windows HDR."},
        {"Restore the display baseline before changing HDR mode", L"MonitorMage could not restore your previous display colors. Check the picture before switching HDR."},
        {"HDR was requested but its completion timer could not start; corrections remain off", L"The HDR change was requested, but MonitorMage could not check the result. Check Windows display settings, then turn MonitorMage on again."},
        {"The selected display did not become available after the mode change", L"The selected display is no longer available. Choose a connected display."},
        {"The display mode did not settle after repeated changes", L"The display settings keep changing. Wait for them to settle, then turn MonitorMage on again."},
        {"Display restoration could not be verified during the mode change", L"MonitorMage could not confirm that your previous display colors were restored after the display changed."},
        {"Display mode changed; re-enable after checking the new configuration", L"The display settings changed. Check the picture, then turn MonitorMage on again."},
        {"Display configuration changed before activation", L"The display settings changed before MonitorMage could start. Try turning it on again."},
        {"Display configuration changed or is unsupported", L"MonitorMage cannot adjust the display with its current settings. Check the selected display."},
        {"Recovery helper stopped; disabling corrections", L"MonitorMage has paused because crash recovery stopped working. Restart MonitorMage before trying again."},
        {"HDR switching is unavailable for this display mode", L"Windows HDR is not available with these display settings."},
        {"Display state changed; refresh and try HDR again", L"The display settings changed. Try switching Windows HDR again."},
        {"Windows foreground notifications are unavailable; automatic profiles cannot be enabled", L"MonitorMage cannot detect when you switch programs. Restart MonitorMage to try automatic profiles again."},
        {"Application rules require a full executable path", L"Choose a program using Browse or Running programs."},
        {"Choose a saved look for the application rule", L"Choose a saved look for this program."},
        {"Duplicate executable rule", L"This program already has a rule. Edit that rule instead."},
        {"At most 256 application rules are supported", L"You can save up to 256 program rules. Remove a rule before adding another."},
        {"This is a 3D LUT. The current hardware backend accepts 1D transforms only", L"This is a 3D LUT. MonitorMage currently supports only 1D LUTs. Choose a 1D file instead."},
        {"LUT curves must be finite and nondecreasing", L"This LUT reverses tones or contains invalid values. Choose curves that only rise or stay level."},
        {"This LUT produces out-of-range values on 0..1 input; it cannot be used as a normalized hardware curve", L"This LUT goes beyond the brightness range MonitorMage supports. Choose a LUT made for the standard 0 to 1 range."},
        {"Cannot open LUT file", L"The LUT file could not be opened. Check that it still exists and you have permission to read it."},
        {"Cannot read LUT file", L"The LUT file could not be read. Try another copy of the file."},
        {"LUT file exceeds 16 MiB", L"This LUT file is too large. Choose a file smaller than 16 MB."},
        {"Windows color filter settings changed; try again", L"Showing the current Windows color filter settings."},
        {"Windows color filter settings changed during the request", L"Showing the current Windows color filter settings."},
        {"Windows did not confirm the color filter change; check Windows settings", L"Settings saved. Check the picture or open Windows settings."},
        {"Windows uses an unrecognized color filter setting", L"This Windows color filter setting is not supported by MonitorMage. Change it in Windows settings."},
        {"The Windows request was sent but could not be monitored; check Windows settings", L"The color filter change was requested, but MonitorMage could not check the result. Check Windows color filter settings."},
    };
    for (const auto& message : messages) if (detail == message.detail) return message.text;
    if (detail == "Driver did not retain the requested correction")
        return L"The display driver did not keep these settings. Try a smaller adjustment or check other display apps.";
    if (detail == "This image is a genuine 3D LUT with channel mixing. It cannot be represented by the current 1D hardware backend")
        return L"This image contains a 3D LUT. MonitorMage currently supports only 1D LUTs. Choose a 1D file instead.";
    if (detail == "Image is not a 1D ramp or a recognized ReShade/Hald LUT layout")
        return L"MonitorMage cannot find color curves in this image. Choose a 1D ramp or a supported LUT image.";
    if (detail == "Image LUT must be fully opaque") return L"This LUT image has transparent pixels. Use an image without transparency.";
    const auto starts = [&](std::string_view prefix) { return detail.substr(0, prefix.size()) == prefix; };
    if (starts("Cannot initialize recovery") || starts("Cannot map recovery") || starts("Cannot start recovery") || starts("Recovery helper did not") || starts("Cannot locate recovery"))
        return L"MonitorMage could not start crash recovery, so your adjustments have not been started. Try restarting MonitorMage.";
    if (starts("Cannot enumerate outputs") || starts("Cannot read display configuration") || starts("Cannot identify display output"))
        return L"MonitorMage could not read your display settings. Check that the display is connected and try again.";
    if (starts("Windows refused the HDR change"))
        return L"Windows could not switch HDR. Try changing it in Windows display settings.";
    if (starts("Windows startup setting failed"))
        return L"MonitorMage could not change its Windows startup setting. Check that Windows allows startup apps.";
    if (starts("Cannot open preset")) return L"The preset could not be opened. Check that the file still exists and you can read it.";
    if ((starts("Cannot read preset") || starts("Cannot read complete preset") || starts("Cannot measure preset"))) return L"The preset could not be read. Try another copy of the file.";
    if (starts("Cannot ") && (detail.find("write") != detail.npos || detail.find("replace") != detail.npos || detail.find("flush") != detail.npos || detail.find("temporary") != detail.npos || detail.find("publish preset") != detail.npos))
        return L"The file could not be saved. Check free space and folder permissions, or use Save as to choose another folder.";
    if (starts("Unsupported preset version") || starts("Unsupported correction version") || starts("Unsupported application profile version"))
        return L"This file uses a format this version of MonitorMage cannot read. Try a newer version of MonitorMage.";
    if (detail.find("preset") != detail.npos && (starts("Invalid") || starts("Unknown") || starts("Missing") || starts("Duplicate") || starts("Unsupported") || starts("Preset value")))
        return L"This preset contains invalid or unsupported settings. Try another copy of the file.";
    if (detail == "Invalid contrast enabled flag" || detail == "Invalid adjustment enabled flag" || detail == "Invalid curve sample" || detail == "Missing curve sample" || detail == "Unsupported HDR gamma" || detail == "Invalid HDR correction flag" || detail == "Invalid SDR correction flag")
        return L"This preset contains invalid or unsupported settings. Try another copy of the file.";
    if (starts("Preset exceeds") || starts("Preset is too large")) return L"This preset is too large or contains unreadable text. Choose another preset.";
    if (starts("filesystem error:")) return L"MonitorMage could not access a file or folder. Check that it exists and you have permission to use it.";
    if (starts("Invalid") || starts("Incomplete") || starts("Unknown") || starts("Duplicate") || starts("Missing") || starts("Extra") || starts("Conflicting")) {
        if (detail.find("LUT") != detail.npos || detail.find("cube") != detail.npos || detail.find("spi1d") != detail.npos || detail.find("CAL") != detail.npos || detail.find("CSV") != detail.npos)
            return L"This color curve file contains missing or invalid data. Try another copy or export it again as a 1D LUT.";
    }
    return std::wstring(detail.begin(), detail.end());
}
}
