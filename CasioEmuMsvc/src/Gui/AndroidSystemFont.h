#pragma once

#ifdef __ANDROID__

#include <optional>
#include <string>
#include <string_view>

struct AndroidSystemFont {
	std::string path;
	int collection_index = 0;
};

// Uses Android's public font matcher on API 29+, and the device's own
// fonts.xml configuration on API 24-28.
std::optional<AndroidSystemFont> FindAndroidSystemFont(
	std::string_view language_tag,
	std::u16string_view sample);

std::optional<AndroidSystemFont> FindAndroidSystemCJKFont(std::string_view preference);

#endif
