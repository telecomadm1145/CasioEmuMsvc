#pragma once
#include "Localization.h"
#include <filesystem>
#include <imgui.h>
#include <imgui_impl_sdlrenderer2.h>
#include <string>

inline const ImWchar* GetCJKRanges() {
	static const ImWchar ranges[] = {
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

inline std::string GetCJKFontPath() {
	auto preference = "Localization.CJKPreference"_l;

#ifdef _WIN32
	if (preference == "JP") {
		if (std::filesystem::exists("C:\\Windows\\Fonts\\YuGothM.ttc"))
			return "C:\\Windows\\Fonts\\YuGothM.ttc";
		if (std::filesystem::exists("C:\\Windows\\Fonts\\msgothic.ttc"))
			return "C:\\Windows\\Fonts\\msgothic.ttc";
	}
	if (preference == "CN") {
		if (std::filesystem::exists("C:\\Windows\\Fonts\\msyh.ttc"))
			return "C:\\Windows\\Fonts\\msyh.ttc";
		if (std::filesystem::exists("C:\\Windows\\Fonts\\simsun.ttc"))
			return "C:\\Windows\\Fonts\\simsun.ttc";
	}
#elif defined(__ANDROID__)
	if (std::filesystem::exists("/system/fonts/NotoSansCJK-Regular.ttc"))
		return "/system/fonts/NotoSansCJK-Regular.ttc";
#else
	if (std::filesystem::exists("/usr/share/fonts/noto-cjk/NotoSansCJK-Regular.ttc"))
		return "/usr/share/fonts/noto-cjk/NotoSansCJK-Regular.ttc";
#endif
	return "";
}
extern int RebuildFont_Requested;
extern float RebuildFont_Scale;
inline void RebuildFont(float scale = 0.0f) {
	auto& io = ImGui::GetIO();
	ImFontConfig config = ImFontConfig();
	io.Fonts->Clear();

#ifdef __ANDROID__
	constexpr float defaultscale = 3.0f;
#else
	constexpr float defaultscale = 1.0f;
#endif
	if (scale == 0) {
		scale = defaultscale;
	}

	// 加载基础等宽字体
	bool base_font_loaded = false;
	if (std::filesystem::exists("C:\\Windows\\Fonts\\Consola.ttf")) {
		io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\Consola.ttf", 15 * scale, 0, io.Fonts->GetGlyphRangesVietnamese());
		base_font_loaded = true;
	}
	else if (std::filesystem::exists("/system/fonts/DroidSansMono.ttf")) {
		io.Fonts->AddFontFromFileTTF("/system/fonts/DroidSansMono.ttf", 15 * scale, 0, io.Fonts->GetGlyphRangesVietnamese());
		base_font_loaded = true;
	}

	if (!base_font_loaded) {
		printf("[Ui][Warn] Monospace font not found\n");
		io.Fonts->AddFontDefault(&config);
	}

	// 如果启用CJK支持，加载CJK字体
	auto enable_cjk = "Localization.EnableCJK"_l;
	if (enable_cjk == "1" || enable_cjk == "true") {
		std::string cjk_font_path = GetCJKFontPath();
		if (!cjk_font_path.empty()) {
			config.MergeMode = true;
			io.Fonts->AddFontFromFileTTF(cjk_font_path.c_str(), 16 * scale, &config, GetCJKRanges());
		}
		else {
			printf("[Ui][Warn] CJK font not found\n");
		}
	}

	io.Fonts->Build();
	//if ((ImGui::GetCurrentContext() ? ImGui::GetIO().BackendRendererUserData : nullptr) != 0) {
	//	ImGui_ImplSDLRenderer2_DestroyFontsTexture();
	//	ImGui_ImplSDLRenderer2_CreateFontsTexture();
	//}
}
inline void SetupDefaultTheme() {
	ImGui::StyleColorsDark();
	ImGuiStyle& style = ImGui::GetStyle();
	style.WindowRounding = 4.0f;
	style.Colors[ImGuiCol_WindowBg].w = 0.9f;
	style.FrameRounding = 4.0f;
#ifdef __ANDROID__
	style.ScaleAllSizes(3.0f);
#endif
}