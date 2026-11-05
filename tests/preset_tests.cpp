#include "presets.h"
#include <windows.h>
#include <iostream>
#include <stdexcept>
#include <fstream>

namespace {
void check(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
template<class F> void rejects(F action, const char* message) {
    try { action(); } catch (const std::exception&) { return; }
    throw std::runtime_error(message);
}
std::string changed(std::string text, const char* from, const char* to) {
    const auto at = text.find(from); check(at != std::string::npos, "Fixture field missing");
    text.replace(at, strlen(from), to); return text;
}
}
int main(int argc,char** argv) {
    std::filesystem::path folder;
    try {
        presets::Data look; look.contrast = 18; look.pivot = 42;
        presets::Data correction; correction.kind = presets::Kind::Correction; correction.gamma = 24; correction.hdrFix = false;
        const auto lookText = presets::encode(look), correctionText = presets::encode(correction);
        check(presets::decode(lookText) == look, "Look must round-trip exactly");
        check(!presets::decode(lookText).saturationEnabled&&presets::decode(lookText).saturation==100,"Older looks must not change desktop saturation");
        check(!presets::decode(lookText).monochromeEnabled,"Old looks must keep Channel mixer off");
        auto mixerLook=look;mixerLook.monochromeEnabled=true;mixerLook.monochrome={95,35,0};
        const auto mixerText=presets::encode(mixerLook);
        check(mixerText.find("Version=7")!=std::string::npos&&presets::decode(mixerText)==mixerLook,"Channel mixer must round-trip in a new format version");
        mixerLook.monochromeEnabled=false;check(presets::decode(presets::encode(mixerLook))==mixerLook,"Disabled channel mix must be retained");
        rejects([&]{presets::decode(changed(mixerText,"MixRed=0.95","MixRed=2.01"));},"Invalid channel weight must be refused");
        rejects([&]{presets::decode(changed(mixerText,"MonochromeEnabled=true","MonochromeEnabled=maybe"));},"Invalid mixer flag must be refused");
        for(int amount:{0,50,100,120,200,300}){
            auto matrixLook=look;matrixLook.saturationEnabled=true;matrixLook.saturation=amount;
            check(presets::decode(presets::encode(matrixLook))==matrixLook,"Saturation including grayscale must round-trip");
            matrixLook.saturationEnabled=false;
            check(presets::decode(presets::encode(matrixLook))==matrixLook,"Disabled saturation must retain its setting");
        }
        auto matrixLook=look;matrixLook.saturationEnabled=true;matrixLook.saturation=120;
        const auto matrixText=presets::encode(matrixLook);
        rejects([&]{presets::decode(changed(matrixText,"Saturation=1.20","Saturation=1.21"));},"Out-of-range saturation must be refused");
        rejects([&]{presets::decode(changed(matrixText,"[Matrix]\r\nEnabled=true","[Matrix]\r\nEnabled=maybe"));},"Invalid saturation enabled flag must be refused");
        rejects([&]{presets::decode(changed(matrixText,"Saturation=1.20","Saturation=nan"));},"Non-finite saturation must be refused");
        matrixLook.saturation=300;const auto strongText=presets::encode(matrixLook);
        check(strongText.find("Version=6")!=std::string::npos,"Extended saturation needs a version older builds reject explicitly");
        rejects([&]{presets::decode(changed(strongText,"Saturation=3.00","Saturation=3.01"));},"Saturation beyond the new range must be refused");
        auto disabledLook = look; disabledLook.contrastEnabled = false;
        check(presets::decode(presets::encode(disabledLook)) == disabledLook, "Disabled effect must round-trip with its parameters intact");
        const auto legacy = changed(changed(lookText, "Version=2", "Version=1"), "Enabled=true\r\n", "");
        check(presets::decode(legacy) == look, "Old look files must default to enabled contrast");
        rejects([&] { presets::decode(changed(lookText, "Enabled=true", "Enabled=maybe")); }, "Invalid effect enabled flag must be refused");
        check(presets::decode(correctionText) == correction, "Correction must round-trip exactly");
        auto sdrCorrection=correction;sdrCorrection.applyInSdr=true;
        check(presets::decode(presets::encode(sdrCorrection))==sdrCorrection,"SDR applicability must round-trip in correction presets");
        check(!presets::decode(correctionText).applyInSdr,"Existing correction presets must remain HDR-only");
        check(presets::decode("\xef\xbb\xbf" + lookText) == look, "UTF-8 BOM must be accepted");
        check(lookText.find("Gamma=") == std::string::npos && correctionText.find("Contrast=") == std::string::npos, "Files must isolate creative settings and correction settings");
        rejects([&] { presets::decode(changed(lookText, "Version=2", "Version=99")); }, "Unknown versions must be refused");
        rejects([&] { presets::decode(changed(lookText, "Contrast=0.18", "Contrast=nan")); }, "Non-finite numbers must be refused");
        rejects([&] { presets::decode(changed(lookText, "Contrast=0.18", "Contrast=0.26")); }, "Out-of-range values must be refused");
        rejects([&] { presets::decode(changed(lookText, "Contrast=0.18", "Contrast=0.123")); }, "Unsupported precision must not be silently rounded");
        rejects([&] { presets::decode(changed(lookText, "Contrast=0.18", "Contrast=0,18")); }, "File numbers must be independent of system locale");
        rejects([&] { presets::decode(lookText + "Pivot=0.60\n"); }, "Duplicate fields must be refused");
        rejects([&] { presets::decode(lookText + "[Correction]\nGamma=2.4\n"); }, "Mixed-role fields must be refused");
        rejects([&] { presets::decode(changed(correctionText, "Gamma=2.4", "Gamma=3.0")); }, "Unsupported gamma must be refused");
        rejects([&] { presets::decode(changed(correctionText, "Enabled=false", "Enabled=maybe")); }, "Invalid booleans must be refused");
        rejects([&] { presets::decode(std::string(1048577, 'a')); }, "Oversized files must be refused");
        rejects([&] { presets::decode(lookText + std::string(1, '\0')); }, "Embedded NUL must be refused");
        wchar_t temp[32768]{}; check(GetTempPathW(static_cast<DWORD>(std::size(temp)), temp) != 0, "No temporary directory");
        folder = std::filesystem::path(temp) / (L"LutPresetTests-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64()));
        check(std::filesystem::create_directory(folder), "Temporary test directory must be new");
        const auto path = folder / L"Farver-\u00e6\u00f8\u00e5.lutpreset";
        presets::write(path, look, false);
        check(presets::read(path) == look, "Unicode paths must round-trip");
        presets::Draft draft{path, look, look}; draft.current.contrast = -12;
        check(draft.dirty() && !draft.canAutosave(false), "Manual mode must retain unsaved edits without scheduling a write");
        check(presets::read(path) == look, "Draft edits must leave the saved file unchanged");
        check(draft.canAutosave(true), "Opted-in autosave must recognize an edited named preset");
        presets::Draft unnamed = draft; unnamed.path.clear();
        check(!unnamed.canAutosave(true), "Autosave must not invent a name for an unnamed preset");
        rejects([&] { presets::write(path, draft.current, false); }, "Creating a new preset must not overwrite an existing file");
        check(presets::read(path) == look, "Failed Save As must preserve old contents");
        HANDLE lock = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        check(lock != INVALID_HANDLE_VALUE, "Could not lock destination fixture");
        bool failed = false;
        try { presets::write(path, draft.current, true); } catch (const std::exception&) { failed = true; }
        CloseHandle(lock);
        check(failed && presets::read(path) == look, "Failed replacement must preserve existing contents");
        presets::write(path, draft.current, true); draft.markSaved();
        check(!draft.dirty() && presets::read(path) == draft.current, "Successful save must commit the entire draft");
        for (const auto& entry : std::filesystem::directory_iterator(folder)) check(entry.path() == path, "Temporary write files must be cleaned up");
        std::filesystem::remove(path); std::filesystem::remove(folder);
        if(argc==2){
            unsigned examples=0;
            for(const auto& entry:std::filesystem::recursive_directory_iterator(argv[1]))if(entry.path().extension()==L".lutpreset"){
                const auto data=presets::read(entry.path());
                check(presets::decode(presets::encode(data))==data,"Bundled example must parse and round-trip");++examples;
            }
            check(examples>=12,"Expected bundled correction and look examples");
        }
        std::cout << "Preset parsing, role separation, drafts and atomic file replacement passed without display changes.\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
