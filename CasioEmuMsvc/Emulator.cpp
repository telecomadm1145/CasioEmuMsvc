#include <SDL.h>
#include "Emulator.hpp"
#include "Chipset/Chipset.hpp"
#include "Logger.hpp"
#include "ModelInfo.h"
#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

namespace casioemu {
	Emulator::Emulator(std::map<std::string, std::string>& _argv_map, bool _paused) : Paused(_paused), argv_map(_argv_map), chipset(*new Chipset(*this)) {
		// std::lock_guard<decltype(access_mx)> access_lock(access_mx);

		running = true;
		model_path = argv_map["model"];

		LoadModelDefition();

		int hardware_id = ModelDefinition.hardware_id;
		if (hardware_id < HW_MIN || hardware_id > HW_MAX)
			PANIC("Unknown hardware id %d\n", hardware_id);
		this->hardware_id = (HardwareId)hardware_id;

		if (ModelDefinition.real_hardware) {
			cycles_per_second = hardware_id == HW_ES_PLUS ? 128 * 1024 * 2 : hardware_id == HW_CLASSWIZ ? 1024 * 1024 * 2
																										: 2048 * 1024 * 2;
		}
		else {
			cycles_per_second = 1024 * 1024 * 8;
		}
		timer_interval = 20;

		cycles.Setup(cycles_per_second, timer_interval);
		chipset.Setup();

		BatteryVoltage = 1.5;
		SolarPanelVoltage = 1.5;

		interface_background = ModelDefinition.sprites["rsd_interface"];
		if (interface_background.dest.x != 0 || interface_background.dest.y != 0)
			PANIC("rsd_interface must have dest x and y coordinate zero\n");

		auto width = interface_background.dest.w;
		auto height = interface_background.dest.h;
		try {
			std::size_t pos;

			auto width_iter = argv_map.find("width");
			if (width_iter != argv_map.end()) {
				width = std::stoi(width_iter->second, &pos, 0);
				if (pos != width_iter->second.size())
					PANIC("width parameter has extraneous trailing characters\n");
			}

			auto height_iter = argv_map.find("height");
			if (height_iter != argv_map.end()) {
				height = std::stoi(height_iter->second, &pos, 0);
				if (pos != height_iter->second.size())
					PANIC("height parameter has extraneous trailing characters\n");
			}
		}
		catch (std::invalid_argument const&) {
			PANIC("invalid width/height parameter\n");
		}
		catch (std::out_of_range const&) {
			PANIC("out of range width/height parameter\n");
		}
		SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "best");
		window = SDL_CreateWindow(
			std::string(ModelDefinition.model_name).c_str(),
			SDL_WINDOWPOS_UNDEFINED,
			SDL_WINDOWPOS_UNDEFINED,
			width, height,
			SDL_WINDOW_SHOWN | (SDL_WINDOW_RESIZABLE));
		if (!window)
			PANIC("SDL_CreateWindow failed: %s\n", SDL_GetError());
		renderer = SDL_CreateRenderer(window, -1, 0);
		if (!renderer)
			PANIC("SDL_CreateRenderer failed: %s\n", SDL_GetError());

		interface_surface = IMG_Load(GetModelFilePath(ModelDefinition.interface_path).c_str());
		if (!interface_surface)
			PANIC("IMG_Load failed: %s\n", IMG_GetError());
		interface_texture = SDL_CreateTextureFromSurface(renderer, interface_surface);

		SetupInternals();
		cycles.Reset();
		if (ModelDefinition.real_hardware) {
			tick_thread = new std::thread([this] {
				auto iteration_end = std::chrono::steady_clock::now();
				while (1) {
					{
						// std::lock_guard<decltype(access_mx)> access_lock(access_mx);
						if (!Running())
							break;
						TimerCallback();
					}

					iteration_end += std::chrono::milliseconds(timer_interval);
					auto now = std::chrono::steady_clock::now();
					if (iteration_end > now)
						std::this_thread::sleep_until(iteration_end);
					else // in case the computer is not fast enough or Paused
						iteration_end = now;
				}
			});
		}
		else {
			tick_thread = new std::thread([this] {
				while (1) {
					{
						if (!Running())
							break;
						if (!Paused)
							Tick();
					}
				}
			});
			SDL_AddTimer(
				25,
				[](Uint32 interval, void* param) -> Uint32 {
					auto emu = ((Emulator*)param);
					emu->chipset.EmulatorTick();
					return interval;
				},
				this);
		}

