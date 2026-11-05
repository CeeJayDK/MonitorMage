#pragma once
#include "adjustments.h"
#include <filesystem>
#include <string>
namespace lut1d {
using Channels=std::array<std::vector<double>,3>;
struct Loaded {Channels channels;std::wstring description;};
Channels decode(const std::string& text);
Channels decodeSpi(const std::string& text);
Channels decodeCal(const std::string& text);
Channels decodeCsv(const std::string& text);
Loaded read(const std::filesystem::path& path);
void write(const std::filesystem::path& path,const Channels& channels);
}
