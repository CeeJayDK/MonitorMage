#pragma once
#include "adjustments.h"
#include <filesystem>
#include <string>
namespace lut1d
{
using Channels = std::array<std::vector<double>, 3>;
struct Loaded
{
	Channels channels;
	std::wstring description;
};

Channels DecodeCube(const std::string& text);
Channels DecodeSpi(const std::string& text);
Channels DecodeCal(const std::string& text);
Channels DecodeCsv(const std::string& text);
Loaded Read(const std::filesystem::path& path);
void Write(const std::filesystem::path& path, const Channels& channels);
}
