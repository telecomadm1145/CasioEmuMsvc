// dllmain.cpp : 定义 DLL 应用程序的入口点。
#include "pch.h"

PluginApi* g_api;
class TestWindow :public UIWindow {
public:
	TestWindow() : UIWindow("ExamplePlugin") {

	}
	void RenderCore() override {
		ImGui::Text("This is an example!");
	}
};
extern "C" _declspec(dllexport) void fPluginLoad(PluginApi* api) {
	if (api == 0)
		return;
	g_api = api;
	if (!api->RegisterPlugin("example", "ExamplePlugin", 0))
		return;
	api->AddWindow(new TestWindow());
}

BOOL APIENTRY DllMain(HMODULE hModule,
	DWORD  ul_reason_for_call,
	LPVOID lpReserved
)
{
	switch (ul_reason_for_call)
	{
	case DLL_PROCESS_ATTACH:
		break;
	case DLL_THREAD_ATTACH:
	case DLL_THREAD_DETACH:
	case DLL_PROCESS_DETACH:
		break;
	}
	return TRUE;
}

