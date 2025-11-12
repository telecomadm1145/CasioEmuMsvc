#include "Config.hpp"
#include "Ui.hpp"
#include "imgui_impl_sdl2.h"
#include "Emulator.hpp"
#include "Logger.hpp"
#include "Ext/AppContext.hpp"
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
#include <mmsystem.h>
#include <objbase.h>
#pragma comment(lib, "winmm.lib")
#endif
#ifdef __ANDROID__
#include <unistd.h>
#endif
#include "StartupUi/StartupUi.h"
#include <Gui.h>
#include <Plugin/PluginMan.h>
#include "GDB/GDBServer.hpp"

using namespace casioemu;

struct TouchState {
	bool touching = false;
	float startX = 0.0f;
	float startY = 0.0f;
	float currentX = 0.0f;
	float currentY = 0.0f;
	Uint32 startTime = 0;
	SDL_FingerID fingerId = -1;
	bool dragging = false;
};

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

void HandleEvent(const SDL_Event& event, Emulator& emulator, bool guiCreated, TouchState& touchState1, TouchState& touchState2, TouchTrail& trail1, TouchTrail& trail2) {
    if (guiCreated) {
        ImGui_ImplSDL2_ProcessEvent(&event);
    }

    switch (event.type) {
        case SDL_WINDOWEVENT:
            if (event.window.event == SDL_WINDOWEVENT_CLOSE) {
                GetAppContext().eventBus.publish("app.exit_requested", {});
                emulator.Shutdown();
            }
            return;
    }

    switch (event.type) {
        case SDL_KEYDOWN:
        case SDL_KEYUP:
        case SDL_TEXTINPUT:
            if (guiCreated && ImGui::GetIO().WantCaptureKeyboard) break;
            emulator.UIEvent((SDL_Event&)event);
            break;
        case SDL_MOUSEMOTION:
        case SDL_MOUSEBUTTONDOWN:
        case SDL_MOUSEBUTTONUP:
        case SDL_MOUSEWHEEL:
            if (guiCreated && ImGui::GetIO().WantCaptureMouse) break;
            emulator.UIEvent((SDL_Event&)event);
            break;
            
#ifdef __ANDROID__
        case SDL_FINGERDOWN:
        {
            if (guiCreated && ImGui::GetIO().WantCaptureMouse) {
            } else {
                emulator.UIEvent((SDL_Event&)event);
            }
            int wid, hei;
            SDL_GetWindowSize(emulator.window, &wid, &hei);
            float touchX = event.tfinger.x * wid;
            float touchY = event.tfinger.y * hei;

            if (touchState1.fingerId == -1) {
                touchState1.fingerId = event.tfinger.fingerId;
                touchState1.touching = true;
                touchState1.currentX = touchX;
                touchState1.currentY = touchY;
            } else if (touchState2.fingerId == -1) {
                touchState2.fingerId = event.tfinger.fingerId;
                touchState2.touching = true;
                touchState2.currentX = touchX;
                touchState2.currentY = touchY;
            }
        }
        break;

        case SDL_FINGERUP:
        {
            if (guiCreated && ImGui::GetIO().WantCaptureMouse) {
            } else {
                emulator.UIEvent((SDL_Event&)event);
            }
            if (event.tfinger.fingerId == touchState1.fingerId) {
                touchState1.fingerId = -1;
                touchState1.touching = false;
            } else if (event.tfinger.fingerId == touchState2.fingerId) {
                touchState2.fingerId = -1;
                touchState2.touching = false;
            }
        }
        break;

        case SDL_FINGERMOTION:
        {
            if (guiCreated && ImGui::GetIO().WantCaptureMouse) {
            } else {
                emulator.UIEvent((SDL_Event&)event);
            }
            int wid, hei;
            SDL_GetWindowSize(emulator.window, &wid, &hei);
            float touchX = event.tfinger.x * wid;
            float touchY = event.tfinger.y * hei;
            
            if (event.tfinger.fingerId == touchState1.fingerId) {
                touchState1.currentX = touchX;
                touchState1.currentY = touchY;
                trail1.samples[trail1.current_index] = { touchX, touchY, SDL_GetTicks() };
                trail1.current_index = (trail1.current_index + 1) % TRAIL_BUFFER_SIZE;
            } else if (event.tfinger.fingerId == touchState2.fingerId) {
                touchState2.currentX = touchX;
                touchState2.currentY = touchY;
                trail2.samples[trail2.current_index] = { touchX, touchY, SDL_GetTicks() };
                trail2.current_index = (trail2.current_index + 1) % TRAIL_BUFFER_SIZE;
            }
        }
        break;
#endif
    }
}

