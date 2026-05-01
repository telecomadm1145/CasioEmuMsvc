#include "ThemeManager.h"
#ifndef TEST_BUILD

#include "Ext/cam/hct.h"
#include "Ext/cam/tones.h"
#endif // !TEST_BUILD
#include "Gui.h"
#include "Localization.h"
#include <SDL.h>
#include <algorithm>
#include <cmath>
#include <fstream>
#include <imgui_impl_sdlrenderer2.h>
#include <imgui_internal.h>
#include <map>
#include <vector>
#ifndef TEST_BUILD

using namespace material_color_utilities;
#endif

// ============================================================================
// 设置持久化
// ============================================================================
void ThemeManager::SaveSettings() {
	std::ofstream file("./theme.bin", std::ios::binary);
	if (file.is_open()) {
		Binary::Write(file, m_settings);
		file.close();
	}
}

void ThemeManager::LoadSettings() {
	std::ifstream file("./theme.bin", std::ios::binary);
	if (file.is_open()) {
		Binary::Read(file, m_settings);
		file.close();

		if (m_settings.isDarkMode) {
			SetDarkMode();
		}
		else {
			SetLightMode();
		}
		if (strlen(m_settings.language) > 0) {
			g_local.ChangeLanguage(m_settings.language);
		}
		m_fontScale = m_settings.scale;
		m_fontRebuildRequested = true;
	}
}

// ============================================================================
// 字体重建
// ============================================================================
void ThemeManager::RequestFontRebuild() {
	m_fontRebuildRequested = true;
}

void ThemeManager::SetFontScale(float scale) {
	m_fontScale = scale;
}

void ThemeManager::ProcessFontRebuild() {
	if (!m_fontRebuildRequested)
		return;

	RebuildFont(m_fontScale);
	if (m_fontScale != 0) {
		ImGuiStyle igs;

		// Load the user's saved unscaled base style (includes custom sizes + colors)
		if (m_settings.isDarkMode) {
			if (!is_mem_equal(m_settings.igs_dark, ImGuiStyle{})) {
				igs = m_settings.igs_dark;
			}
			else {
				igs = ImGuiStyle();
				ImGui::StyleColorsDark(&igs);
			}
		}
		else {
			if (!is_mem_equal(m_settings.igs_light, ImGuiStyle{})) {
				igs = m_settings.igs_light;
			}
			else {
				igs = ImGuiStyle();
				ImGui::StyleColorsLight(&igs);
			}
		}

		// Apply scale to the unscaled base
		igs.ScaleAllSizes(m_fontScale);
		ImGui::GetStyle() = igs;
	}
	ImGui_ImplSDLRenderer2_DestroyDeviceObjects();
	m_fontRebuildRequested = false;
}

// ============================================================================
// 背景重载
// ============================================================================
void ThemeManager::RequestBgReload() {
	m_bgReloadRequested = true;
}

// ============================================================================
// UI 缩放（原 UI::Scaling）
// ============================================================================
float ThemeManager::GetDensityDpi() {
	SDL_DisplayMode displayMode;
	float densityDpi = 160.0f;

	if (SDL_GetCurrentDisplayMode(0, &displayMode) == 0) {
		float physicalWidth, physicalHeight;
		if (SDL_GetDisplayDPI(0, &densityDpi, &physicalWidth, &physicalHeight) != 0) {
			if (displayMode.h <= 480)
				densityDpi = 120.0f;
			else if (displayMode.h <= 800)
				densityDpi = 160.0f;
			else if (displayMode.h <= 1280)
				densityDpi = 240.0f;
			else if (displayMode.h <= 1920)
				densityDpi = 320.0f;
			else if (displayMode.h <= 2560)
				densityDpi = 480.0f;
			else
				densityDpi = 640.0f;
		}
	}
	return densityDpi;
}

