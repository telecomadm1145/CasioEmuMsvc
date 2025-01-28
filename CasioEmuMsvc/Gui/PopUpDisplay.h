#pragma once
class ScreenMirror {
private:
	SDL_Window* mirrorWindow;
	SDL_Renderer* mirrorRenderer;
	SDL_Texture* mirrorTexture;
	int captureWidth;
	int captureHeight;
	SDL_Rect displayRect;
	bool isOpen;

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
		: captureWidth(captureWidth), captureHeight(captureHeight), isOpen(false), mirrorWindow(nullptr), mirrorRenderer(nullptr), mirrorTexture(nullptr) {
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

		mirrorRenderer = SDL_CreateRenderer(mirrorWindow, -1,
			SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);

		if (!mirrorRenderer) {
			SDL_Log("Error creating mirror renderer: %s", SDL_GetError());
			SDL_DestroyWindow(mirrorWindow);
			mirrorWindow = nullptr;
			return false;
		}

		mirrorTexture = SDL_CreateTexture(mirrorRenderer,
			SDL_PIXELFORMAT_RGBA32,
			SDL_TEXTUREACCESS_STREAMING,
			captureWidth, captureHeight);

		if (!mirrorTexture) {
			SDL_Log("Error creating mirror texture: %s", SDL_GetError());
			SDL_DestroyRenderer(mirrorRenderer);
			SDL_DestroyWindow(mirrorWindow);
			mirrorRenderer = nullptr;
			mirrorWindow = nullptr;
			return false;
		}

		int windowWidth = captureWidth;
		int windowHeight = captureHeight;
		updateDisplayRect(windowWidth, windowHeight);

		isOpen = true;
		return true;
	}

	void destroy() {
		if (mirrorTexture) {
			SDL_DestroyTexture(mirrorTexture);
			mirrorTexture = nullptr;
		}
		if (mirrorRenderer) {
			SDL_DestroyRenderer(mirrorRenderer);
			mirrorRenderer = nullptr;
		}
		if (mirrorWindow) {
			SDL_DestroyWindow(mirrorWindow);
			mirrorWindow = nullptr;
		}
		isOpen = false;
	}

	bool handleEvents() {
		if (!isOpen)
			return false;

		SDL_Event event;
		while (SDL_PollEvent(&event)) {
			switch (event.type) {
			case SDL_QUIT:
				isOpen = false;
				break;
			case SDL_WINDOWEVENT:
				if (event.window.event == SDL_WINDOWEVENT_CLOSE) {
					isOpen = false;
				}
				else if (event.window.event == SDL_WINDOWEVENT_SIZE_CHANGED) {
					updateDisplayRect(event.window.data1, event.window.data2);
				}
				break;
			}
		}
		return isOpen;
	}

	void update(void* pixels, int pitch) {
		if (!isOpen)
			return;

		SDL_UpdateTexture(mirrorTexture, nullptr, pixels, pitch);
		SDL_SetRenderDrawColor(mirrorRenderer, 0, 0, 0, 255);
		SDL_RenderClear(mirrorRenderer);
		int windowWidth = 0, windowHeight = 0;
		SDL_GetWindowSize(mirrorWindow, &windowWidth, &windowHeight);
		updateDisplayRect(windowWidth, windowHeight);
		SDL_RenderCopy(mirrorRenderer, mirrorTexture, nullptr, &displayRect);
		SDL_RenderPresent(mirrorRenderer);
	}

	bool isAlive() const { return isOpen; }
};