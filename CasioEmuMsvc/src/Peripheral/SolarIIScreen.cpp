#include "SolarIIScreen.hpp"
#include "Screen.hpp"
#include "ScreenRenderSupport.hpp"
#include "Peripheral.hpp"
#include "Chipset/MMURegion.hpp"
#include "Emulator.hpp"
#include "Gui/HwController.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <ios>
#include <istream>
#include <ostream>
#include <typeinfo>

namespace casioemu {
    class SolarIIScreen : public Peripheral, public IScreenFrameProvider {
        struct StatusBit {
            uint8_t offset;
            uint8_t bit;
        };

        static constexpr int FRAME_WIDTH = 64;
        static constexpr int FRAME_HEIGHT_WITH_STATUS_ROW = 9;
        static constexpr size_t DISPLAY_BASE_ADDR = 0xF800;
        static constexpr size_t DISPLAY_ADDR = 0xF801;
        static constexpr size_t DISPLAY_LEN = 0x17;
        static constexpr size_t DISPLAY_STORAGE_LEN = 0x18;
        static constexpr std::array<StatusBit, 109> STATUS_BITS = {{
            {0x11, 6}, {0x11, 2}, {0x12, 6}, {0x12, 2}, {0x13, 6}, {0x13, 2},
            {0x14, 6}, {0x14, 2}, {0x15, 6}, {0x15, 2}, {0x16, 4}, {0x16, 2},
            {0x16, 0}, {0x08, 1}, {0x01, 5}, {0x01, 6}, {0x01, 4}, {0x09, 5},
            {0x09, 6}, {0x09, 4}, {0x11, 5}, {0x11, 4}, {0x01, 1}, {0x01, 2},
            {0x01, 0}, {0x09, 1}, {0x09, 2}, {0x09, 0}, {0x11, 1}, {0x11, 0},
            {0x02, 5}, {0x02, 6}, {0x02, 4}, {0x0A, 5}, {0x0A, 6}, {0x0A, 4},
            {0x12, 5}, {0x12, 4}, {0x02, 1}, {0x02, 2}, {0x02, 0}, {0x0A, 1},
            {0x0A, 2}, {0x0A, 0}, {0x12, 1}, {0x12, 0}, {0x03, 5}, {0x03, 6},
            {0x03, 4}, {0x0B, 5}, {0x0B, 6}, {0x0B, 4}, {0x13, 5}, {0x13, 4},
            {0x03, 1}, {0x03, 2}, {0x03, 0}, {0x0B, 1}, {0x0B, 2}, {0x0B, 0},
            {0x13, 1}, {0x13, 0}, {0x04, 5}, {0x04, 6}, {0x04, 4}, {0x0C, 5},
            {0x0C, 6}, {0x0C, 4}, {0x14, 5}, {0x14, 4}, {0x04, 1}, {0x04, 2},
            {0x04, 0}, {0x0C, 1}, {0x0C, 2}, {0x0C, 0}, {0x14, 1}, {0x14, 0},
            {0x05, 5}, {0x05, 6}, {0x05, 4}, {0x0D, 5}, {0x0D, 6}, {0x0D, 4},
            {0x15, 5}, {0x15, 4}, {0x05, 1}, {0x05, 2}, {0x05, 0}, {0x0D, 1},
            {0x0D, 2}, {0x0D, 0}, {0x15, 1}, {0x15, 0}, {0x16, 6}, {0x06, 5},
            {0x06, 6}, {0x06, 4}, {0x0E, 5}, {0x0E, 6}, {0x0E, 4}, {0x16, 5},
            {0x06, 1}, {0x06, 2}, {0x06, 0}, {0x0E, 1}, {0x0E, 2}, {0x0E, 0},
            {0x16, 1},
        }};
        static constexpr std::array<const char*, STATUS_BITS.size()> STATUS_SPRITE_NAMES = {{
            "rsd_shift",
            "rsd_mode",
            "rsd_sto",
            "rsd_rcl",
            "rsd_hyp",
            "rsd_m",
            "rsd_k",
            "rsd_deg",
            "rsd_rad",
            "rsd_gra",
            "rsd_fix",
            "rsd_sci",
            "rsd_sd",
            "rsd_b_minus",
            "rsd_b_1_up",
            "rsd_b_1_up_left",
            "rsd_b_1_up_right",
            "rsd_b_1_mid",
            "rsd_b_1_down_left",
            "rsd_b_1_down_right",
            "rsd_b_1_down",
            "rsd_b_1_dot",
            "rsd_b_2_up",
            "rsd_b_2_up_left",
            "rsd_b_2_up_right",
            "rsd_b_2_mid",
            "rsd_b_2_down_left",
            "rsd_b_2_down_right",
            "rsd_b_2_down",
            "rsd_b_2_dot",
            "rsd_b_3_up",
            "rsd_b_3_up_left",
            "rsd_b_3_up_right",
            "rsd_b_3_mid",
            "rsd_b_3_down_left",
            "rsd_b_3_down_right",
            "rsd_b_3_down",
            "rsd_b_3_dot",
            "rsd_b_4_up",
            "rsd_b_4_up_left",
            "rsd_b_4_up_right",
            "rsd_b_4_mid",
            "rsd_b_4_down_left",
            "rsd_b_4_down_right",
            "rsd_b_4_down",
            "rsd_b_4_dot",
            "rsd_b_5_up",
            "rsd_b_5_up_left",
            "rsd_b_5_up_right",
            "rsd_b_5_mid",
            "rsd_b_5_down_left",
            "rsd_b_5_down_right",
            "rsd_b_5_down",
            "rsd_b_5_dot",
            "rsd_b_6_up",
            "rsd_b_6_up_left",
            "rsd_b_6_up_right",
            "rsd_b_6_mid",
            "rsd_b_6_down_left",
            "rsd_b_6_down_right",
            "rsd_b_6_down",
            "rsd_b_6_dot",
            "rsd_b_7_up",
            "rsd_b_7_up_left",
            "rsd_b_7_up_right",
            "rsd_b_7_mid",
            "rsd_b_7_down_left",
            "rsd_b_7_down_right",
            "rsd_b_7_down",
            "rsd_b_7_dot",
            "rsd_b_8_up",
            "rsd_b_8_up_left",
            "rsd_b_8_up_right",
            "rsd_b_8_mid",
            "rsd_b_8_down_left",
            "rsd_b_8_down_right",
            "rsd_b_8_down",
            "rsd_b_8_dot",
            "rsd_b_9_up",
            "rsd_b_9_up_left",
            "rsd_b_9_up_right",
            "rsd_b_9_mid",
            "rsd_b_9_down_left",
            "rsd_b_9_down_right",
            "rsd_b_9_down",
            "rsd_b_9_dot",
            "rsd_b_10_up",
            "rsd_b_10_up_left",
            "rsd_b_10_up_right",
            "rsd_b_10_mid",
            "rsd_b_10_down_left",
            "rsd_b_10_down_right",
            "rsd_b_10_down",
            "rsd_b_10_dot",
            "rsd_s_minus",
            "rsd_s_1_up",
            "rsd_s_1_up_left",
            "rsd_s_1_up_right",
            "rsd_s_1_mid",
            "rsd_s_1_down_left",
            "rsd_s_1_down_right",
            "rsd_s_1_down",
            "rsd_s_2_up",
            "rsd_s_2_up_left",
            "rsd_s_2_up_right",
            "rsd_s_2_mid",
            "rsd_s_2_down_left",
            "rsd_s_2_down_right",
            "rsd_s_2_down",
        }};

