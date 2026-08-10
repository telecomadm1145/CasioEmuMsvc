#include "Emulator.hpp"
#include "Chipset/Chipset.hpp"
#include "Logger.hpp"
#include "ModelConfig.h"
#include "ModelInfo.h"
#include "RendererBackend.h"
#include <SDL.h>
#include <cassert>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <sstream>
#include <string>
#include <utility>

namespace casioemu {
	namespace {
		unsigned int GetLimitedCyclesPerSecond(int hardware_id) {
			switch (hardware_id) {
			// case HW_FX_5800P:
			case HW_ES_PLUS:
				return 128 * 1024 * 2;
			case HW_SOLARII:
				return 64 * 1024 * 2;
			case HW_CLASSWIZ:
				return 1024 * 1024 * 2;
			default:
				return 2048 * 1024 * 2;
			}
		}

		bool HasSvgExtension(const std::string& path) {
			const auto ext = std::filesystem::path(path).extension().string();
			if (ext.size() != 4)
				return false;
			return (ext[0] == '.') &&
				(ext[1] == 's' || ext[1] == 'S') &&
				(ext[2] == 'v' || ext[2] == 'V') &&
				(ext[3] == 'g' || ext[3] == 'G');
		}

		std::string ReadBinaryFile(const std::string& path) {
			std::ifstream stream(path, std::ios::binary);
			if (!stream)
				return {};
			return {std::istreambuf_iterator<char>{stream}, std::istreambuf_iterator<char>{}};
		}

		SDL_Texture* CreateSizedSvgTexture(SDL_Renderer* renderer, const std::string& svg, int width, int height) {
			if (!renderer || svg.empty() || width <= 0 || height <= 0)
				return nullptr;
			SDL_RWops* rw = SDL_RWFromConstMem(svg.data(), static_cast<int>(svg.size()));
			if (!rw)
				return nullptr;
			SDL_Surface* surface = IMG_LoadSizedSVG_RW(rw, width, height);
			SDL_RWclose(rw);
			if (!surface) {
				SDL_Log("[Emulator][Warn] IMG_LoadSizedSVG_RW failed for interface SVG: %s", IMG_GetError());
				return nullptr;
			}
			SDL_Texture* texture = SDL_CreateTextureFromSurface(renderer, surface);
			SDL_FreeSurface(surface);
			if (texture)
				SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_BLEND);
			return texture;
		}

		SDL_Renderer* TryCreateRendererWithTargetTexture(SDL_Window* window, const std::string& driver_label, std::string& errors) {
			struct RendererAttempt {
				Uint32 flags;
				const char* label;
			};
			const RendererAttempt attempts[] = {
				{SDL_RENDERER_ACCELERATED | SDL_RENDERER_TARGETTEXTURE, "accelerated target texture"},
				{SDL_RENDERER_TARGETTEXTURE, "target texture"},
				{0, "default"}
			};

			for (const auto& attempt : attempts) {
				SDL_ClearError();
				SDL_Renderer* result = SDL_CreateRenderer(window, -1, attempt.flags);
				if (!result) {
					errors += driver_label;
					errors += ", ";
					errors += attempt.label;
					errors += ": ";
					errors += SDL_GetError();
					errors += "\n";
					continue;
				}

				SDL_RendererInfo info{};
				if (SDL_GetRendererInfo(result, &info) == 0 && (info.flags & SDL_RENDERER_TARGETTEXTURE)) {
					SDL_Log("[Emulator][Info] Using SDL renderer '%s' (%s, %s, flags=0x%x)",
						info.name ? info.name : "unknown",
						driver_label.c_str(),
						attempt.label,
						info.flags);
					return result;
				}

				errors += driver_label;
				errors += ", ";
				errors += attempt.label;
				errors += ": renderer does not support target textures\n";
				SDL_DestroyRenderer(result);
			}

			return nullptr;
		}

