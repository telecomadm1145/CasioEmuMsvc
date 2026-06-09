#include "Chipset/Chipset.hpp"
#include "Chipset/MMU.hpp"
#include "Emulator.hpp"
#include "ModelInfo.h"

#include <SDL.h>
#include <SDL_image.h>
#include <emscripten.h>
#include <emscripten/html5.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <string>

bool low_perf_ext = false;
char* n_ram_buffer = nullptr;
casioemu::MMU* me_mmu = nullptr;
casioemu::Emulator* m_emu = nullptr;
uint32_t pc_cache = 0;

int screen_flashing_threshold = 20;
float screen_fading_blending_coefficient = 0.0f;
bool enable_screen_fading = false;
float screen_flashing_brightness_coeff = 1.5f;
int screen_buffer_select = 0;
bool audio_enable = false;

namespace {
	std::unique_ptr<casioemu::Emulator> g_emulator;
	std::unique_ptr<std::map<std::string, std::string>> g_argv;
	Uint64 g_last_cpu_tick = 0;
	Uint64 g_last_emu_tick = 0;
	bool g_sdl_ready = false;

	void Notify(const char* message, const char* level = "info") {
		printf("[Web][%s] %s\n", level, message);
		EM_ASM({
			if (Module.casioemuStatus) {
				Module.casioemuStatus(UTF8ToString($0), UTF8ToString($1));
			}
		}, message, level);
	}

	void SyncPersistentFs() {
		EM_ASM({
			if (Module.casioemuSyncStarted) {
				Module.casioemuSyncStarted();
			}
			FS.syncfs(false, function(err) {
				if (Module.casioemuSyncFinished) {
					Module.casioemuSyncFinished(!!err, err ? String(err) : "");
				}
			});
		});
	}

	bool FileExists(const std::filesystem::path& path) {
		std::error_code ec;
		return std::filesystem::exists(path, ec) && std::filesystem::is_regular_file(path, ec);
	}

	bool ValidateModelFolder(const std::filesystem::path& model_path, std::string& error) {
		const auto config_path = model_path / "config.bin";
		if (!FileExists(config_path)) {
			error = "config.bin was not found in the selected model folder.";
			return false;
		}

		casioemu::ModelInfo model;
		std::ifstream config(config_path, std::ios::binary);
		if (!config) {
			error = "config.bin could not be opened.";
			return false;
		}

		try {
			model.Read(config);
		}
		catch (const std::exception& ex) {
			error = std::string("config.bin could not be parsed: ") + ex.what();
			return false;
		}

		if (model.interface_path.empty() || !FileExists(model_path / model.interface_path)) {
			error = "The interface image referenced by config.bin was not found.";
			return false;
		}
		if (model.rom_path.empty() || !FileExists(model_path / model.rom_path)) {
			error = "The ROM image referenced by config.bin was not found.";
			return false;
		}
		if (model.hardware_id == casioemu::HW_FX_5800P && (model.flash_path.empty() || !FileExists(model_path / model.flash_path))) {
			error = "The flash image referenced by config.bin was not found.";
			return false;
		}

		return true;
	}

	void PumpEmulationClock() {
		if (!g_emulator || !g_emulator->Running()) {
			return;
		}

		const Uint64 now = SDL_GetTicks64();
		if (g_last_cpu_tick == 0) {
			g_last_cpu_tick = now;
			g_last_emu_tick = now;
		}

		int catchup_steps = 0;
		while (now >= g_last_cpu_tick + g_emulator->timer_interval && catchup_steps < 4) {
			g_emulator->TimerCallback();
			g_last_cpu_tick += g_emulator->timer_interval;
			++catchup_steps;
		}
		if (catchup_steps == 4 && now > g_last_cpu_tick + 250) {
			g_last_cpu_tick = now;
		}

		if (now >= g_last_emu_tick + 25) {
			g_emulator->chipset.EmulatorTick();
			g_last_emu_tick = now;
		}
	}

