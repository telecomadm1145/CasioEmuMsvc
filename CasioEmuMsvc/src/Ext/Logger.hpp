#pragma once
#include "Config.hpp"

#include <cstdio>
#include <string>

namespace casioemu::logger {
	// Used only by old codebase, new code should print directly.
	//void Info(const char* format, auto... args) {
	//	printf(format, args...);
	//}
	constexpr auto Info = printf;
} // namespace casioemu::logger
