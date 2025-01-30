#include "Ui.hpp"
#include <Localization.h>
#include <Gui.h>
#include "Theme.h"
#include <fstream>
#include <string>

struct ThemeSettings {
    bool isDarkMode = true;
    char language[30] = "";
    float scale = 1.0f;
};

static ThemeSettings g_settings;

void SaveThemeSettings() {
    std::ofstream file("./theme.txt");
    if (file.is_open()) {
        file << g_settings.isDarkMode << std::endl;
        file << g_settings.language << std::endl;
        file << g_settings.scale << std::endl;
        file.close();
    }
}

void LoadThemeSettings() {
    std::ifstream file("./theme.txt");
    if (file.is_open()) {
        file >> g_settings.isDarkMode;
        file >> g_settings.language;
        file >> g_settings.scale;
        file.close();

        if (g_settings.isDarkMode) {
            ImGui::StyleColorsDark();
        } else {
            ImGui::StyleColorsLight();
        }
        if (strlen(g_settings.language) > 0) {
            g_local.ChangeLanguage(g_settings.language);
        }
        RebuildFont_Scale = g_settings.scale;
        RebuildFont_Requested = true;
    }
}

class ThemeWindow : public UIWindow {
public:
    ThemeWindow() : UIWindow("Theme") {
        LoadThemeSettings();
    }

    void RenderCore() override {
        if (ImGui::Button("Ui.DarkMode"_lc)) {
            ImGui::StyleColorsDark();
            g_settings.isDarkMode = true;
            SaveThemeSettings();
        }
        ImGui::SameLine();
        if (ImGui::Button("Ui.LightMode"_lc)) {
            ImGui::StyleColorsLight();
            g_settings.isDarkMode = false;
            SaveThemeSettings();
        }

        ImGui::InputText("##language_input", g_settings.language, 30);
        if (ImGui::Button("Ui.ChangeLang"_lc)) {
            g_local.ChangeLanguage(g_settings.language);
            RebuildFont_Requested = true;
            SaveThemeSettings();
        }

        ImGui::TextUnformatted("Ui.CurrentLang"_lc);
        ImGui::SameLine();
        ImGui::TextUnformatted("Localization.LanguageName"_lc);

        if (ImGui::SliderFloat("Ui.Scale"_lc, &RebuildFont_Scale, 0, 5, "%.2f")) {
            g_settings.scale = RebuildFont_Scale;
        }
        if (ImGui::Button("Ui.ApplyScale"_lc)) {
            RebuildFont_Requested = true;
            SaveThemeSettings();
        }
    }
};

UIWindow* MakeThemeWindow() {
    return new ThemeWindow();
}