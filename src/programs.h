#pragma once
#include <windows.h>
#include <filesystem>
#include <string>
#include <vector>
namespace programs
{
struct Program
{
	std::filesystem::path path;
	std::wstring name;
	SIZE_T memory = 0;
	bool visible = false, memory_known = false;
};

std::wstring DisplayName(const std::filesystem::path&);
bool IsUnderFolder(const std::filesystem::path&, const std::filesystem::path&);
void SortPrograms(std::vector<Program>&, bool by_memory);
std::vector<Program> RunningPrograms(bool include_background = false, bool by_memory = true);
}
