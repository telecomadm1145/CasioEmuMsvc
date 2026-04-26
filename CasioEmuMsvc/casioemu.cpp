#include "Config.hpp"
#include "Ui.hpp"
#include "imgui_impl_sdl2.h"

#include "Emulator.hpp"
#include "Localization.h"
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
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <map>
#include <mutex>
#include <ostream>
#include <string>
#include <thread>
#if _WIN32
#include <Windows.h>
#include <combaseapi.h>
#include <timeapi.h>
#pragma comment(lib, "winmm.lib")
#include "sdl_win32_extra.h"
#endif

#ifdef __ANDROID__
#include <unistd.h>
#endif
#ifdef ENABLE_SENTRY
#include <sentry.h>
#endif

#include "DiscordRPC.h"
#include "StartupUi/StartupUi.h"
#include <Gui.h>
#include <Plugin/PluginMan.h>
#include <ThemeManager.h>

using namespace casioemu;
SDL_Surface* background;
SDL_Texture* bg_txt;
bool low_perf_ext = false;

// Driver chain tried in order after a crash: default (auto) → opengl → software
static const char* kRendererDrivers[] = {"default", "opengl", "software"};
static const int kRendererDriverCount = 3;
static const char* kCrashLockFile = ".crash.switch_renderer";
static const char* kRendererHintFile = ".renderer_hint.cfg";

static std::string ReadRendererHint() {
	std::ifstream f(kRendererHintFile);
	if (!f.is_open())
		return "default";
	std::string s;
	std::getline(f, s);
	for (int i = 0; i < kRendererDriverCount; ++i)
		if (s == kRendererDrivers[i])
			return s;
	return "default";
}

static void WriteRendererHint(const std::string& driver) {
	std::ofstream f(kRendererHintFile, std::ios::trunc);
	if (f.is_open())
		f << driver;
}

static std::string NextRendererDriver(const std::string& current) {
	for (int i = 0; i < kRendererDriverCount - 1; ++i)
		if (current == kRendererDrivers[i])
			return kRendererDrivers[i + 1];
	return kRendererDrivers[kRendererDriverCount - 1];
}

static void TouchCrashLock() {
	std::ofstream f(kCrashLockFile, std::ios::trunc);
}

static void RemoveCrashLock() {
	std::filesystem::remove(kCrashLockFile);
}

int main(int argc, char* argv[]) {
#ifdef _WIN32
	SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE);
	timeBeginPeriod(1);
	SetConsoleCP(65001); // Set to UTF8
	SetConsoleOutputCP(65001);
	CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
#endif //  _WIN32
#ifdef ENABLE_SENTRY
	sentry_options_t* options = sentry_options_new();
	sentry_options_set_dsn(options, "https://335230bc5e18c7b25464556638c4cfdc@o4510804732018688.ingest.us.sentry.io/4510805048950784");
	// This is also the default-path. For further information and recommendations:
	// https://docs.sentry.io/platforms/native/configuration/options/#database-path
	sentry_options_set_database_path(options, ".sentry");
	sentry_options_set_release(options, "CasioEmuMsvc@" EMULATOR_VERSION);
	sentry_options_set_debug(options, 1);
	sentry_init(options);
#endif
#ifdef __ANDROID__
	chdir(SDL_AndroidGetExternalStoragePath());
#endif
	g_local.Load();

#ifndef __ANDROID__
	std::string rendererDriver = ReadRendererHint();
	bool previouslyCrashed = std::filesystem::exists(kCrashLockFile);
	if (previouslyCrashed) {
		rendererDriver = NextRendererDriver(rendererDriver);
		WriteRendererHint(rendererDriver);
		printf("[Startup][Warn] Previous session crashed. Switching renderer to: %s\n", rendererDriver.c_str());

		char msg[256];
		snprintf(msg, sizeof(msg),
			"The previous session crashed.\n"
			"Automatically switching to the '%s' renderer backend.\n"
			"If crashes persist, try updating your GPU drivers.\n"
			"If you think this is a error, delete .renderer_hint.cfg to reset to default.",
			rendererDriver.c_str());
		SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_WARNING, "CasioEmuMsvc - Renderer Fallback", msg, nullptr);

		RemoveCrashLock();
	}
	if (rendererDriver != "default") {
		// SDL_RENDER_DRIVER is checked by SDL when creating a renderer
		SDL_SetHint(SDL_HINT_RENDER_DRIVER, rendererDriver.c_str());
		printf("[Startup][Info] Renderer hint set to: %s\n", rendererDriver.c_str());
	}
