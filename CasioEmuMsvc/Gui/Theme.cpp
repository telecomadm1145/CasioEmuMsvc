#include "Theme.h"
#include "FileDialog.hpp"
#include "Ui.hpp"
#include <Gui.h>
#include <Localization.h>
#include <fstream>
#include <string>
#include <vibration.h>

static ThemeSettings g_settings;

void SetModernDarkTheme() {
    ImGuiStyle& style = ImGui::GetStyle();
    ImVec4* colors = style.Colors;

    style.WindowRounding = 5.0f;
    style.FrameRounding = 4.0f;
    style.PopupRounding = 4.0f;
    style.GrabRounding = 4.0f;
    style.TabRounding = 4.0f;

    style.WindowPadding = ImVec2(10, 10);
    style.FramePadding = ImVec2(6, 4);
    style.ItemSpacing = ImVec2(8, 6);
    style.ItemInnerSpacing = ImVec2(6, 6);
    style.IndentSpacing = 25.0f;
    style.ScrollbarSize = 15.0f;
    style.GrabMinSize = 10.0f;

    style.WindowBorderSize = 1.0f;
    style.ChildBorderSize = 1.0f;
    style.PopupBorderSize = 1.0f;
    style.FrameBorderSize = 0.0f;
    style.TabBorderSize = 0.0f;

    // Modern Dark Palette
    colors[ImGuiCol_Text]                   = ImVec4(0.95f, 0.96f, 0.98f, 1.00f);
    colors[ImGuiCol_TextDisabled]           = ImVec4(0.50f, 0.50f, 0.50f, 1.00f);
    colors[ImGuiCol_WindowBg]               = ImVec4(0.10f, 0.105f, 0.11f, 1.00f);
    colors[ImGuiCol_ChildBg]                = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
    colors[ImGuiCol_PopupBg]                = ImVec4(0.12f, 0.12f, 0.13f, 0.94f);
    colors[ImGuiCol_Border]                 = ImVec4(0.25f, 0.25f, 0.27f, 0.50f);
    colors[ImGuiCol_BorderShadow]           = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
    colors[ImGuiCol_FrameBg]                = ImVec4(0.16f, 0.16f, 0.17f, 1.00f);
    colors[ImGuiCol_FrameBgHovered]         = ImVec4(0.20f, 0.20f, 0.22f, 1.00f);
    colors[ImGuiCol_FrameBgActive]          = ImVec4(0.24f, 0.24f, 0.26f, 1.00f);
    colors[ImGuiCol_TitleBg]                = ImVec4(0.10f, 0.10f, 0.11f, 1.00f);
    colors[ImGuiCol_TitleBgActive]          = ImVec4(0.12f, 0.12f, 0.13f, 1.00f);
    colors[ImGuiCol_TitleBgCollapsed]       = ImVec4(0.00f, 0.00f, 0.00f, 0.51f);
    colors[ImGuiCol_MenuBarBg]              = ImVec4(0.14f, 0.14f, 0.15f, 1.00f);
    colors[ImGuiCol_ScrollbarBg]            = ImVec4(0.02f, 0.02f, 0.02f, 0.53f);
    colors[ImGuiCol_ScrollbarGrab]          = ImVec4(0.31f, 0.31f, 0.31f, 1.00f);
    colors[ImGuiCol_ScrollbarGrabHovered]   = ImVec4(0.41f, 0.41f, 0.41f, 1.00f);
    colors[ImGuiCol_ScrollbarGrabActive]    = ImVec4(0.51f, 0.51f, 0.51f, 1.00f);
    colors[ImGuiCol_CheckMark]              = ImVec4(0.26f, 0.59f, 0.98f, 1.00f);
    colors[ImGuiCol_SliderGrab]             = ImVec4(0.26f, 0.59f, 0.98f, 0.78f);
    colors[ImGuiCol_SliderGrabActive]       = ImVec4(0.26f, 0.59f, 0.98f, 1.00f);
    colors[ImGuiCol_Button]                 = ImVec4(0.20f, 0.25f, 0.30f, 1.00f);
    colors[ImGuiCol_ButtonHovered]          = ImVec4(0.28f, 0.35f, 0.42f, 1.00f);
    colors[ImGuiCol_ButtonActive]           = ImVec4(0.16f, 0.20f, 0.25f, 1.00f);
    colors[ImGuiCol_Header]                 = ImVec4(0.20f, 0.25f, 0.30f, 0.55f);
    colors[ImGuiCol_HeaderHovered]          = ImVec4(0.28f, 0.35f, 0.42f, 0.80f);
    colors[ImGuiCol_HeaderActive]           = ImVec4(0.26f, 0.59f, 0.98f, 1.00f);
    colors[ImGuiCol_Separator]              = ImVec4(0.25f, 0.25f, 0.27f, 1.00f);
    colors[ImGuiCol_SeparatorHovered]       = ImVec4(0.10f, 0.40f, 0.75f, 0.78f);
    colors[ImGuiCol_SeparatorActive]        = ImVec4(0.10f, 0.40f, 0.75f, 1.00f);
    colors[ImGuiCol_ResizeGrip]             = ImVec4(0.26f, 0.59f, 0.98f, 0.25f);
    colors[ImGuiCol_ResizeGripHovered]      = ImVec4(0.26f, 0.59f, 0.98f, 0.67f);
    colors[ImGuiCol_ResizeGripActive]       = ImVec4(0.26f, 0.59f, 0.98f, 0.95f);
    colors[ImGuiCol_Tab]                    = ImVec4(0.15f, 0.15f, 0.16f, 0.86f);
    colors[ImGuiCol_TabHovered]             = ImVec4(0.28f, 0.35f, 0.42f, 0.80f);
    colors[ImGuiCol_TabActive]              = ImVec4(0.20f, 0.25f, 0.32f, 1.00f);
    colors[ImGuiCol_TabUnfocused]           = ImVec4(0.10f, 0.10f, 0.11f, 0.97f);
    colors[ImGuiCol_TabUnfocusedActive]     = ImVec4(0.14f, 0.14f, 0.15f, 1.00f);
    colors[ImGuiCol_PlotLines]              = ImVec4(0.61f, 0.61f, 0.61f, 1.00f);
    colors[ImGuiCol_PlotLinesHovered]       = ImVec4(1.00f, 0.43f, 0.35f, 1.00f);
    colors[ImGuiCol_PlotHistogram]          = ImVec4(0.90f, 0.70f, 0.00f, 1.00f);
    colors[ImGuiCol_PlotHistogramHovered]   = ImVec4(1.00f, 0.60f, 0.00f, 1.00f);
    colors[ImGuiCol_TextSelectedBg]         = ImVec4(0.26f, 0.59f, 0.98f, 0.35f);
    colors[ImGuiCol_DragDropTarget]         = ImVec4(1.00f, 1.00f, 0.00f, 0.90f);
    colors[ImGuiCol_NavHighlight]           = ImVec4(0.26f, 0.59f, 0.98f, 1.00f);
    colors[ImGuiCol_NavWindowingHighlight]  = ImVec4(1.00f, 1.00f, 1.00f, 0.70f);
    colors[ImGuiCol_NavWindowingDimBg]      = ImVec4(0.80f, 0.80f, 0.80f, 0.20f);
    colors[ImGuiCol_ModalWindowDimBg]       = ImVec4(0.80f, 0.80f, 0.80f, 0.35f);
}

