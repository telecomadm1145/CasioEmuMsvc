#pragma once
#include <Ui.hpp>
#include "Binary.h"
#include <iostream>

struct ThemeSettings {
    bool isDarkMode = true;
	ImGuiStyle igs_light = ImGuiStyle();
	ImGuiStyle igs_dark = ImGuiStyle();
    char language[30] = "";
    float scale = 1.0f;
    char injectionFilePath[256] = "./hc-inj.txt"; // Default injection file path
    bool lowPerformanceMode = false;

    void Write(std::ostream& stm) const {
        Binary::Write(stm, isDarkMode);
        stm.write(language, sizeof(language));
        Binary::Write(stm, scale);
        stm.write(injectionFilePath, sizeof(injectionFilePath));
		Binary::Write(stm, lowPerformanceMode);
		Binary::Write(stm, igs_light);
		Binary::Write(stm, igs_dark);
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
    }
};

UIWindow* MakeThemeWindow();

void SaveThemeSettings();
void LoadThemeSettings();
const ThemeSettings& GetThemeSettings();