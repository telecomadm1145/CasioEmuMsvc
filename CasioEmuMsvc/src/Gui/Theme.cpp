#include "Theme.h"
#include "FileDialog.hpp"
#include "SysDialog.h"
#include "Ui.hpp"
#include <Gui.h>
#include <Localization.h>
#include <fstream>
#include <string>
#include <vibration.h>
#include "Ext/DiscordRPC.h"

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

		UIHelpers::SectionHeader("Appearance");

		if (ImGui::Button("Ui.DarkMode"_lc)) {
			tm.SetDarkMode();
		}
		ImGui::SameLine();
		if (ImGui::Button("Ui.LightMode"_lc)) {
			tm.SetLightMode();
		}

#ifndef __ANDROID__
		ImGui::SameLine();
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

		float currentFontScale = tm.GetFontScale();
		if (ImGui::SliderFloat("Ui.Scale"_lc, &currentFontScale, 0.5f, 3.0f, "%.2f")) {
			settings.scale = currentFontScale;
			tm.SetFontScale(currentFontScale);
		}
		if (ImGui::Button("Ui.ApplyScale"_lc)) {
			tm.RequestFontRebuild();
			tm.SaveSettings();
		}

		UIHelpers::SectionHeader("Language");

		if (strlen(settings.language) == 0) {
			strncpy(settings.language, g_local.GetCurrentLanguage().c_str(), sizeof(settings.language));
		}

		static const struct {
			const char* code;
			const char* badge;
			const char* displayNameKey;
		} availableLanguages[] = {
			{"en_US", "EN", "Localization.Lang.en_US"},
			{"zh_CN", "ZH", "Localization.Lang.zh_CN"},
			{"ja_JP", "JA", "Localization.Lang.ja_JP"},
			{"vi_VN", "VI", "Localization.Lang.vi_VN"},
			{"ru_RU", "RU", "Localization.Lang.ru_RU"},
			{"ko_KR", "KO", "Localization.Lang.ko_KR"},
			{"fr_FR", "FR", "Localization.Lang.fr_FR"},
			{"de_DE", "DE", "Localization.Lang.de_DE"},
			{"es_ES", "ES", "Localization.Lang.es_ES"}
		};

		std::string currentLangStr(settings.language);
		std::string currentDisplayName = "Localization.LanguageName"_l;
		std::string currentBadge = "??";
		for (const auto& lang : availableLanguages) {
			if (currentLangStr == lang.code) {
				currentBadge = lang.badge;
				currentDisplayName = g_local.Get(lang.displayNameKey);
				break;
			}
		}
		std::string currentLabel = std::string("[") + currentBadge + "] " + currentDisplayName;

		ImGui::SetNextItemWidth(300.0f);
		if (ImGui::BeginCombo("##language_combo", currentLabel.c_str())) {
			for (const auto& lang : availableLanguages) {
				std::string dispName = g_local.Get(lang.displayNameKey);
				std::string itemLabel = std::string("[") + lang.badge + "] " + dispName;
				bool isSelected = (currentLangStr == lang.code);
				if (ImGui::Selectable(itemLabel.c_str(), isSelected)) {
					strncpy(settings.language, lang.code, sizeof(settings.language));
					g_local.ChangeLanguage(settings.language);
					tm.RequestFontRebuild();
					tm.SaveSettings();
				}
				if (isSelected) {
					ImGui::SetItemDefaultFocus();
				}
			}
			ImGui::EndCombo();
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

		UIHelpers::SectionHeader("Background & Injection");

		if (ImGui::Button("UI.ChangeBg"_lc)) {
			SystemDialogs::OpenFileDialog([](std::filesystem::path pth) {
				std::filesystem::copy_file(pth, "background.jpg", std::filesystem::copy_options::overwrite_existing);
			});
			tm.RequestBgReload();
		}

		ImGui::Spacing();
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

		UIHelpers::SectionHeader("Auto-Tint (MD3 Monet)");
		
		if (ImGui::Checkbox("Theme.Tint"_lc, &settings.enableAutoTint)) {
			tm.SaveSettings();
		}

		if (settings.enableAutoTint) {
			ImVec4 seedColor = settings.seedColor;
			if (ImGui::ColorEdit4("Theme.SeedColor"_lc, (float*)&seedColor, ImGuiColorEditFlags_NoAlpha)) {
				tm.SetSeedColor(seedColor);
			}
			if (ImGui::Button("Theme.MD"_lc)) {
#ifndef TEST_BUILD
				seedColor = tm.ExtractDominantColor(bg_txt, renderer);
#endif
				tm.SetSeedColor(seedColor);
			}
		}
		
		UIHelpers::SectionHeader("Integrations");
		if (ImGui::Checkbox("Theme.EnableDiscordRPC"_lc, &settings.enableDiscordRPC)) {
			tm.SaveSettings();
			if (m_emu) {
				DiscordRPC::UpdatePresence(m_emu->ModelDefinition.model_name);
			} else {
				DiscordRPC::UpdatePresence("");
			}
		}

		UIHelpers::SectionHeader("Advanced Style Settings");

		if (ImGui::CollapsingHeader("Advanced Style Editor")) {
			// Edit the unscaled base style
			auto& base = settings.isDarkMode ? settings.igs_dark : settings.igs_light;
			ImGuiStyle ims_backup;
			std::memcpy(&ims_backup, &ImGui::GetStyle(), sizeof(base));
			std::memcpy(&ImGui::GetStyle(), &base, sizeof(base));
			ImGui::ShowStyleEditor();
			std::memcpy(&base, &ImGui::GetStyle(), sizeof(base));
			std::memcpy(&ImGui::GetStyle(), &ims_backup, sizeof(base));
		}

		ImGui::Spacing();
		if (ImGui::Button("Files.Save"_lc)) {
			strncpy(settings.injectionFilePath, tempInjectionFilePath, sizeof(settings.injectionFilePath));
			tm.SaveSettings();
			tm.RequestFontRebuild();
		}
	}
};

UIWindow* MakeThemeWindow() {
	return new ThemeWindow();
}