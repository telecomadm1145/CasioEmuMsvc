#pragma once
#include <string>
#include <vector>

// Stub Localization class for Izanagi
class Localization {
public:
    void Load() {}
    const char* GetCStr(const char* key) const { return key; }
    std::string Get(const char* key) const { return key; }
    std::string operator[](const char* key) const { return key; }
};

extern Localization g_local;

inline const char* operator""_lc(const char* str, size_t) {
    return str; // Just return the key
}

inline std::string operator""_l(const char* str, size_t) {
    return str;
}
