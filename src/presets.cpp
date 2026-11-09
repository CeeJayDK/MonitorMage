#include "presets.h"
#include <windows.h>
#include <cmath>
#include <iomanip>
#include <locale>
#include <map>
#include <sstream>
#include <stdexcept>
#include <vector>

namespace presets
{
namespace
{
std::string Trim(const std::string& value)
{
	const auto first = value.find_first_not_of(" \t\r");
	return first == std::string::npos ? "" : value.substr(first, value.find_last_not_of(" \t\r") - first + 1);
}

int ParseHundredths(const std::string& text, int minimum, int maximum)
{
	std::istringstream input(text);
	input.imbue(std::locale::classic());
	double value = 0;
	if (!(input >> value) || !std::isfinite(value))
		throw std::runtime_error("Invalid preset number");
	input >> std::ws;
	const double scaled = value * 100;
	if (!input.eof() || scaled < minimum - 1e-8 || scaled > maximum + 1e-8 ||
	    std::abs(scaled - std::round(scaled)) > 1e-8)
		throw std::runtime_error("Preset value is outside the supported range or 0.01 precision");
	return static_cast<int>(std::lround(scaled));
}

void Validate(const Data& data)
{
	color::Validate(data.Adjustments);
	if (data.Saturation < 0 || data.Saturation > color::SATURATION_MAXIMUM)
		throw std::runtime_error("Unsupported saturation");
	for (int value : data.Monochrome)
		if (value < 0 || value > 200)
			throw std::runtime_error("Unsupported channel mix");
	if ((data.kind != Kind::Look && data.kind != Kind::Correction) || data.Contrast < -25 || data.Contrast > 25 ||
	    data.Pivot < 20 || data.Pivot > 80 || (data.Gamma != 22 && data.Gamma != 24))
		throw std::runtime_error("Unsupported preset settings");
}

[[noreturn]] void FileError(const char* action)
{
	throw std::runtime_error(std::string(action) + " (Windows error " + std::to_string(GetLastError()) + ")");
}

struct File
{
	HANDLE handle;
	explicit File(HANDLE value) : handle(value) {}
	~File()
	{
		if (handle != INVALID_HANDLE_VALUE)
			CloseHandle(handle);
	}
	void Close()
	{
		if (handle != INVALID_HANDLE_VALUE)
		{
			CloseHandle(handle);
			handle = INVALID_HANDLE_VALUE;
		}
	}
};
}

bool operator==(const Data& a, const Data& b)
{
	return a.kind == b.kind &&
	       (a.kind == Kind::Look ? a.Contrast == b.Contrast && a.Pivot == b.Pivot &&
	                                   a.ContrastEnabled == b.ContrastEnabled && a.Adjustments == b.Adjustments &&
	                                   a.Saturation == b.Saturation && a.SaturationEnabled == b.SaturationEnabled &&
	                                   a.Monochrome == b.Monochrome && a.MonochromeEnabled == b.MonochromeEnabled
	                             : a.Gamma == b.Gamma && a.HdrFix == b.HdrFix && a.ApplyInSdr == b.ApplyInSdr);
}

std::string Encode(const Data& data)
{
	Validate(data);
	const bool mixer = data.MonochromeEnabled || data.Monochrome != std::array<int, 3> { 21, 72, 7 };
	std::ostringstream out;
	out.imbue(std::locale::classic());
	out << std::fixed << std::setprecision(2);
	out << "; MonitorMage preset. Captured monitor calibration is never included.\r\n"
		   "[LutWorkbench]\r\nVersion="
		<< (data.kind == Kind::Look
	            ? (mixer                                              ? 7
	               : data.SaturationEnabled || data.Saturation != 100 ? (data.Saturation > 120 ? 6 : 5)
	               : data.Adjustments != color::Adjustments {} ? (data.Adjustments.Imported[0].size() > 1024 ? 4 : 3)
	                                                           : 2)
	        : data.ApplyInSdr ? 2
	                          : 1)
		<< "\r\nType=";
	if (data.kind == Kind::Look)
		out << "Look\r\n\r\n[Look]\r\nEnabled=" << (data.ContrastEnabled ? "true" : "false")
			<< "\r\nContrast=" << data.Contrast / 100.0 << "\r\nPivot=" << data.Pivot / 100.0 << "\r\n";
	else
		out << "Correction\r\n\r\n[Correction]\r\nHDRGammaEnabled=" << (data.HdrFix ? "true" : "false")
			<< "\r\nGamma=" << (data.Gamma == 22 ? "2.2" : "2.4") << "\r\n";
	if (data.kind == Kind::Correction && data.ApplyInSdr)
		out << "AlsoApplyInSDR=true\r\n";
	if (data.kind == Kind::Look &&
	    (data.Adjustments != color::Adjustments {} || data.SaturationEnabled || data.Saturation != 100 || mixer))
	{
		const auto& adjustments = data.Adjustments;
		out << "\r\n[Adjustments]\r\n";
		const char* balance[] { "Red", "Green", "Blue", "Warmth", "Tint" };
		for (int i = 0; i < 5; ++i)
			out << balance[i] << '=' << adjustments.Balance[i] / 100.0 << "\r\n";
		out << "Midtone=" << adjustments.Midtone / 100.0 << "\r\n";
		const char* levels[] { "InputBlack", "InputWhite", "OutputBlack", "OutputWhite" };
		for (int i = 0; i < 4; ++i)
			out << levels[i] << '=' << adjustments.Levels[i] / 100.0 << "\r\n";
		out << "Shadows=" << adjustments.Shadows / 100.0 << "\r\nHighlights=" << adjustments.Highlights / 100.0
			<< "\r\n";
		out << "BalanceEnabled=" << (adjustments.BalanceEnabled ? "true" : "false")
			<< "\r\nGammaEnabled=" << (adjustments.GammaEnabled ? "true" : "false")
			<< "\r\nLevelsEnabled=" << (adjustments.LevelsEnabled ? "true" : "false")
			<< "\r\nToneEnabled=" << (adjustments.ToneEnabled ? "true" : "false") << "\r\n";
		out << "CurvesEnabled=" << (adjustments.CurvesEnabled ? "true" : "false")
			<< "\r\nImportEnabled=" << (adjustments.ImportEnabled ? "true" : "false") << "\r\n";
		const auto samples = [&](const std::string& key, const std::vector<double>& values)
		{
			out << key << '=' << std::defaultfloat << std::setprecision(17);
			for (size_t i = 0; i < values.size(); ++i)
			{
				if (i)
					out << ',';
				out << values[i];
			}
			out << "\r\n" << std::fixed << std::setprecision(2);
		};
		for (int i = 0; i < 4; ++i)
			samples("Curve" + std::to_string(i), adjustments.Curves[i]);
		for (int i = 0; i < 3; ++i)
			samples("Imported" + std::to_string(i), adjustments.Imported[i]);
	}
	if (data.kind == Kind::Look && (data.SaturationEnabled || data.Saturation != 100 || mixer))
		out << "\r\n[Matrix]\r\nEnabled=" << (data.SaturationEnabled ? "true" : "false")
			<< "\r\nSaturation=" << data.Saturation / 100.0 << "\r\n";
	if (data.kind == Kind::Look && mixer)
	{
		out << "MonochromeEnabled=" << (data.MonochromeEnabled ? "true" : "false") << "\r\n";
		const char* channels[] { "Red", "Green", "Blue" };
		for (int i = 0; i < 3; ++i)
			out << "Mix" << channels[i] << '=' << data.Monochrome[i] / 100.0 << "\r\n";
	}
	return out.str();
}

Data Decode(const std::string& text)
{
	if (text.size() > 1048576 || text.find('\0') != std::string::npos)
		throw std::runtime_error("Preset is too large or contains invalid text");
	std::istringstream input(text.substr(text.compare(0, 3, "\xef\xbb\xbf") == 0 ? 3 : 0));
	std::string line, section;
	std::map<std::string, std::string> values;
	while (std::getline(input, line))
	{
		line = Trim(line);
		if (line.empty() || line[0] == ';' || line[0] == '#')
			continue;
		if (line.front() == '[' && line.back() == ']')
		{
			section = line.substr(1, line.size() - 2);
			if (section != "LutWorkbench" && section != "Look" && section != "Correction" && section != "Adjustments" &&
			    section != "Matrix")
				throw std::runtime_error("Unknown preset section");
			continue;
		}
		const auto equals = line.find('=');
		if (equals == std::string::npos || section.empty())
			throw std::runtime_error("Invalid preset syntax");
		const auto key = section + "." + Trim(line.substr(0, equals));
		if (!values.emplace(key, Trim(line.substr(equals + 1))).second)
			throw std::runtime_error("Duplicate preset field");
	}
	auto take = [&](const char* key)
	{
		const auto at = values.find(key);
		if (at == values.end())
			throw std::runtime_error("Missing preset field");
		const auto value = at->second;
		values.erase(at);
		return value;
	};
	const auto version = take("LutWorkbench.Version");
	if (version != "1" && version != "2" && version != "3" && version != "4" && version != "5" && version != "6" &&
	    version != "7")
		throw std::runtime_error("Unsupported preset version");
	const auto type = take("LutWorkbench.Type");
	Data result;
	if (type == "Look")
	{
		if (version != "1")
		{
			const auto enabled = take("Look.Enabled");
			if (enabled != "true" && enabled != "false")
				throw std::runtime_error("Invalid contrast enabled flag");
			result.ContrastEnabled = enabled == "true";
		}
		result.Contrast = ParseHundredths(take("Look.Contrast"), -25, 25);
		result.Pivot = ParseHundredths(take("Look.Pivot"), 20, 80);
		if (version == "3" || version == "4" || version == "5" || version == "6" || version == "7")
		{
			auto& adjustments = result.Adjustments;
			const char* balance[] { "Adjustments.Red", "Adjustments.Green", "Adjustments.Blue", "Adjustments.Warmth",
				                    "Adjustments.Tint" };
			for (int i = 0; i < 5; ++i)
				adjustments.Balance[i] = ParseHundredths(take(balance[i]), i < 3 ? 50 : -50, i < 3 ? 150 : 50);
			adjustments.Midtone = ParseHundredths(take("Adjustments.Midtone"), 50, 200);
			const char* levels[] { "Adjustments.InputBlack", "Adjustments.InputWhite", "Adjustments.OutputBlack",
				                   "Adjustments.OutputWhite" };
			for (int i = 0; i < 4; ++i)
				adjustments.Levels[i] = ParseHundredths(take(levels[i]), 0, 100);
			adjustments.Shadows = ParseHundredths(take("Adjustments.Shadows"), -50, 50);
			adjustments.Highlights = ParseHundredths(take("Adjustments.Highlights"), -50, 50);
			const auto flag = [&](const char* key)
			{
				const auto value = take(key);
				if (value != "true" && value != "false")
					throw std::runtime_error("Invalid adjustment enabled flag");
				return value == "true";
			};
			adjustments.BalanceEnabled = flag("Adjustments.BalanceEnabled");
			adjustments.GammaEnabled = flag("Adjustments.GammaEnabled");
			adjustments.LevelsEnabled = flag("Adjustments.LevelsEnabled");
			adjustments.ToneEnabled = flag("Adjustments.ToneEnabled");
			adjustments.CurvesEnabled = flag("Adjustments.CurvesEnabled");
			adjustments.ImportEnabled = flag("Adjustments.ImportEnabled");
			const auto samples = [&](const std::string& key)
			{
				const auto text = take(key.c_str());
				std::vector<double> values;
				if (text.empty())
					return values;
				std::istringstream input(text);
				input.imbue(std::locale::classic());
				std::string part;
				while (std::getline(input, part, ','))
				{
					std::istringstream number(part);
					number.imbue(std::locale::classic());
					double value;
					if (!(number >> value))
						throw std::runtime_error("Invalid curve sample");
					number >> std::ws;
					if (!number.eof())
						throw std::runtime_error("Invalid curve sample");
					values.push_back(value);
				}
				if (text.back() == ',')
					throw std::runtime_error("Missing curve sample");
				return values;
			};
			for (int i = 0; i < 4; ++i)
				adjustments.Curves[i] = samples("Adjustments.Curve" + std::to_string(i));
			for (int i = 0; i < 3; ++i)
				adjustments.Imported[i] = samples("Adjustments.Imported" + std::to_string(i));
		}
		if (version == "5" || version == "6" || version == "7")
		{
			const auto enabled = take("Matrix.Enabled");
			if (enabled != "true" && enabled != "false")
				throw std::runtime_error("Invalid saturation enabled flag");
			result.SaturationEnabled = enabled == "true";
			result.Saturation =
				ParseHundredths(take("Matrix.Saturation"), 0, version != "5" ? color::SATURATION_MAXIMUM : 120);
			if (version == "7")
			{
				const auto mono = take("Matrix.MonochromeEnabled");
				if (mono != "true" && mono != "false")
					throw std::runtime_error("Invalid channel mixer enabled flag");
				result.MonochromeEnabled = mono == "true";
				const char* keys[] { "Matrix.MixRed", "Matrix.MixGreen", "Matrix.MixBlue" };
				for (int i = 0; i < 3; ++i)
					result.Monochrome[i] = ParseHundredths(take(keys[i]), 0, 200);
			}
		}
	}
	else if (type == "Correction")
	{
		if (version != "1" && version != "2")
			throw std::runtime_error("Unsupported correction version");
		result.kind = Kind::Correction;
		const auto gamma = take("Correction.Gamma");
		if (gamma != "2.2" && gamma != "2.4")
			throw std::runtime_error("Unsupported HDR gamma");
		result.Gamma = gamma == "2.2" ? 22 : 24;
		const auto enabled = take("Correction.HDRGammaEnabled");
		if (enabled != "true" && enabled != "false")
			throw std::runtime_error("Invalid HDR correction flag");
		result.HdrFix = enabled == "true";
		if (version == "2")
		{
			const auto sdr = take("Correction.AlsoApplyInSDR");
			if (sdr != "true" && sdr != "false")
				throw std::runtime_error("Invalid SDR correction flag");
			result.ApplyInSdr = sdr == "true";
		}
	}
	else
		throw std::runtime_error("Unknown preset type");
	if (!values.empty())
		throw std::runtime_error("Unknown or mixed-role preset field");
	Validate(result);
	return result;
}

Data Read(const std::filesystem::path& path)
{
	File file(CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL,
	                      nullptr));
	if (file.handle == INVALID_HANDLE_VALUE)
		FileError("Cannot open preset");
	LARGE_INTEGER length {};
	if (!GetFileSizeEx(file.handle, &length))
		FileError("Cannot measure preset");
	if (length.QuadPart < 0 || length.QuadPart > 1048576)
		throw std::runtime_error("Preset exceeds 1 MiB");
	std::string bytes(static_cast<size_t>(length.QuadPart), '\0');
	DWORD count = 0;
	if (!ReadFile(file.handle, bytes.data(), static_cast<DWORD>(bytes.size()), &count, nullptr) ||
	    count != bytes.size())
		FileError("Cannot read complete preset");
	return Decode(bytes);
}