void SaveThemeSettings() {
	std::ofstream file("./theme.bin", std::ios::binary);
	if (file.is_open()) {
		Binary::Write(file, g_settings);
		file.close();
	}
}

void LoadThemeSettings() {
	std::ifstream file("./theme.bin", std::ios::binary);
	if (file.is_open()) {
		Binary::Read(file, g_settings);
		file.close();

		if (g_settings.isDarkMode) {
			SetModernDarkTheme();
		}
		else {
			ImGui::StyleColorsLight();
		}
		if (strlen(g_settings.language) > 0) {
			g_local.ChangeLanguage(g_settings.language);
		}
		RebuildFont_Scale = g_settings.scale;
		RebuildFont_Requested = true;
	} else {
        // Apply default theme if no settings file found
        if (g_settings.isDarkMode) {
            SetModernDarkTheme();
        }
    }
}

const ThemeSettings& GetThemeSettings() {
	return g_settings;
}

class ThemeWindow : public UIWindow {
private:
	bool showFileDialog;
	char tempInjectionFilePath[256];

public:
	ThemeWindow() : UIWindow("Theme"), showFileDialog(false) {
		LoadThemeSettings();
		strncpy(tempInjectionFilePath, g_settings.injectionFilePath, sizeof(tempInjectionFilePath));
	}

