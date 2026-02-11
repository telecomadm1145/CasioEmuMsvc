#pragma once
#include "Localization.h"
#include <filesystem>
#include <imgui.h>
#include <imgui_impl_sdlrenderer2.h>
#include <iostream>
#include <string>
#include <vector>

// -----------------------------------------------------------------------------
// 辅助：通用范围定义
// -----------------------------------------------------------------------------
inline const ImWchar* GetCJKRanges() {
	static const ImWchar ranges[] = {
		0x0020,
		0x00FF, // Basic Latin + Latin Supplement
		0x2000,
		0x206F, // General Punctuation
		0x3000,
		0x30FF, // CJK Symbols and Punctuations, Hiragana, Katakana
		0x31F0,
		0x31FF, // Katakana Phonetic Extensions
		0xFF00,
		0xFFEF, // Half-width characters
		0x4e00,
		0x9FAF, // CJK Ideograms
		0,
	};
	return &ranges[0];
}

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
inline std::string GetCJKFontPath() {
	auto preference = "Localization.CJKPreference"_l;
	std::vector<std::string> candidates;

#ifdef _WIN32
	if (preference == "JP") {
		candidates = {
			"C:\\Windows\\Fonts\\msgothic.ttc", // MS Gothic 是日文等宽的首选
			"C:\\Windows\\Fonts\\YuGothM.ttc",
			"C:\\Windows\\Fonts\\meiryo.ttc"};
	}
	else {
		candidates = {
			"C:\\Windows\\Fonts\\msyh.ttc",	  // 雅黑
			"C:\\Windows\\Fonts\\simhei.ttf", // 黑体 (较粗，但在某些低分屏上可读性好)
			"C:\\Windows\\Fonts\\simsun.ttc"  // 宋体 (最传统)
		};
	}
#elif defined(__ANDROID__)
	candidates = {
		"/system/fonts/NotoSansCJK-Regular.ttc",
		"/system/fonts/DroidSansFallback.ttf"};
#else // Linux
	// 尝试寻找 CJK 的 Mono 版本 (如果有)，否则使用 Regular
	candidates = {
		"/usr/share/fonts/noto-cjk/NotoSansCJK-Mono.ttc", // 最佳：Noto 的等宽版本
		"/usr/share/fonts/noto-cjk/NotoSansCJK-Regular.ttc",
		"/usr/share/fonts/truetype/wqy/wqy-microhei.ttc",
		"/usr/share/fonts/truetype/wqy/wqy-zenhei.ttc",
		"/usr/share/fonts/google-noto-cjk/NotoSansCJK-Regular.ttc",
		"/usr/share/fonts/droid/DroidSansFallbackFull.ttf"};
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
	bool base_loaded = false;

	if (!mono_font_path.empty()) {
		io.Fonts->AddFontFromFileTTF(mono_font_path.c_str(), 15 * scale, &config, io.Fonts->GetGlyphRangesDefault());
		base_loaded = true;
		printf("[Ui][Info] Loaded Monospace Font: %s\n", mono_font_path.c_str());
	}
	else {
		printf("[Ui][Warn] No monospace font found! Falling back to ImGui Default (ProggyClean).\n");
		// ImGui 自带的默认字体 (ProggyClean) 也是等宽的，是一个安全的最后防线
		io.Fonts->AddFontDefault(&config);
		base_loaded = true;
	}

	// 2. 合并 CJK 字体
	auto enable_cjk = "Localization.EnableCJK"_l;
	if (enable_cjk == "1" || enable_cjk == "true") {
		std::string cjk_font_path = GetCJKFontPath();

		if (!cjk_font_path.empty()) {
			config.MergeMode = true;

			// 稍微放大一点 CJK 字体，通常中文比同号的英文显得小
			// 保持 16 vs 15 或者 15 vs 15 都可以，看你视觉偏好
			io.Fonts->AddFontFromFileTTF(cjk_font_path.c_str(), 16 * scale, &config, GetCJKRanges());
		}
	}

	io.Fonts->Build();
}

// SetupDefaultTheme() has moved to ThemeManager::ApplyDefaultTheme().