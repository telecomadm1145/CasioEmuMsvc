#ifdef _WIN32
#include <SDL_syswm.h>
#include <Windows.h>
#include <dwmapi.h>
#pragma comment(lib, "dwmapi.lib")
void* GetSDLWindowHandle(SDL_Window* window) {
	SDL_SysWMinfo wmInfo;
	SDL_VERSION(&wmInfo.version);

	if (SDL_GetWindowWMInfo(window, &wmInfo)) {
		return wmInfo.info.win.window;
	}

	return nullptr;
}
void EnableDarkTitleBar(void* hwnd) {
	BOOL value = TRUE;

	DwmSetWindowAttribute(
		(HWND)hwnd,
		DWMWA_USE_IMMERSIVE_DARK_MODE,
		&value,
		sizeof(value));
}
#else
void EnableDarkTitleBar(void* hwnd) {
}
void* GetSDLWindowHandle(struct SDL_Window* window) {
	return nullptr;
}
#endif // _WIN32