	void RenderCore() override {
        // Theme Selection
        if (ImGui::CollapsingHeader("Appearance", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::Text("Theme");
            ImGui::SameLine();
            if (ImGui::Button("Ui.DarkMode"_lc)) {
                SetModernDarkTheme();
                g_settings.isDarkMode = true;
                SaveThemeSettings();
            }
            ImGui::SameLine();
            if (ImGui::Button("Ui.LightMode"_lc)) {
                ImGui::StyleColorsLight();
                g_settings.isDarkMode = false;
                SaveThemeSettings();
            }

            ImGui::Separator();

            // Scaling
            ImGui::Text("Interface Scale");
            if (ImGui::SliderFloat("Ui.Scale"_lc, &RebuildFont_Scale, 0.5f, 5.0f, "%.2f")) {
                g_settings.scale = RebuildFont_Scale;
            }
            if (ImGui::Button("Ui.ApplyScale"_lc)) {
                RebuildFont_Requested = true;
                SaveThemeSettings();
            }
        }

        if (ImGui::CollapsingHeader("Localization", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::Text("Language Code");
            ImGui::SameLine();
            ImGui::SetNextItemWidth(100);
            ImGui::InputText("##language_input", g_settings.language, 30);
            ImGui::SameLine();
            if (ImGui::Button("Ui.ChangeLang"_lc)) {
                g_local.ChangeLanguage(g_settings.language);
                RebuildFont_Requested = true;
                SaveThemeSettings();
            }

            ImGui::SameLine();
            if (ImGui::Button("Ui.ForceUpdateLang"_lc)) {
                if (strlen(g_settings.language) > 0) {
                    std::string langFile = "./locales/" + std::string(g_settings.language) + ".lc";
                    if (std::filesystem::exists(langFile)) {
                        std::filesystem::remove(langFile);
                    }
                    g_local.ChangeLanguage(g_settings.language);
                    RebuildFont_Requested = true;
                }
            }
            if (ImGui::IsItemHovered()) {
                ImGui::BeginTooltip();
                ImGui::TextUnformatted("Ui.ForceUpdateLangDesc"_lc);
                ImGui::EndTooltip();
            }

            ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "%s: %s", "Ui.CurrentLang"_lc, "Localization.LanguageName"_lc);
        }

        if (ImGui::CollapsingHeader("Performance & Hardware", ImGuiTreeNodeFlags_DefaultOpen)) {
#ifndef __ANDROID__
            if (ImGui::Checkbox("Ui.LowPerformanceMode"_lc, &g_settings.lowPerformanceMode)) {
                SaveThemeSettings();
            }
            if (ImGui::IsItemHovered()) {
                ImGui::BeginTooltip();
                ImGui::TextUnformatted("Ui.LowPerformanceTooltip"_lc);
                ImGui::EndTooltip();
            }
#endif
#ifdef __ANDROID__
            ImGui::Checkbox("Ui.DisableVibration"_lc, &setting_DisableVibration);
#endif
        }

        if (ImGui::CollapsingHeader("Developer Options", ImGuiTreeNodeFlags_None)) {
            ImGui::TextUnformatted("Ui.InjectionFilePath"_lc);
            ImGui::InputText("##injection_file_path", tempInjectionFilePath, sizeof(tempInjectionFilePath));
            ImGui::SameLine();
            if (ImGui::Button("Ui.Browse"_lc)) {
                showFileDialog = true;
            }

            if (showFileDialog) {
                if (FileDialog::ShowFileOpenDialog("Select Injection File", "Text Files (*.txt){.txt},All Files (*.*){.*}",
                        tempInjectionFilePath, sizeof(tempInjectionFilePath))) {
                    showFileDialog = false;
                }
            }

            if (ImGui::Button("Button.Positive"_lc)) {
                strncpy(g_settings.injectionFilePath, tempInjectionFilePath, sizeof(g_settings.injectionFilePath));
                SaveThemeSettings();
            }
        }
	}
};

UIWindow* MakeThemeWindow() {
	return new ThemeWindow();
}