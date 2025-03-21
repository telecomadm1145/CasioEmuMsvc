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
					touchState.startX - 10, touchState.startY,
					touchState.startX + 10, touchState.startY);

				// Draw vertical line of the cross
				SDL_RenderDrawLine(renderer,
					touchState.startX, touchState.startY - 10,
					touchState.startX, touchState.startY + 10);
			}

			if (!touchState2.touching) {
				// Set different color for second touch
				SDL_SetRenderDrawColor(renderer, 0, 255, 0, 255);

				// Draw horizontal line of the cross
				SDL_RenderDrawLine(renderer,
					touchState2.startX - 10, touchState2.startY,
					touchState2.startX + 10, touchState2.startY);

				// Draw vertical line of the cross
				SDL_RenderDrawLine(renderer,
					touchState2.startX, touchState2.startY - 10,
					touchState2.startX, touchState2.startY + 10);
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
			if (!touchState.touching) {
				touchState.touching = true;
				touchState.startX = event.tfinger.x * wid; // 转换为窗口坐标
				touchState.startY = event.tfinger.y * hei;
				touchState.startTime = SDL_GetTicks();
				touchState.fingerId = event.tfinger.fingerId;
			}
			else if (!touchState2.touching) {
				touchState2.touching = true;
				touchState2.startX = event.tfinger.x * wid;
				touchState2.startY = event.tfinger.y * hei;
				touchState2.fingerId = event.tfinger.fingerId;
				SDL_Event wheelEvent;
				SDL_memset(&wheelEvent, 0, sizeof(wheelEvent));
				wheelEvent.type = SDL_MOUSEMOTION;
				// wheelEvent.button.button = SDL_BUTTON_LEFT;
				wheelEvent.motion.x = touchState.startX;
				wheelEvent.motion.y = touchState.startY;
				ImGui_ImplSDL2_ProcessEvent(&wheelEvent);
			}
			break;
		case SDL_FINGERUP:
			if (touchState.dragging && touchState.fingerId == event.tfinger.fingerId) {
				SDL_Event motionEvent;
				SDL_memset(&motionEvent, 0, sizeof(motionEvent));
				motionEvent.type = SDL_MOUSEBUTTONUP;
				motionEvent.button.button = SDL_BUTTON_LEFT;
				motionEvent.motion.x = touchState.startX;
				motionEvent.motion.y = touchState.startY;
				ImGui_ImplSDL2_ProcessEvent(&motionEvent);
				touchState.dragging = false;
			}
			if (touchState2.dragging && touchState2.fingerId == event.tfinger.fingerId) {
				SDL_Event motionEvent;
				SDL_memset(&motionEvent, 0, sizeof(motionEvent));
				motionEvent.type = SDL_MOUSEBUTTONUP;
				motionEvent.button.button = SDL_BUTTON_LEFT;
				motionEvent.motion.x = touchState.startX;
				motionEvent.motion.y = touchState.startY;
				ImGui_ImplSDL2_ProcessEvent(&motionEvent);
				touchState2.dragging = false;
			}
			if (touchState.touching && touchState.fingerId == event.tfinger.fingerId) {

				float endX = event.tfinger.x * wid;
				float endY = event.tfinger.y * hei;
				Uint32 endTime = SDL_GetTicks();

				if (endTime - touchState.startTime < LONG_PRESS_DELAY) { // 单击
					float dist = std::hypot(endX - touchState.startX, endY - touchState.startY);
					if (dist < 10.0f) { // 避免滑动时触发单击
						SDL_Event clickEventDown;
						SDL_memset(&clickEventDown, 0, sizeof(clickEventDown));
						clickEventDown.type = SDL_MOUSEMOTION;
						clickEventDown.button.button = SDL_BUTTON_LEFT;
						clickEventDown.button.x = endX;
						clickEventDown.button.y = endY;
						ImGui_ImplSDL2_ProcessEvent(&clickEventDown);
						SDL_memset(&clickEventDown, 0, sizeof(clickEventDown));
						clickEventDown.type = SDL_MOUSEBUTTONDOWN;
						clickEventDown.button.button = SDL_BUTTON_LEFT;
						clickEventDown.button.x = endX;
						clickEventDown.button.y = endY;
						ImGui_ImplSDL2_ProcessEvent(&clickEventDown);
						SDL_Event clickEventUp;
						SDL_memset(&clickEventUp, 0, sizeof(clickEventUp));
						clickEventUp.type = SDL_MOUSEBUTTONUP;
						clickEventUp.button.button = SDL_BUTTON_LEFT;
						clickEventUp.button.x = endX;
						clickEventUp.button.y = endY;
						ImGui_ImplSDL2_ProcessEvent(&clickEventUp);
						lastTapTime = endTime;
						lastTapX = endX;
						lastTapY = endY;
					}
				}
				else {
					// 长按（可以添加长按处理逻辑）
					SDL_Event longPressEvent;
					SDL_memset(&longPressEvent, 0, sizeof(longPressEvent));
					longPressEvent.type = SDL_MOUSEMOTION;
					longPressEvent.button.button = SDL_BUTTON_LEFT;
					longPressEvent.button.x = endX;
					longPressEvent.button.y = endY;
					ImGui_ImplSDL2_ProcessEvent(&longPressEvent);
					SDL_memset(&longPressEvent, 0, sizeof(longPressEvent));
					longPressEvent.type = SDL_MOUSEBUTTONDOWN;
					longPressEvent.button.button = SDL_BUTTON_RIGHT;
					longPressEvent.button.x = endX;
					longPressEvent.button.y = endY;
					ImGui_ImplSDL2_ProcessEvent(&longPressEvent);
					SDL_memset(&longPressEvent, 0, sizeof(longPressEvent));
					longPressEvent.type = SDL_MOUSEBUTTONUP;
					longPressEvent.button.button = SDL_BUTTON_RIGHT;
					longPressEvent.button.x = endX;
					longPressEvent.button.y = endY;
					ImGui_ImplSDL2_ProcessEvent(&longPressEvent);
				}
				touchState.touching = false;
			}
			if (touchState2.touching && touchState2.fingerId == event.tfinger.fingerId) {
				touchState2.touching = false;
			}
			break;

		case SDL_FINGERMOTION: {
			if (touchState.touching && !touchState2.touching &&
				touchState.fingerId == event.tfinger.fingerId) {
				// 单指滑动
				float currentX = event.tfinger.x * wid;
				float currentY = event.tfinger.y * hei;
				float deltaX = currentX - touchState.startX;
				float deltaY = currentY - touchState.startY;

				if ((deltaX * deltaX + deltaY * deltaY) > 1.f) {
					// 发送鼠标移动事件
					SDL_Event motionEvent;
					SDL_memset(&motionEvent, 0, sizeof(motionEvent));
					motionEvent.type = SDL_MOUSEBUTTONDOWN;
					motionEvent.button.button = SDL_BUTTON_LEFT;
					motionEvent.motion.x = currentX;
					motionEvent.motion.y = currentY;
					ImGui_ImplSDL2_ProcessEvent(&motionEvent);
					SDL_memset(&motionEvent, 0, sizeof(motionEvent));
					motionEvent.type = SDL_MOUSEMOTION;
					motionEvent.motion.x = currentX;
					motionEvent.motion.y = currentY;
					motionEvent.motion.state = SDL_BUTTON_LMASK; // 按住左键拖动
					ImGui_ImplSDL2_ProcessEvent(&motionEvent);
					touchState.dragging = true;
				}
			}
			else if (touchState.touching && !touchState.dragging && touchState2.touching &&
					 touchState.fingerId != touchState2.fingerId && touchState.fingerId == event.tfinger.fingerId) {
				// 双指缩放
				float x1 = 0.0f, y1 = 0.0f, x2 = 0.0f, y2 = 0.0f;
				if (touchState.fingerId == event.tfinger.fingerId) {
					y1 = event.tfinger.y * hei;
					y2 = touchState.startY;
				}
				/*else if (touchState2.fingerId == event.tfinger.fingerId) {
					x1 = touchState.startX;
					y1 = touchState.startY;
					x2 = event.tfinger.x * wid;
					y2 = event.tfinger.y * hei;
				}*/
				float delta = (y2 - y1);

				if (std::abs(delta) > 1.0f) {
					SDL_Event wheelEvent;
					SDL_memset(&wheelEvent, 0, sizeof(wheelEvent));
					wheelEvent.type = SDL_MOUSEWHEEL;
					wheelEvent.wheel.preciseY = delta / 100;
					wheelEvent.wheel.mouseX = touchState.startX;
					wheelEvent.wheel.mouseY = touchState.startY;
					ImGui_ImplSDL2_ProcessEvent(&wheelEvent);
					touchState.startY = y1;
				}
			}
		}
			if (touchState.touching && touchState.fingerId == event.tfinger.fingerId) {
				trail1.samples[trail1.current_index] = {
					event.tfinger.x * wid,
					event.tfinger.y * hei,
					SDL_GetTicks()};
				trail1.current_index = (trail1.current_index + 1) % TRAIL_BUFFER_SIZE;
			}

			if (touchState2.touching && touchState2.fingerId == event.tfinger.fingerId) {
				trail2.samples[trail2.current_index] = {
					event.tfinger.x * wid,
					event.tfinger.y * hei,
					SDL_GetTicks()};
				trail2.current_index = (trail2.current_index + 1) % TRAIL_BUFFER_SIZE;
			}
			break;
#else
		case SDL_MOUSEBUTTONDOWN:
		case SDL_MOUSEBUTTONUP:
		case SDL_MOUSEMOTION:
#endif
		case SDL_KEYDOWN:
		case SDL_KEYUP:
		case SDL_TEXTINPUT:
		case SDL_MOUSEWHEEL:
#ifdef SINGLE_WINDOW
			ImGui_ImplSDL2_ProcessEvent(&event);
			if (ImGui::GetIO().WantCaptureMouse) {
				break;
			}
#else
			if ((SDL_GetKeyboardFocus() != emulator.window) && guiCreated) {
				ImGui_ImplSDL2_ProcessEvent(&event);
				break;
			}
#endif
			[[fallthrough]];
		default:
			emulator.UIEvent(event);
			break;
		}
	}
	return 0;
};