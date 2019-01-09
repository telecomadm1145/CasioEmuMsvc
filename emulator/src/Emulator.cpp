#include "Emulator.hpp"

#include "Chipset/Chipset.hpp"
#include "Logger.hpp"
#include "Data/EventCode.hpp"

#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <chrono>

namespace casioemu
{
	Emulator::Emulator(std::map<std::string, std::string> &_argv_map, unsigned int _timer_interval, unsigned int _cycles_per_second, bool _paused) : paused(_paused), argv_map(_argv_map), cycles(_cycles_per_second, _timer_interval), chipset(*new Chipset(*this))
	{
		std::lock_guard<std::recursive_mutex> access_lock(access_mx);

		running = true;
		timer_interval = _timer_interval;
		model_path = argv_map["model"];

		lua_state = luaL_newstate();
		luaL_openlibs(lua_state);

		SetupLuaAPI();
		LoadModelDefition();

		interface_background = GetModelInfo("rsd_interface");
		if (interface_background.dest.x != 0 || interface_background.dest.y != 0)
			PANIC("rsd_interface must have dest x and y coordinate zero\n");

		int width = interface_background.src.w, height = interface_background.src.h;
		try
		{
			std::size_t pos;

			auto width_iter = argv_map.find("width");
			if (width_iter != argv_map.end())
			{
				width = std::stoi(width_iter->second, &pos, 0);
				if (pos != width_iter->second.size())
					PANIC("width parameter has extraneous trailing characters\n");
			}

			auto height_iter = argv_map.find("height");
			if (height_iter != argv_map.end())
			{
				height = std::stoi(height_iter->second, &pos, 0);
				if (pos != height_iter->second.size())
					PANIC("height parameter has extraneous trailing characters\n");
			}
		}
		catch (std::invalid_argument const&)
		{
			PANIC("invalid width/height parameter\n");
		}
		catch (std::out_of_range const&)
		{
			PANIC("out of range width/height parameter\n");
		}

		SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "1");
		window = SDL_CreateWindow(
			std::string(GetModelInfo("model_name")).c_str(),
			SDL_WINDOWPOS_UNDEFINED,
			SDL_WINDOWPOS_UNDEFINED,
			width, height,
			SDL_WINDOW_SHOWN |
			(argv_map.count("resizable") ? SDL_WINDOW_RESIZABLE : 0)
		);
		if (!window)
			PANIC("SDL_CreateWindow failed: %s\n", SDL_GetError());
		renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
		if (!renderer)
			PANIC("SDL_CreateRenderer failed: %s\n", SDL_GetError());

		SDL_Surface *loaded_surface = IMG_Load(GetModelFilePath(GetModelInfo("interface_image_path")).c_str());
		if (!loaded_surface)
			PANIC("IMG_Load failed: %s\n", IMG_GetError());
		interface_texture = SDL_CreateTextureFromSurface(renderer, loaded_surface);
		SDL_FreeSurface(loaded_surface);

		SetupInternals();
		cycles.Reset();

		tick_thread = new std::thread([this] {
			while (1)
			{
				{
					std::lock_guard<std::recursive_mutex> access_lock(access_mx);
					if (!Running())
						break;
					TimerCallback();
				}

				std::this_thread::sleep_for(std::chrono::milliseconds(timer_interval));
			}
		});

		RunStartupScript();

		chipset.Reset();

		if (argv_map.find("paused") != argv_map.end())
			SetPaused(true);

