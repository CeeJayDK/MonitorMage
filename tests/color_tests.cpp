#include "color_math.h"
#include "recovery_rules.h"
#include <iostream>
#include <limits>
#include <string>

namespace {
void check(bool value, const char* name) { if (!value) throw std::runtime_error(name); }
void near(double actual, double expected, double tolerance, const char* name) {
    check(std::isfinite(actual) && std::abs(actual - expected) <= tolerance, name);
}
template<class F> void rejects(F action, const char* name) {
    bool rejected = false;
    try { action(); } catch (const std::invalid_argument&) { rejected = true; }
    check(rejected, name);
}
void tests() {
    // Published PQ anchors, independent of the correction implementation.
    near(color::pqEncode(100), 0.508078421517399, 1e-12, "PQ 100-nit anchor");
    near(color::pqEncode(1000), 0.751827096247041, 1e-12, "PQ 1000-nit anchor");
    near(color::pqDecode(1), 10000, 1e-8, "PQ maximum");
    check(color::pqEncode(0) == 0 && color::pqDecode(0) == 0, "Exact black");
    for (double nits : {0.001, 0.01, 0.1, 1.0, 10.0, 100.0, 1000.0, 10000.0})
        near(color::pqDecode(color::pqEncode(nits)), nits, std::max(1e-8, nits * 1e-10), "PQ roundtrip");
    for (double code : {0.0, 0.01, color::srgbCodeKnee, 0.1, 0.5, 1.0})
        near(color::srgbEncode(color::srgbDecode(code)), code, 1e-12, "sRGB roundtrip");

    // Work from source SDR values to expected luminance rather than only checking
    // that two implementations of the LUT formula agree.
    for (double white : {80.0, 100.0, 200.0, 300.0, 480.0}) for (double gamma : {2.2, 2.4}) {
        const color::HdrGammaCurve curve(white, gamma);
        for (double signal : {0.01, 0.04, 0.1, 0.25, 0.5, 0.75}) {
            const double incoming = color::pqEncode(white * color::srgbDecode(signal));
            const double corrected = color::pqDecode(curve.map(incoming));
            near(corrected, white * std::pow(signal, gamma), 1e-7, "SDR-in-HDR target luminance");
        }
        check(curve.map(0) == 0, "Correction black endpoint");
        check(curve.map(curve.whiteCode()) == curve.whiteCode(), "Fixed SDR white");
        check(curve.map(1) == 1, "HDR peak unchanged");
        near(curve.map(curve.whiteCode() - 1e-9), curve.whiteCode(), 1e-8, "SDR-white continuity");
        for (std::size_t entries : {std::size_t{256}, std::size_t{1024}, std::size_t{4096}}) {
            const auto lut = curve.sample(entries);
            double previous = 0;
            for (std::size_t i = 0; i < entries; ++i) {
                const double x = static_cast<double>(i) / (entries - 1);
                check(std::isfinite(lut[i]) && lut[i] >= previous && lut[i] >= 0 && lut[i] <= 1, "Monotonic bounded LUT");
                if (x >= curve.whiteCode()) check(lut[i] == x, "Exact HDR highlight passthrough");
                previous = lut[i];
            }
        }
    }
    const color::HdrGammaCurve reference(200, 2.2);
    const double low = color::pqEncode(200 * color::srgbDecode(0.1));
    check(reference.map(low) < low, "Expected shadow darkening");
    rejects([] { color::HdrGammaCurve invalid(0, 2.2); }, "Invalid white rejected");
    rejects([] { color::HdrGammaCurve invalid(200, std::numeric_limits<double>::quiet_NaN()); }, "NaN rejected");
    rejects([&] { reference.map(-0.1); }, "Invalid PQ input rejected");
    rejects([&] { reference.sample(1); }, "Short LUT rejected");
    rejects([&] { reference.sample(4097); }, "Oversized LUT rejected");

    // Exercise creative contrast in source SDR code space, before HDR gamma.
    for (double contrast : {-0.25, 0.0, 0.25}) for (bool fix : {false, true}) {
        double previous = 0;
        for (int i = 0; i <= 4096; ++i) {
            const double x = i / 4096.0;
            const double y = color::combinedHdr(x, 280, 2.2, contrast, 0.5, fix);
            check(std::isfinite(y) && y >= previous && y <= 1, "Combined HDR curve monotonicity");
            if (x >= color::pqEncode(280)) check(y == x, "Combined HDR highlight passthrough");
            if (!fix && contrast == 0) check(y == x, "Disabled HDR operations preserve identity");
            if (fix && contrast == 0) near(y, color::HdrGammaCurve(280, 2.2).map(x), 1e-15, "Zero-contrast HDR regression");
            previous = y;
        }
        for (double signal : {0.1, 0.25, 0.5, 0.75}) {
            const double input = color::pqEncode(280 * color::srgbDecode(signal));
            const double adjusted = color::contrast(signal, contrast, 0.5);
            const double expected = 280 * (fix ? std::pow(adjusted, 2.2) : color::srgbDecode(adjusted));
            near(color::pqDecode(color::combinedHdr(input, 280, 2.2, contrast, 0.5, fix)), expected, 1e-7, "Combined HDR luminance semantics");
        }
    }
    live::Ramp baseline{}, last{}, candidate{}, external{};
    for (size_t c = 0; c < 3; ++c) for (size_t i = 0; i < 256; ++i) {
        baseline[c][i] = static_cast<std::uint16_t>(i * 257);
        last[c][i] = static_cast<std::uint16_t>(color::contrast(i / 255.0, 0.2, 0.5) * 65535);
        candidate[c][i] = static_cast<std::uint16_t>(color::contrast(i / 255.0, -0.2, 0.5) * 65535);
        external[c][i] = static_cast<std::uint16_t>(i * 200);
    }
    check(live::ownedRamp(baseline, baseline, last, candidate, false, false), "Untouched baseline recoverable");
    check(live::ownedRamp(last, baseline, last, candidate, true, false), "Committed correction recoverable");
    check(live::ownedRamp(candidate, baseline, last, candidate, true, true), "Crash during write recoverable");
    check(!live::ownedRamp(candidate, baseline, last, candidate, true, false), "Unpublished candidate not owned");
    check(!live::ownedRamp(external, baseline, last, candidate, true, true), "External calibration preserved");
}
}
int main() {
    try { tests(); std::cout << "HDR curve tests passed. No display API calls.\n"; return 0; }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
