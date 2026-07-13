#pragma once
#include "Localization.h"
#include <filesystem>
#include <imgui.h>
#include <imgui_impl_sdlrenderer2.h>
#include <iostream>
#include <string>
#include <vector>
#ifdef __ANDROID__
#include "AndroidSystemFont.h"
#include "RuntimeFontGlyphCache.h"
#include <android/log.h>
#endif

#ifndef __ANDROID__
inline const ImWchar* GetCJKRanges() {
	static const ImWchar ranges[] = {
		0x0020, 0x00FF,
		0x2000, 0x206F,
		0x3000, 0x30FF,
		0x31F0, 0x31FF,
		0xFF00, 0xFFEF,
		0x4E00, 0x9FAF,
		0,
	};
	return ranges;
}
#endif

// -----------------------------------------------------------------------------
// 辅助：在路径列表中寻找第一个存在的字体文件
// -----------------------------------------------------------------------------
inline std::string FindBestFont(const std::vector<std::string>& candidates) {
	for (const auto& path : candidates) {
		if (std::filesystem::exists(path)) {
			return path;
		}
	}
	return "";
}

// -----------------------------------------------------------------------------
// 获取等宽字体 (Monospace Font) - 核心修正
// -----------------------------------------------------------------------------
inline std::string GetMonospaceFontPath() {
	std::vector<std::string> candidates;

#ifdef _WIN32
	// Windows 等宽字体回退链
	candidates = {
		// 1. Cascadia (Win10/11 Terminal 默认字体，极其适合代码)
		"C:\\Windows\\Fonts\\CascadiaMono.ttf",
		"C:\\Windows\\Fonts\\CascadiaCode.ttf",
		// 2. Consolas (经典的编程字体)
		"C:\\Windows\\Fonts\\Consola.ttf",
		// 3. Courier New (最后的兜底)
		"C:\\Windows\\Fonts\\cour.ttf"};
#elif defined(__ANDROID__)
	candidates = {
		"/system/fonts/DroidSansMono.ttf",
		"/system/fonts/NotoSansMono-Regular.ttf"};
#elif defined(__APPLE__)
	candidates = {
		"/System/Library/Fonts/Monaco.ttf",
		"/System/Library/Fonts/Menlo.ttc",
		"/System/Library/Fonts/SFNSMono.ttf",
		"/Library/Fonts/Courier New.ttf"};
#else // Linux / Unix
	// Linux 等宽字体非常多，这里列出主流发行版的默认项
	candidates = {
		// Ubuntu / Debian
		"/usr/share/fonts/truetype/ubuntu/UbuntuMono-R.ttf",
		"/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf",
		"/usr/share/fonts/liberation/LiberationMono-Regular.ttf",
		// Fedora / Arch / Others
		"/usr/share/fonts/noto/NotoMono-Regular.ttf",
		"/usr/share/fonts/TTF/DejaVuSansMono.ttf",
		"/usr/share/fonts/gnu-free/FreeMono.ttf"};
#endif

	return FindBestFont(candidates);
}

