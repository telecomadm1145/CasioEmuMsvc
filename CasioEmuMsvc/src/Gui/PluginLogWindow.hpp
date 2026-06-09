#pragma once
#include "Ui.hpp"
#include <string>

extern std::string g_PluginLoadLog;

class PluginLogWindow : public UIWindow {
public:
    PluginLogWindow();
    void RenderCore() override;
};