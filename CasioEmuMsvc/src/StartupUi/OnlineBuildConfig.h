#pragma once

#include <cstddef>

// CMake and release CI override this header from OnlineBuildConfig.h.in.
#define CASIOEMU_ONLINE_BUILD_KEY_ID ""
inline const volatile unsigned char CASIOEMU_ONLINE_BUILD_KEY_MASKED[] = { 0 };
inline const volatile unsigned char CASIOEMU_ONLINE_BUILD_KEY_MASK[] = { 0 };
inline constexpr std::size_t CASIOEMU_ONLINE_BUILD_KEY_LENGTH = 0;
#define CASIOEMU_ONLINE_SERVER_KEY_ID ""
#define CASIOEMU_ONLINE_SERVER_PUBLIC_KEY_B64 ""
#define CASIOEMU_ONLINE_BUILD_TIMESTAMP "19700101T000000Z"
#if defined(_WIN32)
#define CASIOEMU_ONLINE_BUILD_OS "Windows"
#elif defined(__APPLE__)
#define CASIOEMU_ONLINE_BUILD_OS "macOS"
#elif defined(__ANDROID__)
#define CASIOEMU_ONLINE_BUILD_OS "Android"
#else
#define CASIOEMU_ONLINE_BUILD_OS "Linux"
#endif
#if defined(__aarch64__) || defined(_M_ARM64)
#define CASIOEMU_ONLINE_BUILD_ARCH "arm64"
#elif defined(__arm__) || defined(_M_ARM)
#define CASIOEMU_ONLINE_BUILD_ARCH "arm"
#elif defined(_WIN64) || defined(__x86_64__)
#define CASIOEMU_ONLINE_BUILD_ARCH "x86_64"
#else
#define CASIOEMU_ONLINE_BUILD_ARCH "x86"
#endif
#define CASIOEMU_ONLINE_API_PROTOCOL 2