// -----------------------------------------------------------------------------
// 获取 CJK 字体
// 注意：即使是代码环境，CJK 字体通常也可以使用非严格等宽的黑体（Gothic），
// 因为 ImGui 会处理字符宽度，但最好还是选字形工整的。
// -----------------------------------------------------------------------------
inline std::string GetCJKFontPath(int* font_no = nullptr) {
	if (font_no) {
		*font_no = 0;
	}
	auto preference = "Localization.CJKPreference"_l;
	std::vector<std::string> candidates = {
		"temp/generated/fonts/CasioEmuGuiCJKSubset.otf",
		"./temp/generated/fonts/CasioEmuGuiCJKSubset.otf",
		"../temp/generated/fonts/CasioEmuGuiCJKSubset.otf"};

#ifdef _WIN32
	if (preference == "JP") {
		candidates.insert(candidates.end(), {
			"C:\\Windows\\Fonts\\msgothic.ttc", // MS Gothic 是日文等宽的首选
			"C:\\Windows\\Fonts\\YuGothM.ttc",
			"C:\\Windows\\Fonts\\meiryo.ttc"});
	}
	else if (preference == "KR") {
		candidates.insert(candidates.end(), {
			"C:\\Windows\\Fonts\\malgun.ttf",  // Malgun Gothic (맑은 고딕)
			"C:\\Windows\\Fonts\\gulim.ttc",   // Gulim (굴림)
			"C:\\Windows\\Fonts\\batang.ttc"   // Batang (바탕)
		});
	}
	else {
		candidates.insert(candidates.end(), {
			"C:\\Windows\\Fonts\\msyh.ttc",	  // 雅黑
			"C:\\Windows\\Fonts\\simhei.ttf", // 黑体 (较粗，但在某些低分屏上可读性好)
			"C:\\Windows\\Fonts\\simsun.ttc"  // 宋体 (最传统)
		});
	}
#elif defined(__ANDROID__)
	if (auto system_font = FindAndroidSystemCJKFont(preference)) {
		if (font_no) {
			*font_no = system_font->collection_index;
		}
		return system_font->path;
	}
	candidates.insert(candidates.end(), {
		"/system/fonts/NotoSansCJK-Regular.ttc",
		"/system/fonts/DroidSansFallback.ttf"});
#elif defined(__APPLE__)
	if (preference == "JP") {
		candidates.insert(candidates.end(), {
			"/System/Library/Fonts/ヒラギノ角ゴシック W4.ttc",
			"/System/Library/Fonts/Supplemental/Osaka.ttf"
		});
	}
	else if (preference == "KR") {
		candidates.insert(candidates.end(), {
			"/System/Library/Fonts/AppleGothic.ttf",
			"/System/Library/Fonts/Supplemental/AppleGothic.ttf"
		});
	}
	else {
		candidates.insert(candidates.end(), {
			"/System/Library/Fonts/PingFang.ttc",
			"/System/Library/Fonts/STHeiti Light.ttc",
			"/System/Library/Fonts/Supplemental/Arial Unicode.ttf"
		});
	}
#else // Linux
	// 尝试寻找 CJK 的 Mono 版本 (如果有)，否则使用 Regular
	candidates.insert(candidates.end(), {
		"/usr/share/fonts/noto-cjk/NotoSansCJK-Mono.ttc", // 最佳：Noto 的等宽版本
		"/usr/share/fonts/noto-cjk/NotoSansCJK-Regular.ttc",
		"/usr/share/fonts/truetype/wqy/wqy-microhei.ttc",
		"/usr/share/fonts/truetype/wqy/wqy-zenhei.ttc",
		"/usr/share/fonts/google-noto-cjk/NotoSansCJK-Regular.ttc",
		"/usr/share/fonts/droid/DroidSansFallbackFull.ttf"});
#endif

	return FindBestFont(candidates);
}

// Theme/font/scaling globals are now managed by ThemeManager.
// See ThemeManager.h for the unified API.

