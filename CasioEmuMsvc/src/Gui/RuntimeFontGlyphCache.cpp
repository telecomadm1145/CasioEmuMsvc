#include "RuntimeFontGlyphCache.h"
#include <imgui_internal.h>

RuntimeFontGlyphCache& RuntimeFontGlyphCache::Instance() {
	static RuntimeFontGlyphCache cache;
	return cache;
}

bool RuntimeFontGlyphCache::AddText(std::string_view utf8_text) {
	bool changed = false;
	const ImFont* current_font = nullptr;
	if (ImGui::GetCurrentContext() && !ImGui::GetIO().Fonts->Fonts.empty()) {
		current_font = ImGui::GetIO().Fonts->Fonts[0];
	}
	const char* cursor = utf8_text.data();
	const char* end = cursor + utf8_text.size();
	while (cursor < end && m_codepoints.size() < kMaxCachedGlyphs) {
		unsigned int codepoint = 0;
		const int length = ImTextCharFromUtf8(&codepoint, cursor, end);
		if (length <= 0) {
			break;
		}
		if (codepoint > 0x00FF && codepoint <= IM_UNICODE_CODEPOINT_MAX
			&& (!current_font || !current_font->FindGlyphNoFallback(static_cast<ImWchar>(codepoint)))
			&& m_codepoints.insert(codepoint).second) {
			m_text.append(cursor, static_cast<size_t>(length));
			changed = true;
		}
		cursor += length;
	}
	return changed;
}
