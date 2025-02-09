#include "PluginApi.h"

extern PluginApi* g_pluginapi;

#ifdef _WIN32
#include <Windows.h>
#include <string>

void LoadPlugins() {
	// Load all plugins in the plugins directory
	WIN32_FIND_DATAA findData;
	HANDLE hFind = FindFirstFileA("CasioEmuMsvc.Plugin.*.dll", &findData);
	if (hFind == INVALID_HANDLE_VALUE) {
		return;
	}
	do {
		HMODULE hModule = LoadLibraryA(findData.cFileName);
		if (hModule) {
			auto load = (PluginLoad)GetProcAddress(hModule, "fPluginLoad");
			if (load) {
				load(g_pluginapi);
			}
		}
	} while (FindNextFileA(hFind, &findData));
	FindClose(hFind);
}
#endif // _WIN32