void ThemeManager::UpdateUIScale() {
	ImGuiIO& io = ImGui::GetIO();
	windowWidth = io.DisplaySize.x;
	windowHeight = io.DisplaySize.y;
	aspectRatio = windowWidth / windowHeight;

	float densityDpi = GetDensityDpi();
	float densityScale = densityDpi / 160.0f;

	float baseScale = std::min(windowWidth / 1920.0f, windowHeight / 1080.0f);

	float screenSizeAdjustment = 1.0f;
	float diagonalPixels = sqrt(pow(windowWidth, 2) + pow(windowHeight, 2));
	float diagonalInches = diagonalPixels / densityDpi;

	if (diagonalInches <= 4.0f) {
		screenSizeAdjustment = 0.75f;
	}
	else if (diagonalInches <= 5.0f) {
		screenSizeAdjustment = 0.8f;
	}
	else if (diagonalInches <= 6.0f) {
		screenSizeAdjustment = 0.90f;
	}
	else if (diagonalInches <= 7.0f) {
		screenSizeAdjustment = 1.0f;
	}
	else if (diagonalInches <= 10.0f) {
		screenSizeAdjustment = 1.1f;
	}
	else {
		screenSizeAdjustment = 1.2f;
	}

	fontScale = baseScale * screenSizeAdjustment * sqrt(densityScale);
	fontScale = std::clamp(fontScale, 0.5f, 1.5f);
	io.FontGlobalScale = std::max(fontScale, 0.75f);

	float touchScale = std::max(fontScale, 1.0f);
	padding = 9.5f * touchScale;
	buttonHeight = 38.0f * touchScale;
	minColumnWidth = 55.0f * fontScale;
	labelWidth = 85.0f * fontScale;

	ImGuiStyle& style = ImGui::GetStyle();

	style.WindowPadding = ImVec2(padding, padding);
	style.FramePadding = ImVec2(padding * 0.8f, padding * 0.8f);
	style.ItemSpacing = ImVec2(padding * 0.7f, padding * 0.7f);
	style.ItemInnerSpacing = ImVec2(padding * 0.5f, padding * 0.5f);
	style.TouchExtraPadding = ImVec2(padding * 0.6f, padding * 0.6f);

	style.ScrollbarSize = 24.0f * touchScale;
	style.GrabMinSize = 30.0f * touchScale;
	style.WindowTitleAlign = ImVec2(0.5f, 0.5f);
	style.MouseCursorScale = 1.2f * touchScale;

	float rounding = 7.5f * std::min(touchScale, 1.3f);
	style.WindowRounding = rounding;
	style.ChildRounding = rounding;
	style.FrameRounding = rounding;
	style.ScrollbarRounding = rounding;
	style.GrabRounding = rounding;
	style.TabRounding = rounding;
	style.PopupRounding = rounding;

	if (aspectRatio > 1.8f) {
		style.ItemSpacing.x *= 1.15f;
		style.WindowPadding.x *= 1.15f;
	}

	style.FramePadding.y = std::max(buttonHeight * 0.1f, 0.85f);
}

// ============================================================================
// 主题切换
// ============================================================================
void ThemeManager::SetLightMode() {
	if (is_mem_equal(m_settings.igs_light, {})) {
		// First time: initialize with default light theme
		ImGuiStyle base = ImGuiStyle();
		ImGui::StyleColorsLight(&base);
		m_settings.igs_light = base;
	}

	// Apply the unscaled base, then scale it
	ImGuiStyle styled = m_settings.igs_light;
	styled.ScaleAllSizes(m_fontScale);
	ImGui::GetStyle() = styled;

	m_settings.isDarkMode = false;
	SaveSettings();
}

void ThemeManager::SetDarkMode() {
	if (is_mem_equal(m_settings.igs_dark, {})) {
		// First time: initialize with default dark theme
		ImGuiStyle base = ImGuiStyle();
		ImGui::StyleColorsDark(&base);
		m_settings.igs_dark = base;
	}

	// Apply the unscaled base, then scale it
	ImGuiStyle styled = m_settings.igs_dark;
	styled.ScaleAllSizes(m_fontScale);
	ImGui::GetStyle() = styled;

	m_settings.isDarkMode = true;
	SaveSettings();
}

void ThemeManager::ApplyDefaultTheme() {
	ImGui::StyleColorsDark();
	ImGuiStyle& style = ImGui::GetStyle();
	style.WindowRounding = 2.0f;
	style.FrameRounding = 2.0f;
	style.Colors[ImGuiCol_WindowBg].w = 0.95f;
#ifdef __ANDROID__
	style.ScaleAllSizes(3.0f);
#endif
}

// ============================================================================
// Auto-Tint (MD3 Monet) Implementation
// ============================================================================

