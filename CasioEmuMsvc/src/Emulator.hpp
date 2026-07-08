#pragma once
#include "Config.hpp"
#include <SDL.h>
#include <SDL_image.h>
#include <atomic>
#include <cstdint>
#include <map>
#include <string>

#include <condition_variable>
#include <mutex>
#include <queue>
#include <thread>

#include "ModelInfo.h"
#include "QrCode.h"

namespace casioemu {
	class Chipset;
	class CPU;
	class MMU;

	/**
	 * A mutex that ensures that a thread cannot get the mutex right after it's released if there are another waiting thread.
	 */
	class FairRecursiveMutex {
		std::mutex m;
		std::thread::id holding;
		int recursive_count;
		std::queue<std::condition_variable> waiting;

	public:
		FairRecursiveMutex();
		~FairRecursiveMutex();
		void lock();
		void unlock();
	};

	class Emulator {
	public:
		SDL_Renderer* renderer = nullptr;
		SDL_Surface* interface_surface = nullptr;
		SDL_Texture* interface_texture = nullptr;
		SDL_Texture* scaled_interface_texture = nullptr;
		int scaled_interface_texture_w = 0;
		int scaled_interface_texture_h = 0;
		bool interface_is_svg = false;
		std::string interface_svg_data;
		unsigned int cycles_per_second;
		unsigned int timer_interval;
		bool running, Paused;
		bool headless = false;
		std::atomic<bool> m_step_requested;
		unsigned int last_frame_tick_count;
		std::string model_path;
		bool pause_on_mem_error;

#ifndef CASIOEMU_CORE_WEB
		std::atomic<bool> screenshot_requested{};
		std::atomic<bool> mirroring_requested{};
		std::atomic<bool> recording_requested{};
		std::atomic<bool> recording_stop_requested{};
		std::atomic<bool> recording_active{};
		std::atomic<unsigned int> recording_frame_count{};
		std::atomic<int> capture_scale{3};
		std::atomic<uint32_t> capture_background_rgb{0xd6e3d6};
#endif

		std::thread* tick_thread;

		SpriteInfo interface_background;
		SDL_Rect emu_rect{};

		/**
		 * A bunch of internally used methods for encapsulation purposes.
		 */
		void LoadModelDefition();
		void TimerCallback();
		void SetupLuaAPI();
		void SetupInternals();
		void RunStartupScript();

	public:
		ModelInfo ModelDefinition{};
		SDL_Window* window = nullptr;
		Emulator(std::map<std::string, std::string>& argv_map, bool Paused = false);
		Emulator(ModelInfo def, bool paused = false, bool headless = true, std::string modelPath = "");
		~Emulator();

		FairRecursiveMutex access_mx;
		HardwareId hardware_id;
		std::map<std::string, std::string>& argv_map;

		// private:
	public:
		/**
		 * The cycle manager structure. This structure is reset every time the
		 * emulator starts emulating CPU cycles and in every timer callback
		 * it's queried for the number of cycles that need to be emulated in the
		 * callback. This ensures that only as many cycles are emulated in a period
		 * of time as many would be in real life.
		 *
		 * Note that it's assumed that the GetDelta function is called once every
		 * timer_interval milliseconds. It's up to the timer to make sure that
		 * there's no drift.
		 */
		struct Cycles {
			void Setup(Uint64 cycles_per_second, unsigned int timer_interval);
			void Reset();
			Uint64 GetDelta();
			Uint64 ticks_now, cycles_emulated, cycles_per_second;
			unsigned int timer_interval;
		} cycles;
		/**
		 * A reference to the emulator chipset. This object holds all CPU, MMU, memory and
		 * peripheral state. The emulator interfaces with the chipset by issuing interrupts
		 * and rendering the screen buffer. It may also read internal state for testing purposes.
		 */
		Chipset& chipset;

		float BatteryVoltage, SolarPanelVoltage;
		QrCodeCapture qr_code;

		bool Running();
		void HandleMemoryError();
		void Shutdown();
		void Tick();
		/**
		 * Called when SDL_WINDOWEVENT_EXPOSED event is received. Does not re-frame.
		 */
		void Repaint();
		void Frame();
		void WindowResize(int width, int height);
		void ExecuteCommand(std::string command);
		unsigned int GetCyclesPerSecond();
		void SetClockSpeed(float speed);
		bool GetPaused();
		void SetPaused(bool paused);
		void RequestStep();
		void UIEvent(SDL_Event event);
		SDL_Renderer* GetRenderer();
		SDL_Texture* GetInterfaceTexture();
		std::string GetModelFilePath(std::string relative_path);

		friend class CPU;
		friend class MMU;
	};
} // namespace casioemu
