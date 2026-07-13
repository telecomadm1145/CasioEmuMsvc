#pragma once

#include <imgui.h>
#include <string>
#include <string_view>
#include <unordered_set>

class RuntimeFontGlyphCache {
public:
	static RuntimeFontGlyphCache& Instance();

	bool AddText(std::string_view utf8_text);
	const std::string& GetText() const { return m_text; }

private:
	static constexpr size_t kMaxCachedGlyphs = 4096;

	std::string m_text;
	std::unordered_set<unsigned int> m_codepoints;
};