ImVec4 ThemeManager::RGBtoHSV(const ImVec4& rgb) {
	float r = rgb.x, g = rgb.y, b = rgb.z;
	float max = std::max({ r, g, b });
	float min = std::min({ r, g, b });
	float delta = max - min;

	ImVec4 hsv;
	hsv.w = rgb.w; // preserve alpha

	// Hue
	if (delta < 0.00001f) {
		hsv.x = 0.0f;
	}
	else if (max == r) {
		hsv.x = 60.0f * fmodf(((g - b) / delta), 6.0f);
	}
	else if (max == g) {
		hsv.x = 60.0f * (((b - r) / delta) + 2.0f);
	}
	else {
		hsv.x = 60.0f * (((r - g) / delta) + 4.0f);
	}
	if (hsv.x < 0.0f)
		hsv.x += 360.0f;

	// Saturation
	hsv.y = (max < 0.00001f) ? 0.0f : (delta / max);

	// Value
	hsv.z = max;

	return hsv;
}

ImVec4 ThemeManager::HSVtoRGB(const ImVec4& hsv) {
	float h = hsv.x, s = hsv.y, v = hsv.z;

	float c = v * s;
	float x = c * (1.0f - fabsf(fmodf(h / 60.0f, 2.0f) - 1.0f));
	float m = v - c;

	float r, g, b;
	if (h < 60.0f) {
		r = c;
		g = x;
		b = 0;
	}
	else if (h < 120.0f) {
		r = x;
		g = c;
		b = 0;
	}
	else if (h < 180.0f) {
		r = 0;
		g = c;
		b = x;
	}
	else if (h < 240.0f) {
		r = 0;
		g = x;
		b = c;
	}
	else if (h < 300.0f) {
		r = x;
		g = 0;
		b = c;
	}
	else {
		r = c;
		g = 0;
		b = x;
	}

	return ImVec4(r + m, g + m, b + m, hsv.w);
}

ImVec4 ThemeManager::ExtractDominantColor(SDL_Texture* texture, SDL_Renderer* renderer) {
	if (!texture)
		return ImVec4(0.5f, 0.5f, 0.5f, 1.0f);

	// Query texture format and size
	Uint32 format;
	int w, h;
	SDL_QueryTexture(texture, &format, nullptr, &w, &h);

	// Create a temporary surface to read pixels
	SDL_Surface* surface = SDL_CreateRGBSurfaceWithFormat(0, w, h, 32, SDL_PIXELFORMAT_RGBA32);
	if (!surface)
		return ImVec4(0.5f, 0.5f, 0.5f, 1.0f);

	// Render texture to surface
	SDL_SetRenderTarget(renderer, texture);
	SDL_RenderReadPixels(renderer, nullptr, surface->format->format, surface->pixels, surface->pitch);
	SDL_SetRenderTarget(renderer, nullptr);

	// Sample pixels and build hue histogram
	std::map<int, int> hueHistogram;				   // hue bucket (0-35) -> count
	int sampleStep = std::max(1, std::min(w, h) / 32); // sample every Nth pixel

	SDL_LockSurface(surface);
	Uint32* pixels = (Uint32*)surface->pixels;

	for (int y = 0; y < h; y += sampleStep) {
		for (int x = 0; x < w; x += sampleStep) {
			Uint32 pixel = pixels[y * (surface->pitch / 4) + x];
			Uint8 r, g, b, a;
			SDL_GetRGBA(pixel, surface->format, &r, &g, &b, &a);

			// Skip very dark or very transparent pixels
			if (a < 32 || (r < 20 && g < 20 && b < 20))
				continue;

			ImVec4 rgb(r / 255.0f, g / 255.0f, b / 255.0f, a / 255.0f);
			ImVec4 hsv = RGBtoHSV(rgb);

			// Skip desaturated pixels
			if (hsv.y < 0.15f)
				continue;

			int hueBucket = (int)(hsv.x / 10.0f); // 0-35 buckets
			hueHistogram[hueBucket]++;
		}
	}

	SDL_UnlockSurface(surface);
	SDL_FreeSurface(surface);

	// Find dominant hue
	int dominantBucket = 0;
	int maxCount = 0;
	for (const auto& [bucket, count] : hueHistogram) {
		if (count > maxCount) {
			maxCount = count;
			dominantBucket = bucket;
		}
	}

	// Convert back to RGB (at medium saturation and value)
	float dominantHue = dominantBucket * 10.0f + 5.0f; // center of bucket
	ImVec4 seedHSV(dominantHue, 0.6f, 0.7f, 1.0f);
	return HSVtoRGB(seedHSV);
}
// 辅助函数：设置颜色的 Alpha 值
static inline ImVec4 SetAlpha(const ImVec4& col, float alpha) {
	return ImVec4(col.x, col.y, col.z, alpha);
}

