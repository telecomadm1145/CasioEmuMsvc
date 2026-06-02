#pragma once
#include "Chipset/MMU.hpp"
#include "Emulator.hpp"
#include "LabelFile.h"
#include "ThemeManager.h"
#include "imgui/imgui.h"
#include "imgui/imgui_internal.h"


class CodeViewer;
CodeViewer* test_gui(bool* guiCreated, SDL_Window*, SDL_Renderer*);
void gui_cleanup();
void gui_loop();
extern char* n_ram_buffer;
extern int top_bar_size;
extern casioemu::MMU* me_mmu;
extern casioemu::Emulator* m_emu;
extern SDL_Window* window;
extern SDL_Renderer* renderer;
extern std::vector<Label> g_labels;
extern uint32_t pc_cache;

void SetDebugbreak(void); 
class UIWindow {
public:
	UIWindow(const char* name) : name(name) {
#ifdef __ANDROID__
		inital_size = ImVec2(
			800 * ThemeManager::Instance().fontScale,
			800 * ThemeManager::Instance().fontScale);
#else
		inital_size = ImVec2(800, 800);
#endif
	}
	const char* name{};
	bool open = true;
	bool bring_to_front_requested = false;
	ImVec2 inital_size;
	ImGuiWindowFlags flags{};

	virtual void Render() {
		if (!open)
			return;
#ifdef __ANDROID__
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,
			ImVec2(ThemeManager::Instance().padding, ThemeManager::Instance().padding));
#endif
		if (bring_to_front_requested) {
			ImGui::SetNextWindowFocus();
			bring_to_front_requested = false;
		}
		ImGui::SetNextWindowSize(inital_size, ImGuiCond_FirstUseEver);
		if (ImGui::Begin(name, &open, flags)) {
			RenderCore();
		}
		ImGui::End();

#ifdef __ANDROID__
		ImGui::PopStyleVar();
#endif
	}
	void BringToFront() {
		bring_to_front_requested = true;
	}
	virtual void RenderCore() = 0;
	virtual ~UIWindow() {}


};

// protected:
//	// syntax: prompt_if_error(func)(...);
//	auto prompt_if_error(auto f) {
//	}

namespace UIHelpers {
	// Button with keyboard shortcut tooltip
	inline bool ButtonWithShortcut(const char* label, const char* shortcut, ImVec2 size = { 0, 0 }) {
		bool pressed = ImGui::Button(label, size);
		if (shortcut && ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) {
			ImGui::BeginTooltip();
			ImGui::TextDisabled("Shortcut: %s", shortcut);
			ImGui::EndTooltip();
		}
		return pressed;
	}

	// (?) Help marker - displays a tooltip on hover
	inline void HelpMarker(const char* desc) {
		ImGui::TextDisabled("(?)");
		if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) {
			ImGui::BeginTooltip();
			ImGui::PushTextWrapPos(ImGui::GetFontSize() * 25.0f);
			ImGui::TextUnformatted(desc);
			ImGui::PopTextWrapPos();
			ImGui::EndTooltip();
		}
	}

	// Group/Section header with separator line
	inline void SectionHeader(const char* label) {
		ImGui::Spacing();
		ImGui::TextUnformatted(label);
		ImGui::Separator();
		ImGui::Spacing();
	}

	// Colored status badge
	inline void StatusBadge(const char* text, const ImVec4& color) {
		ImGui::PushStyleColor(ImGuiCol_Button, color);
		ImGui::PushStyleColor(ImGuiCol_ButtonHovered, color);
		ImGui::PushStyleColor(ImGuiCol_ButtonActive, color);
		ImGui::SmallButton(text);
		ImGui::PopStyleColor(3);
	}

	// Color constants (proper 0.0 - 1.0 range)
	inline constexpr ImVec4 kColorSuccess = ImVec4(0.30f, 0.80f, 0.30f, 1.0f);
	inline constexpr ImVec4 kColorWarning = ImVec4(0.95f, 0.80f, 0.20f, 1.0f);
	inline constexpr ImVec4 kColorError   = ImVec4(0.95f, 0.30f, 0.30f, 1.0f);
	inline constexpr ImVec4 kColorInfo    = ImVec4(0.40f, 0.60f, 0.90f, 1.0f);
	inline constexpr ImVec4 kColorMuted   = ImVec4(0.55f, 0.55f, 0.60f, 1.0f);

	// Clickable address link (implemented in Ui.cpp)
	void ClickableAddress(uint32_t addr);
}

inline constexpr ImGuiTableFlags pretty_table = ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersOuter | ImGuiTableFlags_BordersV | ImGuiTableFlags_Reorderable | ImGuiTableFlags_Resizable;