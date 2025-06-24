#include "Config.hpp"
#include "Ui.hpp"
#include "imgui_impl_sdl2.h"

#include "Emulator.hpp"
#include "Logger.hpp"
#include "SDL_events.h"
#include "SDL_keyboard.h"
#include "SDL_mouse.h"
#include "SDL_video.h"
#include <SDL.h>
#include <SDL_image.h>
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <iterator>
#include <map>
#include <mutex>
#include <ostream>
#include <string>
#include <thread>
#include "Localization.h"
#if _WIN32
#include <Windows.h>
#pragma comment(lib, "winmm.lib")
#endif

#ifdef __ANDROID__

#include <unistd.h>

#endif

#include "StartupUi/StartupUi.h"
#include <Gui.h>
#include <Plugin/PluginMan.h>

using namespace casioemu;

int main(int argc, char* argv[]) {
#ifdef _WIN32
	SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE);
	timeBeginPeriod(1);
	SetConsoleCP(65001); // Set to UTF8
	SetConsoleOutputCP(65001);
	CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
#endif //  _WIN32
#ifdef __ANDROID__
	chdir(SDL_AndroidGetExternalStoragePath());
#endif
	g_local.Load();

	std::map<std::string, std::string> argv_map;
	for (int ix = 1; ix != argc; ++ix) {
		std::string key, value;
		char* eq_pos = strchr(argv[ix], '=');
		if (eq_pos) {
			key = std::string(argv[ix], eq_pos);
			value = eq_pos + 1;
		}
		else {
			key = "model";
			value = argv[ix];
		}

		if (argv_map.find(key) == argv_map.end())
			argv_map[key] = value;
		else
			logger::Info("[argv][Info] #%i: key '%s' already set\n", ix, key.c_str());
	}
	bool headless = argv_map.find("headless") != argv_map.end();

	int sdlFlags = SDL_INIT_VIDEO | SDL_INIT_TIMER;
	if (SDL_Init(sdlFlags) != 0)
		PANIC("SDL_Init failed: %s\n", SDL_GetError());

	int imgFlags = IMG_INIT_PNG;
	if (IMG_Init(imgFlags) != imgFlags)
		PANIC("IMG_Init failed: %s\n", IMG_GetError());
	if (headless && argv_map["model"].empty()) {
		PANIC("No model path supplied.\n");
	}
	if (argv_map["model"].empty()) {
		auto s = sui_loop();
		argv_map["model"] = std::move(s);
		if (argv_map["model"].empty())
			return -1;
	}

	Emulator emulator(argv_map);
	m_emu = &emulator;

	// static std::atomic<bool> running(true);

	bool guiCreated = false;
	auto frame_event = SDL_RegisterEvents(1);
	bool busy = false;
	std::thread t3([&]() {
		SDL_Event se{};
		se.type = frame_event;
		se.user.windowID = SDL_GetWindowID(emulator.window);
		while (1) {
			if (!busy)
				SDL_PushEvent(&se);
#ifdef __ANDROID__
			SDL_Delay(40);
#else
			SDL_Delay(1);
#endif
		}
	});
	t3.detach();
#ifdef DBG
	test_gui(&guiCreated, emulator.window, emulator.renderer);
#endif
	SDL_Surface* background = IMG_Load("background.jpg");
	SDL_Texture* bg_txt = 0;
	if (background) {
		bg_txt = SDL_CreateTextureFromSurface(renderer, background);
	}

	SDL_ShowWindow(emulator.window);

	struct TouchState {
		bool touching = false;
		float startX = 0.0f;
		float startY = 0.0f;
		float currentX = 0.0f;
		float currentY = 0.0f;
		Uint32 startTime = 0;
		int fingerId = -1; // 用于区分多点触摸

		bool dragging = false;
	};

	// 在文件开头添加结构体和缓冲区定义
	struct TouchSample {
		float x, y;
		Uint32 time;
	};

	const int TRAIL_BUFFER_SIZE = 512;
	struct TouchTrail {
		TouchSample samples[TRAIL_BUFFER_SIZE]{};
		int current_index = 0;
		int count = 0;
	};

	TouchTrail trail1, trail2;

	// 在主循环渲染部分添加（在SDL_RenderPresent之前）：
	const Uint32 TRAIL_DURATION = 500; // 轨迹持续500ms

	TouchState touchState;
	TouchState touchState2; // 用于第二个手指

	const Uint32 LONG_PRESS_DELAY = 500;		 // 长按延时（毫秒）
	const float DOUBLE_TAP_MAX_DELAY = 300.0f;	 // 双击最大时间间隔 (毫秒)
	const float DOUBLE_TAP_MAX_DISTANCE = 20.0f; // 双击最大距离 (像素)
	static Uint32 lastTapTime = 0;
	static float lastTapX = 0;
	static float lastTapY = 0;