// 辅助函数：加深或变亮颜色 (factor > 1 变亮, < 1 变暗)
static inline ImVec4 Tint(const ImVec4& col, float factor) {
	return ImVec4(ImSaturate(col.x * factor), ImSaturate(col.y * factor), ImSaturate(col.z * factor), col.w);
}
// ImVec4 (0~1 float) -> ARGB (0xAARRGGBB)
static inline uint32_t ImVec4ToArgb(const ImVec4& c) {
	uint8_t a = static_cast<uint8_t>(ImSaturate(c.w) * 255.0f + 0.5f);
	uint8_t r = static_cast<uint8_t>(ImSaturate(c.x) * 255.0f + 0.5f);
	uint8_t g = static_cast<uint8_t>(ImSaturate(c.y) * 255.0f + 0.5f);
	uint8_t b = static_cast<uint8_t>(ImSaturate(c.z) * 255.0f + 0.5f);

	return (static_cast<uint32_t>(a) << 24) |
		(static_cast<uint32_t>(r) << 16) |
		(static_cast<uint32_t>(g) << 8) |
		(static_cast<uint32_t>(b));
}

// ARGB (0xAARRGGBB) -> ImVec4 (0~1 float)
static inline ImVec4 ArgbToImVec4(uint32_t argb) {
	float a = ((argb >> 24) & 0xFF) / 255.0f;
	float r = ((argb >> 16) & 0xFF) / 255.0f;
	float g = ((argb >> 8) & 0xFF) / 255.0f;
	float b = ((argb) & 0xFF) / 255.0f;
	return ImVec4(r, g, b, a);
}
void ThemeManager::GenerateMonetPalette(const ImVec4& seed, ImGuiStyle& style, bool isDark) {
#ifndef TEST_BUILD
	// 1. 把 ImGui 的 seedColor 转成 ARGB + HCT 对象
	uint32_t seed_argb = ImVec4ToArgb(seed);
	Hct seed_hct = Hct(seed_argb);

	const double hue = seed_hct.get_hue();

	// 2. 按官方 MD3 规则创建 Tonal Palettes
	//    这些常数来自 Material Color Utilities 的默认 Scheme 实现
	TonalPalette primary = TonalPalette(hue, 48.0);
	TonalPalette secondary = TonalPalette(hue, 16.0);
	TonalPalette tertiary = TonalPalette(hue + 60.0, 24.0);
	TonalPalette neutral = TonalPalette(hue, 4.0);
	TonalPalette neutral_v = TonalPalette(hue, 8.0);

	auto tone = [](const TonalPalette& p, double t) -> ImVec4 {
		return ArgbToImVec4(static_cast<uint32_t>(p.get(t)));
		};

	ImVec4 primaryCol, primaryContainer, secondaryCol, secondaryContainer;
	ImVec4 tertiaryCol, surface, surfaceVariant, outline;
	ImVec4 onSurface, onPrimary, onPrimaryContainer;

	if (isDark) {
		// ---- Dark scheme: 对应 Material 3 官方角色定义 ----
		primaryCol = tone(primary, 80);
		onPrimary = tone(primary, 20);
		primaryContainer = tone(primary, 30);
		onPrimaryContainer = tone(primary, 90);

		secondaryCol = tone(secondary, 80);
		secondaryContainer = tone(secondary, 30);

		tertiaryCol = tone(tertiary, 80);

		surface = tone(neutral, 6);
		surfaceVariant = tone(neutral_v, 30);
		onSurface = tone(neutral, 90);
		outline = tone(neutral_v, 60);
	}
	else {
		// ---- Light scheme ----
		primaryCol = tone(primary, 40);
		onPrimary = tone(primary, 100);
		primaryContainer = tone(primary, 90);
		onPrimaryContainer = tone(primary, 10);

		secondaryCol = tone(secondary, 40);
		secondaryContainer = tone(secondary, 90);

		tertiaryCol = tone(tertiary, 40);

		surface = tone(neutral, 98);
		surfaceVariant = tone(neutral_v, 90);
		onSurface = tone(neutral, 10);
		outline = tone(neutral_v, 50);
	}

	// 3. 把这些角色映射回 ImGuiStyle 颜色
	style.Colors[ImGuiCol_Text] = onSurface;
	style.Colors[ImGuiCol_TextDisabled] = SetAlpha(onSurface, 0.50f);

	// 背景
	style.Colors[ImGuiCol_WindowBg] = SetAlpha(surface, 0.70f);
	style.Colors[ImGuiCol_ChildBg] = SetAlpha(surface, 0.00f);
	style.Colors[ImGuiCol_PopupBg] = SetAlpha(surfaceVariant, 1.00f);
	style.Colors[ImGuiCol_Border] = outline;
	style.Colors[ImGuiCol_BorderShadow] = ImVec4(0, 0, 0, 0);

	// Frame 背景（输入框等）
	style.Colors[ImGuiCol_FrameBg] = isDark ? SetAlpha(surfaceVariant, 0.50f) : surfaceVariant;
	style.Colors[ImGuiCol_FrameBgHovered] = isDark ? SetAlpha(surfaceVariant, 0.80f) : Tint(surfaceVariant, 0.90f);
	style.Colors[ImGuiCol_FrameBgActive] = primaryContainer;

	// 标题栏 & 菜单
	style.Colors[ImGuiCol_TitleBg] = SetAlpha(surface, 1.0f);
	style.Colors[ImGuiCol_TitleBgActive] = SetAlpha(surfaceVariant, 1.0f);
	style.Colors[ImGuiCol_TitleBgCollapsed] = SetAlpha(surface, 0.7f);
	style.Colors[ImGuiCol_MenuBarBg] = surfaceVariant;

	// 滚动条
	style.Colors[ImGuiCol_ScrollbarBg] = SetAlpha(surface, 0.0f);
	style.Colors[ImGuiCol_ScrollbarGrab] = SetAlpha(outline, 0.5f);
	style.Colors[ImGuiCol_ScrollbarGrabHovered] = SetAlpha(outline, 0.8f);
	style.Colors[ImGuiCol_ScrollbarGrabActive] = primaryCol;

	// Checkbox / Slider / Radio
	style.Colors[ImGuiCol_CheckMark] = primaryCol;
	style.Colors[ImGuiCol_SliderGrab] = primaryCol;
	style.Colors[ImGuiCol_SliderGrabActive] = onPrimary;

	// 按钮
	style.Colors[ImGuiCol_Button] = isDark ? SetAlpha(primaryCol, 0.2f) : SetAlpha(primaryCol, 0.1f);
	style.Colors[ImGuiCol_ButtonHovered] = isDark ? SetAlpha(primaryCol, 0.4f) : SetAlpha(primaryCol, 0.2f);
	style.Colors[ImGuiCol_ButtonActive] = isDark ? SetAlpha(primaryCol, 0.6f) : SetAlpha(primaryCol, 0.3f);

	// Header / Selectable / CollapsingHeader
	style.Colors[ImGuiCol_Header] = SetAlpha(secondaryContainer, 0.3f);
	style.Colors[ImGuiCol_HeaderHovered] = SetAlpha(secondaryContainer, 0.6f);
	style.Colors[ImGuiCol_HeaderActive] = secondaryContainer;

	// 分隔线
	style.Colors[ImGuiCol_Separator] = outline;
	style.Colors[ImGuiCol_SeparatorHovered] = primaryCol;
	style.Colors[ImGuiCol_SeparatorActive] = primaryCol;

	// Resize Grip
	style.Colors[ImGuiCol_ResizeGrip] = SetAlpha(primaryCol, 0.15f);
	style.Colors[ImGuiCol_ResizeGripHovered] = primaryCol;
	style.Colors[ImGuiCol_ResizeGripActive] = primaryCol;

	// Tabs
	style.Colors[ImGuiCol_Tab] = SetAlpha(surfaceVariant, 0.5f);
	style.Colors[ImGuiCol_TabHovered] = primaryContainer;
	style.Colors[ImGuiCol_TabSelected] = isDark ? primaryContainer : surface;
	style.Colors[ImGuiCol_TabDimmed] = SetAlpha(surfaceVariant, 0.2f);
	style.Colors[ImGuiCol_TabDimmedSelected] = surfaceVariant;

	// Plot / Graph
	style.Colors[ImGuiCol_PlotLines] = tertiaryCol;
	style.Colors[ImGuiCol_PlotLinesHovered] = Tint(tertiaryCol, 1.2f);
	style.Colors[ImGuiCol_PlotHistogram] = SetAlpha(tertiaryCol, 0.8f);
	style.Colors[ImGuiCol_PlotHistogramHovered] = tertiaryCol;

	// 其他
	style.Colors[ImGuiCol_TextSelectedBg] = SetAlpha(primaryCol, 0.4f);
	style.Colors[ImGuiCol_DragDropTarget] = tertiaryCol;
	style.Colors[ImGuiCol_NavHighlight] = primaryCol;
	style.Colors[ImGuiCol_NavWindowingHighlight] = primaryCol;
	style.Colors[ImGuiCol_NavWindowingDimBg] = ImVec4(0, 0, 0, 0.7f);
	style.Colors[ImGuiCol_ModalWindowDimBg] = ImVec4(0, 0, 0, 0.7f);
#endif
}
void ThemeManager::ExtractAndApplyAutoTint(SDL_Texture* bgTexture, SDL_Renderer* renderer) {
	if (!m_settings.enableAutoTint || !bgTexture || !renderer)
		return;

	// Extract dominant color from background
	ImVec4 dominantColor = ExtractDominantColor(bgTexture, renderer);
	m_settings.seedColor = dominantColor;

	// Generate and apply Monet palette
	ImGuiStyle& base = m_settings.isDarkMode ? m_settings.igs_dark : m_settings.igs_light;
	GenerateMonetPalette(dominantColor, base, m_settings.isDarkMode);

	// Apply to current style
	RequestFontRebuild();
	SaveSettings();
} // 小工具：把 (source_argb, factorKey) 合成一个 64bit key
static inline uint64_t MakeHarmonizeKey(uint32_t src_argb, uint8_t factorKey) {
	return (static_cast<uint64_t>(src_argb) << 8) | factorKey;
}

