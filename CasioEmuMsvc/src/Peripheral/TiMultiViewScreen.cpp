#include "TiMultiViewScreen.hpp"
#include "Chipset/Chipset.hpp"
#include "Chipset/T4xCore.hpp"
#include "Emulator.hpp"
#include "Gui/HwController.h"
#include "OrdinaryLcdHistory.hpp"
#include "Peripheral.hpp"
#include "Screen.hpp"
#include "ScreenOutput.hpp"
#include "ScreenRenderSupport.hpp"
#include "TiLcdTarget.hpp"
#include "LcdResponse.hpp"
#include "LcdPlatform.hpp"
#if !defined(CASIOEMU_CORE_WEB) && !defined(__EMSCRIPTEN__) && !defined(__ANDROID__)
#include "Gui/ThemeManager.h"
#endif
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <typeinfo>
#include <istream>
#include <ostream>
#include <mutex>
#include <stdexcept>

extern bool low_perf_ext;

namespace casioemu {
	class TiMultiViewScreen final : public Peripheral, public IScreenFrameProvider {
		static constexpr unsigned Width = T4xCore::Width, Height = T4xCore::Height;
		static constexpr size_t StatusCount = TI_MV_STATUS_BITS.size();
		// ScreenOutput currently uses the common 192-column alpha stride and
		// reserves row 0 for status. Export includes it; the body is 96x31.
		std::array<float, 192 * (Height + 1)> alpha{};
		std::array<float, 192 * (Height + 1)> target{};
		mutable std::mutex response_mutex;
		uint64_t last_ns = 0, generation = 0;
		std::vector<SpriteInfo> sprites{StatusCount + 1};
		std::vector<uint8_t> present = std::vector<uint8_t>(StatusCount + 1);
		ScreenOutput output;
		std::array<SvgSpriteTextureCache, StatusCount> status_textures;
#ifndef CASIOEMU_CORE_WEB
		PixelScreenTexture pixel_texture;
#endif

