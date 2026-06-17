#pragma once
#include "Binary.h"
#include <imgui.h>
#include <iostream>
#include <unordered_map>
#include <string_view>
#include <SDL.h>

#define THEME_CONFIG_MAP(CFG_VAR, CFG_STR)\
	CFG_VAR(bool, isDarkMode, true)\
	CFG_VAR(ImGuiStyle, igs_light, ImGuiStyle())\
	CFG_VAR(ImGuiStyle, igs_dark, ImGuiStyle())\
	CFG_STR(language, 30, "en_US")\
	CFG_VAR(float, scale, 1.0f)\
	CFG_STR(injectionFilePath, 256, "./hc-inj.txt")\
	CFG_VAR(bool, lowPerformanceMode, false)\
	CFG_VAR(bool, enableAutoTint, false)\
	CFG_VAR(ImVec4, seedColor, ImVec4(0.5f, 0.5f, 0.5f, 1.0f))\
	CFG_VAR(bool, enableDiscordRPC, true)\
	CFG_VAR(int, windowX, SDL_WINDOWPOS_CENTERED)\
	CFG_VAR(int, windowY, SDL_WINDOWPOS_CENTERED)\
	CFG_VAR(int, windowW, 1600)\
	CFG_VAR(int, windowH, 1080)

#define DECLARE_VAR(type, name, def_val) type name;
#define DECLARE_STR(name, size, def_val) char name[size];
#define RESET_VAR(type, name, def_val) name = def_val;
#define RESET_STR(name, size, def_val) strncpy(name, def_val, size); name[size - 1] = '\0';
#define WRITE_VAR(type, name, def_val) WriteField(stm, #name, &name, sizeof(type));
#define WRITE_STR(name, size, def_val) WriteField(stm, #name, name, static_cast<uint32_t>(strlen(name)));

#define READ_VAR(type, name, def_val)\
	if (!matched && fieldName == #name) {\
		matched = true;\
		if (valSize == sizeof(type)) {\
			stm.read((char*)&name, valSize);\
			if (stm.fail()) name = def_val;\
		} else {\
			name = def_val;\
		}\
	}

#define READ_STR(name, str_capacity, def_val)\
	if (!matched && fieldName == #name) {\
		matched = true;\
		uint32_t readLen = (valSize < str_capacity) ? valSize : (str_capacity - 1);\
		if (readLen > 0) stm.read(name, readLen);\
		name[readLen] = '\0';\
		if (stm.fail()) {\
			strncpy(name, def_val, str_capacity); name[str_capacity - 1] = '\0';\
		}\
	}

struct ThemeSettings {
	static constexpr uint32_t MAGIC_HEADER = 0x47464354;
	static constexpr uint8_t FIELD_MARKER = 0xFD;
	THEME_CONFIG_MAP(DECLARE_VAR, DECLARE_STR)
	ThemeSettings() {
		ResetToDefaults();
	}
	~ThemeSettings() {}

	void ResetToDefaults() {
		THEME_CONFIG_MAP(RESET_VAR, RESET_STR)
	}

	static void WriteVarInt(std::ostream& stm, uint32_t value) {
		do {
			uint8_t byte = value & 0x7F;
			value >>= 7;
			if (value > 0) byte |= 0x80;
			stm.write((char*)&byte, 1);
		} while (value > 0);
	}

	static bool ReadVarInt(std::istream& stm, std::streamsize fsz, uint32_t& out_val) {
		out_val = 0;
		int shift = 0;
		for (int i = 0; i < 4; ++i) { // max 4 bytes ≈ 268MB
			if (fsz - stm.tellg() < 1) return false;
			uint8_t byte = 0;
			stm.read((char*)&byte, 1);
			out_val |= (static_cast<uint32_t>(byte & 0x7F) << shift);
			if ((byte & 0x80) == 0) return true;
			shift += 7;
		}
		return false;
	}

	static void WriteField(std::ostream& stm, const char* name, const void* data, uint32_t size) {
		uint8_t marker = FIELD_MARKER;
		uint8_t nameLen = static_cast<uint8_t>(strlen(name));
		stm.write((char*)&marker, 1);
		stm.write((char*)&nameLen, 1);
		stm.write(name, nameLen);
		WriteVarInt(stm, size);
		stm.write((const char*)data, size);
	}

