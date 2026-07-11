#pragma once

// CMake and release CI override this header from OnlineBuildConfig.h.in.
#define CASIOEMU_ONLINE_CLIENT_KEY_ID ""
#define CASIOEMU_ONLINE_CLIENT_KEY_B64 ""
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