		SDL_Renderer* CreateRendererWithTargetTexture(SDL_Window* window) {
			const char* initial_hint_value = SDL_GetHint(SDL_HINT_RENDER_DRIVER);
			const std::string initial_hint = initial_hint_value ? initial_hint_value : "";
			const std::string initial_label = initial_hint.empty() ? "current renderer hint" : "renderer hint '" + initial_hint + "'";
			std::string errors;

			SDL_Renderer* result = TryCreateRendererWithTargetTexture(window, initial_label, errors);
			if (result)
				return result;

			if (!initial_hint.empty()) {
				SDL_SetHint(SDL_HINT_RENDER_DRIVER, nullptr);
				result = TryCreateRendererWithTargetTexture(window, "SDL default renderer selection", errors);
				if (result)
					return result;
			}

			if (initial_hint != "software") {
				SDL_SetHint(SDL_HINT_RENDER_DRIVER, "software");
				result = TryCreateRendererWithTargetTexture(window, "software renderer hint", errors);
				if (result)
					return result;
			}

			if (initial_hint.empty())
				SDL_SetHint(SDL_HINT_RENDER_DRIVER, nullptr);
			else
				SDL_SetHint(SDL_HINT_RENDER_DRIVER, initial_hint.c_str());
			SDL_SetError("No SDL renderer with target texture support is available.\n%s", errors.c_str());
			return nullptr;
		}
	}

	Emulator::Emulator(std::map<std::string, std::string>& _argv_map, bool _paused, std::shared_ptr<ModelResourceStore> resources)
		: Paused(_paused), model_resources(std::move(resources)), argv_map(_argv_map), chipset(*new Chipset(*this)), m_step_requested(false) {
		// std::lock_guard<decltype(access_mx)> access_lock(access_mx);

		running = true;
		model_path = argv_map["model"];

		LoadModelDefition();

		int hardware_id = ModelDefinition.hardware_id;
		if (hardware_id < HW_MIN || hardware_id > HW_MAX)
			PANIC("Unknown hardware id %d\n", hardware_id);
		this->hardware_id = (HardwareId)hardware_id;
		bool full_spd = !ModelDefinition.real_hardware;
		if (ModelDefinition.extra.find("limit_spd") != ModelDefinition.extra.end()) {
			full_spd = false;
		}
		if (!full_spd) {
			cycles_per_second = GetLimitedCyclesPerSecond(hardware_id);
		}
		else {
			cycles_per_second = 1024 * 1024 * 8;
		}
		if (hardware_id == HW_EPS6800) {
			cycles_per_second = 100000;
		}
		timer_interval = hardware_id == HW_EPS6800 ? 40 : 20;

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
			SDL_WINDOW_HIDDEN | SDL_WINDOW_RESIZABLE);
		if (!window)
			PANIC("SDL_CreateWindow failed: %s\n", SDL_GetError());
		SetPreferredRendererDriverHint();
		renderer = CreateRendererWithTargetTexture(window);
		if (!renderer)
			PANIC("SDL_CreateRenderer failed: %s\n", SDL_GetError());

		interface_is_svg = HasSvgExtension(ModelDefinition.interface_path);
		if (model_resources) {
			const auto interface_data = ReadModelResource(ModelDefinition.interface_path);
			if (interface_is_svg) interface_svg_data.assign(interface_data.begin(), interface_data.end());
			SDL_RWops* rw = SDL_RWFromConstMem(interface_data.data(), static_cast<int>(interface_data.size()));
			interface_surface = rw ? IMG_Load_RW(rw, 1) : nullptr;
		}
		else {
			const auto interface_path = GetModelFilePath(ModelDefinition.interface_path);
			if (interface_is_svg) interface_svg_data = ReadBinaryFile(interface_path);
			interface_surface = IMG_Load(interface_path.c_str());
		}
		if (!interface_surface)
			PANIC("IMG_Load failed: %s\n", IMG_GetError());
		interface_texture = SDL_CreateTextureFromSurface(renderer, interface_surface);

		SetupInternals();
		cycles.Reset();
		// EPS reset clears CPU/SFR state but preserves its RAM image. Do this before
		// the worker starts so firmware sees a clean reset with the restored RAM.
		if (hardware_id == HW_EPS6800)
			chipset.Reset();
		if (hardware_id == HW_EPS6800 && argv_map.find("paused") != argv_map.end())
			SetPaused(true);
		#ifdef __EMSCRIPTEN__
		tick_thread = nullptr;
		#else
		bool limit_spd = ModelDefinition.extra.find("limit_spd") != ModelDefinition.extra.end();
		if (ModelDefinition.real_hardware || limit_spd) {
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
		if (!ModelDefinition.real_hardware && limit_spd) {
			SDL_AddTimer(
				25,
				[](Uint32 interval, void* param) -> Uint32 {
					auto emu = ((Emulator*)param);
					emu->chipset.EmulatorTick();
					return interval;
				},
				this);
		}
		#endif

		RunStartupScript();

		if (hardware_id != HW_EPS6800)
			chipset.Reset();

		if (argv_map.find("paused") != argv_map.end())
			SetPaused(true);

		pause_on_mem_error = argv_map.find("pause_on_mem_error") != argv_map.end();
	}

	Emulator::Emulator(ModelInfo def, bool paused, bool headless, std::string modelPath) : Paused(paused), argv_map(*new std::map<std::string, std::string>()), chipset(*new Chipset(*this)), m_step_requested(false), headless(headless) {
		running = true;
		model_path = modelPath.empty() ? argv_map["model"] : std::move(modelPath);

		ModelDefinition = def;

		int hardware_id = ModelDefinition.hardware_id;
		if (hardware_id < HW_MIN || hardware_id > HW_MAX)
			PANIC("Unknown hardware id %d\n", hardware_id);
		this->hardware_id = (HardwareId)hardware_id;
		bool full_spd = !ModelDefinition.real_hardware;
		if (ModelDefinition.extra.find("limit_spd") != ModelDefinition.extra.end()) {
			full_spd = false;
		}
		if (!full_spd) {
			cycles_per_second = GetLimitedCyclesPerSecond(hardware_id);
		}
		else {
			cycles_per_second = 1024 * 1024 * 8;
		}
		if (hardware_id == HW_EPS6800) {
			cycles_per_second = 100000;
		}
		timer_interval = hardware_id == HW_EPS6800 ? 40 : 20;

		cycles.Setup(cycles_per_second, timer_interval);
		chipset.Setup();

		BatteryVoltage = 1.5;
		SolarPanelVoltage = 1.5;
		if (!headless) {
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
				SDL_WINDOW_HIDDEN | SDL_WINDOW_RESIZABLE);
			if (!window)
				PANIC("SDL_CreateWindow failed: %s\n", SDL_GetError());
			SetPreferredRendererDriverHint();
			renderer = CreateRendererWithTargetTexture(window);
			if (!renderer)
				PANIC("SDL_CreateRenderer failed: %s\n", SDL_GetError());

			interface_is_svg = HasSvgExtension(ModelDefinition.interface_path);
			const auto interface_path = GetModelFilePath(ModelDefinition.interface_path);
			if (interface_is_svg) interface_svg_data = ReadBinaryFile(interface_path);
			interface_surface = IMG_Load(interface_path.c_str());
			if (!interface_surface)
				PANIC("IMG_Load failed: %s\n", IMG_GetError());
			interface_texture = SDL_CreateTextureFromSurface(renderer, interface_surface);
		}
		SetupInternals();
		cycles.Reset();
		// EPS reset clears CPU/SFR state but preserves its RAM image. Do this before
		// the worker starts so firmware sees a clean reset with the restored RAM.
		if (hardware_id == HW_EPS6800)
			chipset.Reset();
		if (!headless) {
		#ifdef __EMSCRIPTEN__
			tick_thread = nullptr;
		#else
			bool limit_spd = ModelDefinition.extra.find("limit_spd") != ModelDefinition.extra.end();
			if (ModelDefinition.real_hardware || limit_spd) {
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
			if (!ModelDefinition.real_hardware && limit_spd) {
				SDL_AddTimer(
					25,
					[](Uint32 interval, void* param) -> Uint32 {
						auto emu = ((Emulator*)param);
						emu->chipset.EmulatorTick();
						return interval;
					},
					this);
			}
		#endif

			RunStartupScript();
		}

		if (hardware_id != HW_EPS6800)
			chipset.Reset();
	}

	Emulator::~Emulator() {
		if (!headless) {
			if (tick_thread && tick_thread->joinable())
				tick_thread->join();
			delete tick_thread;

			// std::lock_guard<decltype(access_mx)> access_lock(access_mx);

			if (scaled_interface_texture)
				SDL_DestroyTexture(scaled_interface_texture);
			SDL_DestroyTexture(interface_texture);
			if (interface_surface)
				SDL_FreeSurface(interface_surface);
			SDL_DestroyRenderer(renderer);
			SDL_DestroyWindow(window);
		}

		delete& chipset;
	}

	void Emulator::HandleMemoryError() {
		if (pause_on_mem_error) {
			logger::Info("execution Paused due to memory error\n");
			SetPaused(true);
		}
	}

	void Emulator::UIEvent(SDL_Event event) {
		if (headless)
			return;
		// std::lock_guard<decltype(access_mx)> access_lock(access_mx);
#ifndef CASIOEMU_CORE_WEB
		if (event.type == SDL_KEYDOWN) {
			if (event.key.keysym.sym == SDL_KeyCode::SDLK_F12) {
				if (event.key.keysym.mod & KMOD_CTRL) {
					if (recording_active.load()) {
						recording_stop_requested.store(true);
					}
					else {
						recording_requested.store(true);
					}
				}
				else {
					screenshot_requested.store(true);
				}
			}
		}
#endif
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
		std::string error;
		const bool loaded = model_resources
			? LoadModelInfoFromResourceStore(*model_resources, ModelDefinition, &error)
			: LoadModelInfoFromFolder(std::filesystem::path(model_path), ModelDefinition, nullptr, &error);
		if (!loaded)
			PANIC("Failed to load model configuration: %s", error.c_str());
	}

	std::string Emulator::GetModelFilePath(std::string relative_path) const {
		if (model_resources) return {};
		return
#ifdef __ANDROID__
		(SDL_AndroidGetExternalStoragePath() / std::filesystem::path(model_path) / relative_path).string();
#else
			(std::filesystem::path(model_path) / relative_path).string();
#endif
	}

	bool Emulator::HasModelResource(const std::string& name) const {
		if (model_resources) return model_resources->Exists(name);
		std::error_code ec;
		return std::filesystem::is_regular_file(GetModelFilePath(name), ec);
	}

	std::vector<std::uint8_t> Emulator::ReadModelResource(const std::string& name) const {
		if (model_resources) return model_resources->Read(name);
		std::ifstream stream(GetModelFilePath(name), std::ios::binary);
		if (!stream) throw std::runtime_error("Cannot open model resource: " + name);
		return {std::istreambuf_iterator<char>{stream}, std::istreambuf_iterator<char>{}};
	}

	void Emulator::WriteModelSessionResource(const std::string& name, const std::vector<std::uint8_t>& data) {
		if (model_resources) {
			model_resources->WriteSession(name, data);
			return;
		}
		std::ofstream stream(GetModelFilePath(name), std::ios::binary);
		if (!stream || !stream.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size())))
			throw std::runtime_error("Cannot write model resource: " + name);
	}

	void Emulator::TimerCallback() {
		// std::lock_guard<decltype(access_mx)> access_lock(access_mx);
		if (hardware_id == HW_EPS6800) {
			constexpr Uint64 cycles_per_eps_frame = 4000;
			const auto cycles_to_emulate = cycles.GetDelta();
			if (Paused) {
				eps_frame_cycle_remainder.store(0, std::memory_order_relaxed);
				return;
			}
			eps_frame_cycle_remainder.fetch_add(cycles_to_emulate, std::memory_order_relaxed);
			while (eps_frame_cycle_remainder.load(std::memory_order_relaxed) >= cycles_per_eps_frame) {
				if (chipset.RunEpsFrame()) {
					SetPaused(true);
					eps_frame_cycle_remainder.store(0, std::memory_order_relaxed);
					break;
				}
				eps_frame_cycle_remainder.fetch_sub(cycles_per_eps_frame, std::memory_order_relaxed);
			}
			return;
		}

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
		if (headless)
			return;
		// std::lock_guard<decltype(access_mx)> access_lock(access_mx);

		const bool board_interface = !ModelDefinition.board_path.empty() &&
			interface_background.dest.w > 0 && interface_background.dest.h > 0;
		if (board_interface) {
			int w, h;
			SDL_GetWindowSize(window, &w, &h);
			auto wf = (double)w / interface_background.dest.w;
			auto hf = (double)h / interface_background.dest.h;
			auto uf = std::min(wf, hf);
			SDL_Rect dest{};
			dest.w = std::max(1, static_cast<int>(interface_background.dest.w * uf));
			dest.h = std::max(1, static_cast<int>(interface_background.dest.h * uf));
			dest.x = (w - dest.w) / 2;
			dest.y = (h - dest.h) / 2;

			bool face_available = true;
			if (interface_is_svg) {
				if (!scaled_interface_texture || scaled_interface_texture_w != dest.w || scaled_interface_texture_h != dest.h) {
					if (scaled_interface_texture)
						SDL_DestroyTexture(scaled_interface_texture);
					scaled_interface_texture = CreateSizedSvgTexture(renderer, interface_svg_data, dest.w, dest.h);
					scaled_interface_texture_w = dest.w;
					scaled_interface_texture_h = dest.h;
				}
				face_available = scaled_interface_texture != nullptr;
			}

			if (face_available) {
				SDL_SetRenderTarget(renderer, nullptr);
				SDL_RenderSetViewport(renderer, nullptr);
				SDL_RenderSetClipRect(renderer, nullptr);
				SDL_RenderSetScale(renderer, 1.0f, 1.0f);
#ifndef SINGLE_WINDOW
				SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
				SDL_RenderClear(renderer);
#endif
				if (interface_is_svg) {
					SDL_SetTextureColorMod(scaled_interface_texture, 255, 255, 255);
					SDL_SetTextureAlphaMod(scaled_interface_texture, 255);
					SDL_RenderCopy(renderer, scaled_interface_texture, nullptr, &dest);
				}
				else {
					SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
					SDL_RenderFillRect(renderer, &dest);
					SDL_SetTextureColorMod(interface_texture, 255, 255, 255);
					SDL_SetTextureAlphaMod(interface_texture, 255);
					SDL_Rect tmp = interface_background.src;
					SDL_RenderCopy(renderer, interface_texture, &tmp, &dest);
				}

				SDL_Rect old_viewport{};
				float old_scale_x = 1.0f, old_scale_y = 1.0f;
				SDL_RenderGetViewport(renderer, &old_viewport);
				SDL_RenderGetScale(renderer, &old_scale_x, &old_scale_y);
				SDL_RenderSetViewport(renderer, &dest);
				SDL_RenderSetScale(renderer, static_cast<float>(uf), static_cast<float>(uf));
				chipset.Frame();
				SDL_RenderSetScale(renderer, old_scale_x, old_scale_y);
				SDL_RenderSetViewport(renderer, &old_viewport);
				emu_rect = dest;
				Repaint();
				return;
			}
		}

		const int render_target_w = interface_background.dest.w;
		const int render_target_h = interface_background.dest.h;

		// create texture `tx` with the same format as `interface_texture`
		Uint32 format;
		SDL_QueryTexture(interface_texture, &format, nullptr, nullptr, nullptr);
		SDL_Texture* tx = SDL_CreateTexture(renderer, format, SDL_TEXTUREACCESS_TARGET, render_target_w, render_target_h);

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
		SDL_RenderSetViewport(renderer, nullptr);
		SDL_RenderSetClipRect(renderer, nullptr);
		SDL_RenderSetScale(renderer, 1.0f, 1.0f);
#ifndef SINGLE_WINDOW
		SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
		SDL_RenderClear(renderer);
#endif
		int w, h;
		SDL_GetWindowSize(window, &w, &h);
		auto wf = (double)w / render_target_w;
		auto hf = (double)h / render_target_h;
		auto uf = std::min(wf, hf);
		SDL_Rect dest{};
		dest.w = render_target_w * uf;
		dest.h = render_target_h * uf;
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
		eps_frame_cycle_remainder.store(0, std::memory_order_relaxed);
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
