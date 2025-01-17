#pragma once
#include <filesystem>
#include <imgui.h>
inline const ImWchar* GetKanji() {
	static const ImWchar ranges[] = {
		0x2000,
		0x206F, // General Punctuation
		0x3000,
		0x30FF, // CJK Symbols and Punctuations, Hiragana, Katakana
		0x31F0,
		0x31FF, // Katakana Phonetic Extensions
		0xFF00,
		0xFFEF, // Half-width characters
		0xFFFD,
		0xFFFD, // Invalid
		0x4e00,
		0x9FAF, // CJK Ideograms
		0,
	};
	return &ranges[0];
}
inline void SetupDefaultFont() {
	auto& io = ImGui::GetIO();
	ImFontConfig config = ImFontConfig();
	// config.MergeMode = true;
#ifdef __ANDROID__
    config.OversampleH = config.OversampleV = 3.0;
    config.SizePixels = 15;
#endif
	if (std::filesystem::exists("C:\\Windows\\Fonts\\CascadiaCode.ttf"))
		io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\CascadiaCode.ttf", 15);
	else {
		printf("[Ui][Warn] \"CascadiaCode.ttf\" not found\n");
		io.Fonts->AddFontDefault(&config);
	}
#if LANGUAGE == 2
	if (std::filesystem::exists("NotoSansSC-Medium.otf"))
		io.Fonts->AddFontFromFileTTF("NotoSansSC-Medium.otf", 18, &config, GetKanji());
	else if (std::filesystem::exists("C:\\Windows\\Fonts\\msyh.ttc")) {
		printf("[Ui][Warn] fallback to MSYH.\n");
		io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\msyh.ttc", 18, &config, GetKanji());
	}
	else {
		printf("[Ui][Warn] No chinese font available!\n");
	}
#else
#endif
	// config.GlyphOffset = ImVec2(0,1.5);
	io.Fonts->Build();
#ifdef __ANDROID__
    io.FontGlobalScale = 3.0;
#endif
};
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