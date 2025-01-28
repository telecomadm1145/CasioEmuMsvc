#pragma once
#include "Containers/ConcurrentObject.h"
#include <cstdint>
#include <cstdio>
#include <exception>
#include <iostream>
#include <map>
#include <mutex>
#include <string>
#include <vector>

#ifdef __GNUG__
#define FUNCTION_NAME __PRETTY_FUNCTION__
#else
#define FUNCTION_NAME __func__
#endif

#ifdef __clang__
#define __debugbreak __builtin_trap
#endif

#define ENABLE_CRASH_CHECK
#ifdef ENABLE_CRASH_CHECK
#define PANIC(...)           \
	{                        \
		printf(__VA_ARGS__); \
		__debugbreak();      \
	}
#else
#define PANIC(...) 0;
#endif

#define LOCK(x) \
	std::lock_guard<std::mutex> lock_##x{x};

// Enable debug feature

#define DBG

// #define SINGLE_WINDOW
#if !defined(SINGLE_WINDOW) && defined(__ANDROID__)
#define SINGLE_WINDOW
#endif
