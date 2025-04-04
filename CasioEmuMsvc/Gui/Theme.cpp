#include "Ui.hpp"
#include <Localization.h>
#include <Gui.h>
#include "Theme.h"
#include <fstream>
#include <string>
#include "FileDialog.hpp"

static ThemeSettings g_settings;

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
        
        ImGui::Separator();

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
};

UIWindow* MakeThemeWindow() {
    return new ThemeWindow();
}