int main(int argc, char* argv[]) {
#ifdef _WIN32
	SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE);
	timeBeginPeriod(1);
	SetConsoleCP(65001); // Set to UTF8
	SetConsoleOutputCP(65001);
	CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
#endif
#ifdef __ANDROID__
	chdir(SDL_AndroidGetExternalStoragePath());
#endif
	g_local.Load();
	std::map<std::string, std::string> argv_map;
    int gdb_port = 1234;
	for (int ix = 1; ix != argc; ++ix) {
		std::string key, value;
		char* eq_pos = strchr(argv[ix], '=');
		if (eq_pos) {
			key = std::string(argv[ix], eq_pos);
			value = eq_pos + 1;
		} else {
			key = "model";
			value = argv[ix];
		}
		if (key == "gdb_port") {
            try {
                gdb_port = std::stoi(value);
            } catch (const std::exception& e) {
                logger::Info("[argv][Warn] Invalid GDB port value '%s'. Using default %d.\n", value.c_str(), gdb_port);
            }
        } else {
            if (argv_map.find(key) == argv_map.end())
                argv_map[key] = value;
            else
                logger::Info("[argv][Info] #%i: key '%s' already set\n", ix, key.c_str());
        }
	}
	bool headless = argv_map.find("headless") != argv_map.end();

	if (argv_map["model"].empty()) {
		auto s = sui_loop();
		argv_map["model"] = std::move(s);
		if (argv_map["model"].empty())
			return -1;
	}

	int sdlFlags = SDL_INIT_VIDEO | SDL_INIT_TIMER;
	if (SDL_Init(sdlFlags) != 0)
		PANIC("SDL_Init failed: %s\n", SDL_GetError());

	int imgFlags = IMG_INIT_PNG;
	if (IMG_Init(imgFlags) != imgFlags)
		PANIC("IMG_Init failed: %s\n", IMG_GetError());
    
	if (headless && argv_map["model"].empty()) {
		PANIC("No model path supplied.\n");
	}

	Emulator emulator(argv_map);
	m_emu = &emulator;

	GDBServer gdbServer(&emulator, gdb_port);
    
	bool guiCreated = false;
	auto frame_event = SDL_RegisterEvents(1);
	std::atomic<bool> frame_thread_running = {true};
	std::thread t3([&]() {
		SDL_Event se{};
		se.type = frame_event;
		if(emulator.window)
			se.user.windowID = SDL_GetWindowID(emulator.window);
		while (frame_thread_running) {
			if (emulator.window)
				SDL_PushEvent(&se);
#ifdef __ANDROID__
			SDL_Delay(40); // 25fps
#else
			SDL_Delay(16); // ~60fps
#endif
		}
	});
	
#ifdef DBG
	test_gui(&guiCreated, emulator.window, emulator.renderer, &gdbServer);
#endif
	SDL_Surface* background = IMG_Load("background.jpg");
	SDL_Texture* bg_txt = 0;
	if (background) {
		bg_txt = SDL_CreateTextureFromSurface(renderer, background);
        SDL_FreeSurface(background);
	}
	SDL_ShowWindow(emulator.window);
	TouchState touchState1, touchState2;
	TouchTrail trail1, trail2;

	// 在主循环渲染部分添加（在SDL_RenderPresent之前）：
	const Uint32 TRAIL_DURATION = 500; // 轨迹持续500ms
	const Uint32 LONG_PRESS_DELAY = 500;		 // 长按延时（毫秒）
	const float DOUBLE_TAP_MAX_DELAY = 300.0f;	 // 双击最大时间间隔 (毫秒)
	const float DOUBLE_TAP_MAX_DISTANCE = 20.0f; // 双击最大距离 (像素)
	static Uint32 lastTapTime = 0;
	static float lastTapX = 0;
	static float lastTapY = 0;

