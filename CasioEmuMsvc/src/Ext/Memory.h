#pragma once
#include <cstdint>
#include <string>
#include <algorithm>
#include <cstddef>

using byte = unsigned char;

inline byte GetByte(const char* pattern) {
	if (*pattern == '?') {
		return 0;
	}

	byte high = (byte)(pattern[0] >= '0' && pattern[0] <= '9' ? pattern[0] - '0' : pattern[0] - 'A' + 10);
	byte low = (byte)(pattern[1] >= '0' && pattern[1] <= '9' ? pattern[1] - '0' : pattern[1] - 'A' + 10);

	return (byte)((high << 4) | low);
}

inline void* FindSignature(const byte* start, size_t size, const std::string& signature) {
	std::string upperSignature = signature;
	std::transform(upperSignature.begin(), upperSignature.end(), upperSignature.begin(), ::toupper);
	const char* pattern = upperSignature.c_str();
	const char* oldPat = pattern;
	const byte* end = start + size;
	void* firstMatch = nullptr;

	byte patByte = GetByte(pattern);

	for (const byte* pCur = start; pCur < end; ++pCur) {
		if (*pattern == 0) {
			return firstMatch;
		}

		while (*pattern == ' ' || *pattern == '\n') {
			++pattern;
		}

		if (*pattern == 0) {
			return firstMatch;
		}

		if (oldPat != pattern) {
			oldPat = pattern;
			if (*pattern != '?') {
				patByte = GetByte(pattern);
			}
		}

		if (*pattern == '?' || *pCur == patByte) {
			if (firstMatch == nullptr) {
				firstMatch = const_cast<byte*>(pCur);
			}

			if (pattern[1] == 0 || pattern[2] == 0) {
				return firstMatch;
			}

			pattern += 2;
		}
		else {
			pattern = upperSignature.c_str();
			firstMatch = nullptr;
		}
	}

	return nullptr;
}