#endif

	DiscordRPC::Init();
	DiscordRPC::UpdatePresence("");

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
		if (argv_map["model"].empty()) {
			DiscordRPC::Shutdown();
			return -1;
		}
	}

	bool no_dbg = !argv_map["no_dbg"].empty();
	low_perf_ext = !argv_map["low_perf_ext"].empty();
	Emulator emulator(argv_map);
	m_emu = &emulator;

	// static std::atomic<bool> running(true);

	DiscordRPC::UpdatePresence(emulator.ModelDefinition.model_name);

	bool guiCreated = false;
	auto frame_event = SDL_RegisterEvents(1);
	bool busy = false;
	bool running = true;
	std::thread t3([&]() {
		SDL_Event se{};
		se.type = frame_event;
		se.user.windowID = SDL_GetWindowID(emulator.window);
		while (running) {
			if (!busy)
				SDL_PushEvent(&se);
#ifdef __ANDROID__
			SDL_Delay(40);
#else
			if (ThemeManager::Instance().Settings().lowPerformanceMode || low_perf_ext)
				SDL_Delay(24);
			else
				SDL_Delay(1);
#endif
		}
	});
	t3.detach();
#ifdef DBG
	if (!no_dbg) {
		test_gui(&guiCreated, emulator.window, emulator.renderer);
		background = IMG_Load("background.jpg");
		bg_txt = 0;
		if (background) {
			bg_txt = SDL_CreateTextureFromSurface(renderer, background);
			ThemeManager::Instance().ExtractAndApplyAutoTint(bg_txt, renderer);
		}
	}
#endif
#ifdef _WIN32
	EnableDarkTitleBar(GetSDLWindowHandle(emulator.window));
#endif
	SDL_ShowWindow(emulator.window);
	SDL_RaiseWindow(emulator.window);

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

	auto SendMouseEvent = [](Uint32 type, float x, float y, int button = SDL_BUTTON_LEFT) {
		SDL_Event event;
		SDL_memset(&event, 0, sizeof(event));
		event.type = type;
		if (type == SDL_MOUSEMOTION) {
			event.motion.x = x;
			event.motion.y = y;
			event.motion.state = (button == SDL_BUTTON_LEFT) ? SDL_BUTTON_LMASK : 0;
		}
		else {
			event.button.button = button;
			event.button.x = x;
			event.button.y = y;
		}
		ImGui_ImplSDL2_ProcessEvent(&event);
	};

#if defined(_WIN32) || defined(__ANDROID__)
	LoadPlugins();