	void Write(std::ostream& stm) const {
		uint32_t magic = MAGIC_HEADER;
		stm.write((char*)&magic, 4);
		THEME_CONFIG_MAP(WRITE_VAR, WRITE_STR)
	}

	void Read(std::istream& stm) {
		stm.seekg(0, std::ios::end);
		std::streamsize fsz = stm.tellg();
		stm.seekg(0, std::ios::beg);	
		if (fsz < static_cast<std::streamsize>(sizeof(uint32_t))) return;
		uint32_t magic = 0;
		stm.read((char*)&magic, 4);

		if (magic != MAGIC_HEADER) {
			stm.clear();
			stm.seekg(0, std::ios::beg);
			LegacyRead(stm, fsz);
			return;
		}

		while (stm.tellg() < fsz && stm.tellg() != static_cast<std::streampos>(-1)) {
			uint8_t marker = 0;
			if (fsz - stm.tellg() < 1) break;
			stm.read((char*)&marker, 1);
			if (marker != FIELD_MARKER) break;
			uint8_t nameLen = 0;
			if (fsz - stm.tellg() < 1) break;
			stm.read((char*)&nameLen, 1);
			if (fsz - stm.tellg() < nameLen) break;
			char nameBuf[256] = {0};
			stm.read(nameBuf, nameLen);
			std::string_view fieldName(nameBuf, nameLen);
			uint32_t valSize = 0;
			if (!ReadVarInt(stm, fsz, valSize)) break;
			if (fsz - stm.tellg() < valSize) break;
			auto dataPos = stm.tellg();
			bool matched = false;
			THEME_CONFIG_MAP(READ_VAR, READ_STR)
			if (stm.fail()) stm.clear();
			stm.seekg(dataPos + static_cast<std::streampos>(valSize), std::ios::beg);
		}
	}

	void LegacyRead(std::istream& is, std::streamsize fsz) {
		struct LegacyThemeSettings {
			bool isDarkMode;
			ImGuiStyle igs_light;
			ImGuiStyle igs_dark;
			char language[30];
			float scale;
			char injectionFilePath[256];
			bool lowPerformanceMode;
			bool enableAutoTint;
			ImVec4 seedColor;
			bool enableDiscordRPC;
			int windowX;
			int windowY;
			int windowW;
			int windowH;
		};

		if (fsz >= static_cast<std::streamsize>(sizeof(LegacyThemeSettings))) {
			LegacyThemeSettings legacy;
			is.read((char*)&legacy, sizeof(LegacyThemeSettings));
			if (!is.fail()) {
				isDarkMode = legacy.isDarkMode;
				igs_light = legacy.igs_light;
				igs_dark = legacy.igs_dark;
				strncpy(language, legacy.language, sizeof(language)); language[sizeof(language)-1] = '\0';
				scale = legacy.scale;
				strncpy(injectionFilePath, legacy.injectionFilePath, sizeof(injectionFilePath)); injectionFilePath[sizeof(injectionFilePath)-1] = '\0';
				lowPerformanceMode = legacy.lowPerformanceMode;
				enableAutoTint = legacy.enableAutoTint;
				seedColor = legacy.seedColor;
				enableDiscordRPC = legacy.enableDiscordRPC;
				windowX = legacy.windowX;
				windowY = legacy.windowY;
				windowW = legacy.windowW;
				windowH = legacy.windowH;
			}
		} else if (fsz > 0) {
			LegacyThemeSettings legacy{};
			is.read((char*)&legacy, fsz);
			isDarkMode = legacy.isDarkMode;
			igs_light = legacy.igs_light;
			igs_dark = legacy.igs_dark;
			strncpy(language, legacy.language, sizeof(language)); language[sizeof(language)-1] = '\0';
			scale = legacy.scale;
			strncpy(injectionFilePath, legacy.injectionFilePath, sizeof(injectionFilePath)); injectionFilePath[sizeof(injectionFilePath)-1] = '\0';
			lowPerformanceMode = legacy.lowPerformanceMode;
			enableAutoTint = legacy.enableAutoTint;
			seedColor = legacy.seedColor;
			enableDiscordRPC = legacy.enableDiscordRPC;
			windowX = legacy.windowX;
			windowY = legacy.windowY;
			windowW = legacy.windowW;
			windowH = legacy.windowH;
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