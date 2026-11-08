#pragma once
#include <windows.h>
#include <string>
namespace hdr
{
struct State
{
	bool available = false, on = false, modern = false;
	LUID adapter {};
	UINT32 target = 0;
	std::wstring source, reason;
};

State ReadHdrState(const std::wstring& source);
void SetHdrEnabled(const State& expected, bool on);
}
