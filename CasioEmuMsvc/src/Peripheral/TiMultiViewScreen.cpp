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
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <typeinfo>

namespace casioemu {
	class TiMultiViewScreen final : public Peripheral, public IScreenFrameProvider {
		static constexpr unsigned Width = T4xCore::Width, Height = T4xCore::Height;
		static constexpr size_t StatusCount = TI_MV_STATUS_BITS.size();
		// ScreenOutput currently uses the common 192-column alpha stride and
		// reserves row 0 for status. Only 96x31 pixels are rendered/exported.
		std::array<float, 192 * (Height + 1)> alpha{};
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
		void UpdateFrameAlpha() override {
			const auto state = emulator.chipset.t4x->ReadDisplay();
			const auto& frame = state.lcd;
			const bool enabled = (state.control & 1) != 0;
			// R37 (0x25) is the 4-bit contrast SFR: the ROM's 2nd +/-
			// handler saturates it at 0..15. Zero is its startup setting.
			// Align that setting with MathPrint's default visual level (112),
			// then use the same alpha curve and residual settings.
			auto settings_lock = ordinary_lcd_history::UntrackedChange::LockSettings();
			const auto levels = enabled ? ti_lcd::CalculateTargetLevels(
											  112 + state.contrast, screen_residual_enabled, screen_residual_alpha_scale)
										: ti_lcd::TargetLevels{0.0f, 0.0f};
			for (unsigned y = 0; y < Height; ++y)
				for (unsigned x = 0; x < Width; ++x)
					alpha[(y + 1) * 192 + x] = (frame[y * (Width / 8) + x / 8] & (128 >> (x % 8))) ? levels.on : levels.off;
			for (size_t i = 0; i < StatusCount; ++i) {
				unsigned bit = TI_MV_STATUS_BITS[i];
				alpha[i] = (frame[T4xCore::BodyBytes + bit / 8] & (128 >> (bit % 8))) ? levels.on : levels.off;
			}
		}
		int GetFrameWidth() const override { return Width; }
		int GetFrameHeight() const override { return Height; }
		void WriteFrameRgba(uint8_t* out, int r, int g, int b) const override {
			if (!out)
				return;
			for (unsigned y = 0; y < Height; ++y)
				for (unsigned x = 0; x < Width; ++x) {
					unsigned offset = (y * Width + x) * 4;
					out[offset] = static_cast<uint8_t>(std::clamp(r, 0, 255));
					out[offset + 1] = static_cast<uint8_t>(std::clamp(g, 0, 255));
					out[offset + 2] = static_cast<uint8_t>(std::clamp(b, 0, 255));
					out[offset + 3] = static_cast<uint8_t>(std::clamp(static_cast<int>(alpha[(y + 1) * 192 + x]), 0, 255));
				}
		}
		int GetStatusAlphaCount() const override { return StatusCount; }
		void WriteStatusAlpha(uint8_t* out, int count) const override {
			if (out && count > 0)
				for (int i = 0; i < std::min(count, GetStatusAlphaCount()); ++i)
					out[i] = static_cast<uint8_t>(std::clamp(static_cast<int>(alpha[i]), 0, 255));
		}
		void Frame() override {
			UpdateFrameAlpha();
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
