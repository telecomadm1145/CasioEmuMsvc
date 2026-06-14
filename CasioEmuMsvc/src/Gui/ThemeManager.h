#pragma once
#include "Binary.h"
#include <imgui.h>
#include <iostream>
#include <unordered_map>

// ============================================================================
// ThemeSettings — 序列化数据结构（保持不变以兼容 theme.bin）
// ============================================================================
struct ThemeSettings {
	bool isDarkMode = true;
	ImGuiStyle igs_light = ImGuiStyle();
	ImGuiStyle igs_dark = ImGuiStyle();
	char language[30] = "";
	float scale = 1.0f;
	char injectionFilePath[256] = "./hc-inj.txt";
	bool lowPerformanceMode = false;

	// Auto-tint (MD3 Monet)
	bool enableAutoTint = false;
	ImVec4 seedColor = ImVec4(0.5f, 0.5f, 0.5f, 1.0f);
  bool enableDiscordRPC = true;
	void Write(std::ostream& stm) const {
		Binary::Write(stm, isDarkMode);
		stm.write(language, sizeof(language));
		Binary::Write(stm, scale);
		stm.write(injectionFilePath, sizeof(injectionFilePath));
		Binary::Write(stm, lowPerformanceMode);
		Binary::Write(stm, igs_light);
		Binary::Write(stm, igs_dark);
		Binary::Write(stm, enableAutoTint);
		Binary::Write(stm, seedColor);
		Binary::Write(stm, enableDiscordRPC);
	}

	void Read(std::istream& stm) {
		Binary::Read(stm, isDarkMode);
		stm.read(language, sizeof(language));
		Binary::Read(stm, scale);
		stm.read(injectionFilePath, sizeof(injectionFilePath));
		if (stm.peek() != EOF) {
			Binary::Read(stm, lowPerformanceMode);
		}
		Binary::Read(stm, igs_light);
		Binary::Read(stm, igs_dark);
		if (stm.peek() != EOF) {
			Binary::Read(stm, enableAutoTint);
			Binary::Read(stm, seedColor);
		}
		if (stm.peek() != EOF) {
			Binary::Read(stm, enableDiscordRPC);
		}
	}
};

// ============================================================================
// ThemeManager — 全局单例，统一管理主题、字体缩放、UI 缩放
// ============================================================================
class ThemeManager {
public:
	static ThemeManager& Instance() {
		static ThemeManager instance;
		return instance;
	}

	// —— 设置存取 ——
	ThemeSettings& Settings() { return m_settings; }
	const ThemeSettings& Settings() const { return m_settings; }
	void SaveSettings();
	void LoadSettings();

	// —— 字体重建 ——
	void RequestFontRebuild();
	void SetFontScale(float scale);
	float GetFontScale() const { return m_fontScale; }
	bool IsFontRebuildRequested() const { return m_fontRebuildRequested; }
	/// 在主循环中调用。执行字体重建 + style 缩放，替代散落的 if(RebuildFont_Requested) 块
	void ProcessFontRebuild();

	// —— 背景重载 ——
	void RequestBgReload();
	bool IsBgReloadRequested() const { return m_bgReloadRequested; }
	void ClearBgReloadRequest() { m_bgReloadRequested = false; }

	// —— UI 缩放（原 UI::Scaling）——
	void UpdateUIScale();
	float GetDensityDpi();

	// 缩放参数（原 UI::Scaling 的 inline static 字段，保持 public 方便直接读取）
	float fontScale = 1.0f;
	float padding = 8.0f;
	float buttonHeight = 38.0f;
	float minColumnWidth = 55.0f;
	float labelWidth = 85.0f;
	float windowWidth = 0.0f;
	float windowHeight = 0.0f;
	float aspectRatio = 1.0f;

	// —— 主题切换 ——
	void SetDarkMode();
	void SetLightMode();
	/// 应用默认主题（原 SetupDefaultTheme）
	void ApplyDefaultTheme();

	// —— Auto-Tint (MD3 Monet) ——
	void ExtractAndApplyAutoTint(struct SDL_Texture* bgTexture, struct SDL_Renderer* renderer);
	void SetSeedColor(const ImVec4& color);
	ImVec4 GetSeedColor() const { return m_settings.seedColor; }
	ImVec4 ExtractDominantColor(SDL_Texture* texture, SDL_Renderer* renderer);
	ImVec4 Harmonize(const ImVec4& source, float factor = 0.3f) const;

private:
	ThemeManager() = default;
	ThemeManager(const ThemeManager&) = delete;
	ThemeManager& operator=(const ThemeManager&) = delete;

	ThemeSettings m_settings;
	float m_fontScale = 0.0f;
	bool m_fontRebuildRequested = false;
	bool m_bgReloadRequested = false;
	mutable std::unordered_map<uint64_t, ImVec4> m_harmonizeCache;

	// Auto-tint helpers
	void GenerateMonetPalette(const ImVec4& seed, ImGuiStyle& style, bool isDark);
	ImVec4 RGBtoHSV(const ImVec4& rgb);
	ImVec4 HSVtoRGB(const ImVec4& hsv);
};

inline ImColor operator~(const ImColor& color) {
	return ImColor{ThemeManager::Instance().Harmonize(color)};
}
inline ImVec4 operator~(const ImVec4& color) {
	return ImVec4{ThemeManager::Instance().Harmonize(color)};
}