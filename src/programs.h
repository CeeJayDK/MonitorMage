#pragma once
#include <windows.h>
#include <filesystem>
#include <string>
#include <vector>
namespace programs {
struct Program {std::filesystem::path path;std::wstring name;SIZE_T memory=0;bool visible=false,memoryKnown=false;};
std::wstring name(const std::filesystem::path&);
bool under(const std::filesystem::path&,const std::filesystem::path&);
void sort(std::vector<Program>&,bool byMemory);
std::vector<Program> running(bool includeBackground=false,bool byMemory=true);
}
