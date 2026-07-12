#pragma once
#include <SDL.h>
#include "RendererBackend.h"

inline constexpr const char* SCREEN_MIRROR_WINDOW_DATA_KEY = "CasioEmu.ScreenMirror";

class ScreenMirror {
private:
	SDL_Window* mirrorWindow;
	SDL_Renderer* mirrorRenderer;
	int captureWidth;
	int captureHeight;
	Uint32 windowId;
	SDL_Rect displayRect;
	bool isOpen;
	bool watchingEvents;

	static int SDLCALL eventWatch(void* userdata, SDL_Event* event) {
		auto* self = static_cast<ScreenMirror*>(userdata);
		if (!self || !event || !self->isOpen || event->type != SDL_WINDOWEVENT || event->window.windowID != self->windowId) {
			return 0;
		}

		if (event->window.event == SDL_WINDOWEVENT_CLOSE) {
			self->isOpen = false;
		}
		else if (event->window.event == SDL_WINDOWEVENT_SIZE_CHANGED) {
			self->updateDisplayRect(event->window.data1, event->window.data2);
		}
		return 0;
	}

	void updateDisplayRect(int windowWidth, int windowHeight) {
		float aspectRatio = (float)captureWidth / captureHeight;
		float windowRatio = (float)windowWidth / windowHeight;

		if (windowRatio > aspectRatio) {
			displayRect.h = windowHeight;
			displayRect.w = (int)(windowHeight * aspectRatio);
			displayRect.x = (windowWidth - displayRect.w) / 2;
			displayRect.y = 0;
		}
		else {
			displayRect.w = windowWidth;
			displayRect.h = (int)(windowWidth / aspectRatio);
			displayRect.x = 0;
			displayRect.y = (windowHeight - displayRect.h) / 2;
		}
	}

public:
	ScreenMirror(int captureWidth, int captureHeight)
		: mirrorWindow(nullptr), mirrorRenderer(nullptr), captureWidth(captureWidth), captureHeight(captureHeight), windowId(0), isOpen(false), watchingEvents(false) {
	}

	~ScreenMirror() {
		destroy();
	}

	bool create() {
		mirrorWindow = SDL_CreateWindow("Live Mirror",
			SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
			captureWidth, captureHeight,
			SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE);

		if (!mirrorWindow) {
			SDL_Log("Error creating mirror window: %s", SDL_GetError());
			return false;
		}
		windowId = SDL_GetWindowID(mirrorWindow);
		SDL_SetWindowData(mirrorWindow, SCREEN_MIRROR_WINDOW_DATA_KEY, this);

		casioemu::SetPreferredRendererDriverHint();
		mirrorRenderer = SDL_CreateRenderer(mirrorWindow, -1,
			SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);

		if (!mirrorRenderer) {
			SDL_Log("Error creating mirror renderer: %s", SDL_GetError());
			SDL_SetWindowData(mirrorWindow, SCREEN_MIRROR_WINDOW_DATA_KEY, nullptr);
			SDL_DestroyWindow(mirrorWindow);
			mirrorWindow = nullptr;
			return false;
		}

		int windowWidth = captureWidth;
		int windowHeight = captureHeight;
		updateDisplayRect(windowWidth, windowHeight);

		isOpen = true;
		SDL_AddEventWatch(eventWatch, this);
		watchingEvents = true;
		return true;
	}

	void destroy() {
		if (watchingEvents) {
			SDL_DelEventWatch(eventWatch, this);
			watchingEvents = false;
		}
		if (mirrorRenderer) {
			SDL_DestroyRenderer(mirrorRenderer);
			mirrorRenderer = nullptr;
		}
		if (mirrorWindow) {
			SDL_SetWindowData(mirrorWindow, SCREEN_MIRROR_WINDOW_DATA_KEY, nullptr);
			SDL_DestroyWindow(mirrorWindow);
			mirrorWindow = nullptr;
		}
		windowId = 0;
		isOpen = false;
	}

	bool handleEvents() {
		return isOpen;
	}

	SDL_Renderer* renderer() const {
		return mirrorRenderer;
	}

	SDL_Rect contentRect() {
		int windowWidth = 0, windowHeight = 0;
		SDL_GetWindowSize(mirrorWindow, &windowWidth, &windowHeight);
		updateDisplayRect(windowWidth, windowHeight);
		return displayRect;
	}

	void clear(const SDL_Color& colour) {
		if (!isOpen)
			return;

		SDL_SetRenderDrawColor(mirrorRenderer, colour.r, colour.g, colour.b, colour.a);
		SDL_RenderClear(mirrorRenderer);
	}

	void present() {
		if (!isOpen)
			return;
		SDL_RenderPresent(mirrorRenderer);
	}

	bool isAlive() const { return isOpen; }
};
