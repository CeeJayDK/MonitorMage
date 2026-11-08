#include "lut1d.h"
#include "presets.h"
#include <windows.h>
#include <iostream>
#include <fstream>
namespace
{
void Check(bool value, const char* text)
{
	if (!value)
		throw std::runtime_error(text);
}

template <class F>
void Rejects(F f, const char* text)
{
	try
	{
		f();
	}
	catch (const std::exception&)
	{
		return;
	}
	throw std::runtime_error(text);
}

void AssertNear(double a, double b, double epsilon, const char* text)
{
	Check(std::abs(a - b) <= epsilon, text);
}

void Compare(const lut1d::Channels& a, const lut1d::Channels& b, double epsilon)
{
	Check(a[0].size() == b[0].size(), "Round-trip changed sample count");
	for (size_t c = 0; c < 3; ++c)
		for (size_t i = 0; i < a[c].size(); ++i)
			AssertNear(a[c][i], b[c][i], epsilon, "Round-trip changed LUT samples");
}
}

int wmain(int argc, wchar_t** argv)
{
	std::filesystem::path temporary;
	try
	{
		Check(argc == 2, "Missing fixture root");
		const std::filesystem::path root(argv[1]), images = root / L"lut-images", ocio = root / L"opencolorio";
		const auto horizontal = lut1d::Read(images / L"horizontal-8.png").channels;
		for (const wchar_t* name : { L"vertical-8.png", L"repeated-rows-8.png" })
			Compare(horizontal, lut1d::Read(images / name).channels, 0);
		AssertNear(horizontal[0][8], 120 / 255.0, 1e-15, "PNG gamma metadata must not alter numerical samples");
		const auto precise = lut1d::Read(images / L"horizontal-16.png").channels;
		Compare(precise, lut1d::Read(images / L"horizontal-16.tiff").channels, 0);
		AssertNear(precise[1][128], std::lround(std::pow(.5, 1.1) * 62000) / 65535.0, 1e-15,
		           "16-bit image precision was reduced");
		const auto floats = lut1d::Read(images / L"float-32.tiff").channels;
		AssertNear(floats[1][8], std::pow(.5, 1.1), 1e-7, "Floating TIFF values were changed");
		const auto tiled = lut1d::Read(images / L"reshade-separable.png").channels;
		Compare(tiled, lut1d::Read(images / L"hald-separable.png").channels, 0);
		AssertNear(tiled[0][1], 1 / 3.0, 1e-15, "Wrong ReShade red axis");
		AssertNear(tiled[1][3], 230 / 255.0, 1e-15, "Wrong green axis");
		AssertNear(tiled[2][3], 204 / 255.0, 1e-15, "Wrong blue axis");
		for (const wchar_t* name : { L"transparent.png", L"reshade-coupled.png", L"not-a-lut.png",
		                             L"out-of-range-float.tiff", L"multipage.tiff" })
			Rejects([&] { lut1d::Read(images / name); }, "An unsupported image LUT was accepted");
		for (const wchar_t* name : { L"lut1d_1.spi1d", L"lut1d_2.spi1d", L"lut1d_3.spi1d" })
		{
			const auto values = lut1d::Read(ocio / name).channels;
			Check(values[0].size() <= 4096, "Dense LUT was not bounded");
			AssertNear(values[0].front(), 0, 1e-14, "Wrong OCIO black endpoint");
			AssertNear(values[0].back(), 1, 1e-14, "Wrong OCIO white endpoint");
		}
		const auto srgb = lut1d::Read(ocio / L"sRGB_to_linear.spi1d").channels;
		Check(srgb[0].size() == 4096, "Expected bounded source resampling");
		AssertNear(color::SampleCurve(srgb[0], .5), color::SrgbDecode(.5), 2e-7,
		           "SPI extended input range was ignored");
		for (const wchar_t* name : { L"lut1d_4.spi1d", L"lut1d_5.spi1d" })
			Rejects([&] { lut1d::Read(ocio / name); }, "Out-of-range source transform must not be silently clipped");
		const auto domain = lut1d::DecodeCube("LUT_1D_SIZE 2\nLUT_1D_INPUT_RANGE 0 2\n0 0 0\n1 1 1\n");
		AssertNear(domain[0].back(), .5, 0, "Resolve input range was ignored");
		const auto csv = lut1d::DecodeCsv("input,red,green,blue\n0,0,0,0\n0.25,0.5,0.4,0.3\n1,1,1,1\n");
		AssertNear(csv[0][1], 2 / 3.0, 1e-15, "Nonuniform CSV indexing was ignored");
		Rejects([&] { lut1d::DecodeSpi("Version 1 From 0 1 Length 2 Components 3 { 0 0 0 1 1 1 } extra"); },
		        "Extra SPI data accepted");
		Rejects([&] { lut1d::DecodeCsv("input,r,g,b\n0,0,0,0\n0,1,1,1\n"); }, "Duplicate indices accepted");
		Rejects(
			[&]
			{ lut1d::DecodeCsv("input,output_red,output_green,output_blue,model\n0,0,0,0,hdr_pq\n1,1,1,1,hdr_pq\n"); },
			"HDR diagnostic CSV treated as SDR look");
		wchar_t temp[MAX_PATH] {};
		Check(GetTempPathW(MAX_PATH, temp) != 0, "No temporary folder");
		temporary = std::filesystem::path(temp) / (L"LutFormats-" + std::to_wstring(GetCurrentProcessId()) + L"-" +
		                                           std::to_wstring(GetTickCount64()));
		Check(std::filesystem::create_directory(temporary), "Fixture folder already exists");
		for (const wchar_t* extension : { L".cube", L".spi1d", L".cal", L".csv", L".png", L".tif" })
		{
			const auto path = temporary / (std::wstring(L"roundtrip") + extension);
			lut1d::Write(path, precise);
			Compare(precise, lut1d::Read(path).channels, 1e-15);
		}
		presets::Data data;
		for (int c = 0; c < 3; ++c)
			for (int i = 0; i < 4096; ++i)
				data.adjustments.imported[c].push_back(std::pow(i / 4095.0, 1 + c * .1));
		const auto encoded = presets::Encode(data);
		Check(encoded.find("Version=4") != std::string::npos, "Dense embedded LUTs need version 4");
		Check(presets::Decode(encoded) == data, "4096-sample preset must round-trip exactly");
		for (const auto& entry : std::filesystem::directory_iterator(temporary))
			std::filesystem::remove(entry.path());
		std::filesystem::remove(temporary);
		std::cout
			<< "Real OCIO files, native image precision, separability, ranges and six-format round-trips passed without display changes.\n";
		return 0;
	}
	catch (const std::exception& e)
	{
		if (!temporary.empty())
		{
			for (const auto& entry : std::filesystem::directory_iterator(temporary))
				std::filesystem::remove(entry.path());
			std::filesystem::remove(temporary);
		}
		std::cerr << e.what() << '\n';
		return 1;
	}
}