        std::array<uint8_t, STATUS_BITS.size()> status_alpha{};
        std::array<SpriteInfo, STATUS_BITS.size()> status_sprite_info{};
        std::array<bool, STATUS_BITS.size()> status_sprite_present{};
        std::array<SvgSpriteTextureCache, STATUS_BITS.size()> status_svg_textures{};
        std::array<uint8_t, DISPLAY_STORAGE_LEN> display_data{};
        MMURegion region_display_control{}, region_display{};
        MMURegion region_range{}, region_mode{}, region_contrast{}, region_brightness{}, region_refresh_rate{};
        SDL_Renderer* renderer{};
        SDL_Texture* interface_texture{};
        ColourInfo ink_colour{};
        uint8_t display_control = 0;
        uint8_t screen_range = 0, screen_mode = 0, screen_contrast = 0, screen_brightness = 0, screen_refresh_rate = 0;

        const uint8_t* DisplayData() const {
            return display_data.data();
        }

        uint8_t CalculateSolarIIStatusAlpha(bool enabled) const {
            if (!screen_residual_enabled) {
                return enabled ? 255 : 0;
            }

            const float contrast = static_cast<float>(std::clamp<int>(screen_contrast, 0, 0x1F));
            const float ink_alpha_on = 255.0f * std::clamp(0.75f + contrast / 36.0f, 0.0f, 1.0f);
            const float ink_alpha_off = 255.0f * std::clamp(0.01f + contrast * 0.0045f, 0.0f, 1.0f);
            const float alpha = enabled ? ink_alpha_on : ink_alpha_off * screen_residual_alpha_scale;
            return static_cast<uint8_t>(std::clamp(static_cast<int>(alpha + 0.5f), 0, 255));
        }