#endif

	while (emulator.Running()) {
		SDL_Event event{};
		busy = false;
		DiscordRPC::Update();
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
			if (!no_dbg)
				gui_loop();
			emulator.Frame();
			SDL_RenderPresent(emulator.renderer);
#endif
			if (!no_dbg) {
				ThemeManager::Instance().ProcessFontRebuild();
				if (ThemeManager::Instance().IsBgReloadRequested()) {
					SDL_DestroyTexture(bg_txt);
					SDL_FreeSurface(background);
					background = IMG_Load("background.jpg");
					if (background) {
						bg_txt = SDL_CreateTextureFromSurface(renderer, background);
						ThemeManager::Instance().ExtractAndApplyAutoTint(bg_txt, renderer);
					}
					ThemeManager::Instance().ClearBgReloadRequest();
				}
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
		case SDL_FINGERDOWN: {
			float x = event.tfinger.x * wid;
			float y = event.tfinger.y * hei;

			// Primary Finger
			if (!touchState.touching) {
				touchState.touching = true;
				touchState.startX = x;
				touchState.startY = y;
				touchState.currentX = x;
				touchState.currentY = y;
				touchState.startTime = SDL_GetTicks();
				touchState.fingerId = event.tfinger.fingerId;
				touchState.dragging = false;
			}
			// Secondary Finger (Multitouch)
			else if (!touchState2.touching) {
				touchState2.touching = true;
				touchState2.startX = x;
				touchState2.startY = y;
				touchState2.currentX = x;
				touchState2.currentY = y;
				touchState2.fingerId = event.tfinger.fingerId;
				touchState2.dragging = false;

				// Optional: Send initial motion to setup zooming center
				SendMouseEvent(SDL_MOUSEMOTION, touchState.currentX, touchState.currentY);
			}
			break;
		}

		case SDL_FINGERUP: {
			float endX = event.tfinger.x * wid;
			float endY = event.tfinger.y * hei;
			Uint32 endTime = SDL_GetTicks();

			// Handle Primary Finger
			if (touchState.touching && touchState.fingerId == event.tfinger.fingerId) {
				touchState.currentX = endX;
				touchState.currentY = endY;

				if (touchState.dragging) {
					// It was a drag operation, just release
					SendMouseEvent(SDL_MOUSEBUTTONUP, endX, endY, SDL_BUTTON_LEFT);
				}
				else {
					// It was NOT a drag, check for Tap or Long Press
					if (endTime - touchState.startTime < LONG_PRESS_DELAY) {
						// Short Tap - synthesize full click
						SendMouseEvent(SDL_MOUSEMOTION, endX, endY);
						SendMouseEvent(SDL_MOUSEBUTTONDOWN, endX, endY, SDL_BUTTON_LEFT);
						SendMouseEvent(SDL_MOUSEBUTTONUP, endX, endY, SDL_BUTTON_LEFT);
					}
					else {
						// Long Press - synthesize Right Click
						SendMouseEvent(SDL_MOUSEMOTION, endX, endY);
						SendMouseEvent(SDL_MOUSEBUTTONDOWN, endX, endY, SDL_BUTTON_RIGHT);
						SendMouseEvent(SDL_MOUSEBUTTONUP, endX, endY, SDL_BUTTON_RIGHT);
					}
				}

				touchState.touching = false;
				touchState.dragging = false;
			}

			// Handle Secondary Finger
			if (touchState2.touching && touchState2.fingerId == event.tfinger.fingerId) {
				// Usually just cleanup for the second finger, unless you want specific 2-finger tap logic
				if (touchState2.dragging) {
					SendMouseEvent(SDL_MOUSEBUTTONUP, endX, endY, SDL_BUTTON_LEFT);
				}
				touchState2.touching = false;
				touchState2.dragging = false;
			}
			break;
		}

		case SDL_FINGERMOTION: {
			float currentX = event.tfinger.x * wid;
			float currentY = event.tfinger.y * hei;

			// --- Logic for Primary Finger Interaction ---
			if (touchState.touching && touchState.fingerId == event.tfinger.fingerId) {

				// If only one finger is down, handle Dragging
				if (!touchState2.touching) {
					float deltaX = currentX - touchState.startX;
					float deltaY = currentY - touchState.startY;
					float distSq = deltaX * deltaX + deltaY * deltaY;

					// 1. Detect start of Drag (Threshold: 10 pixels roughly)
					if (!touchState.dragging && distSq > 100.0f) {
						SendMouseEvent(SDL_MOUSEBUTTONDOWN, currentX, currentY, SDL_BUTTON_LEFT);
						touchState.dragging = true;
					}

					// 2. Process Drag
					if (touchState.dragging) {
						SendMouseEvent(SDL_MOUSEMOTION, currentX, currentY);
					}
				}
				// If two fingers are down, and this is the primary finger moving -> Scroll/Zoom
				else if (touchState2.touching) {
					// Calculate relative vertical movement for scrolling
					float moveY = currentY - touchState.currentY;

					// Threshold to prevent micro-jitters
					if (std::abs(moveY) > 1.0f) {
						SDL_Event wheelEvent;
						SDL_memset(&wheelEvent, 0, sizeof(wheelEvent));
						wheelEvent.type = SDL_MOUSEWHEEL;
						// Use relative movement, not absolute position difference
						wheelEvent.wheel.preciseY = moveY / 20.0f; // Scale factor
						wheelEvent.wheel.mouseX = currentX;
						wheelEvent.wheel.mouseY = currentY;
						ImGui_ImplSDL2_ProcessEvent(&wheelEvent);
					}
				}

				// Update Primary State & Trail
				touchState.currentX = currentX;
				touchState.currentY = currentY;
				trail1.samples[trail1.current_index] = {currentX, currentY, SDL_GetTicks()};
				trail1.current_index = (trail1.current_index + 1) % TRAIL_BUFFER_SIZE;
			}

			// --- Logic for Secondary Finger Interaction ---
			if (touchState2.touching && touchState2.fingerId == event.tfinger.fingerId) {

				// If secondary finger moves while primary is holding, also trigger scroll
				if (touchState.touching) {
					float moveY = currentY - touchState2.currentY;
					if (std::abs(moveY) > 1.0f) {
						SDL_Event wheelEvent;
						SDL_memset(&wheelEvent, 0, sizeof(wheelEvent));
						wheelEvent.type = SDL_MOUSEWHEEL;
						wheelEvent.wheel.preciseY = moveY / 20.0f;
						wheelEvent.wheel.mouseX = touchState.currentX; // Use primary as anchor
						wheelEvent.wheel.mouseY = touchState.currentY;
						ImGui_ImplSDL2_ProcessEvent(&wheelEvent);
					}
				}

				// Update Secondary State & Trail
				touchState2.currentX = currentX;
				touchState2.currentY = currentY;
				trail2.samples[trail2.current_index] = {currentX, currentY, SDL_GetTicks()};
				trail2.current_index = (trail2.current_index + 1) % TRAIL_BUFFER_SIZE;
			}
			break;
		}
#else
		// Desktop handling remains largely the same
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
			if (!no_dbg)
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
	running = false;
	if (t3.joinable()) {
		t3.join();
	}
	if (bg_txt) {
		SDL_DestroyTexture(bg_txt);
	}
#ifdef ENABLE_SENTRY
	sentry_close();
#endif
	DiscordRPC::Shutdown();

	return 0;
};