inline void RebuildFont(float scale = 0.0f) {
	auto& io = ImGui::GetIO();
	ImFontConfig config = ImFontConfig();

	// 对于代码显示，PixelSnapH 非常重要，能防止字母边缘模糊
	config.PixelSnapH = true;

	io.Fonts->Clear();

#ifdef __ANDROID__
	constexpr float defaultscale = 3.0f;
#else
	constexpr float defaultscale = 1.0f;
#endif
	if (scale == 0) {
		scale = defaultscale;
	}

	// 1. 加载等宽基础字体 (Monospace Base)
	// 这是改动最大的地方，确保英文和代码符号绝对等宽
	std::string mono_font_path = GetMonospaceFontPath();
#ifdef __ANDROID__
	ImWchar extended_script_sample = 0;
	const char* extended_script_name = nullptr;
#endif

	if (!mono_font_path.empty()) {
		io.Fonts->AddFontFromFileTTF(mono_font_path.c_str(), 15 * scale, &config, io.Fonts->GetGlyphRangesDefault());
		printf("[Ui][Info] Loaded Monospace Font: %s\n", mono_font_path.c_str());
	}
	else {
		printf("[Ui][Warn] No monospace font found! Falling back to ImGui Default (ProggyClean).\n");
		// ImGui 自带的默认字体 (ProggyClean) 也是等宽的，是一个安全的最后防线
		io.Fonts->AddFontDefault(&config);
	}

	// Merge additional glyph ranges based on localization settings.
	const bool load_vietnamese = "Localization.LoadVietnamese"_l == "1" || "Localization.LoadVietnamese"_l == "true";
	const bool load_cyrillic = "Localization.LoadCyrillic"_l == "1" || "Localization.LoadCyrillic"_l == "true";
	const bool load_greek = "Localization.LoadGreek"_l == "1" || "Localization.LoadGreek"_l == "true";
	const bool load_thai = "Localization.LoadThai"_l == "1" || "Localization.LoadThai"_l == "true";
#ifdef __ANDROID__
	auto merge_system_font = [&](bool enabled, std::string_view language_tag,
		std::u16string_view sample, const ImWchar* ranges, ImWchar validation_sample,
		const char* script_name) {
		if (!enabled) {
			return;
		}
		extended_script_sample = validation_sample;
		extended_script_name = script_name;
		auto system_font = FindAndroidSystemFont(language_tag, sample);
		if (!system_font) {
			__android_log_print(ANDROID_LOG_ERROR, "CasioEmuFont",
				"No Android system font found for %s", script_name);
			return;
		}
		ImFontConfig merge_config = config;
		merge_config.MergeMode = true;
		merge_config.FontNo = system_font->collection_index;
		ImFont* font = io.Fonts->AddFontFromFileTTF(
			system_font->path.c_str(), 15 * scale, &merge_config, ranges);
		__android_log_print(font ? ANDROID_LOG_INFO : ANDROID_LOG_ERROR, "CasioEmuFont",
			"%s Android system font for %s: %s (collection index %d)",
			font ? "Selected" : "Failed to load", script_name,
			system_font->path.c_str(), system_font->collection_index);
	};
	merge_system_font(load_vietnamese, "vi-VN", u"Tiếng Việt",
		io.Fonts->GetGlyphRangesVietnamese(), 0x1EBF, "Vietnamese");
	merge_system_font(load_cyrillic, "ru-RU", u"Русский",
		io.Fonts->GetGlyphRangesCyrillic(), 0x042F, "Cyrillic");
	merge_system_font(load_greek, "el-GR", u"Ελληνικά",
		io.Fonts->GetGlyphRangesGreek(), 0x03A9, "Greek");
	merge_system_font(load_thai, "th-TH", u"ไทย",
		io.Fonts->GetGlyphRangesThai(), 0x0E44, "Thai");
#else
	if (!mono_font_path.empty()) {
		config.MergeMode = true;
		if (load_vietnamese) {
			io.Fonts->AddFontFromFileTTF(mono_font_path.c_str(), 15 * scale, &config, io.Fonts->GetGlyphRangesVietnamese());
		}
		if (load_cyrillic) {
			io.Fonts->AddFontFromFileTTF(mono_font_path.c_str(), 15 * scale, &config, io.Fonts->GetGlyphRangesCyrillic());
		}
		if (load_greek) {
			io.Fonts->AddFontFromFileTTF(mono_font_path.c_str(), 15 * scale, &config, io.Fonts->GetGlyphRangesGreek());
		}
		if (load_thai) {
			io.Fonts->AddFontFromFileTTF(mono_font_path.c_str(), 15 * scale, &config, io.Fonts->GetGlyphRangesThai());
		}
		config.MergeMode = false;
	}
#endif

	// 2. 合并 CJK 字体
	const std::string enable_cjk = "Localization.EnableCJK"_l;
	const bool locale_needs_cjk = enable_cjk == "1" || enable_cjk == "true";
#ifdef __ANDROID__
	const std::string runtime_glyph_text = RuntimeFontGlyphCache::Instance().GetText();
	const bool needs_cjk_font = locale_needs_cjk || !runtime_glyph_text.empty();
	if (needs_cjk_font) {
		const std::string localized_text = g_local.CollectFontGlyphText();
		ImFontGlyphRangesBuilder glyph_builder;
		glyph_builder.AddText(localized_text.c_str());
		glyph_builder.AddText(runtime_glyph_text.c_str());
		ImVector<ImWchar> localized_ranges;
		glyph_builder.BuildRanges(&localized_ranges);

		int cjk_font_no = 0;
		std::string cjk_font_path = GetCJKFontPath(&cjk_font_no);

		if (!cjk_font_path.empty()) {
			ImFontConfig merge_config = config;
			merge_config.MergeMode = true;
			merge_config.FontNo = cjk_font_no;

			// 稍微放大一点 CJK 字体，通常中文比同号的英文显得小
			ImFont* cjk_font = io.Fonts->AddFontFromFileTTF(
				cjk_font_path.c_str(), 16 * scale, &merge_config, localized_ranges.Data);
			if (cjk_font) {
				__android_log_print(ANDROID_LOG_INFO, "CasioEmuFont",
					"Selected CJK system font: %s (collection index %d, localized ranges %d)",
					cjk_font_path.c_str(), cjk_font_no, localized_ranges.Size / 2);
			}
			else {
				__android_log_print(ANDROID_LOG_ERROR, "CasioEmuFont",
					"Failed to load CJK system font: %s (collection index %d)",
					cjk_font_path.c_str(), cjk_font_no);
			}
		}
		else {
			__android_log_print(ANDROID_LOG_ERROR, "CasioEmuFont", "No CJK system font found");
		}
	}
	const bool font_build_succeeded = io.Fonts->Build();
	if (extended_script_sample != 0) {
		const bool sample_present = font_build_succeeded && !io.Fonts->Fonts.empty()
			&& io.Fonts->Fonts[0]->FindGlyphNoFallback(extended_script_sample) != nullptr;
		__android_log_print(
			font_build_succeeded && sample_present ? ANDROID_LOG_INFO : ANDROID_LOG_ERROR,
			"CasioEmuFont",
			"Atlas build=%s, glyphs=%d, %s sample U+%04X present=%s",
			font_build_succeeded ? "true" : "false",
			io.Fonts->Fonts.empty() ? 0 : io.Fonts->Fonts[0]->Glyphs.Size,
			extended_script_name,
			static_cast<unsigned int>(extended_script_sample),
			sample_present ? "true" : "false");
	}
	if (locale_needs_cjk) {
		ImWchar sample = 0x4E2D;
		const std::string preference = "Localization.CJKPreference"_l;
		if (preference == "JP") {
			sample = 0x65E5;
		}
		else if (preference == "KR") {
			sample = 0xD55C;
		}
		const bool sample_present = font_build_succeeded && !io.Fonts->Fonts.empty()
			&& io.Fonts->Fonts[0]->FindGlyphNoFallback(sample) != nullptr;
		__android_log_print(
			font_build_succeeded && sample_present ? ANDROID_LOG_INFO : ANDROID_LOG_ERROR,
			"CasioEmuFont",
			"Atlas build=%s, glyphs=%d, sample U+%04X present=%s",
			font_build_succeeded ? "true" : "false",
			io.Fonts->Fonts.empty() ? 0 : io.Fonts->Fonts[0]->Glyphs.Size,
			static_cast<unsigned int>(sample),
			sample_present ? "true" : "false");
	}
#else
	if (locale_needs_cjk) {
		const std::string cjk_font_path = GetCJKFontPath();
		if (!cjk_font_path.empty()) {
			ImFontConfig merge_config = config;
			merge_config.MergeMode = true;
			const ImWchar* ranges = "Localization.CJKPreference"_l == "KR"
				? io.Fonts->GetGlyphRangesKorean()
				: GetCJKRanges();
			io.Fonts->AddFontFromFileTTF(cjk_font_path.c_str(), 16 * scale, &merge_config, ranges);
		}
	}
	io.Fonts->Build();
#endif
}
