#pragma once
#include <Ui.hpp>
#include "Binary.h"
#include <iostream>

struct ThemeSettings {
    bool isDarkMode = true;
    char language[30] = "";
    float scale = 1.0f;
    char injectionFilePath[256] = "./hc-inj.txt"; // Default injection file path

    void Write(std::ostream& stm) const {
        Binary::Write(stm, isDarkMode);
        stm.write(language, sizeof(language));
        Binary::Write(stm, scale);
        stm.write(injectionFilePath, sizeof(injectionFilePath));
    }

    void Read(std::istream& stm) {
        Binary::Read(stm, isDarkMode);
        stm.read(language, sizeof(language));
        Binary::Read(stm, scale);
        stm.read(injectionFilePath, sizeof(injectionFilePath));
    }
};

UIWindow* MakeThemeWindow();

void SaveThemeSettings();
void LoadThemeSettings();
const ThemeSettings& GetThemeSettings();