		pause_on_mem_error = argv_map.find("pause_on_mem_error") != argv_map.end();
	}

	Emulator::~Emulator()
	{
		if (tick_thread->joinable())
			tick_thread->join();
		delete tick_thread;
		
		std::lock_guard<std::recursive_mutex> access_lock(access_mx);

		SDL_DestroyTexture(interface_texture);
		SDL_DestroyRenderer(renderer);
		SDL_DestroyWindow(window);

		luaL_unref(lua_state, LUA_REGISTRYINDEX, lua_model_ref);
		lua_close(lua_state);
		delete &chipset;
	}

	void Emulator::HandleMemoryError()
	{
		if (pause_on_mem_error)
		{
			logger::Info("execution paused due to memory error\n");
			SetPaused(true);
		}
	}

	void Emulator::UIEvent(SDL_Event &event)
	{
		std::lock_guard<std::recursive_mutex> access_lock(access_mx);

		int w, h;
		SDL_GetWindowSize(window, &w, &h);

		// For mouse events, rescale the coordinates from window size to original size.
		switch (event.type)
		{
		case SDL_MOUSEBUTTONDOWN:
		case SDL_MOUSEBUTTONUP:
			event.button.x *= (float) interface_background.dest.w / w;
			event.button.y *= (float) interface_background.dest.h / h;
			break;
		case SDL_MOUSEMOTION:
			event.motion.x *= (float) interface_background.dest.w / w;
			event.motion.y *= (float) interface_background.dest.h / h;
			event.motion.xrel *= (float) interface_background.dest.w / w;
			event.motion.yrel *= (float) interface_background.dest.h / h;
			break;
		case SDL_MOUSEWHEEL:
			event.wheel.x *= (float) interface_background.dest.w / w;
			event.wheel.y *= (float) interface_background.dest.h / h;
			break;
		}
		chipset.UIEvent(event);
	}

	void Emulator::RunStartupScript()
	{
		if (argv_map.find("script") == argv_map.end())
			return;

		if (luaL_loadfile(lua_state, argv_map["script"].c_str()) != LUA_OK)
		{
			logger::Info("%s\n", lua_tostring(lua_state, -1));
			lua_pop(lua_state, 1);
			return;
		}

		if (lua_pcall(lua_state, 0, 1, 0) != LUA_OK)
		{
			logger::Info("%s\n", lua_tostring(lua_state, -1));
			lua_pop(lua_state, 1);
			return;
		}
	}

	void Emulator::SetupLuaAPI()
	{
		*(Emulator **)lua_newuserdata(lua_state, sizeof(Emulator *)) = this;
		lua_newtable(lua_state);
		lua_newtable(lua_state);
		lua_pushcfunction(lua_state, [](lua_State *lua_state) {
			Emulator *emu = *(Emulator **)lua_topointer(lua_state, 1);
			emu->Tick();
			return 0;
		});
		lua_setfield(lua_state, -2, "tick");
		lua_pushcfunction(lua_state, [](lua_State *lua_state) {
			Emulator *emu = *(Emulator **)lua_topointer(lua_state, 1);
			emu->Shutdown();
			return 0;
		});
		lua_setfield(lua_state, -2, "shutdown");
		lua_pushcfunction(lua_state, [](lua_State *lua_state) {
			Emulator *emu = *(Emulator **)lua_topointer(lua_state, 1);
			emu->SetPaused(lua_toboolean(lua_state, 2));
			return 0;
		});
		lua_setfield(lua_state, -2, "set_paused");
		lua_model_ref = LUA_REFNIL;
		lua_pushcfunction(lua_state, [](lua_State *lua_state) {
			Emulator *emu = *(Emulator **)lua_topointer(lua_state, 1);
			if (emu->lua_model_ref != LUA_REFNIL)
				PANIC("emu.model invoked twice\n");
			emu->lua_model_ref = luaL_ref(lua_state, LUA_REGISTRYINDEX);
			return 0;
		});
		lua_setfield(lua_state, -2, "model");
		lua_pre_tick_ref = LUA_REFNIL;
		lua_pushcfunction(lua_state, [](lua_State *lua_state) {
			Emulator *emu = *(Emulator **)lua_topointer(lua_state, 1);
			luaL_unref(lua_state, LUA_REGISTRYINDEX, emu->lua_pre_tick_ref);
			emu->lua_pre_tick_ref = luaL_ref(lua_state, LUA_REGISTRYINDEX);
			return 0;
		});
		lua_setfield(lua_state, -2, "pre_tick");
		lua_post_tick_ref = LUA_REFNIL;
		lua_pushcfunction(lua_state, [](lua_State *lua_state) {
			Emulator *emu = *(Emulator **)lua_topointer(lua_state, 1);
			luaL_unref(lua_state, LUA_REGISTRYINDEX, emu->lua_post_tick_ref);
			emu->lua_post_tick_ref = luaL_ref(lua_state, LUA_REGISTRYINDEX);
			return 0;
		});
		lua_setfield(lua_state, -2, "post_tick");
		lua_setfield(lua_state, -2, "__index");
		lua_pushcfunction(lua_state, [](lua_State *) {
			return 0;
		});
		lua_setfield(lua_state, -2, "__newindex");
		lua_setmetatable(lua_state, -2);
		lua_setglobal(lua_state, "emu");
	}

	void Emulator::SetupInternals()
	{
		chipset.SetupInternals();
	}

	void Emulator::LoadModelDefition()
	{
		if (luaL_loadfile(lua_state, (model_path + "/model.lua").c_str()) != LUA_OK)
			PANIC("LoadModelDefition failed: %s\n", lua_tostring(lua_state, -1));

		if (lua_pcall(lua_state, 0, 0, 0) != LUA_OK)
			PANIC("LoadModelDefition failed: %s\n", lua_tostring(lua_state, -1));

		if (lua_model_ref == LUA_REFNIL)
			PANIC("LoadModelDefition failed: model failed to call emu.model\n");
	}

	std::string Emulator::GetModelFilePath(std::string relative_path)
	{
		return model_path + "/" + relative_path;
	}

	void Emulator::TimerCallback()
	{
		std::lock_guard<std::recursive_mutex> access_lock(access_mx);

		Uint64 cycles_to_emulate = cycles.GetDelta();
		for (Uint64 ix = 0; ix != cycles_to_emulate; ++ix)
			if (!paused)
				Tick();

		if (chipset.GetRequireFrame())
		{
			SDL_Event event;
			SDL_zero(event);
			event.type = SDL_USEREVENT;
			event.user.code = CE_FRAME_REQUEST;
			SDL_PushEvent(&event);
		}
	}

	void Emulator::Frame()
	{
		std::lock_guard<std::recursive_mutex> access_lock(access_mx);

		// create texture `tx` with the same format as `interface_texture`
		Uint32 format;
		SDL_QueryTexture(interface_texture, &format, nullptr, nullptr, nullptr);
		SDL_Texture* tx = SDL_CreateTexture(renderer, format, SDL_TEXTUREACCESS_TARGET, interface_background.dest.w, interface_background.dest.h);

		// render on `tx`
		SDL_SetRenderTarget(renderer, tx);
		SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
		SDL_RenderClear(renderer);
		SDL_SetTextureColorMod(interface_texture, 255, 255, 255);
		SDL_SetTextureAlphaMod(interface_texture, 255);
		SDL_RenderCopy(renderer, interface_texture, &interface_background.src, nullptr);
		chipset.Frame();

		// resize and copy `tx` to screen
		SDL_SetRenderTarget(renderer, nullptr);
		SDL_RenderCopy(renderer, tx, nullptr, nullptr);
		SDL_DestroyTexture(tx);
		SDL_RenderPresent(renderer);
	}

	void Emulator::Tick()
	{
		if (lua_pre_tick_ref != LUA_REFNIL)
		{
			lua_geti(lua_state, LUA_REGISTRYINDEX, lua_pre_tick_ref);
			if (lua_pcall(lua_state, 0, 0, 0) != LUA_OK)
			{
				logger::Info("pre-tick hook failed: %s\n", lua_tostring(lua_state, -1));
				lua_pop(lua_state, 1);
				luaL_unref(lua_state, LUA_REGISTRYINDEX, lua_pre_tick_ref);
				lua_pre_tick_ref = LUA_REFNIL;
				logger::Info("  pre-tick hook unregistered\n");
			}
		}

		chipset.Tick();

		if (lua_post_tick_ref != LUA_REFNIL)
		{
			lua_geti(lua_state, LUA_REGISTRYINDEX, lua_post_tick_ref);
			if (lua_pcall(lua_state, 0, 0, 0) != LUA_OK)
			{
				logger::Info("post-tick hook failed: %s\n", lua_tostring(lua_state, -1));
				lua_pop(lua_state, 1);
				luaL_unref(lua_state, LUA_REGISTRYINDEX, lua_post_tick_ref);
				lua_post_tick_ref = LUA_REFNIL;
				logger::Info("  post-tick hook unregistered\n");
			}
		}
	}

	bool Emulator::Running()
	{
		return running;
	}

	bool Emulator::GetPaused()
	{
		return paused;
	}

	void Emulator::Shutdown()
	{
		std::lock_guard<std::recursive_mutex> access_lock(access_mx);

		running = false;
	}

	void Emulator::ExecuteCommand(std::string command)
	{
		std::lock_guard<std::recursive_mutex> access_lock(access_mx);

		const char *ugly_string_data_ptr = command.c_str();
		if (lua_load(lua_state, [](lua_State *, void *data, size_t *size) {
			char **ugly_string_data_ptr_ptr = (char **)data;
			if (!*ugly_string_data_ptr_ptr)
				return (const char *)nullptr;
			const char *result = *ugly_string_data_ptr_ptr;
			*size = strlen(result);
			*ugly_string_data_ptr_ptr = nullptr;
			return result;
		}, &ugly_string_data_ptr, "stdin", "t") != LUA_OK)
		{
			logger::Info("%s\n", lua_tostring(lua_state, -1));
			lua_pop(lua_state, 1);
			return;
		}

		if (lua_pcall(lua_state, 0, 0, 0) != LUA_OK)
		{
			logger::Info("%s\n", lua_tostring(lua_state, -1));
			lua_pop(lua_state, 1);
			return;
		}
	}

	void Emulator::SetPaused(bool _paused)
	{
		paused = _paused;
	}

	Emulator::Cycles::Cycles(Uint64 _cycles_per_second, unsigned int _timer_interval)
	{
		cycles_per_second = _cycles_per_second;
		timer_interval = _timer_interval;
		diff_cap = cycles_per_second * timer_interval / 1000;
		performance_frequency = SDL_GetPerformanceFrequency();
	}

	void Emulator::Cycles::Reset()
	{
		ticks_at_reset = SDL_GetPerformanceCounter();
		cycles_emulated = 0;
	}

	Uint64 Emulator::Cycles::GetDelta()
	{
		Uint64 ticks_now = SDL_GetPerformanceCounter();
		Uint64 cycles_to_have_been_emulated_by_now = (double)(ticks_now - ticks_at_reset) / performance_frequency * cycles_per_second;
		Uint64 diff = cycles_to_have_been_emulated_by_now - cycles_emulated;
		if (diff > diff_cap)
			diff = diff_cap;
		cycles_emulated = cycles_to_have_been_emulated_by_now;
		return diff;
	}

	SDL_Renderer *Emulator::GetRenderer()
	{
		return renderer;
	}

	SDL_Texture *Emulator::GetInterfaceTexture()
	{
		return interface_texture;
	}

	unsigned int Emulator::GetCyclesPerSecond()
	{
		return cycles.cycles_per_second;
	}
}