        bool IsSolarIIDisplayEnabled() const {
            const uint8_t mode = screen_mode & 0x07;
            return mode == 0x05 || mode == 0x06;
        }

    public:
        using Peripheral::Peripheral;
		~SolarIIScreen() override {
			for (auto& texture : status_svg_textures)
				texture.Reset();
		}

        void Initialise() override {
            renderer = emulator.GetRenderer();
            interface_texture = emulator.GetInterfaceTexture();
            ink_colour = emulator.ModelDefinition.ink_color;
            status_sprite_present.fill(false);
			for (auto& texture : status_svg_textures)
				texture.Reset();
            for (size_t i = 0; i < STATUS_SPRITE_NAMES.size(); ++i) {
                auto iter = emulator.ModelDefinition.sprites.find(STATUS_SPRITE_NAMES[i]);
                if (iter == emulator.ModelDefinition.sprites.end())
                    continue;
                status_sprite_info[i] = iter->second;
                status_sprite_present[i] = true;
            }

            region_display_control.Setup(0xF800, 1, "SolarIIScreen/Control", &display_control, MMURegion::DefaultRead<uint8_t>, MMURegion::DefaultWrite<uint8_t>, emulator);
            region_display.Setup(
                DISPLAY_ADDR, DISPLAY_LEN, "SolarIIScreen/Buffer", this,
                [](MMURegion* region, size_t offset) {
                    auto* screen = static_cast<SolarIIScreen*>(region->userdata);
                    return screen->display_data[offset - DISPLAY_BASE_ADDR];
                },
                [](MMURegion* region, size_t offset, uint8_t data) {
                    auto* screen = static_cast<SolarIIScreen*>(region->userdata);
                    screen->display_data[offset - DISPLAY_BASE_ADDR] = data;
                },
                emulator);
            region_range.Setup(0xF030, 1, "SolarIIScreen/Range", &screen_range, MMURegion::DefaultRead<uint8_t>, MMURegion::DefaultWrite<uint8_t>, emulator);
            region_mode.Setup(0xF031, 1, "SolarIIScreen/Mode", &screen_mode, MMURegion::DefaultRead<uint8_t>, MMURegion::DefaultWrite<uint8_t>, emulator);
            region_contrast.Setup(0xF032, 1, "SolarIIScreen/Contrast", &screen_contrast, MMURegion::DefaultRead<uint8_t, 0x1F>, MMURegion::DefaultWrite<uint8_t, 0x1F>, emulator);
            region_brightness.Setup(0xF033, 1, "SolarIIScreen/Brightness", &screen_brightness, MMURegion::DefaultRead<uint8_t, 0x07>, MMURegion::DefaultWrite<uint8_t, 0x07>, emulator);
            region_refresh_rate.Setup(0xF034, 1, "SolarIIScreen/RefreshRate", &screen_refresh_rate, MMURegion::DefaultRead<uint8_t>, MMURegion::DefaultWrite<uint8_t>, emulator);
        }

        void* QueryInterface(const char* name) override {
            if (strcmp(name, typeid(IScreenFrameProvider).name()) == 0) {
                return static_cast<IScreenFrameProvider*>(this);
            }
            return Peripheral::QueryInterface(name);
        }

