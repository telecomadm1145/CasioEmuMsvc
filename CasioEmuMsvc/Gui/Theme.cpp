#include "Ui.hpp"
#include <Localization.h>
#include <Gui.h>
#include "Theme.h"
class ThemeWindow : public UIWindow {
public:
	ThemeWindow() : UIWindow("Theme") {
	}
	void RenderCore() override {
		if (ImGui::Button("Ui.DarkMode"_lc)) {
			ImGui::StyleColorsDark();
		}
		ImGui::SameLine();
		if (ImGui::Button("Ui.LightMode"_lc)) {
			ImGui::StyleColorsLight();
		}
		static char lang_input[30]{};
		ImGui::InputText("##language_input", lang_input, 30);
		if (ImGui::Button("Ui.ChangeLang"_lc)) {
			g_local.ChangeLanguage(lang_input);
			RebuildFont_Requested = true;
		}
		ImGui::TextUnformatted("Ui.CurrentLang"_lc);
		ImGui::SameLine();
		ImGui::TextUnformatted("Localization.LanguageName"_lc);
		ImGui::SliderFloat("Ui.Scale"_lc, &RebuildFont_Scale, 0, 5, "%.2f");
		if (ImGui::Button("Ui.ApplyScale"_lc)) {
			RebuildFont_Requested = true;
		}
	}
};

UIWindow* MakeThemeWindow() {
	return new ThemeWindow();
}
