#include <SDL_syswm.h>
void* GetSDLWindowHandle(SDL_Window* window) {
	SDL_SysWMinfo wmInfo;
	SDL_VERSION(&wmInfo.version); // ±ÿ–Î…Ë÷√∞Ê±æ£°

	if (SDL_GetWindowWMInfo(window, &wmInfo)) {
		return wmInfo.info.win.window;
	}

	return nullptr;
}
#ifdef _WIN32
#include <Windows.h>
#include <dwmapi.h>
#pragma comment(lib, "dwmapi.lib")

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
#endif // _WIN32