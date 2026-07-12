#pragma once

#include <SDL.h>

namespace casioemu {
	inline const char* PreferredAcceleratedRendererDriver() {
#if defined(__ANDROID__) || defined(__EMSCRIPTEN__)
		return "opengles2";
#else
		return "opengl";
#endif
	}

	inline void SetPreferredRendererDriverHint(bool respect_existing_hint = true) {
		if (respect_existing_hint && SDL_GetHint(SDL_HINT_RENDER_DRIVER)) {
			return;
		}
		SDL_SetHint(SDL_HINT_RENDER_DRIVER, PreferredAcceleratedRendererDriver());
	}
}
