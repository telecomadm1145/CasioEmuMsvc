#include "AndroidSystemFont.h"

#ifdef __ANDROID__

#include <android/api-level.h>
#include <android/font.h>
#include <android/font_matcher.h>
#include <dlfcn.h>
#include <filesystem>
#include "tinyxml2/tinyxml2.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <string>

namespace {

struct LanguageSpec {
	std::string_view tag;
	std::u16string_view sample;
};

LanguageSpec GetLanguageSpec(std::string_view preference) {
	static constexpr char16_t kChineseSample[] = u"中文";
	static constexpr char16_t kJapaneseSample[] = u"日本語";
	static constexpr char16_t kKoreanSample[] = u"한국어";

	if (preference == "JP") {
		return {"ja-JP", kJapaneseSample};
	}
	if (preference == "KR") {
		return {"ko-KR", kKoreanSample};
	}
	return {"zh-Hans", kChineseSample};
}

template <typename T>
T LoadSymbol(void* library, const char* name) {
	return reinterpret_cast<T>(dlsym(library, name));
}

std::optional<AndroidSystemFont> MatchWithPublicApi(const LanguageSpec& language) {
	using CreateMatcher = AFontMatcher* (*)();
	using DestroyMatcher = void (*)(AFontMatcher*);
	using SetLocales = void (*)(AFontMatcher*, const char*);
	using SetStyle = void (*)(AFontMatcher*, uint16_t, bool);
	using MatchFont = AFont* (*)(const AFontMatcher*, const char*, const uint16_t*, uint32_t, uint32_t*);
	using GetFontPath = const char* (*)(const AFont*);
	using GetCollectionIndex = size_t (*)(const AFont*);
	using CloseFont = void (*)(AFont*);

	void* library = dlopen("libandroid.so", RTLD_NOW | RTLD_LOCAL);
	if (!library) {
		return std::nullopt;
	}

	auto create = LoadSymbol<CreateMatcher>(library, "AFontMatcher_create");
	auto destroy = LoadSymbol<DestroyMatcher>(library, "AFontMatcher_destroy");
	auto set_locales = LoadSymbol<SetLocales>(library, "AFontMatcher_setLocales");
	auto set_style = LoadSymbol<SetStyle>(library, "AFontMatcher_setStyle");
	auto match = LoadSymbol<MatchFont>(library, "AFontMatcher_match");
	auto get_path = LoadSymbol<GetFontPath>(library, "AFont_getFontFilePath");
	auto get_index = LoadSymbol<GetCollectionIndex>(library, "AFont_getCollectionIndex");
	auto close_font = LoadSymbol<CloseFont>(library, "AFont_close");

	std::optional<AndroidSystemFont> result;
	if (create && destroy && set_locales && set_style && match && get_path && get_index && close_font) {
		AFontMatcher* matcher = create();
		if (matcher) {
			const std::string language_tag(language.tag);
			set_locales(matcher, language_tag.c_str());
			set_style(matcher, 400, false);
			uint32_t run_length = 0;
			AFont* font = match(
				matcher,
				"sans-serif",
				reinterpret_cast<const uint16_t*>(language.sample.data()),
				static_cast<uint32_t>(language.sample.size()),
				&run_length);
			if (font) {
				const char* path = get_path(font);
				if (path && run_length > 0 && std::filesystem::is_regular_file(path)) {
					result = AndroidSystemFont{path, static_cast<int>(get_index(font))};
				}
				close_font(font);
			}
			destroy(matcher);
		}
	}

	dlclose(library);
	return result;
}

std::string Trim(std::string text) {
	auto is_space = [](unsigned char character) { return std::isspace(character) != 0; };
	text.erase(text.begin(), std::find_if_not(text.begin(), text.end(), is_space));
	text.erase(std::find_if_not(text.rbegin(), text.rend(), is_space).base(), text.end());
	return text;
}

int LanguageScore(const char* configured_languages, std::string_view language_tag) {
	if (!configured_languages) {
		return 0;
	}
	const std::string languages(configured_languages);
	if (languages.find(language_tag) != std::string::npos) {
		return 4;
	}
	const auto separator = language_tag.find_first_of("-_");
	const std::string_view primary = language_tag.substr(0, separator);
	return !primary.empty() && languages.find(primary) != std::string::npos ? 2 : 0;
}

std::optional<AndroidSystemFont> MatchFromLegacyConfig(
	std::string_view language_tag,
	bool allow_default_sans_serif) {
	for (const char* config_path : {"/system/etc/fonts.xml", "/system/etc/system_fonts.xml"}) {
		tinyxml2::XMLDocument document;
		if (document.LoadFile(config_path) != tinyxml2::XML_SUCCESS) {
			continue;
		}

		std::optional<AndroidSystemFont> best;
		int best_score = 0;
		auto* root = document.RootElement();
		for (auto* family = root ? root->FirstChildElement("family") : nullptr;
			 family;
			 family = family->NextSiblingElement("family")) {
			const int language_score = LanguageScore(family->Attribute("lang"), language_tag);
			const char* family_name = family->Attribute("name");
			const bool is_default_sans = family_name && std::string_view(family_name) == "sans-serif";
			if (language_score == 0 && !(allow_default_sans_serif && is_default_sans)) {
				continue;
			}

			for (auto* font = family->FirstChildElement("font"); font; font = font->NextSiblingElement("font")) {
				const char* value = font->GetText();
				if (!value) {
					continue;
				}
				const char* style = font->Attribute("style");
				int weight = 400;
				font->QueryIntAttribute("weight", &weight);
				const int style_score = (weight == 400 ? 2 : 0) + (!style || std::string_view(style) == "normal" ? 1 : 0);
				const int score = language_score * 10 + (is_default_sans ? 5 : 0) + style_score;
				if (score <= best_score) {
					continue;
				}

				std::filesystem::path path(Trim(value));
				if (!path.is_absolute()) {
					path = std::filesystem::path("/system/fonts") / path;
				}
				if (!std::filesystem::is_regular_file(path)) {
					continue;
				}

				int index = 0;
				font->QueryIntAttribute("index", &index);
				best = AndroidSystemFont{path.string(), index};
				best_score = score;
			}
		}
		if (best) {
			return best;
		}
	}
	return std::nullopt;
}

std::optional<AndroidSystemFont> FindFont(
	const LanguageSpec& language,
	bool allow_default_sans_serif) {
	if (android_get_device_api_level() >= 29) {
		if (auto font = MatchWithPublicApi(language)) {
			return font;
		}
	}
	return MatchFromLegacyConfig(language.tag, allow_default_sans_serif);
}

} // namespace

std::optional<AndroidSystemFont> FindAndroidSystemCJKFont(std::string_view preference) {
	return FindFont(GetLanguageSpec(preference), false);
}

std::optional<AndroidSystemFont> FindAndroidSystemFont(
	std::string_view language_tag,
	std::u16string_view sample) {
	return FindFont({language_tag, sample}, true);
}

#endif
