#include "Config.hpp"
#include "Gui/imgui/imgui_impl_sdl2.h"
#include "Gui/ui.hpp"

#include <SDL.h>
#include <SDL_image.h>
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <iostream>
#include <iterator>
#include <map>
#include <mutex>
#include <ostream>
#include <string>
#include <thread>

#include "Data/EventCode.hpp"
#include "Emulator.hpp"
#include "Logger.hpp"
#include "SDL_events.h"
#include "SDL_keyboard.h"
#include "SDL_mouse.h"
#include "SDL_video.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>


using namespace casioemu;

int main(int argc, char* argv[]) {
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
			logger::Info("[argv] #%i: key '%s' already set\n", ix, key.c_str());
	}

	if (argv_map.find("model") == argv_map.end()) {
		printf("No model path supplied\n");
		exit(2);
	}

	int sdlFlags = SDL_INIT_VIDEO | SDL_INIT_TIMER;
	if (SDL_Init(sdlFlags) != 0)
		PANIC("SDL_Init failed: %s\n", SDL_GetError());

	int imgFlags = IMG_INIT_PNG;
	if (IMG_Init(imgFlags) != imgFlags)
		PANIC("IMG_Init failed: %s\n", IMG_GetError());

	// while(1)
	// 	;
	{
		Emulator emulator(argv_map);
		m_emu = &emulator;

		// Note: argv_map must be destructed after emulator.

		// Used to signal to the console input thread when to stop.
		static std::atomic<bool> running(true);

		std::thread console_input_thread([&] {
			auto input = std::string();
			while (1) {
				getline(std::cin,input);
				if (input.empty())
					continue;

				std::lock_guard<decltype(emulator.access_mx)> access_lock(emulator.access_mx);
				if (!emulator.Running())
					break;
				emulator.ExecuteCommand(input);

				if (!emulator.Running()) {
					SDL_Event event;
					SDL_zero(event);
					event.type = SDL_USEREVENT;
					event.user.code = CE_EMU_STOPPED;
					SDL_PushEvent(&event);
					return;
				}
			}
			});

		bool guiCreated = false;
		std::thread t1([&]() {
			test_gui(&guiCreated);
			while (1) {
				SDL_Event event;
				gui_loop();
				if (!SDL_PollEvent(&event))
					continue;

				switch (event.type) {
				case SDL_WINDOWEVENT:
					switch (event.window.event) {
					case SDL_WINDOWEVENT_CLOSE:
						emulator.Shutdown();
						break;
					case SDL_WINDOWEVENT_RESIZED:
						ImGui_ImplSDL2_ProcessEvent(&event);
						break;
					}
					break;
				default:
					break;
				}
			}
			});
		t1.detach();

		while (emulator.Running()) {

			// std::cout<<SDL_GetMouseFocus()<<","<<emulator.window<<std::endl;
			SDL_Event event;
			if (!SDL_PollEvent(&event))
				continue;

			switch (event.type) {
			case SDL_USEREVENT:
				switch (event.user.code) {
				case CE_FRAME_REQUEST:
					emulator.Frame();
					break;
				case CE_EMU_STOPPED:
					if (emulator.Running())
						PANIC("CE_EMU_STOPPED event received while emulator is still running\n");
					break;
				}
				break;

			case SDL_WINDOWEVENT:

				switch (event.window.event) {
				case SDL_WINDOWEVENT_CLOSE:
					emulator.Shutdown();
					break;
				case SDL_WINDOWEVENT_RESIZED:
					// if (!argv_map.count("resizable"))
					// {
					// 	// Normally, in this case, the window manager should not
					// 	// send resized event, but some still does (such as xmonad)
					// 	break;
					// }
					if (event.window.windowID == SDL_GetWindowID(emulator.window)) {
						emulator.WindowResize(event.window.data1, event.window.data2);
					}
					break;
				case SDL_WINDOWEVENT_EXPOSED:
					emulator.Repaint();
					break;
				}
				break;

			case SDL_MOUSEBUTTONDOWN:
			case SDL_MOUSEBUTTONUP:
			case SDL_KEYDOWN:
			case SDL_KEYUP:
			case SDL_TEXTINPUT:
			case SDL_MOUSEMOTION:
			case SDL_MOUSEWHEEL:
				if ((SDL_GetKeyboardFocus() != emulator.window || SDL_GetMouseFocus() != emulator.window) && guiCreated) {
					ImGui_ImplSDL2_ProcessEvent(&event);
					break;
				}
				emulator.UIEvent(event);
				break;
			}
		}

		running = false;
		console_input_thread.join();
	}

	std::cout << "\nGoodbye" << std::endl;

	IMG_Quit();
	SDL_Quit();

	return 0;
}