#ifdef _WIN32
    LoadPlugins();
#endif

	while (emulator.Running()) {
		SDL_Event event{};
		busy = false;
		if (!SDL_PollEvent(&event))
			continue;
		busy = true;
		if (event.type == frame_event) {
			SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
			SDL_RenderClear(renderer);
			if (bg_txt) {
				int w, h;
				SDL_GetWindowSize(window, &w, &h);
				int bg_w, bg_h;
				SDL_QueryTexture(bg_txt, NULL, NULL, &bg_w, &bg_h);

				float window_aspect = (float)w / h;
				float bg_aspect = (float)bg_w / bg_h;

				SDL_Rect dst_rect;
				if (window_aspect > bg_aspect) {
					dst_rect.w = w;
					dst_rect.h = (int)(w / bg_aspect);
					dst_rect.x = 0;
					dst_rect.y = (h - dst_rect.h) / 2;
				}
				else {
					dst_rect.h = h;
					dst_rect.w = (int)(h * bg_aspect);
					dst_rect.x = (w - dst_rect.w) / 2;
					dst_rect.y = 0;
				}

				SDL_RenderCopy(renderer, bg_txt, NULL, &dst_rect);
			}
			SDL_SetRenderDrawColor(renderer, 0, 0, 0, 20);
			SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
			SDL_RenderFillRect(renderer, 0);
#ifdef SINGLE_WINDOW
			emulator.Frame();
			gui_loop();

			if (!touchState.touching) {
				// Set color for touch indicator
				SDL_SetRenderDrawColor(renderer, 255, 0, 0, 255);

				// Draw horizontal line of the cross
				SDL_RenderDrawLine(renderer,
					touchState.currentX - 10, touchState.currentY,
					touchState.currentX + 10, touchState.currentY);

				// Draw vertical line of the cross
				SDL_RenderDrawLine(renderer,
					touchState.currentX, touchState.currentY - 10,
					touchState.currentX, touchState.currentY + 10);
			}

			if (!touchState2.touching) {
				// Set different color for second touch
				SDL_SetRenderDrawColor(renderer, 0, 255, 0, 255);

				// Draw horizontal line of the cross
				SDL_RenderDrawLine(renderer,
					touchState2.currentX - 10, touchState2.currentY,
					touchState2.currentX + 10, touchState2.currentY);

				// Draw vertical line of the cross
				SDL_RenderDrawLine(renderer,
					touchState2.currentX, touchState2.currentY - 10,
					touchState2.currentX, touchState2.currentY + 10);
			}
			// 渲染第一个触摸轨迹
			Uint32 current_time = SDL_GetTicks();
			SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
			for (int i = 0; i < TRAIL_BUFFER_SIZE; i++) {
				int idx = (trail1.current_index - 1 - i + TRAIL_BUFFER_SIZE) % TRAIL_BUFFER_SIZE;
				TouchSample& sample = trail1.samples[idx];

				Uint32 age = current_time - sample.time;
				if (age > TRAIL_DURATION)
					continue;

				float radius = 50.0f * age / TRAIL_DURATION + 5.f;
				SDL_SetRenderDrawColor(renderer, 255, 255, 255, 120 - 120 * age / TRAIL_DURATION);
				SDL_Rect rect = {
					(int)(sample.x - radius / 2),
					(int)(sample.y - radius / 2),
					(int)radius,
					(int)radius};
				SDL_RenderFillRect(renderer, &rect);
			}

			// 渲染第二个触摸轨迹
			for (int i = 0; i < TRAIL_BUFFER_SIZE; i++) {
				int idx = (trail2.current_index - 1 - i + TRAIL_BUFFER_SIZE) % TRAIL_BUFFER_SIZE;
				TouchSample& sample = trail2.samples[idx];

				Uint32 age = current_time - sample.time;
				if (age > TRAIL_DURATION)
					continue;

				float radius = 50.0f * age / TRAIL_DURATION + 5.f;
				SDL_SetRenderDrawColor(renderer, 255, 255, 255, 120 - 120 * age / TRAIL_DURATION);
				SDL_Rect rect = {
					(int)(sample.x - radius / 2),
					(int)(sample.y - radius / 2),
					(int)radius,
					(int)radius};
				SDL_RenderFillRect(renderer, &rect);
			}

			SDL_RenderPresent(emulator.renderer);
#else
			gui_loop();
			emulator.Frame();
			SDL_RenderPresent(emulator.renderer);
#endif
			if (RebuildFont_Requested) {
				RebuildFont(RebuildFont_Scale);
				if (RebuildFont_Scale != 0) {
					ImGuiStyle igs = ImGuiStyle();
					ImGui::StyleColorsDark(&igs);
					ImGuiStyle& style = igs;
					style.WindowRounding = 4.0f;
					style.Colors[ImGuiCol_WindowBg].w = 0.9f;
					style.FrameRounding = 4.0f;
					style.ScaleAllSizes(RebuildFont_Scale);
					ImGui::GetStyle() = igs;
				}
				ImGui_ImplSDLRenderer2_DestroyDeviceObjects();
				RebuildFont_Requested = 0;
			}
			while (SDL_PollEvent(&event)) {
				if (event.type != frame_event)
					goto hld;
			}
			continue;
		}

	hld:
		int wid, hei;
		SDL_GetWindowSize(window, &wid, &hei);
		switch (event.type) {
		case SDL_WINDOWEVENT:
			switch (event.window.event) {
			case SDL_WINDOWEVENT_CLOSE:
				emulator.Shutdown();
				std::exit(0);
				break;
			case SDL_WINDOWEVENT_RESIZED:
				break;
			}
			break;
#ifdef __ANDROID__
		case SDL_FINGERDOWN:
			// Always send FINGERDOWN to emulator first
			// The emulator's Keyboard::UIEvent should handle it if it's on a button
			// We need a way for Keyboard::UIEvent to indicate if it consumed the event
			// For now, we'll let both ImGui and emulator process it if ImGui is active.
			// This might lead to double presses if not handled carefully in Keyboard.cpp (next step)
			emulator.UIEvent(event); // Pass raw finger event to emulator

			// Existing ImGui touch processing (simplified for now)
			if (!touchState.touching) {
				touchState.touching = true;
				touchState.startX = event.tfinger.x * wid;
				touchState.startY = event.tfinger.y * hei;
				touchState.currentX = touchState.startX;
				touchState.currentY = touchState.startY;
				touchState.startTime = SDL_GetTicks();
				touchState.fingerId = event.tfinger.fingerId;
				touchState.dragging = false;
			} else if (!touchState2.touching) {
				touchState2.touching = true;
				touchState2.startX = event.tfinger.x * wid;
				touchState2.startY = event.tfinger.y * hei;
				touchState2.currentX = touchState2.startX;
				touchState2.currentY = touchState2.startY;
				touchState2.fingerId = event.tfinger.fingerId;
				touchState2.dragging = false;
				// ImGui specific interaction for second touch if needed
				SDL_Event wheelEvent; // Example: pinch might still be mouse wheel for ImGui
				SDL_memset(&wheelEvent, 0, sizeof(wheelEvent));
				wheelEvent.type = SDL_MOUSEMOTION; // Simulating mouse over for ImGui context
				wheelEvent.motion.x = touchState.currentX;
				wheelEvent.motion.y = touchState.currentY;
				if (guiCreated) ImGui_ImplSDL2_ProcessEvent(&wheelEvent);
			}
			break;
		case SDL_FINGERUP:
			// Always send FINGERUP to emulator first
			emulator.UIEvent(event); // Pass raw finger event to emulator

			// Existing ImGui touch processing (simplified)
			if (touchState.dragging && touchState.fingerId == event.tfinger.fingerId) {
				float endX = event.tfinger.x * wid;
				float endY = event.tfinger.y * hei;
				SDL_Event motionEvent;
				SDL_memset(&motionEvent, 0, sizeof(motionEvent));
				motionEvent.type = SDL_MOUSEBUTTONUP;
				motionEvent.button.button = SDL_BUTTON_LEFT;
				motionEvent.button.x = endX;
				motionEvent.button.y = endY;
				if (guiCreated) ImGui_ImplSDL2_ProcessEvent(&motionEvent);
				touchState.dragging = false;
				touchState.currentX = endX;
				touchState.currentY = endY;
			}
			// Handle tap/long press for ImGui if necessary, only if emulator didn't handle it
			// This part needs careful thought: if emulator handled it, we might not want ImGui reaction
			if (touchState.touching && touchState.fingerId == event.tfinger.fingerId) {
				// Potentially check if ImGui should react (e.g., if not over an emulator button)
				// For now, keep ImGui logic, but it might conflict or be redundant
				float endX = event.tfinger.x * wid;
				float endY = event.tfinger.y * hei;
				Uint32 endTime = SDL_GetTicks();
				touchState.currentX = endX;
				touchState.currentY = endY;

				bool is_tap_for_imgui = (endTime - touchState.startTime < LONG_PRESS_DELAY) &&
				                        (std::hypot(endX - touchState.startX, endY - touchState.startY) < 10.0f);

				if (is_tap_for_imgui) {
					// Simulate left click for ImGui
					SDL_Event clickEventDown;
					SDL_memset(&clickEventDown, 0, sizeof(clickEventDown));
					clickEventDown.type = SDL_MOUSEMOTION; // Ensure ImGui knows mouse position
					clickEventDown.motion.x = endX;
					clickEventDown.motion.y = endY;
					if (guiCreated) ImGui_ImplSDL2_ProcessEvent(&clickEventDown);

					SDL_memset(&clickEventDown, 0, sizeof(clickEventDown));
					clickEventDown.type = SDL_MOUSEBUTTONDOWN;
					clickEventDown.button.button = SDL_BUTTON_LEFT;
					clickEventDown.button.x = endX;
					clickEventDown.button.y = endY;
					if (guiCreated) ImGui_ImplSDL2_ProcessEvent(&clickEventDown);

					SDL_Event clickEventUp;
					SDL_memset(&clickEventUp, 0, sizeof(clickEventUp));
					clickEventUp.type = SDL_MOUSEBUTTONUP;
					clickEventUp.button.button = SDL_BUTTON_LEFT;
					clickEventUp.button.x = endX;
					clickEventUp.button.y = endY;
					if (guiCreated) ImGui_ImplSDL2_ProcessEvent(&clickEventUp);
					lastTapTime = endTime; // For double tap logic
					lastTapX = endX;
					lastTapY = endY;
				} else if (endTime - touchState.startTime >= LONG_PRESS_DELAY) { // Long press for ImGui (right click)
                    SDL_Event longPressEvent;
					SDL_memset(&longPressEvent, 0, sizeof(longPressEvent));
					longPressEvent.type = SDL_MOUSEMOTION;
					longPressEvent.motion.x = endX;
					longPressEvent.motion.y = endY;
					if (guiCreated) ImGui_ImplSDL2_ProcessEvent(&longPressEvent);

					SDL_memset(&longPressEvent, 0, sizeof(longPressEvent));
					longPressEvent.type = SDL_MOUSEBUTTONDOWN;
					longPressEvent.button.button = SDL_BUTTON_RIGHT; // Simulate Right Click
					longPressEvent.button.x = endX;
					longPressEvent.button.y = endY;
					if (guiCreated) ImGui_ImplSDL2_ProcessEvent(&longPressEvent);

					SDL_memset(&longPressEvent, 0, sizeof(longPressEvent));
					longPressEvent.type = SDL_MOUSEBUTTONUP;
					longPressEvent.button.button = SDL_BUTTON_RIGHT;
					longPressEvent.button.x = endX;
					longPressEvent.button.y = endY;
					if (guiCreated) ImGui_ImplSDL2_ProcessEvent(&longPressEvent);
                }
				touchState.touching = false;
			}

			if (touchState2.dragging && touchState2.fingerId == event.tfinger.fingerId) {
                // Similar ImGui drag release for second touch if it was dragging
                 float endX = event.tfinger.x * wid;
				float endY = event.tfinger.y * hei;
				SDL_Event motionEvent;
				SDL_memset(&motionEvent, 0, sizeof(motionEvent));
				motionEvent.type = SDL_MOUSEBUTTONUP;
				motionEvent.button.button = SDL_BUTTON_LEFT; // Assuming second touch also drags with left
				motionEvent.button.x = endX;
				motionEvent.button.y = endY;
				if (guiCreated) ImGui_ImplSDL2_ProcessEvent(&motionEvent);
				touchState2.dragging = false;
				touchState2.currentX = endX;
				touchState2.currentY = endY;
            }
			if (touchState2.touching && touchState2.fingerId == event.tfinger.fingerId) {
				touchState2.touching = false;
				// ImGui specific release for second touch if needed (e.g. if it was a tap/longpress)
			}
			break;

		case SDL_FINGERMOTION:
            // Send FINGERMOTION to emulator. It might use it for something (e.g. dragging an item in emu)
            // Or it might ignore it if it only cares about down/up on buttons.
            emulator.UIEvent(event);

            // Existing ImGui gesture logic (drag, pinch-zoom)
			if (touchState.touching && !touchState2.touching &&
				touchState.fingerId == event.tfinger.fingerId) { // Single finger drag for ImGui
				float currentX = event.tfinger.x * wid;
				float currentY = event.tfinger.y * hei;
				float deltaX = currentX - touchState.startX;
				float deltaY = currentY - touchState.startY;
				touchState.currentX = currentX;
				touchState.currentY = currentY;

				if ((deltaX * deltaX + deltaY * deltaY) > 1.f) { // Threshold for dragging
					if (!touchState.dragging) {
						SDL_Event motionEvent;
						SDL_memset(&motionEvent, 0, sizeof(motionEvent));
						motionEvent.type = SDL_MOUSEBUTTONDOWN;
						motionEvent.button.button = SDL_BUTTON_LEFT;
						motionEvent.button.x = currentX;
						motionEvent.button.y = currentY;
						if (guiCreated) ImGui_ImplSDL2_ProcessEvent(&motionEvent);
						touchState.dragging = true;
					}
					SDL_Event motionEvent;
					SDL_memset(&motionEvent, 0, sizeof(motionEvent));
					motionEvent.type = SDL_MOUSEMOTION;
					motionEvent.motion.x = currentX;
					motionEvent.motion.y = currentY;
					motionEvent.motion.state = SDL_BUTTON_LMASK;
					if (guiCreated) ImGui_ImplSDL2_ProcessEvent(&motionEvent);
				}
			} else if (touchState.touching && !touchState.dragging && touchState2.touching &&
					   touchState.fingerId != touchState2.fingerId &&
					   (touchState.fingerId == event.tfinger.fingerId || touchState2.fingerId == event.tfinger.fingerId) ) { // Pinch-zoom for ImGui
				
                // Update the correct finger's current position
                if(touchState.fingerId == event.tfinger.fingerId) {
                    touchState.currentX = event.tfinger.x * wid;
				    touchState.currentY = event.tfinger.y * hei;
                } else if (touchState2.fingerId == event.tfinger.fingerId) {
                    touchState2.currentX = event.tfinger.x * wid;
				    touchState2.currentY = event.tfinger.y * hei;
                }

                // Simplified pinch-zoom: use distance between fingers for wheel event
                // This part of the original code was a bit complex and might need more refinement
                // For now, let's assume a simple pinch based on Y distance change of one finger relative to other's start
                float currentY_finger1 = touchState.currentY;
                float startY_finger2 = touchState2.startY; // or currentY if we track delta between current positions

                // This needs a better pinch detection logic. The original was comparing one finger's current Y to the other's start Y.
                // A more common way is to track the distance between touchState.current and touchState2.current over time.
                // For now, this is a placeholder for more robust pinch logic for ImGui.
                // float delta = (touchState.currentY - touchState2.currentY) - (touchState.startY - touchState2.startY); // Change in distance
                // Simplified: if finger1 moved, compare its Y to finger2's Y
                float deltaY_for_wheel = 0;
                if (touchState.fingerId == event.tfinger.fingerId) { // finger1 moved
                    deltaY_for_wheel = (touchState.currentY - touchState.startY) - (touchState2.currentY - touchState2.startY);
                } else { // finger2 moved
                     deltaY_for_wheel = (touchState2.currentY - touchState2.startY) - (touchState.currentY - touchState.startY);
                }


				if (std::abs(deltaY_for_wheel) > 1.0f) { // Threshold for wheel event
					SDL_Event wheelEvent;
					SDL_memset(&wheelEvent, 0, sizeof(wheelEvent));
					wheelEvent.type = SDL_MOUSEWHEEL;
					wheelEvent.wheel.preciseY = deltaY_for_wheel / 100; // Adjust sensitivity
					wheelEvent.wheel.mouseX = (touchState.currentX + touchState2.currentX) / 2; // Midpoint
					wheelEvent.wheel.mouseY = (touchState.currentY + touchState2.currentY) / 2;
					if (guiCreated) ImGui_ImplSDL2_ProcessEvent(&wheelEvent);
                    // Update startY to avoid continuous wheel events without further motion
                    if(touchState.fingerId == event.tfinger.fingerId) touchState.startY = touchState.currentY;
                    if(touchState2.fingerId == event.tfinger.fingerId) touchState2.startY = touchState2.currentY;
				}
			}
            // Update touch trails
			if (touchState.touching && touchState.fingerId == event.tfinger.fingerId) {
				touchState.currentX = event.tfinger.x * wid;
				touchState.currentY = event.tfinger.y * hei;
				trail1.samples[trail1.current_index] = { touchState.currentX, touchState.currentY, SDL_GetTicks()};
				trail1.current_index = (trail1.current_index + 1) % TRAIL_BUFFER_SIZE;
			}
			if (touchState2.touching && touchState2.fingerId == event.tfinger.fingerId) {
				touchState2.currentX = event.tfinger.x * wid;
				touchState2.currentY = event.tfinger.y * hei;
				trail2.samples[trail2.current_index] = { touchState2.currentX, touchState2.currentY, SDL_GetTicks()};
				trail2.current_index = (trail2.current_index + 1) % TRAIL_BUFFER_SIZE;
			}
			break;
#else // Not __ANDROID__
		case SDL_MOUSEBUTTONDOWN:
		case SDL_MOUSEBUTTONUP:
		case SDL_MOUSEMOTION:
			// Standard mouse handling for non-Android builds
			// ImGui processes it, then if not captured, emulator processes it.
#endif
		case SDL_KEYDOWN:
		case SDL_KEYUP:
		case SDL_TEXTINPUT:
		case SDL_MOUSEWHEEL:
#ifdef SINGLE_WINDOW
			if (guiCreated) ImGui_ImplSDL2_ProcessEvent(&event);
			// For non-Android, or for non-finger events on Android:
			// If ImGui wants mouse/keyboard, it consumes it.
			if (event.type == SDL_MOUSEBUTTONDOWN || event.type == SDL_MOUSEBUTTONUP || event.type == SDL_MOUSEMOTION || event.type == SDL_MOUSEWHEEL) {
				if (guiCreated && ImGui::GetIO().WantCaptureMouse) {
					break; // ImGui captured mouse
				}
			} else if (event.type == SDL_KEYDOWN || event.type == SDL_KEYUP || event.type == SDL_TEXTINPUT) {
				if (guiCreated && ImGui::GetIO().WantCaptureKeyboard) {
					break; // ImGui captured keyboard
				}
			}
			// If not captured by ImGui, or not a mouse/keyboard event ImGui cares about, pass to emulator
			emulator.UIEvent(event);
			break;
#else // Not SINGLE_WINDOW ( предполагает отдельные окна для ImGui и эмулятора)
			if ((SDL_GetKeyboardFocus() != emulator.window) && guiCreated) {
				ImGui_ImplSDL2_ProcessEvent(&event); // ImGui gets event if emulator window not focused
				break;
			}
			// If emulator window is focused, or no separate GUI window, emulator gets the event.
			// (This part of the #else might need more context on how SINGLE_WINDOW affects focus)
			emulator.UIEvent(event);
			break;
#endif
		default:
			// Other events directly to emulator
			emulator.UIEvent(event);
			break;
		}
	}
	return 0;
};