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

#ifndef __debugbreak
    #if defined(_MSC_VER)
    #elif defined(__GNUC__) || defined(__clang__)
        #define __debugbreak __builtin_trap
    #else
        #define __debugbreak() ((void)0)
    #endif
#endif

#define NOOP() ((void)0)

#define ENABLE_CRASH_CHECK
#ifdef ENABLE_CRASH_CHECK
#ifndef PANIC
#define PANIC(...)           \
	{                        \
		printf(__VA_ARGS__); \
		throw 0;      \
	}
#endif
#else
#ifndef PANIC
#define PANIC(...) 0;
#endif
#endif

#define LOCK(x) \
	std::lock_guard<std::mutex> lock_##x{x};

// Enable debug feature

#define DBG

#ifdef min
#undef min
#endif 
#ifdef max
#undef max
#endif 

// #define SINGLE_WINDOW
#if !defined(SINGLE_WINDOW) && defined(__ANDROID__)
#define SINGLE_WINDOW
#endif

#if defined(_MSC_VER) || (defined(__clang__) && defined(_WIN32))
#define DLLEXPORT __declspec(dllexport)
#define DLLIMPORT __declspec(dllimport)
#elif defined(__clang__) || defined(__GNUC__)
#define DLLEXPORT __attribute__((visibility("default")))
#define DLLIMPORT
#else
#define DLLEXPORT
#define DLLIMPORT
#endif

#define PROP(x)                                  \
public:                                          \
	virtual decltype(x) Get##x##() { return x; } \
	virtual void Set##x##(decltype(x) a) { x = a; }

#define PROPABS(y, x)         \
public:                       \
	virtual y Get##x##() = 0; \
	virtual void Set##x##(y a) = 0;

#define EMULATOR_VERSION ""
#define DISCORD_APP_ID ""

#define TEST_BUILD