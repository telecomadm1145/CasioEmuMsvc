#pragma once
#include <string>
#include <vector>

struct PluginInfo {
    std::string id;
    std::string name;
    std::string version;
    std::string author;
    std::string desc;
};

extern std::string g_PluginLoadLog;
extern std::vector<PluginInfo> g_loadedPlugins;
void LoadPlugins();