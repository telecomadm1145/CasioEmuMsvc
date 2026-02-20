#include "Theme.h"
#include "FileDialog.hpp"
#include "SysDialog.h"
#include "Ui.hpp"
#include <Gui.h>
#include <Localization.h>
#include <fstream>
#include <string>
#include <vibration.h>
extern SDL_Surface* background;
extern SDL_Texture* bg_txt;

class ThemeWindow : public UIWindow {
private:
	bool showFileDialog;
	char tempInjectionFilePath[256];

public:
	ThemeWindow() : UIWindow("Theme"), showFileDialog(false) {
		auto& settings = ThemeManager::Instance().Settings();
		strncpy(tempInjectionFilePath, settings.injectionFilePath, sizeof(tempInjectionFilePath));
	}
	std::once_flag loadFlag;
	void RenderCore() override {
		std::call_once(loadFlag, []() {
			ThemeManager::Instance().LoadSettings();
		});
		auto& tm = ThemeManager::Instance();
		auto& settings = tm.Settings();

		if (ImGui::Button("Ui.DarkMode"_lc)) {
			tm.SetDarkMode();
		}
		ImGui::SameLine();
		if (ImGui::Button("Ui.LightMode"_lc)) {
			tm.SetLightMode();
		}
#ifndef __ANDROID__
		if (ImGui::Checkbox("Ui.LowPerformanceMode"_lc, &settings.lowPerformanceMode)) {
			tm.SaveSettings();
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
		ImGui::InputText("##language_input", settings.language, 30);
		if (ImGui::Button("Ui.ChangeLang"_lc)) {
			g_local.ChangeLanguage(settings.language);
			tm.RequestFontRebuild();
			tm.SaveSettings();
		}
		ImGui::SameLine();
		if (ImGui::Button("Ui.ForceUpdateLang"_lc)) {
			if (strlen(settings.language) > 0) {
				std::string langFile = "./locales/" + std::string(settings.language) + ".lc";
				if (std::filesystem::exists(langFile)) {
					std::filesystem::remove(langFile);
				}
				g_local.ChangeLanguage(settings.language);
				tm.RequestFontRebuild();
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

		float currentFontScale = tm.GetFontScale();
		if (ImGui::SliderFloat("Ui.Scale"_lc, &currentFontScale, 0, 5, "%.2f")) {
			settings.scale = currentFontScale;
			tm.SetFontScale(currentFontScale);
		}
		if (ImGui::Button("Ui.ApplyScale"_lc)) {
			tm.RequestFontRebuild();
			tm.SaveSettings();
		}

		ImGui::Separator();

		ImGui::TextUnformatted("Ui.InjectionFilePath"_lc);

		ImGui::InputText("##injection_file_path", tempInjectionFilePath, sizeof(tempInjectionFilePath));

		ImGui::SameLine();
		if (ImGui::Button("Ui.Browse"_lc)) {
			showFileDialog = true;
		}
		if (ImGui::Button("UI.ChangeBg"_lc)) {
			SystemDialogs::OpenFileDialog([](std::filesystem::path pth) {
				std::filesystem::copy_file(pth, "background.jpg", std::filesystem::copy_options::overwrite_existing);
			});
			tm.RequestBgReload();
		}

		if (showFileDialog) {
			if (FileDialog::ShowFileOpenDialog("Select Injection File", "Text Files (*.txt){.txt},All Files (*.*){.*}",
					tempInjectionFilePath, sizeof(tempInjectionFilePath))) {
				showFileDialog = false;
			}
		}
		ImGui::Separator();

		// Auto-Tint (MD3 Monet)
		if (ImGui::Checkbox("Theme.Tint"_lc, &settings.enableAutoTint)) {
			tm.SaveSettings();
		}

		if (settings.enableAutoTint) {
			ImVec4 seedColor = settings.seedColor;
			if (ImGui::ColorEdit4("Theme.SeedColor"_lc, (float*)&seedColor, ImGuiColorEditFlags_NoAlpha)) {
				tm.SetSeedColor(seedColor);
			}
			if (ImGui::Button("Theme.MD"_lc)) {
				seedColor = tm.ExtractDominantColor(bg_txt, renderer);
				tm.SetSeedColor(seedColor);
			}
		}

		ImGui::Separator();

		// Edit the unscaled base style
		auto& base = settings.isDarkMode ? settings.igs_dark : settings.igs_light;
		ImGuiStyle ims_backup;
		std::memcpy(&ims_backup, &ImGui::GetStyle(), sizeof(base));
		std::memcpy(&ImGui::GetStyle(), &base, sizeof(base));
		ImGui::ShowStyleEditor();
		std::memcpy(&base, &ImGui::GetStyle(), sizeof(base));
		std::memcpy(&ImGui::GetStyle(), &ims_backup, sizeof(base));
		if (ImGui::Button("Files.Save"_lc)) {
			strncpy(settings.injectionFilePath, tempInjectionFilePath, sizeof(settings.injectionFilePath));
			// The base style is already edited by ShowStyleEditor above
			// Just save settings and rebuild to apply scale
			tm.SaveSettings();
			tm.RequestFontRebuild();
		}
	}
};

UIWindow* MakeThemeWindow() {
	return new ThemeWindow();
}