#ifdef _WIN32
    LoadPlugins();
#endif
    GetAppContext().eventBus.publish("app.start", {});

    while (emulator.Running()) {
        SDL_Event event;
        if (SDL_WaitEvent(&event)) {
            HandleEvent(event, emulator, guiCreated, touchState1, touchState2, trail1, trail2);
            while (SDL_PollEvent(&event)) {
                HandleEvent(event, emulator, guiCreated, touchState1, touchState2, trail1, trail2);
            }
        } else {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "SDL_WaitEvent failed: %s", SDL_GetError());
            emulator.Shutdown();
        }

        GetAppContext().eventBus.publish("frame.begin", {});
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
            } else {
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
        
        if (touchState1.touching) {
            // Set color for touch indicator
            SDL_SetRenderDrawColor(renderer, 255, 0, 0, 255);
            // Draw horizontal line of the cross
            SDL_RenderDrawLine(renderer, touchState1.currentX - 10, touchState1.currentY, touchState1.currentX + 10, touchState1.currentY);
            // Draw vertical line of the cross
            SDL_RenderDrawLine(renderer, touchState1.currentX, touchState1.currentY - 10, touchState1.currentX, touchState1.currentY + 10);
        }

        if (touchState2.touching) {
            // Set different color for second touch
            SDL_SetRenderDrawColor(renderer, 0, 255, 0, 255);
            // Draw horizontal line of the cross
            SDL_RenderDrawLine(renderer, touchState2.currentX - 10, touchState2.currentY, touchState2.currentX + 10, touchState2.currentY);
            // Draw vertical line of the cross
            SDL_RenderDrawLine(renderer, touchState2.currentX, touchState2.currentY - 10, touchState2.currentX, touchState2.currentY + 10);
        }
        
        // 渲染第一个触摸轨迹
        Uint32 current_time = SDL_GetTicks();
        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
        for (int i = 0; i < TRAIL_BUFFER_SIZE; i++) {
            int idx = (trail1.current_index - 1 - i + TRAIL_BUFFER_SIZE) % TRAIL_BUFFER_SIZE;
            TouchSample& sample = trail1.samples[idx];
            Uint32 age = current_time - sample.time;
            if (age > TRAIL_DURATION) continue;
            float radius = 50.0f * age / TRAIL_DURATION + 5.f;
            SDL_SetRenderDrawColor(renderer, 255, 255, 255, 120 - 120 * age / TRAIL_DURATION);
            SDL_Rect rect = {(int)(sample.x - radius / 2), (int)(sample.y - radius / 2), (int)radius, (int)radius};
            SDL_RenderFillRect(renderer, &rect);
        }

        // 渲染第二个触摸轨迹
        for (int i = 0; i < TRAIL_BUFFER_SIZE; i++) {
            int idx = (trail2.current_index - 1 - i + TRAIL_BUFFER_SIZE) % TRAIL_BUFFER_SIZE;
            TouchSample& sample = trail2.samples[idx];
            Uint32 age = current_time - sample.time;
            if (age > TRAIL_DURATION) continue;
            float radius = 50.0f * age / TRAIL_DURATION + 5.f;
            SDL_SetRenderDrawColor(renderer, 255, 255, 255, 120 - 120 * age / TRAIL_DURATION);
            SDL_Rect rect = {(int)(sample.x - radius / 2), (int)(sample.y - radius / 2), (int)radius, (int)radius};
            SDL_RenderFillRect(renderer, &rect);
        }
#else
        gui_loop();
        emulator.Frame();
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
        SDL_RenderPresent(emulator.renderer);
        GetAppContext().eventBus.publish("frame.end", {});
	}
    
    frame_thread_running = false;
    if(t3.joinable()) {
        t3.join();
    }
    if (bg_txt) {
        SDL_DestroyTexture(bg_txt);
    }
    GetAppContext().eventBus.publish("app.stop", {});
	return 0;
};