		RunStartupScript();

		chipset.Reset();

		if (argv_map.find("Paused") != argv_map.end())
			SetPaused(true);

		pause_on_mem_error = argv_map.find("pause_on_mem_error") != argv_map.end();
	}

	Emulator::~Emulator() {
		if (tick_thread->joinable())
			tick_thread->join();
		delete tick_thread;

		// std::lock_guard<decltype(access_mx)> access_lock(access_mx);

		SDL_DestroyTexture(interface_texture);
		SDL_DestroyRenderer(renderer);
		SDL_DestroyWindow(window);

		delete &chipset;
	}

	void Emulator::HandleMemoryError() {
		if (pause_on_mem_error) {
			logger::Info("execution Paused due to memory error\n");
			SetPaused(true);
		}
	}

	void Emulator::UIEvent(SDL_Event& event) {
		// std::lock_guard<decltype(access_mx)> access_lock(access_mx);
		if (event.type == SDL_KEYDOWN) {
			if (event.key.keysym.sym == SDL_KeyCode::SDLK_F12) {
				screenshot_requested.store(true);
			}
		}
		switch (event.type) {
		case SDL_MOUSEBUTTONDOWN:
		case SDL_MOUSEBUTTONUP:
			event.button.x -= emu_rect.x;
			event.button.x *= (float)interface_background.dest.w / emu_rect.w;
			event.button.y -= emu_rect.y;
			event.button.y *= (float)interface_background.dest.h / emu_rect.h;
			break;
		case SDL_FINGERDOWN:
		case SDL_FINGERUP: {
			// std::swap(event.tfinger.x,event.tfinger.y);
			int w, h;
			SDL_GetWindowSize(window, &w, &h);
			event.tfinger.x *= w;
			event.tfinger.y *= h;

			event.tfinger.x -= emu_rect.x;
			event.tfinger.x *= (float)interface_background.dest.w / emu_rect.w;
			event.tfinger.y -= emu_rect.y;
			event.tfinger.y *= (float)interface_background.dest.h / emu_rect.h;
			break;
		}
		}
		chipset.UIEvent(event);
	}

	void Emulator::RunStartupScript() {
		if (argv_map.find("script") == argv_map.end())
			return;
	}

	void Emulator::SetupLuaAPI() {
	}

	void Emulator::SetupInternals() {
		chipset.SetupInternals();
	}

	void Emulator::LoadModelDefition() {
		std::ifstream ifs(GetModelFilePath("config.bin"), std::ios::in | std::ios::binary);
		if (!ifs.good())
			PANIC("Failed to open config.bin");
		ModelDefinition.Read(ifs);
	}

	std::string Emulator::GetModelFilePath(std::string relative_path) {
		return
#ifdef __ANDROID__
			(SDL_AndroidGetExternalStoragePath() / std::filesystem::path(model_path) / relative_path).string();
#else
			(std::filesystem::path(model_path) / relative_path).string();
#endif
	}

	void Emulator::TimerCallback() {
		// std::lock_guard<decltype(access_mx)> access_lock(access_mx);

		Uint64 cycles_to_emulate = cycles.GetDelta();
		for (Uint64 ix = 0; ix != cycles_to_emulate; ++ix)
			if (!Paused)
				Tick();
	}

	void Emulator::Repaint() {
		// std::lock_guard<decltype(access_mx)> access_lock(access_mx);
		// SDL_RenderPresent(renderer);
	}
	void Emulator::Frame() {
		// std::lock_guard<decltype(access_mx)> access_lock(access_mx);

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
		SDL_Rect tmp = interface_background.src;
		SDL_RenderCopy(renderer, interface_texture, &tmp, nullptr);
		chipset.Frame();
		
		// resize and copy `tx` to screen
		SDL_SetRenderTarget(renderer, nullptr);
		int w, h;
		SDL_GetWindowSize(window, &w, &h);
		auto wf = (double)w / interface_background.src.w;
		auto hf = (double)h / interface_background.src.h;
		auto uf = std::min(wf, hf);
		SDL_Rect dest{};
		dest.w = interface_background.src.w * uf;
		dest.h = interface_background.src.h * uf;
		dest.x = (w - dest.w) / 2;
		dest.y = (h - dest.h) / 2; // Centre it
		SDL_RenderCopy(renderer, tx, nullptr, &dest);
		emu_rect = dest;
		SDL_DestroyTexture(tx);
		Repaint();
	}

	void Emulator::WindowResize(int _width, int _height) {
	}

	void Emulator::Tick() {
		chipset.Tick();
	}

	bool Emulator::Running() {
		return running;
	}

	bool Emulator::GetPaused() {
		return Paused;
	}

	void Emulator::Shutdown() {
		// std::lock_guard<decltype(access_mx)> access_lock(access_mx);

		running = false;
	}

	void Emulator::ExecuteCommand(std::string command) {
	}

	void Emulator::SetPaused(bool _paused) {
		Paused = _paused;
	}

	void Emulator::Cycles::Setup(Uint64 _cycles_per_second, unsigned int _timer_interval) {
		ticks_now = 0;
		cycles_emulated = 0;
		cycles_per_second = _cycles_per_second;
		timer_interval = _timer_interval;
	}

	void Emulator::Cycles::Reset() {
		ticks_now = 0;
		cycles_emulated = 0;
	}

	Uint64 Emulator::Cycles::GetDelta() {
		ticks_now += timer_interval;
		Uint64 cycles_to_have_been_emulated_by_now = ticks_now * cycles_per_second / 1000;
		Uint64 diff = cycles_to_have_been_emulated_by_now - cycles_emulated;
		cycles_emulated = cycles_to_have_been_emulated_by_now;
		return diff;
	}

	SDL_Renderer* Emulator::GetRenderer() {
		return renderer;
	}

	SDL_Texture* Emulator::GetInterfaceTexture() {
		return interface_texture;
	}

	unsigned int Emulator::GetCyclesPerSecond() {
		return cycles.cycles_per_second;
	}

	void Emulator::SetClockSpeed(float speed) {
		cycles.Setup((unsigned int)(cycles_per_second * speed), timer_interval);
	}

	FairRecursiveMutex::FairRecursiveMutex() : holding{}, recursive_count{} {
	}

	FairRecursiveMutex::~FairRecursiveMutex() {
		assert(0 == recursive_count);
	}

	void FairRecursiveMutex::lock() {
		std::unique_lock<std::mutex> lock(m);
		assert((holding == std::thread::id{}) == (recursive_count == 0));
		if (holding == std::this_thread::get_id()) {
			++recursive_count;
			return;
		}
		if (holding != std::thread::id{} or not waiting.empty()) {
			waiting.emplace();
			auto& c = waiting.back();
			c.wait(lock, [&] {
				assert(not waiting.empty());
				assert((holding == std::thread::id{}) == (recursive_count == 0));
				return recursive_count == 0 && &waiting.front() == &c;
			});
			waiting.pop();
		}
		assert(holding == std::thread::id{});
		assert(recursive_count == 0);
		holding = std::this_thread::get_id();
		recursive_count = 1;
	}

	void FairRecursiveMutex::unlock() {
		std::lock_guard<std::mutex> lock(m);
		assert(holding == std::this_thread::get_id());
		assert(recursive_count > 0);
		--recursive_count;
		if (recursive_count == 0) {
			holding = {};
			if (not waiting.empty())
				waiting.front().notify_one(); // the notify_one must be called while m is locked, otherwise the condition variable might be destroyed (as noted on https://en.cppreference.com/w/cpp/thread/condition_variable/notify_one)
		}
	}
} // namespace casioemu