	void WebFrame() {
		if (!g_emulator || !g_emulator->Running()) {
			return;
		}

		SDL_Event event{};
		while (SDL_PollEvent(&event)) {
			switch (event.type) {
			case SDL_QUIT:
				g_emulator->Shutdown();
				break;
			case SDL_WINDOWEVENT:
				if (event.window.event == SDL_WINDOWEVENT_CLOSE) {
					g_emulator->Shutdown();
				}
				break;
			default:
				g_emulator->UIEvent(event);
				break;
			}
		}

		PumpEmulationClock();

		SDL_SetRenderDrawColor(g_emulator->renderer, 18, 20, 22, 255);
		SDL_RenderClear(g_emulator->renderer);
		g_emulator->Frame();
		SDL_RenderPresent(g_emulator->renderer);
	}

	void MountPersistentFs() {
		EM_ASM({
			if (!FS.analyzePath('/models').exists) {
				FS.mkdir('/models');
			}
			FS.mount(IDBFS, {}, '/models');
			FS.syncfs(true, function(err) {
				if (Module.casioemuDidMount) {
					Module.casioemuDidMount(!!err, err ? String(err) : "");
				}
			});
		});
	}
}

extern "C" {

void casioemu_web_stop();

int casioemu_web_start(const char* model_path_raw) {
	if (!g_sdl_ready) {
		Notify("SDL is not ready yet.", "error");
		return -1;
	}
	if (!model_path_raw || !*model_path_raw) {
		Notify("No model folder was supplied.", "error");
		return -2;
	}

	casioemu_web_stop();

	const std::filesystem::path model_path(model_path_raw);
	std::string error;
	if (!ValidateModelFolder(model_path, error)) {
		Notify(error.c_str(), "error");
		return -3;
	}

	try {
		g_argv = std::make_unique<std::map<std::string, std::string>>();
		(*g_argv)["model"] = model_path.string();
		(*g_argv)["no_dbg"] = "1";
		(*g_argv)["low_perf_ext"] = "1";

		g_emulator = std::make_unique<casioemu::Emulator>(*g_argv);
		m_emu = g_emulator.get();
		low_perf_ext = true;
		g_last_cpu_tick = SDL_GetTicks64();
		g_last_emu_tick = g_last_cpu_tick;

		SDL_ShowWindow(g_emulator->window);
		SDL_RaiseWindow(g_emulator->window);
		emscripten_set_main_loop(WebFrame, 0, 0);
		Notify("Model started.", "success");
		return 0;
	}
	catch (const std::exception& ex) {
		Notify(ex.what(), "error");
		g_emulator.reset();
		g_argv.reset();
		m_emu = nullptr;
		return -4;
	}
}

void casioemu_web_stop() {
	emscripten_cancel_main_loop();
	if (g_emulator) {
		g_emulator->Shutdown();
		g_emulator.reset();
	}
	g_argv.reset();
	m_emu = nullptr;
	me_mmu = nullptr;
	n_ram_buffer = nullptr;
	SyncPersistentFs();
}

void casioemu_web_pause(int paused) {
	if (g_emulator) {
		g_emulator->SetPaused(paused != 0);
		Notify(paused ? "Paused." : "Running.", "info");
	}
}

void casioemu_web_reset() {
	if (g_emulator) {
		g_emulator->chipset.Reset();
		Notify("Reset.", "info");
	}
}

void casioemu_web_resize(int width, int height) {
	if (g_emulator && g_emulator->window && width > 0 && height > 0) {
		SDL_SetWindowSize(g_emulator->window, width, height);
	}
}

void casioemu_web_syncfs() {
	SyncPersistentFs();
}

}

int main(int, char**) {
	SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "best");
	SDL_SetHint(SDL_HINT_EMSCRIPTEN_KEYBOARD_ELEMENT, "#canvas");
	SDL_SetHint(SDL_HINT_TOUCH_MOUSE_EVENTS, "1");
	SDL_SetHint(SDL_HINT_MOUSE_TOUCH_EVENTS, "1");

	if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER | SDL_INIT_AUDIO) != 0) {
		Notify(SDL_GetError(), "error");
		return 1;
	}

	const int img_flags = IMG_INIT_PNG;
	if ((IMG_Init(img_flags) & img_flags) != img_flags) {
		Notify(IMG_GetError(), "error");
		return 1;
	}

	MountPersistentFs();
	g_sdl_ready = true;
	Notify("Runtime ready.", "success");
	return 0;
}