void Write(const std::filesystem::path& path, const Data& data, bool replace)
{
	WriteBytes(path, Encode(data), replace);
}

void WriteBytes(const std::filesystem::path& path, const std::string& bytes, bool replace)
{
	std::filesystem::path temporary;
	HANDLE handle = INVALID_HANDLE_VALUE;
	for (int attempt = 0; attempt < 32; ++attempt)
	{
		temporary = path;
		temporary += L".tmp-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64()) +
		             L"-" + std::to_wstring(attempt);
		handle = CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
		if (handle != INVALID_HANDLE_VALUE || GetLastError() != ERROR_FILE_EXISTS)
			break;
	}
	if (handle == INVALID_HANDLE_VALUE)
		FileError("Cannot create temporary preset");
	try
	{
		File file(handle);
		DWORD count = 0;
		if (!WriteFile(file.handle, bytes.data(), static_cast<DWORD>(bytes.size()), &count, nullptr) ||
		    count != bytes.size() || !FlushFileBuffers(file.handle))
			FileError("Cannot write complete preset");
		file.Close();
		// MoveFileEx without REPLACE_EXISTING cannot overwrite a file created since Save As.
		if (!MoveFileExW(temporary.c_str(), path.c_str(),
		                 MOVEFILE_WRITE_THROUGH | (replace ? MOVEFILE_REPLACE_EXISTING : 0)))
			FileError("Cannot publish preset; previous file was preserved");
	}
	catch (...)
	{
		DeleteFileW(temporary.c_str());
		throw;
	}
}
}