ImVec4 ThemeManager::Harmonize(const ImVec4& source, float factor) const {
#ifndef TEST_BUILD
	// 约束 factor 到 [0, 1]
	factor = ImClamp(factor, 0.0f, 1.0f);

	uint32_t src_argb = ImVec4ToArgb(source); // 工具函数前面已经给过
	uint32_t seed_argb = ImVec4ToArgb(m_settings.seedColor);

	// 把 factor 量化到 0~255 作为 key 的一部分
	uint8_t fKey = static_cast<uint8_t>(factor * 255.0f + 0.5f);
	uint64_t key = MakeHarmonizeKey(src_argb, fKey);

	// 1. 先查缓存
	auto it = m_harmonizeCache.find(key);
	if (it != m_harmonizeCache.end()) {
		ImVec4 cached = it->second;
		// 使用调用者自己的 alpha
		cached.w = source.w;
		return cached;
	}

	// 2. 缓存未命中 -> 计算一次 HCT Harmonize
	using namespace material_color_utilities;

	Hct src_hct = Hct(src_argb);
	Hct seed_hct = Hct(seed_argb);

	double s_h = src_hct.get_hue();
	double d_h = seed_hct.get_hue();

	double diff = d_h - s_h;
	if (diff > 180.0)
		diff -= 360.0;
	if (diff < -180.0)
		diff += 360.0;

	double new_h = s_h + diff * factor;
	if (new_h < 0.0)
		new_h += 360.0;
	else if (new_h >= 360.0)
		new_h -= 360.0;

	// 保持原来的 chroma 和 tone，只改 hue
	Hct out_hct(new_h, src_hct.get_chroma(), src_hct.get_tone() * 0.5 + (m_settings.isDarkMode ? 0 : 70) * 0.5);
	uint32_t out_argb = static_cast<uint32_t>(out_hct.ToInt());

	ImVec4 result = ArgbToImVec4(out_argb);
	result.w = source.w;

	// 3. 写入缓存
	m_harmonizeCache.emplace(key, result);

	return result;
#else
	return {};
#endif // !TEST_BUILD
}
void ThemeManager::SetSeedColor(const ImVec4& color) {
	m_settings.seedColor = color;

	m_harmonizeCache.clear();

	if (m_settings.enableAutoTint) {
		ImGuiStyle& base = m_settings.isDarkMode ? m_settings.igs_dark : m_settings.igs_light;
		GenerateMonetPalette(color, base, m_settings.isDarkMode);
		RequestFontRebuild();
		SaveSettings();
	}
}