	public:
		explicit TiMultiViewScreen(Emulator& emulator) : Peripheral(emulator), output(emulator) { clock_type = CLOCK_STOPPED; }
		void Initialise() override {
			if constexpr (lcd_platform::kNativeTemporalSupport) emulator.chipset.t4x->EnableDisplayHistory();
			if (emulator.headless) return;
			sprites[0] = emulator.ModelDefinition.sprites.at("rsd_pixel");
			present[0] = 1;
			for (size_t i = 0; i < StatusCount; ++i) {
				char name[32];
				std::snprintf(name, sizeof(name), "rsd_mv_%02u", TI_MV_STATUS_BITS[i]);
				auto it = emulator.ModelDefinition.sprites.find(name);
				if (it != emulator.ModelDefinition.sprites.end()) {
					sprites[i + 1] = it->second;
					present[i + 1] = 1;
				}
			}
		}
		void Uninitialise() override {
			output.Stop();
			for (auto& texture : status_textures)
				texture.Reset();
#ifndef CASIOEMU_CORE_WEB
			pixel_texture.Reset();
#endif
		}
		void* QueryInterface(const char* name) override {
			if (std::strcmp(name, typeid(IScreenFrameProvider).name()) == 0)
				return static_cast<IScreenFrameProvider*>(this);
			return nullptr;
		}
	private:
		void SetTargets(const T4xCore::DisplayState& state) {
			const auto& frame = state.lcd;
			const bool enabled = (state.control & 1) != 0;
			// R37 (0x25) is the 4-bit contrast SFR: the ROM's 2nd +/-
			// handler saturates it at 0..15. Zero is its startup setting.
			// Use MultiView's full sixteen-level curve and shared residual settings.
			const auto levels = enabled ? ti_lcd::CalculateMultiViewTargetLevels(
												  state.contrast, screen_residual_enabled, screen_residual_alpha_scale)
										: ti_lcd::TargetLevels{0.0f, 0.0f};
			for (unsigned y = 0; y < Height; ++y)
				for (unsigned x = 0; x < Width; ++x)
					target[(y + 1) * 192 + x] = (frame[y * (Width / 8) + x / 8] & (128 >> (x % 8))) ? levels.on : levels.off;
			for (size_t i = 0; i < StatusCount; ++i) {
				unsigned bit = TI_MV_STATUS_BITS[i];
				target[i] = (frame[T4xCore::BodyBytes + bit / 8] & (128 >> (bit % 8))) ? levels.on : levels.off;
			}
		}
		void Settle(uint64_t time_ns) {
			if (time_ns <= last_ns) return;
			const double elapsed_ms = double(time_ns - last_ns) / 1000000.0;
			last_ns = time_ns;
			const auto config = lcd_response::ForHardware(HW_TI_MULTI_VIEW);
			const double rise = lcd_response::GainForElapsed(elapsed_ms, config.rise_half_life_ms);
			const double fall = lcd_response::GainForElapsed(elapsed_ms, config.fall_half_life_ms);
			for (size_t i = 0; i < alpha.size(); ++i)
				alpha[i] = float(lcd_response::BlendWithGains(alpha[i], target[i], rise, fall));
		}
		void UpdateAlphaLocked() {
			auto settings_lock = ordinary_lcd_history::UntrackedChange::LockSettings();
			const auto history = emulator.chipset.t4x->ConsumeDisplayHistory();
			if (generation != history.current.generation) {
				alpha.fill(0); target.fill(0);
				last_ns = history.changes.empty() ? history.current.steady_ns : history.changes.front().steady_ns;
				generation = history.current.generation;
			}
			bool temporal = lcd_platform::kNativeTemporalSupport;
#if !defined(CASIOEMU_CORE_WEB) && !defined(__EMSCRIPTEN__) && !defined(__ANDROID__)
			const bool low_performance = ThemeManager::Instance().Settings().lowPerformanceMode || low_perf_ext;
			temporal = temporal && !low_performance;
#else
			constexpr bool low_performance = false;
#endif
			if (!temporal) {
				SetTargets(history.current);
				const float ratio = lcd_platform::LegacyBlendRatio(0.0f, low_performance);
				for (size_t i = 0; i < alpha.size(); ++i)
					alpha[i] = alpha[i] * ratio + target[i] * (1 - ratio);
				last_ns = history.current.steady_ns;
				return;
			}
			// Settle the old target up to each LCD update, then install the new
			// target. Keeping the intermediate refreshes makes ghosting independent
			// of the GUI frame rate. Presentation time continues while the CPU pauses.
			for (const auto& change : history.changes) {
				Settle(change.steady_ns);
				SetTargets(change);
			}
			Settle(history.current.steady_ns);
			SetTargets(history.current); // Residual settings may change while paused.
		}
	public:
		void UpdateFrameAlpha() override {
			const std::lock_guard lock(response_mutex); UpdateAlphaLocked();
		}
		void Reset() override {
			const std::lock_guard lock(response_mutex);
			alpha.fill(0); target.fill(0); last_ns = 0; generation = 0;
		}
		void SaveState(std::ostream& out) override {
			const std::lock_guard lock(response_mutex); UpdateAlphaLocked();
			out.write("MVL1", 4);
			out.write(reinterpret_cast<const char*>(alpha.data()), sizeof(alpha));
			out.write(reinterpret_cast<const char*>(target.data()), sizeof(target));
			// Keep the MVL1 latch-position field; host timestamps are reanchored on load.
			const auto elapsed_us = emulator.chipset.t4x->ReadDisplay().elapsed_us;
			out.write(reinterpret_cast<const char*>(&elapsed_us), sizeof(elapsed_us));
		}
		void LoadState(std::istream& in) override {
			const std::lock_guard lock(response_mutex);
			char magic[4]{}; in.read(magic, 4);
			if (!in || std::string(magic, 4) != "MVL1") throw std::runtime_error("Invalid MultiView LCD snapshot");
			decltype(alpha) next_alpha{}, next_target{};
			uint64_t next_us = 0;
			in.read(reinterpret_cast<char*>(next_alpha.data()), sizeof(next_alpha));
			in.read(reinterpret_cast<char*>(next_target.data()), sizeof(next_target));
			in.read(reinterpret_cast<char*>(&next_us), sizeof(next_us));
			const auto valid_alpha = [](float value) { return std::isfinite(value) && value >= 0 && value <= 255; };
			const auto display = emulator.chipset.t4x->ReadDisplay();
			if (!in || next_us > display.elapsed_us ||
				!std::all_of(next_alpha.begin(), next_alpha.end(), valid_alpha) ||
				!std::all_of(next_target.begin(), next_target.end(), valid_alpha))
				throw std::runtime_error("Invalid MultiView LCD response state");
			alpha = next_alpha; target = next_target; last_ns = display.steady_ns;
			generation = display.generation;
			// The saved target already corresponds to the restored LCD latch.
			emulator.chipset.t4x->ConsumeDisplayHistory();
		}
		int GetFrameWidth() const override { return Width; }
		int GetFrameHeight() const override { return Height + 1; }
		void WriteFrameRgba(uint8_t* out, int r, int g, int b) const override {
			const std::lock_guard lock(response_mutex);
			if (!out)
				return;
			for (unsigned y = 0; y < Height + 1; ++y)
				for (unsigned x = 0; x < Width; ++x) {
					unsigned offset = (y * Width + x) * 4;
					out[offset] = static_cast<uint8_t>(std::clamp(r, 0, 255));
					out[offset + 1] = static_cast<uint8_t>(std::clamp(g, 0, 255));
					out[offset + 2] = static_cast<uint8_t>(std::clamp(b, 0, 255));
					out[offset + 3] = static_cast<uint8_t>(std::clamp(static_cast<int>(alpha[y * 192 + x]), 0, 255));
				}
		}
		int GetStatusAlphaCount() const override { return StatusCount; }
		void WriteStatusAlpha(uint8_t* out, int count) const override {
			const std::lock_guard lock(response_mutex);
			if (out && count > 0)
				for (int i = 0; i < std::min(count, GetStatusAlphaCount()); ++i)
					out[i] = static_cast<uint8_t>(std::clamp(static_cast<int>(alpha[i]), 0, 255));
		}
		void Frame() override {
			const std::lock_guard lock(response_mutex);
			UpdateAlphaLocked();
			ScreenOutputFrame frame{};
			frame.renderer = emulator.GetRenderer();
			frame.interface_texture = emulator.GetInterfaceTexture();
			frame.interface_surface = emulator.interface_surface;
			frame.sprite_info = &sprites;
			frame.sprite_available = &present;
			frame.ink_colour = &emulator.ModelDefinition.ink_color;
			frame.screen_ink_alpha = alpha.data();
			frame.logical_width = Width;
			frame.logical_height = Height;
			frame.lcd_dest = sprites[0].dest;
			for (size_t i = 0; i < StatusCount; ++i) {
				const auto opacity = static_cast<uint8_t>(std::clamp(static_cast<int>(alpha[i]), 0, 255));
				if (present[i + 1] && opacity)
					RenderModelSprite(frame.renderer, frame.interface_texture, &status_textures[i], sprites[i + 1], *frame.ink_colour, opacity);
			}
#ifndef CASIOEMU_CORE_WEB
			pixel_texture.Render(frame.renderer, frame.lcd_dest, Width, Height, *frame.ink_colour, alpha.data());
#endif
			output.Render(frame);
		}
	};
	Peripheral* CreateTiMultiViewScreen(Emulator& emulator) { return new TiMultiViewScreen(emulator); }
} // namespace casioemu