        void SaveState(std::ostream& os) override {
            os.write(reinterpret_cast<const char*>(&display_control), 1);
            os.write(reinterpret_cast<const char*>(display_data.data()), display_data.size());
            os.write(reinterpret_cast<const char*>(&screen_range), 1);
            os.write(reinterpret_cast<const char*>(&screen_mode), 1);
            os.write(reinterpret_cast<const char*>(&screen_contrast), 1);
            os.write(reinterpret_cast<const char*>(&screen_brightness), 1);
            os.write(reinterpret_cast<const char*>(&screen_refresh_rate), 1);
            os.write(reinterpret_cast<const char*>(status_alpha.data()), status_alpha.size());
        }

        void LoadState(std::istream& is) override {
            is.read(reinterpret_cast<char*>(&display_control), 1);
            is.read(reinterpret_cast<char*>(display_data.data()), display_data.size());
            is.read(reinterpret_cast<char*>(&screen_range), 1);
            is.read(reinterpret_cast<char*>(&screen_mode), 1);
            is.read(reinterpret_cast<char*>(&screen_contrast), 1);
            is.read(reinterpret_cast<char*>(&screen_brightness), 1);
            is.read(reinterpret_cast<char*>(&screen_refresh_rate), 1);
            is.read(reinterpret_cast<char*>(status_alpha.data()), status_alpha.size());
        }

        void UpdateFrameAlpha() override {
            const uint8_t* data = DisplayData();
            if (!data) {
                status_alpha.fill(0);
                return;
            }
            constexpr float kResidualFadeRatio = 0.50f;
            const bool display_enabled = IsSolarIIDisplayEnabled();
            if (!display_enabled) {
                if (screen_residual_enabled) {
                    for (auto& alpha : status_alpha) {
                        alpha = static_cast<uint8_t>(std::clamp(static_cast<int>(static_cast<float>(alpha) * kResidualFadeRatio + 0.5f), 0, 255));
                    }
                }
                else {
                    status_alpha.fill(0);
                }
                return;
            }
            for (size_t i = 0; i < STATUS_BITS.size(); ++i) {
                const auto bit = STATUS_BITS[i];
                const bool enabled = bit.offset < DISPLAY_STORAGE_LEN && (data[bit.offset] & (1 << bit.bit));
                const uint8_t target = CalculateSolarIIStatusAlpha(enabled);
                if (screen_residual_enabled) {
                    const float alpha = static_cast<float>(status_alpha[i]) * kResidualFadeRatio + static_cast<float>(target) * (1.0f - kResidualFadeRatio);
                    status_alpha[i] = static_cast<uint8_t>(std::clamp(static_cast<int>(alpha + 0.5f), 0, 255));
                }
                else {
                    status_alpha[i] = target;
                }
            }
        }

        int GetFrameWidth() const override { return FRAME_WIDTH; }
        int GetFrameHeight() const override { return FRAME_HEIGHT_WITH_STATUS_ROW; }
        void WriteFrameRgba(uint8_t* out, int r, int g, int b) const override {
            (void)r;
            (void)g;
            (void)b;
            if (!out) return;
            std::fill(out, out + FRAME_WIDTH * FRAME_HEIGHT_WITH_STATUS_ROW * 4, 0);
        }
        int GetStatusAlphaCount() const override { return static_cast<int>(status_alpha.size()); }
        void WriteStatusAlpha(uint8_t* out, int max_len) const override {
            if (!out || max_len <= 0) return;
            const int count = std::min(max_len, GetStatusAlphaCount());
            std::copy(status_alpha.begin(), status_alpha.begin() + count, out);
        }
        void Frame() override {
            if (!renderer || !interface_texture)
                return;

            UpdateFrameAlpha();
            SDL_SetTextureColorMod(interface_texture, ink_colour.r, ink_colour.g, ink_colour.b);
            for (size_t i = 0; i < status_alpha.size(); ++i) {
                if (!status_sprite_present[i])
                    continue;
                RenderModelSprite(renderer, interface_texture, &status_svg_textures[i], status_sprite_info[i], ink_colour, status_alpha[i]);
            }
            SDL_SetTextureAlphaMod(interface_texture, 255);
            SDL_SetTextureColorMod(interface_texture, 255, 255, 255);
        }
    };

Peripheral* CreateSolarIIScreen(Emulator& emulator) {
	return new SolarIIScreen(emulator);
}
} // namespace casioemu
