/*

		Screen peripheral implement.
		Copyright (C) 2024 telecomadm1145/Xyzst/user202729/LBPHacker/hieuxyz

		This program is free software: you can redistribute it and/or modify
		it under the terms of the GNU General Public License as published by
		the Free Software Foundation, either version 3 of the License, or
		(at your option) any later version.

		This program is distributed in the hope that it will be useful,
		but WITHOUT ANY WARRANTY; without even the implied warranty of
		MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
		GNU General Public License for more details.

		You should have received a copy of the GNU General Public License
		along with this program.  If not, see <http://www.gnu.org/licenses/>.

*/
#include "Screen.hpp"
#include "Chipset/Chipset.hpp"
#include "Chipset/MMU.hpp"
#include "Chipset/MMURegion.hpp"
#include "Chipset/ePSCpu.h"
#include "Chipset/Eps6800Display.h"
#include "Emulator.hpp"
#include "Ext/Random.hpp"
#include "Gui/HwController.h"
#include "Logger.hpp"
#include "ML620Ports.h"
#include "ModelInfo.h"
#include "Models.h"
#include "PopUpDisplay.h"
#include "Ui.hpp"
#include <SDL_image.h>
#include <algorithm> // for std::min, std::max
#include <array>
#include <atomic>
#include <chrono>
#include <climits>
#include <cmath>
#include <cstdlib>
#include <cstdio>
#include <ctime> // for std::time
#include <filesystem>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <sstream>
#include <string_view>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>

#if !defined(__ANDROID__) && !defined(__EMSCRIPTEN__)
#include "Theme.h"
#elif defined(__ANDROID__)
#include <android/api-level.h>
#include <fcntl.h>
#include <media/NdkMediaCodec.h>
#include <media/NdkMediaFormat.h>
#include <media/NdkMediaMuxer.h>
#include <unistd.h>
#endif

#ifdef _WIN32
#include <windows.h>
#include <wingdi.h>
// Undefine Windows min/max macros to avoid conflicts with std::min/std::max
#undef min
#undef max
#endif
extern bool low_perf_ext;

#ifdef __ANDROID__
#include <android/api-level.h>
#include <android/log.h>
#include <jni.h>

bool saveImageToMediaStore(const void* pixels, int width, int height, int pitch, const char* filename) {
	JNIEnv* env = (JNIEnv*)SDL_AndroidGetJNIEnv();
	jobject activity = (jobject)SDL_AndroidGetActivity();

	// Create a Java direct ByteBuffer from the pixel data
	jobject byteBuffer = env->NewDirectByteBuffer((void*)pixels, height * pitch);

	// Call the Java method to handle saving to MediaStore
	jclass activityClass = env->GetObjectClass(activity);
	jmethodID saveImageMethod = env->GetMethodID(activityClass, "saveImageToMediaStore",
		"(Ljava/nio/ByteBuffer;IIILjava/lang/String;)Z");

	// If the method doesn't exist, we need to add it to the Java side
	if (saveImageMethod == NULL) {
		SDL_Log("Error: saveImageToMediaStore method not found. Please add it to your Java activity.");
		env->DeleteLocalRef(byteBuffer);
		env->DeleteLocalRef(activityClass);
		env->DeleteLocalRef(activity);
		return false;
	}

	jstring jfilename = env->NewStringUTF(filename);
	jboolean result = env->CallBooleanMethod(activity, saveImageMethod, byteBuffer, width, height, pitch, jfilename);

	env->DeleteLocalRef(jfilename);
	env->DeleteLocalRef(byteBuffer);
	env->DeleteLocalRef(activityClass);
	env->DeleteLocalRef(activity);

	return result;
}
#endif

inline constexpr uint8_t reverse_bits(uint8_t n) {
	uint8_t reversed = 0;
	for (int i = 0; i < 8; ++i) {
		reversed |= ((n >> i) & 1) << (7 - i);
	}
	return reversed;
}

// constexpr 生成查找表
inline constexpr std::array<uint8_t, 256> generate_lookup_table() {
	std::array<uint8_t, 256> table = {};
	for (int i = 0; i < 256; ++i) {
		table[i] = reverse_bits(static_cast<uint8_t>(i));
	}
	return table;
}

// 定义查找表
constexpr auto bit_lookup_table = generate_lookup_table();

inline void fillRandomData(unsigned char* buf, size_t size) {
	util::Random::fillRandomBytes(reinterpret_cast<std::uint8_t*>(buf), size);
}

#pragma warning(disable : 4244)

namespace casioemu {
	SDL_Texture* CreateSvgSpriteTexture(SDL_Renderer* renderer, const SpriteInfo& sprite, int width, int height, SDL_Rect* content_bounds) {
		if (!renderer || sprite.svg_shape.empty() || width <= 0 || height <= 0)
			return nullptr;
		if (content_bounds)
			*content_bounds = {0, 0, width, height};
		SDL_RWops* rw = SDL_RWFromConstMem(sprite.svg_shape.data(), static_cast<int>(sprite.svg_shape.size()));
		if (!rw) {
			SDL_Log("[Screen][Warn] SDL_RWFromConstMem failed for SVG sprite: %s", SDL_GetError());
			return nullptr;
		}
		SDL_Surface* surface = IMG_LoadSizedSVG_RW(rw, width, height);
		SDL_RWclose(rw);
		if (!surface) {
			SDL_Log("[Screen][Warn] IMG_LoadSizedSVG_RW failed for SVG sprite: %s", IMG_GetError());
			return nullptr;
		}
		SDL_Surface* converted = SDL_ConvertSurfaceFormat(surface, SDL_PIXELFORMAT_RGBA32, 0);
		SDL_FreeSurface(surface);
		if (!converted) {
			SDL_Log("[Screen][Warn] SDL_ConvertSurfaceFormat failed for SVG sprite: %s", SDL_GetError());
			return nullptr;
		}
		if (content_bounds) {
			int min_x = converted->w;
			int min_y = converted->h;
			int max_x = -1;
			int max_y = -1;
			if (!SDL_MUSTLOCK(converted) || SDL_LockSurface(converted) == 0) {
				for (int y = 0; y < converted->h; ++y) {
					const auto* row = reinterpret_cast<const Uint32*>(
						static_cast<const Uint8*>(converted->pixels) + y * converted->pitch);
					for (int x = 0; x < converted->w; ++x) {
						Uint8 red = 0, green = 0, blue = 0, alpha = 0;
						SDL_GetRGBA(row[x], converted->format, &red, &green, &blue, &alpha);
						if (alpha == 0)
							continue;
						min_x = std::min(min_x, x);
						min_y = std::min(min_y, y);
						max_x = std::max(max_x, x);
						max_y = std::max(max_y, y);
					}
				}
				if (SDL_MUSTLOCK(converted))
					SDL_UnlockSurface(converted);
			}
			if (max_x >= min_x && max_y >= min_y)
				*content_bounds = {min_x, min_y, max_x - min_x + 1, max_y - min_y + 1};
		}
		SDL_Texture* texture = SDL_CreateTextureFromSurface(renderer, converted);
		SDL_FreeSurface(converted);
		if (!texture) {
			SDL_Log("[Screen][Warn] SDL_CreateTextureFromSurface failed for SVG sprite: %s", SDL_GetError());
			return nullptr;
		}
		SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_BLEND);
		return texture;
	}

	class SvgSpriteTextureCache {
	public:
		SvgSpriteTextureCache() = default;
		SvgSpriteTextureCache(const SvgSpriteTextureCache&) = delete;
		SvgSpriteTextureCache& operator=(const SvgSpriteTextureCache&) = delete;
		SvgSpriteTextureCache(SvgSpriteTextureCache&& other) noexcept {
			MoveFrom(other);
		}
		SvgSpriteTextureCache& operator=(SvgSpriteTextureCache&& other) noexcept {
			if (this != &other) {
				Reset();
				MoveFrom(other);
			}
			return *this;
		}
		~SvgSpriteTextureCache() {
			Reset();
		}

		void Reset() {
			if (texture) {
				SDL_DestroyTexture(texture);
				texture = nullptr;
			}
			width = 0;
			height = 0;
			shape_size = 0;
			shape_hash = 0;
			content_bounds = {};
			software_renderer = false;
		}

		SDL_Texture* Get(SDL_Renderer* renderer, const SpriteInfo& sprite, int requested_width, int requested_height) {
			if (sprite.svg_shape.empty())
				return nullptr;
			const int target_width = std::max(1, requested_width);
			const int target_height = std::max(1, requested_height);
			const auto target_shape_size = sprite.svg_shape.size();
			const auto target_shape_hash = std::hash<std::string>{}(sprite.svg_shape);
			if (!texture || width != target_width || height != target_height ||
				shape_size != target_shape_size || shape_hash != target_shape_hash) {
				Reset();
				SDL_RendererInfo renderer_info{};
				software_renderer = SDL_GetRendererInfo(renderer, &renderer_info) == 0 &&
					renderer_info.name && std::string_view(renderer_info.name) == "software";
				texture = CreateSvgSpriteTexture(renderer, sprite, target_width, target_height,
					software_renderer ? &content_bounds : nullptr);
				software_renderer = texture && software_renderer;
				width = texture ? target_width : 0;
				height = texture ? target_height : 0;
				shape_size = texture ? target_shape_size : 0;
				shape_hash = texture ? target_shape_hash : 0;
			}
			return texture;
		}

		bool ContentClip(const SDL_Rect& dest, SDL_Rect& clip) const {
			if (!software_renderer || width <= 0 || height <= 0 || content_bounds.w <= 0 || content_bounds.h <= 0)
				return false;
			const int left = static_cast<int>(std::floor(static_cast<double>(content_bounds.x) * dest.w / width));
			const int top = static_cast<int>(std::floor(static_cast<double>(content_bounds.y) * dest.h / height));
			const int right = static_cast<int>(std::ceil(static_cast<double>(content_bounds.x + content_bounds.w) * dest.w / width));
			const int bottom = static_cast<int>(std::ceil(static_cast<double>(content_bounds.y + content_bounds.h) * dest.h / height));
			clip = {dest.x + left - 1, dest.y + top - 1, std::max(1, right - left + 2), std::max(1, bottom - top + 2)};
			return true;
		}

	private:
		void MoveFrom(SvgSpriteTextureCache& other) noexcept {
			texture = std::exchange(other.texture, nullptr);
			width = std::exchange(other.width, 0);
			height = std::exchange(other.height, 0);
			shape_size = std::exchange(other.shape_size, 0);
			shape_hash = std::exchange(other.shape_hash, 0);
			content_bounds = std::exchange(other.content_bounds, SDL_Rect{});
			software_renderer = std::exchange(other.software_renderer, false);
		}

		SDL_Texture* texture = nullptr;
		int width = 0;
		int height = 0;
		size_t shape_size = 0;
		size_t shape_hash = 0;
		SDL_Rect content_bounds{};
		bool software_renderer = false;
	};

	std::pair<int, int> CurrentRenderTargetSpriteSize(SDL_Renderer* renderer, const SDL_Rect& dest) {
		float scale_x = 1.0f;
		float scale_y = 1.0f;
		if (renderer)
			SDL_RenderGetScale(renderer, &scale_x, &scale_y);
		return {
			std::max(1, static_cast<int>(std::lround(std::abs(static_cast<double>(dest.w) * scale_x)))),
			std::max(1, static_cast<int>(std::lround(std::abs(static_cast<double>(dest.h) * scale_y))))};
	}

	void RenderModelSprite(SDL_Renderer* renderer, SDL_Texture* interface_texture, SvgSpriteTextureCache* svg_texture, const SpriteInfo& sprite, const ColourInfo& ink_colour, uint8_t alpha) {
		SDL_Rect dest = sprite.dest;
		SDL_Texture* texture = nullptr;
		if (svg_texture) {
			auto [target_width, target_height] = CurrentRenderTargetSpriteSize(renderer, dest);
			texture = svg_texture->Get(renderer, sprite, target_width, target_height);
		}
		if (texture) {
			SDL_Rect old_clip{};
			const SDL_bool old_clip_enabled = SDL_RenderIsClipEnabled(renderer);
			if (old_clip_enabled)
				SDL_RenderGetClipRect(renderer, &old_clip);
			SDL_Rect content_clip{};
			bool content_clip_enabled = svg_texture && svg_texture->ContentClip(dest, content_clip);
			if (content_clip_enabled && old_clip_enabled) {
				SDL_Rect intersection{};
				if (SDL_IntersectRect(&content_clip, &old_clip, &intersection))
					content_clip = intersection;
				else
					content_clip = {0, 0, 0, 0};
			}
			if (content_clip_enabled)
				SDL_RenderSetClipRect(renderer, &content_clip);
			SDL_SetTextureColorMod(texture, ink_colour.r, ink_colour.g, ink_colour.b);
			SDL_SetTextureAlphaMod(texture, alpha);
			SDL_RenderCopy(renderer, texture, nullptr, &dest);
			SDL_SetTextureAlphaMod(texture, 255);
			SDL_SetTextureColorMod(texture, 255, 255, 255);
			if (content_clip_enabled)
				SDL_RenderSetClipRect(renderer, old_clip_enabled ? &old_clip : nullptr);
			return;
		}
		if (!interface_texture)
			return;
		SDL_SetTextureAlphaMod(interface_texture, alpha);
		SDL_Rect src = sprite.src;
		SDL_RenderCopy(renderer, interface_texture, &src, &dest);
	}

#ifndef CASIOEMU_CORE_WEB
	SDL_Color ScreenPixelColour(const ColourInfo& ink_colour, float alpha_value) {
		SDL_Color colour{
			static_cast<Uint8>(ink_colour.r),
			static_cast<Uint8>(ink_colour.g),
			static_cast<Uint8>(ink_colour.b),
			Uint8(std::clamp(static_cast<int>(alpha_value), 0, 255))};
		if (alpha_value <= 0.0f) {
			colour.a = 0;
		}
		else if (alpha_value > 255.0f) {
			const int extra = static_cast<int>(alpha_value - 255.0f);
			colour.r = static_cast<uint8_t>(std::max(0, ink_colour.r - extra));
			colour.g = static_cast<uint8_t>(std::max(0, ink_colour.g - static_cast<int>(extra * 0.8f)));
			colour.b = static_cast<uint8_t>(std::max(0, ink_colour.b - static_cast<int>(extra * 0.1f)));
			colour.a = 255;
		}
		return colour;
	}
#endif

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
	struct SpriteBitmap {
		const char* name;
		uint8_t mask, offset;
	};
	inline int update_screen_scan_alpha(float* screen_scan_alpha, Uint64 t, int screen_refresh_rate) {
		int n = (static_cast<Uint64>((t * screen_refresh_rate) / 250)) % 64;

		if (screen_refresh_rate < screen_flashing_threshold) {
			for (size_t i = 0; i < 64; i++) {
				screen_scan_alpha[i] = 1.0f;
			}
			return n;
		}

		// 计算归一化所需的归一化因子
		float normalization_factor = 0.0f;
		std::vector<float> exp_values(64);

		for (size_t i = 0; i < 64; i++) {
			exp_values[i] = std::exp(-screen_flashing_brightness_coeff * i / 64.0f);
			normalization_factor += exp_values[i];
		}

		// 归一化
		for (size_t i = 0; i < 64; i++) {
			screen_scan_alpha[(i + n) % 64] = std::pow(exp_values[i] / normalization_factor * 80., 0.2);
		}

		return n;
	}
	template <HardwareId hardware_id>
	class Screen : public Peripheral, public IScreenFrameProvider {
		static int const N_ROW,
			ROW_SIZE,
			OFFSET,
			ROW_SIZE_DISP,
			SPR_MAX;

		MMURegion region_buffer{}, region_buffer1{}, region_contrast{}, region_brightness{}, region_scan_report_op1{}, region_mode{}, region_range{}, region_select{}, region_offset{}, region_refresh_rate{}, region_scan_report{};
		uint8_t* screen_buffer{}, * screen_buffer1{}, screen_contrast{}, screen_brightness{}, screen_scan_report_op1{}, screen_mode{}, screen_range{}, screen_select{}, screen_offset{}, screen_refresh_rate{}, screen_scan_report{};

		MMURegion region_power{}, region_scan_report_en{};
		uint8_t screen_power{}, screen_scan_report_en{};

		MMURegion region_unk1{}, region_unk2{};

		uint8_t unk_f034{};

		// TI things

		MMURegion ti_port7_data{}, ti_port5_data{};
		int ti_contrast{}, ti_port_status{};
		bool ti_enabled = 0;
		bool ti_a0 = 0;
		bool ti_rw = 0;
		int ti_col = 0;
		int ti_page = 0;

		int ti_port7{};
		int ti_port5{};

		float screen_scan_alpha[64]{};
		float position = 0;
		SDL_Renderer* renderer{};
		SDL_Texture* interface_texture{};
#ifndef CASIOEMU_CORE_WEB
		SDL_Texture* pixel_screen_texture{};
		int pixel_screen_texture_width = 0;
		int pixel_screen_texture_height = 0;
		std::vector<uint8_t> pixel_screen_pixels;
#endif
		float screen_ink_alpha[66 * 192]{};
		std::array<float, 66 * 192> eps_screen_ink_alpha{};
		std::mutex eps_screen_alpha_mutex;
		std::atomic_bool eps_screen_thread_running{false};
		std::thread eps_screen_thread;
		static const SpriteBitmap sprite_bitmap[];
		std::vector<SpriteInfo> sprite_info;
		std::vector<SvgSpriteTextureCache> sprite_svg_textures;
		std::vector<uint8_t> sprite_available;
		ColourInfo ink_colour{};

		bool inited = 0;
		bool enabled_2 = 0;
		int status_ink_alpha_on = 255;
		int status_ink_alpha_off = 0;

		int SpriteCount() const {
			if constexpr (IsEpsFamily(hardware_id))
				return static_cast<int>(emulator.ModelDefinition.status_indicators.size()) + 1;
			return SPR_MAX;
		}

		bool StatusEnabled() const {
			if (!(hardware_id == HW_CLASSWIZ || hardware_id == HW_CLASSWIZ_II) && hardware_id != HW_FX_5800P && hardware_id != HW_ES_PLUS && !IsEpsFamily(hardware_id)) {
				return true;
			}
			if (!enabled_2 || (screen_range & 0b100000)) {
				return false;
			}
			const auto mode = screen_mode & 7;
			return mode == 5 || mode == 6;
		}

		uint8_t LogicalAlpha(float value) const {
			return static_cast<uint8_t>(std::clamp(static_cast<int>(std::lround(value * 255.0f)), 0, 255));
		}

		static constexpr float kClassWizIILowerPlaneWeight = 1.0f / 3.0f;
		static constexpr float kClassWizIIUpperPlaneWeight = 2.0f / 3.0f;

		uint8_t ClassWizIIStatusAlpha(uint8_t offset, uint8_t mask) const {
			if (!StatusEnabled() || !screen_buffer || !screen_buffer1) return 0;
			const auto status_offset = (offset + screen_offset * ROW_SIZE) % ((N_ROW + 1) * ROW_SIZE);
			const bool lower = (screen_buffer[status_offset] & mask) != 0;
			const bool upper = (screen_buffer1[status_offset] & mask) != 0;
			if (!screen_residual_enabled) {
				return LogicalAlpha((lower ? kClassWizIILowerPlaneWeight : 0.0f) + (upper ? kClassWizIIUpperPlaneWeight : 0.0f));
			}
			float alpha = static_cast<float>(status_ink_alpha_off);
			alpha += (static_cast<float>(status_ink_alpha_on - status_ink_alpha_off)) * (lower ? kClassWizIILowerPlaneWeight : 0.0f);
			alpha += (static_cast<float>(status_ink_alpha_on - status_ink_alpha_off)) * (upper ? kClassWizIIUpperPlaneWeight : 0.0f);
			if (screen_refresh_rate >= screen_flashing_threshold) {
				alpha *= screen_scan_alpha[0];
			}
			return static_cast<uint8_t>(std::clamp(static_cast<int>(alpha), 0, 255));
		}

#ifndef CASIOEMU_CORE_WEB
		void ResetPixelScreenTexture() {
			if (pixel_screen_texture) {
				SDL_DestroyTexture(pixel_screen_texture);
				pixel_screen_texture = nullptr;
			}
			pixel_screen_texture_width = 0;
			pixel_screen_texture_height = 0;
			pixel_screen_pixels.clear();
		}

		bool EnsurePixelScreenTexture(int width, int height) {
			if (!renderer || width <= 0 || height <= 0)
				return false;
			if (pixel_screen_texture && pixel_screen_texture_width == width && pixel_screen_texture_height == height)
				return true;

			ResetPixelScreenTexture();
			pixel_screen_texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STREAMING, width, height);
			if (!pixel_screen_texture) {
				SDL_Log("[Screen][Warn] SDL_CreateTexture failed for pixel screen: %s", SDL_GetError());
				return false;
			}
			SDL_SetTextureBlendMode(pixel_screen_texture, SDL_BLENDMODE_BLEND);
#if SDL_VERSION_ATLEAST(2, 0, 12)
			SDL_SetTextureScaleMode(pixel_screen_texture, SDL_ScaleModeNearest);
#endif
			pixel_screen_texture_width = width;
			pixel_screen_texture_height = height;
			pixel_screen_pixels.resize(static_cast<size_t>(width) * static_cast<size_t>(height) * 4);
			return true;
		}

		void WritePixelScreenTexture(int logical_width, int logical_height) {
			if (pixel_screen_pixels.size() < static_cast<size_t>(logical_width) * static_cast<size_t>(logical_height) * 4)
				return;
			for (int y = 0; y != logical_height; ++y) {
				const int source_y = y + 1;
				for (int x = 0; x != logical_width; ++x) {
					const float alpha_value = screen_ink_alpha[x + source_y * 192];
					const SDL_Color colour = ScreenPixelColour(ink_colour, alpha_value);

					const size_t pixel_offset = (static_cast<size_t>(y) * static_cast<size_t>(logical_width) + static_cast<size_t>(x)) * 4;
					pixel_screen_pixels[pixel_offset + 0] = colour.r;
					pixel_screen_pixels[pixel_offset + 1] = colour.g;
					pixel_screen_pixels[pixel_offset + 2] = colour.b;
					pixel_screen_pixels[pixel_offset + 3] = colour.a;
				}
			}
		}

		bool RenderPixelScreenTexture(const SDL_Rect& lcd_dest, int logical_width, int logical_height) {
			if (!EnsurePixelScreenTexture(logical_width, logical_height))
				return false;
			WritePixelScreenTexture(logical_width, logical_height);
			if (SDL_UpdateTexture(pixel_screen_texture, nullptr, pixel_screen_pixels.data(), logical_width * 4) != 0) {
				SDL_Log("[Screen][Warn] SDL_UpdateTexture failed for pixel screen: %s", SDL_GetError());
				return false;
			}
			if (SDL_RenderCopy(renderer, pixel_screen_texture, nullptr, &lcd_dest) != 0) {
				SDL_Log("[Screen][Warn] SDL_RenderCopy failed for pixel screen: %s", SDL_GetError());
				return false;
			}
			return true;
		}
#endif

	public:
		Screen(Emulator& emu)
			: Peripheral(emu) {
#if !defined(TEST_BUILD) && !defined(__EMSCRIPTEN__)
			if constexpr (IsEpsFamily(hardware_id)) {
				eps_screen_thread_running.store(true, std::memory_order_release);
				eps_screen_thread = std::thread([this]() {
					while (eps_screen_thread_running.load(std::memory_order_acquire)) {
						tick();
						SDL_Delay(10);
					}
				});
			}
			else {
				std::thread thd([&]() {
					while (1) {
						tick();
#ifdef __ANDROID__
						SDL_Delay(10);
#elif !defined(__EMSCRIPTEN__)
						if (ThemeManager::Instance().Settings().lowPerformanceMode || low_perf_ext)
							SDL_Delay(10);
#endif
					}
				});
				thd.detach();
			}
#endif
		}
		~Screen() {
			if constexpr (IsEpsFamily(hardware_id)) {
				eps_screen_thread_running.store(false, std::memory_order_release);
				if (eps_screen_thread.joinable())
					eps_screen_thread.join();
			}
			for (auto& texture : sprite_svg_textures)
				texture.Reset();
#ifndef CASIOEMU_CORE_WEB
			ResetPixelScreenTexture();
#endif
			if (screen_buffer)
				delete[] screen_buffer;
			if (screen_buffer1)
				delete[] screen_buffer1;
		}
		void Initialise() override;
		void Uninitialise() override;
		void Frame() override;
		void Reset() override;
		void* QueryInterface(const char* name) override {
			if (strcmp(name, typeid(IScreenFrameProvider).name()) == 0) {
				return static_cast<IScreenFrameProvider*>(this);
			}
			return Peripheral::QueryInterface(name);
		}
		void UpdateFrameAlpha() override {
#ifdef __EMSCRIPTEN__
			tick();
			if constexpr (IsEpsFamily(hardware_id)) {
				std::lock_guard<std::mutex> lock(eps_screen_alpha_mutex);
				std::copy(eps_screen_ink_alpha.begin(), eps_screen_ink_alpha.end(), screen_ink_alpha);
			}
#endif
		}
		int GetFrameWidth() const override {
			if constexpr (hardware_id == HW_EPS6009)
				return std::max(1, emulator.ModelDefinition.screen_width);
			return hardware_id == HW_EPS6800 || hardware_id == HW_EPS9500 ? 96 : 192;
		}
		int GetFrameHeight() const override {
			if constexpr (hardware_id == HW_EPS6009)
				return std::max(1, emulator.ModelDefinition.screen_height);
			if constexpr (hardware_id == HW_EPS9500)
				return 33; // one status row plus 32 dot-matrix rows
			return hardware_id == HW_FX_5800P || hardware_id == HW_ES_PLUS ||
				hardware_id == HW_EPS6800 ? 32 : 64;
		}
		void WriteFrameRgba(uint8_t* out, int r, int g, int b) const override {
			if (!out) return;
			const int width = GetFrameWidth();
			const int height = GetFrameHeight();
			if constexpr (hardware_id == HW_EPS6009) {
				std::fill(out, out + static_cast<size_t>(width) * static_cast<size_t>(height) * 4, 0);
				return;
			}
			for (int y = 0; y < height; ++y) {
				for (int x = 0; x < width; ++x) {
					const float alpha = screen_ink_alpha[y * 192 + x];
					const int idx = (y * width + x) * 4;
					out[idx + 0] = static_cast<uint8_t>(std::clamp(r, 0, 255));
					out[idx + 1] = static_cast<uint8_t>(std::clamp(g, 0, 255));
					out[idx + 2] = static_cast<uint8_t>(std::clamp(b, 0, 255));
					out[idx + 3] = static_cast<uint8_t>(std::clamp(static_cast<int>(alpha), 0, 255));
				}
			}
		}
		int GetStatusAlphaCount() const override {
			return std::max(0, SpriteCount() - 1);
		}
		void WriteStatusAlpha(uint8_t* out, int max_len) const override {
			if (!out || max_len <= 0) return;
			const int count = std::min(max_len, GetStatusAlphaCount());
			for (int i = 0; i < count; ++i) out[i] = 0;
			for (int i = 0; i < count; ++i) {
				out[i] = static_cast<uint8_t>(std::clamp(static_cast<int>(screen_ink_alpha[i]), 0, 255));
			}
		}
		void SaveState(std::ostream& os) override {
			size_t bufSize = (hardware_id == HW_TI) ? (192 * 9) : (N_ROW + 1) * ROW_SIZE;
			if (screen_buffer)
				os.write(reinterpret_cast<const char*>(screen_buffer), bufSize);
			uint8_t hasBuf1 = (screen_buffer1 != nullptr) ? 1 : 0;
			os.write(reinterpret_cast<const char*>(&hasBuf1), 1);
			if (screen_buffer1)
				os.write(reinterpret_cast<const char*>(screen_buffer1), bufSize);
			os.write(reinterpret_cast<const char*>(&screen_contrast), 1);
			os.write(reinterpret_cast<const char*>(&screen_brightness), 1);
			os.write(reinterpret_cast<const char*>(&screen_mode), 1);
			os.write(reinterpret_cast<const char*>(&screen_range), 1);
			os.write(reinterpret_cast<const char*>(&screen_offset), 1);
			os.write(reinterpret_cast<const char*>(&screen_refresh_rate), 1);
			os.write(reinterpret_cast<const char*>(&screen_power), 1);
		}
		void LoadState(std::istream& is) override {
			size_t bufSize = (hardware_id == HW_TI) ? (192 * 9) : (N_ROW + 1) * ROW_SIZE;
			if (screen_buffer)
				is.read(reinterpret_cast<char*>(screen_buffer), bufSize);
			uint8_t hasBuf1 = 0;
			is.read(reinterpret_cast<char*>(&hasBuf1), 1);
			if (hasBuf1 && screen_buffer1)
				is.read(reinterpret_cast<char*>(screen_buffer1), bufSize);
			is.read(reinterpret_cast<char*>(&screen_contrast), 1);
			is.read(reinterpret_cast<char*>(&screen_brightness), 1);
			is.read(reinterpret_cast<char*>(&screen_mode), 1);
			is.read(reinterpret_cast<char*>(&screen_range), 1);
			is.read(reinterpret_cast<char*>(&screen_offset), 1);
			is.read(reinterpret_cast<char*>(&screen_refresh_rate), 1);
			is.read(reinterpret_cast<char*>(&screen_power), 1);
		}
		void tick() {
			float ratio = 0;
			if constexpr (hardware_id == HW_FX_5800P || hardware_id == HW_ES_PLUS)
				ratio = 1 - 1e-4;
			else
				ratio = 1 - 5e-4;
#ifdef __EMSCRIPTEN__
			ratio = 0.0f;
#elif defined(__ANDROID__)
			ratio = 0.80f;
#else
			if (ThemeManager::Instance().Settings().lowPerformanceMode || low_perf_ext) {
				ratio = 0.80f;
			}
#endif
			if constexpr (hardware_id == HW_TI) {
				ratio = 1 - 1e-4;
#ifdef __EMSCRIPTEN__
				ratio = 0.0f;
#elif defined(__ANDROID__)
				ratio = 0.80f;
#else
				if (ThemeManager::Instance().Settings().lowPerformanceMode || low_perf_ext) {
					ratio = 0.80f;
				}
#endif
				if (!ti_enabled) {
					for (size_t i = 0; i < 65 * 192; i++) {
						screen_ink_alpha[i] *= ratio;
					}
					return;
				}
				if (!n_ram_buffer) //  || !emulator.chipset.ti_status_buf) //  || !emulator.chipset.ti_screen_buf
					return;
				float ink_alpha_on = (ti_contrast - 100) * 20.0;
				float ink_alpha_off = std::clamp(ink_alpha_on * 0.1, 0.0, 255.0);
				ink_alpha_off = screen_residual_enabled ? ink_alpha_off * screen_residual_alpha_scale : 0.0f;
				ink_alpha_on = std::clamp(ink_alpha_on, 0.0f, 255.0f);
				if (!screen_residual_enabled) {
					ink_alpha_on = 255.0f;
				}
				uint8_t* screen_buffer = (uint8_t*)n_ram_buffer - casioemu::GetRamBaseAddr(hardware_id) + 0xE708;
				if (emulator.ModelDefinition.real_hardware) {
					screen_buffer = this->screen_buffer;
				}
				for (int ix = 0; ix < 192; ++ix) {
					for (int iy = 0; iy < 64; ++iy) {
						uint32_t i = (ix << 6) | iy;
						int bIndx = (i >> 3);
						int subIndx = (i & 7);
						int mask = (1 << subIndx);
						bool on = (screen_buffer[bIndx] & mask) != 0;
						auto& data = screen_ink_alpha[(iy * 192 + 192) + ix];
						data = data * ratio + (on ? ink_alpha_on : ink_alpha_off) * (1 - ratio);
					}
				}
				screen_buffer = (uint8_t*)n_ram_buffer - casioemu::GetRamBaseAddr(hardware_id) + 0xe5d4;
				if (emulator.ModelDefinition.real_hardware) {
					screen_buffer = this->screen_buffer + 8 * 192;
				}
				int x = 0;
				for (int ix = 1; ix != SPR_MAX; ++ix) {
					auto off = sprite_bitmap[ix].offset;
					auto& data = screen_ink_alpha[x];
					data = data * ratio + ((screen_buffer[off] & sprite_bitmap[ix].mask) ? ink_alpha_on : ink_alpha_off) * (1 - ratio);
					x++;
				}

				return;
			}
			else if (hardware_id == HW_EPS6800) {
				// Match the deterministic ES Plus low-performance cadence: one
				// update every 10 ms, retaining 80% of the preceding LCD state.
				ratio = 0.80f;
				std::array<uint8_t, EPS6800_LCD_RAW_SIZE> lcd{};
				Eps6800LcdControl control{};
				if (!emulator.chipset.epscpu ||
					emulator.chipset.epscpu->CopyLcd(lcd.data(), lcd.size(), &control) != lcd.size())
					return;
				float ink_alpha_on = Eps6800ActiveAlpha(control.contrast);
				float ink_alpha_off = Eps6800InactiveAlpha(control.contrast);
				ink_alpha_off = screen_residual_enabled ? ink_alpha_off * screen_residual_alpha_scale : 0.0f;
				if (!control.visible()) {
					ink_alpha_on = 0.0f;
					ink_alpha_off = 0.0f;
				}
				const float transition_ratio = screen_residual_enabled ? ratio : 0.0f;
				std::lock_guard<std::mutex> lock(eps_screen_alpha_mutex);

				// EPS stores four 8-pixel pages bottom-to-top. The physical top
				// row is a 96-bit segmented annunciator bus; it must not be drawn
				// as dot-matrix pixels. The remaining 31 rows form the 96x31 LCD.
				const auto decoded = DecodeEps6800Display(lcd.data(), lcd.size());
				for (int y = 0; y < static_cast<int>(EPS6800_LCD_PIXEL_HEIGHT); ++y) {
					for (int x = 0; x < static_cast<int>(EPS6800_LCD_WIDTH); ++x) {
						const bool on = decoded.pixels[y * EPS6800_LCD_WIDTH + x] != 0;
						auto& alpha = eps_screen_ink_alpha[(y + 1) * 192 + x];
						alpha = alpha * transition_ratio +
							(on ? ink_alpha_on : ink_alpha_off) * (1 - transition_ratio);
					}
				}

				const auto& status_indicators = emulator.ModelDefinition.status_indicators;
				for (size_t ix = 0; ix < status_indicators.size(); ++ix) {
					const auto& indicator = status_indicators[ix];
					const bool on = indicator.byte_offset < decoded.status.size() &&
						(decoded.status[indicator.byte_offset] & (1u << indicator.bit)) != 0;
					auto& alpha = eps_screen_ink_alpha[ix];
					alpha = alpha * transition_ratio +
						(on ? ink_alpha_on : ink_alpha_off) * (1 - transition_ratio);
				}
				return;
			}
			else if (hardware_id == HW_EPS6800_W192) {
				ratio = 0.80f;
				std::array<uint8_t, EPS6800_W192_LCD_RAW_SIZE> lcd{};
				Eps6800LcdControl control{};
				if (!emulator.chipset.epscpu ||
					emulator.chipset.epscpu->CopyLcd(lcd.data(), lcd.size(), &control) != lcd.size())
					return;
				float ink_alpha_on = Eps6800ActiveAlpha(control.contrast);
				float ink_alpha_off = Eps6800InactiveAlpha(control.contrast);
				ink_alpha_off = screen_residual_enabled ? ink_alpha_off * screen_residual_alpha_scale : 0.0f;
				if (!control.visible()) {
					ink_alpha_on = 0.0f;
					ink_alpha_off = 0.0f;
				}
				const float transition_ratio = screen_residual_enabled ? ratio : 0.0f;
				std::lock_guard<std::mutex> lock(eps_screen_alpha_mutex);
				const auto decoded = DecodeEps6800W192Display(lcd.data(), lcd.size());
				for (size_t y = 0; y < EPS6800_W192_LCD_HEIGHT; ++y) {
					for (size_t x = 0; x < EPS6800_W192_LCD_WIDTH; ++x) {
						const bool on = decoded.pixels[y * EPS6800_W192_LCD_WIDTH + x] != 0;
						auto& alpha = eps_screen_ink_alpha[(y + 1) * 192 + x];
						alpha = alpha * transition_ratio +
							(on ? ink_alpha_on : ink_alpha_off) * (1 - transition_ratio);
					}
				}
				const auto& status_indicators = emulator.ModelDefinition.status_indicators;
				for (size_t ix = 0; ix < status_indicators.size(); ++ix) {
					const auto& indicator = status_indicators[ix];
					const bool on = indicator.byte_offset < decoded.status.size() &&
						(decoded.status[indicator.byte_offset] & (1u << indicator.bit)) != 0;
					auto& alpha = eps_screen_ink_alpha[ix];
					alpha = alpha * transition_ratio +
						(on ? ink_alpha_on : ink_alpha_off) * (1 - transition_ratio);
				}
				return;
			}
			else if (hardware_id == HW_EPS9500) {
				ratio = 0.80f;
				std::array<uint8_t, EPS9500_LCD_RAW_SIZE> lcd{};
				Eps6800LcdControl control{};
				if (!emulator.chipset.epscpu ||
					emulator.chipset.epscpu->CopyLcd(lcd.data(), lcd.size(), &control) != lcd.size())
					return;
				float ink_alpha_on = Eps6800ActiveAlpha(control.contrast);
				float ink_alpha_off = Eps6800InactiveAlpha(control.contrast);
				ink_alpha_off = screen_residual_enabled ? ink_alpha_off * screen_residual_alpha_scale : 0.0f;
				if (!control.visible()) {
					ink_alpha_on = 0.0f;
					ink_alpha_off = 0.0f;
				}
				const float transition_ratio = screen_residual_enabled ? ratio : 0.0f;
				std::lock_guard<std::mutex> lock(eps_screen_alpha_mutex);
				const auto decoded = DecodeEps9500Display(lcd.data(), lcd.size());
				for (int y = 0; y < static_cast<int>(EPS9500_LCD_HEIGHT); ++y) {
					for (int x = 0; x < static_cast<int>(EPS9500_LCD_WIDTH); ++x) {
						const bool on = decoded.pixels[y * EPS9500_LCD_WIDTH + x] != 0;
						auto& alpha = eps_screen_ink_alpha[(y + 1) * 192 + x];
						alpha = alpha * transition_ratio +
							(on ? ink_alpha_on : ink_alpha_off) * (1 - transition_ratio);
					}
				}
				const auto& status_indicators = emulator.ModelDefinition.status_indicators;
				for (size_t ix = 0; ix < status_indicators.size(); ++ix) {
					const auto& indicator = status_indicators[ix];
					const bool on = indicator.byte_offset < decoded.status.size() &&
						(decoded.status[indicator.byte_offset] & (1u << indicator.bit)) != 0;
					auto& alpha = eps_screen_ink_alpha[ix];
					alpha = alpha * transition_ratio +
						(on ? ink_alpha_on : ink_alpha_off) * (1 - transition_ratio);
				}
				return;
			}
			else if (hardware_id == HW_EPS6009) {
				ratio = 0.80f;
				std::array<uint8_t, 0x88> lcd{};
				Eps6800LcdControl control{};
				if (!emulator.chipset.epscpu ||
					emulator.chipset.epscpu->CopyLcd(lcd.data(), lcd.size(), &control) != lcd.size())
					return;
				float ink_alpha_on = 230.0f;
				float ink_alpha_off = 8.0f;
				ink_alpha_off = screen_residual_enabled ? ink_alpha_off * screen_residual_alpha_scale : 0.0f;
				if (!control.visible()) {
					ink_alpha_on = 0.0f;
					ink_alpha_off = 0.0f;
				}
				const float transition_ratio = screen_residual_enabled ? ratio : 0.0f;
				std::lock_guard<std::mutex> lock(eps_screen_alpha_mutex);
				const auto& status_indicators = emulator.ModelDefinition.status_indicators;
				for (size_t ix = 0; ix < status_indicators.size(); ++ix) {
					const auto& indicator = status_indicators[ix];
					const bool on = indicator.byte_offset < lcd.size() &&
						(lcd[indicator.byte_offset] & (1u << indicator.bit)) != 0;
					auto& alpha = eps_screen_ink_alpha[ix];
					alpha = alpha * transition_ratio +
						(on ? ink_alpha_on : ink_alpha_off) * (1 - transition_ratio);
				}
				return;
			}

			if (screen_refresh_rate < screen_flashing_threshold && !enable_screen_fading)
				;
			else {
				int n = update_screen_scan_alpha(screen_scan_alpha, SDL_GetTicks64(), screen_refresh_rate);
				screen_scan_report = ((n / (screen_scan_report_en ? screen_scan_report_op1 : 64)) % 2 ? 3 : 0) ^ (n % 64 == 0 ? 1 : (n % 64 == 32 ? 2 : 0));
			}
			if (screen_refresh_rate < 6) {
				screen_refresh_rate = 6;
			}
			auto sb = screen_brightness;
			if (sb < 3) {
				sb = 3;
			}
			auto contrast = (int)screen_contrast;
			// if (screen_contrast2_en) {
			//        contrast += screen_contrast2 * 0.5;
			// }
			if (contrast < 0) {
				contrast = 0;
			}
			auto coeff = 16;
			auto off = 0;
			if constexpr (hardware_id != HW_CLASSWIZ_II) {
				coeff = 28;
				off = -240;
			}
			int ink_alpha_on = off + contrast * coeff - sb * 8;
			int ink_alpha_off = off + 20 + (contrast) * (coeff - 11) - sb * 13;
			ink_alpha_off = screen_residual_enabled ? static_cast<int>(ink_alpha_off * screen_residual_alpha_scale) : 0;
			if (ink_alpha_on < 0)
				ink_alpha_on = 0;
			if (ink_alpha_off < 0)
				ink_alpha_off = 0;
			if (!screen_residual_enabled) {
				ink_alpha_on = 255;
				ink_alpha_off = 0;
			}
			bool enable_status, enable_dotmatrix, clear_dots;

			bool mode_6 = false;

			auto screen_buffer = this->screen_buffer;
			uint8_t* screen_buffer1;
			size_t row_size = ROW_SIZE;
			if constexpr (hardware_id == HW_CLASSWIZ_II) {
				screen_buffer1 = this->screen_buffer1;
			}
			if (screen_buffer_select != 0) {
				screen_buffer = (uint8_t*)n_ram_buffer - casioemu::GetRamBaseAddr(hardware_id) + casioemu::GetScreenBufferOffset(emulator.hardware_id, screen_buffer_select);
				if (hardware_id == HW_CLASSWIZ_II) {
					screen_buffer1 = screen_buffer + 0x600;
				}
				row_size = ROW_SIZE_DISP;
			}

			if (!enabled_2)
				goto clean_scr;

			switch (screen_mode & 7) {
			case 4: // 100
				enable_dotmatrix = true;
				clear_dots = true;
				enable_status = false;
				break;

			case 5: // 101
				enable_dotmatrix = true;
				clear_dots = false;
				enable_status = true;
				break;

			case 6: // 110
				enable_dotmatrix = true;
				clear_dots = true;
				enable_status = true;
				mode_6 = true;
				break;

			default:
				goto clean_scr;
			}
			if (screen_range & 0b100000)
				goto clean_scr;
			{
				bool flip_screen_h = screen_mode & 0b1000;
				bool flip_screen_v = !(screen_mode & 0b10000);
				if constexpr (hardware_id == HW_CLASSWIZ || hardware_id == HW_CLASSWIZ_II) {
				}
				else {
					flip_screen_v = flip_screen_v = 0;
				}
				int rng1 = (4 - (screen_range & 0x3));
				ink_alpha_off *= (4 / rng1);
				ink_alpha_on *= (4 / rng1);
				int rng = rng1 * 8;

				if (enable_status) {
					int ink_alpha = ink_alpha_off;
					status_ink_alpha_on = ink_alpha_on;
					status_ink_alpha_off = ink_alpha_off;
					if constexpr (hardware_id == HW_CLASSWIZ_II) {
						for (int ix = 1; ix != SPR_MAX; ++ix) {
							ink_alpha = ink_alpha_off;
							auto off = (sprite_bitmap[ix].offset + screen_offset * row_size) % ((N_ROW + 1) * row_size);
							if (screen_buffer[off] & sprite_bitmap[ix].mask)
								ink_alpha += (ink_alpha_on - ink_alpha_off) * kClassWizIILowerPlaneWeight;
							if (screen_buffer1[off] & sprite_bitmap[ix].mask)
								ink_alpha += (ink_alpha_on - ink_alpha_off) * kClassWizIIUpperPlaneWeight;
							if (screen_refresh_rate >= screen_flashing_threshold)
								ink_alpha *= screen_scan_alpha[0];
							screen_ink_alpha[ix - 1] = screen_ink_alpha[ix - 1] * ratio + ink_alpha * (1 - ratio);
						}
					}
					else {
						int x = 0;
						for (int ix = 1; ix != SPR_MAX; ++ix) {
							auto off = (sprite_bitmap[ix].offset + screen_offset * row_size) % ((N_ROW + 1) * row_size);
							if (screen_buffer[off] & sprite_bitmap[ix].mask)
								ink_alpha = ink_alpha_on;
							else
								ink_alpha = ink_alpha_off;
							if (screen_refresh_rate >= screen_flashing_threshold)
								ink_alpha *= screen_scan_alpha[0];
							screen_ink_alpha[x] = screen_ink_alpha[x] * ratio + ink_alpha * (1 - ratio);
							x++;
						}
					}
				}
				else {
					if constexpr (hardware_id == HW_CLASSWIZ_II) {
						for (size_t i = 0; i < 192; i++) {
							screen_ink_alpha[i] *= ratio;
						}
					}
					else {
						for (size_t i = 0; i < 192; i++) {
							screen_ink_alpha[i] *= ratio;
						}
					}
				}

				if (enable_dotmatrix) {
					static constexpr auto SPR_PIXEL = 0;
					SDL_Rect dest = Screen<hardware_id>::sprite_info[SPR_PIXEL].dest;
					int ink_alpha = ink_alpha_off;
					if (mode_6) {
						ink_alpha_on = ink_alpha_off /= 2.55;
					}
					if constexpr (hardware_id == HW_CLASSWIZ_II) {
						for (int iy2 = 1; iy2 != (N_ROW + 1); ++iy2) {
							int iy = (iy2 + screen_offset) % (N_ROW + 1);
							bool clear = 0;
							if (iy2 >= rng && iy2 < 32)
								clear = 1;
							if (iy2 >= 32) {
								if (iy2 <= 32 + rng) {
									iy = (iy2 - 32 + rng + screen_offset) % (N_ROW + 1);
								}
								else {
									clear = 1;
								}
							}
							dest.x = sprite_info[SPR_PIXEL].dest.x;
							dest.y = sprite_info[SPR_PIXEL].dest.y + (iy2 - 1) * sprite_info[SPR_PIXEL].src.h;
							int x = 0;
							for (int ix = 0; ix != ROW_SIZE_DISP; ++ix) {
								auto index = (flip_screen_v ? N_ROW - iy : iy) * row_size + ix;
								for (uint8_t mask = 0x80; mask; mask >>= 1, dest.x += sprite_info[SPR_PIXEL].src.w) {
									ink_alpha = ink_alpha_off;
									if (!clear_dots && screen_buffer[index] & mask)
										ink_alpha += (ink_alpha_on - ink_alpha_off) * kClassWizIILowerPlaneWeight;
									if (!clear_dots && screen_buffer1[index] & mask)
										ink_alpha += (ink_alpha_on - ink_alpha_off) * kClassWizIIUpperPlaneWeight;
									if (screen_refresh_rate >= screen_flashing_threshold)
										ink_alpha *= screen_scan_alpha[iy];
									if (clear)
										ink_alpha = 0;
									float& dat = screen_ink_alpha[(flip_screen_h ? (191 - x) : x) + iy2 * 192];
									dat = dat * ratio + ink_alpha * (1 - ratio);
									x++;
								}
							}
						}
					}
					else {
						for (int iy2 = 1; iy2 != (N_ROW + 1); ++iy2) {
							int iy = (iy2 + screen_offset) % (N_ROW + 1);
							bool clear = 0;
							if (iy2 >= rng && iy2 < 32)
								clear = 1;
							if (iy2 >= 32) {
								if (iy2 <= 32 + rng) {
									iy = (iy2 - 32 + rng + screen_offset) % (N_ROW + 1);
								}
								else {
									clear = 1;
								}
							}
							dest.x = sprite_info[SPR_PIXEL].dest.x;
							dest.y = sprite_info[SPR_PIXEL].dest.y + (iy2 - 1) * sprite_info[SPR_PIXEL].src.h;
							int x = 0;
							for (int ix = 0; ix != ROW_SIZE_DISP; ++ix) {
								auto index = (flip_screen_v ? N_ROW + 1 - iy : iy) * row_size + ix;
								for (uint8_t mask = 0x80; mask; mask >>= 1, dest.x += sprite_info[SPR_PIXEL].src.w) {
									if (screen_buffer[index] & mask)
										ink_alpha = ink_alpha_on;
									else
										ink_alpha = ink_alpha_off;
									if (screen_refresh_rate >= screen_flashing_threshold)
										ink_alpha *= screen_scan_alpha[iy];
									if (clear)
										ink_alpha = 0;
									float& dat = screen_ink_alpha[(flip_screen_h ? (191 - x) : x) + iy2 * 192];
									dat = dat * ratio + ink_alpha * (1 - ratio);
									x++;
								}
							}
						}
					}
				}
				else {
					if constexpr (hardware_id == HW_CLASSWIZ_II) {
						for (size_t i = 192; i < 64 * 192; i++) {
							screen_ink_alpha[i] *= ratio;
						}
					}
					else {
						for (size_t i = 192; i < 64 * 192; i++) {
							screen_ink_alpha[i] *= ratio;
						}
					}
				}
			}
			return;
		clean_scr:
			if constexpr (hardware_id == HW_CLASSWIZ_II) {
				for (size_t i = 0; i < 64 * 192; i++) {
					screen_ink_alpha[i] *= ratio;
				}
			}
			else {
				for (size_t i = 0; i < 64 * 192; i++) {
					screen_ink_alpha[i] *= ratio;
				}
			}
			return;
		}
	};

	template <>
	const int Screen<HW_TI>::N_ROW = 64;
	template <>
	const int Screen<HW_TI>::ROW_SIZE = 32;
	template <>
	const int Screen<HW_TI>::OFFSET = 32;
	template <>
	const int Screen<HW_TI>::ROW_SIZE_DISP = 24;
	template <>
	const int Screen<HW_TI>::SPR_MAX = 14;
	template <>
	const SpriteBitmap Screen<HW_TI>::sprite_bitmap[] = {
		{"rsd_pixel", 0, 0},
		{"rsd_2nd", 1, 17},
		{"rsd_fix", 0, 0x00},
		{"rsd_hbo", 0, 0x00},
		{"rsd_sci", 0, 0x01},
		{"rsd_eng", 0, 0x01},
		{"rsd_deg", 0, 0x01},
		{"rsd_rad", 0, 0x01},
		{"rsd_bat", 0, 0x02},
		{"rsd_wait", 1, 164},
		{"rsd_left", 0, 0x02},
		{"rsd_up", 0, 0x02},
		{"rsd_down", 0, 0x02},
		{"rsd_right", 0, 0x02},
	};

	template <>
	const int Screen<HW_CLASSWIZ_II>::N_ROW = 63;
	template <>
	const int Screen<HW_CLASSWIZ_II>::ROW_SIZE = 32;
	template <>
	const int Screen<HW_CLASSWIZ_II>::OFFSET = 32;
	template <>
	const int Screen<HW_CLASSWIZ_II>::ROW_SIZE_DISP = 24;
	template <>
	const int Screen<HW_CLASSWIZ_II>::SPR_MAX = 21;

	template <>
	const int Screen<HW_CLASSWIZ>::N_ROW = 63;
	template <>
	const int Screen<HW_CLASSWIZ>::ROW_SIZE = 32;
	template <>
	const int Screen<HW_CLASSWIZ>::OFFSET = 32;
	template <>
	const int Screen<HW_CLASSWIZ>::ROW_SIZE_DISP = 24;
	template <>
	const int Screen<HW_CLASSWIZ>::SPR_MAX = 21;

	template <>
	const int Screen<HW_ES_PLUS>::N_ROW = 31;
	template <>
	const int Screen<HW_ES_PLUS>::ROW_SIZE = 16;
	template <>
	const int Screen<HW_ES_PLUS>::OFFSET = 16;
	template <>
	const int Screen<HW_ES_PLUS>::ROW_SIZE_DISP = 12;
	template <>
	const int Screen<HW_ES_PLUS>::SPR_MAX = 19;

	template <>
	const int Screen<HW_FX_5800P>::N_ROW = 31;
	template <>
	const int Screen<HW_FX_5800P>::ROW_SIZE = 16;
	template <>
	const int Screen<HW_FX_5800P>::OFFSET = 16;
	template <>
	const int Screen<HW_FX_5800P>::ROW_SIZE_DISP = 12;
	template <>
	const int Screen<HW_FX_5800P>::SPR_MAX = 20;

	// that's meaningless, just make compiler happy xd
	template <>
	const int Screen<HW_EPS6800>::N_ROW = 31;
	template <>
	const int Screen<HW_EPS6800>::ROW_SIZE = 16;
	template <>
	const int Screen<HW_EPS6800>::OFFSET = 16;
	template <>
	const int Screen<HW_EPS6800>::ROW_SIZE_DISP = 12;
	template <>
	const int Screen<HW_EPS6800>::SPR_MAX = 1;

	template <>
	const int Screen<HW_EPS6800_W192>::N_ROW = 63;
	template <>
	const int Screen<HW_EPS6800_W192>::ROW_SIZE = 24;
	template <>
	const int Screen<HW_EPS6800_W192>::OFFSET = 0;
	template <>
	const int Screen<HW_EPS6800_W192>::ROW_SIZE_DISP = 24;
	template <>
	const int Screen<HW_EPS6800_W192>::SPR_MAX = 1;

	template <>
	const int Screen<HW_EPS6009>::N_ROW = 0;
	template <>
	const int Screen<HW_EPS6009>::ROW_SIZE = 1;
	template <>
	const int Screen<HW_EPS6009>::OFFSET = 0;
	template <>
	const int Screen<HW_EPS6009>::ROW_SIZE_DISP = 1;
	template <>
	const int Screen<HW_EPS6009>::SPR_MAX = 1;

	template <>
	const int Screen<HW_EPS9500>::N_ROW = 32;
	template <>
	const int Screen<HW_EPS9500>::ROW_SIZE = 16;
	template <>
	const int Screen<HW_EPS9500>::OFFSET = 16;
	template <>
	const int Screen<HW_EPS9500>::ROW_SIZE_DISP = 12;
	template <>
	const int Screen<HW_EPS9500>::SPR_MAX = 1;

	template <>
	const SpriteBitmap Screen<HW_CLASSWIZ_II>::sprite_bitmap[] = {
		{"rsd_pixel", 0, 0},
		{"rsd_s", 0x01, 0x01},
		{"rsd_math", 0x01, 0x03},
		{"rsd_d", 0x01, 0x04},
		{"rsd_r", 0x01, 0x05},
		{"rsd_g", 0x01, 0x06},
		{"rsd_fix", 0x01, 0x07},
		{"rsd_sci", 0x01, 0x08},
		{"rsd_fx", 0x01, 0x09},
		{"rsd_e", 0x01, 0x0A},
		{"rsd_cmplx", 0x01, 0x0B},
		{"rsd_angle", 0x01, 0x0C},
		{"rsd_wdown", 0x01, 0x0D},
		{"rsd_verify", 0x01, 0x0E},
		{"rsd_gx", 0x01, 0x0F},
		{"rsd_left", 0x01, 0x10},
		{"rsd_down", 0x01, 0x11},
		{"rsd_up", 0x01, 0x12},
		{"rsd_right", 0x01, 0x13},
		{"rsd_pause", 0x01, 0x15},
		{"rsd_sun", 0x01, 0x16} };

	template <>
	const SpriteBitmap Screen<HW_CLASSWIZ>::sprite_bitmap[] = {
		{"rsd_pixel", 0, 0},
		{"rsd_s", 0x01, 0x00},
		{"rsd_a", 0x01, 0x01},
		{"rsd_m", 0x01, 0x02},
		{"rsd_sto", 0x01, 0x03},
		{"rsd_math", 0x01, 0x05},
		{"rsd_d", 0x01, 0x06},
		{"rsd_r", 0x01, 0x07},
		{"rsd_g", 0x01, 0x08},
		{"rsd_fix", 0x01, 0x09},
		{"rsd_sci", 0x01, 0x0A},
		{"rsd_e", 0x01, 0x0B},
		{"rsd_cmplx", 0x01, 0x0C},
		{"rsd_angle", 0x01, 0x0D},
		{"rsd_wdown", 0x01, 0x0F},
		{"rsd_left", 0x01, 0x10},
		{"rsd_down", 0x01, 0x11},
		{"rsd_up", 0x01, 0x12},
		{"rsd_right", 0x01, 0x13},
		{"rsd_pause", 0x01, 0x15},
		{"rsd_sun", 0x01, 0x16} };

	template <>
	const SpriteBitmap Screen<HW_ES_PLUS>::sprite_bitmap[] = {
		{"rsd_pixel", 0, 0},
		{"rsd_s", 0x10, 0x00},
		{"rsd_a", 0x04, 0x00},
		{"rsd_m", 0x10, 0x01},
		{"rsd_sto", 0x02, 0x01},
		{"rsd_rcl", 0x40, 0x02},
		{"rsd_stat", 0x40, 0x03},
		{"rsd_cmplx", 0x80, 0x04},
		{"rsd_mat", 0x40, 0x05},
		{"rsd_vct", 0x02, 0x05},
		{"rsd_d", 0x20, 0x07},
		{"rsd_r", 0x02, 0x07},
		{"rsd_g", 0x10, 0x08},
		{"rsd_fix", 0x01, 0x08},
		{"rsd_sci", 0x20, 0x09},
		{"rsd_math", 0x40, 0x0A},
		{"rsd_down", 0x08, 0x0A},
		{"rsd_up", 0x80, 0x0B},
		{"rsd_disp", 0x10, 0x0B} };

	template <>
	const SpriteBitmap Screen<HW_FX_5800P>::sprite_bitmap[] = {
		{"rsd_pixel", 0, 0},
		{"rsd_s", 0x10, 0x00},
		{"rsd_a", 0x04, 0x00},
		{"rsd_m", 0x10, 0x01},
		{"rsd_sto", 0x02, 0x01},
		{"rsd_rcl", 0x40, 0x02},
		{"rsd_sd", 0x40, 0x03},
		{"rsd_reg", 0x80, 0x04},
		{"rsd_fmla", 0x40, 0x05},
		{"rsd_prgm", 0x10, 0x05},
		{"rsd_eng", 0x02, 0x05},
		{"rsd_d", 0x20, 0x07},
		{"rsd_r", 0x02, 0x07},
		{"rsd_g", 0x10, 0x08},
		{"rsd_fix", 0x01, 0x08},
		{"rsd_sci", 0x20, 0x09},
		{"rsd_math", 0x40, 0x0A},
		{"rsd_down", 0x08, 0x0A},
		{"rsd_up", 0x80, 0x0B},
		{"rsd_disp", 0x10, 0x0B} };

	template <>
	const SpriteBitmap Screen<HW_EPS6800>::sprite_bitmap[] = {
		{"rsd_pixel", 0, 0} };

	template <>
	const SpriteBitmap Screen<HW_EPS6800_W192>::sprite_bitmap[] = {
		{"rsd_pixel", 0, 0} };

	template <>
	const SpriteBitmap Screen<HW_EPS6009>::sprite_bitmap[] = {
		{"rsd_pixel", 0, 0} };

	template <>
	const SpriteBitmap Screen<HW_EPS9500>::sprite_bitmap[] = {
		{"rsd_pixel", 0, 0} };

	template <HardwareId hardware_id>
	void Screen<hardware_id>::Initialise() {
		if (!inited) {
			renderer = emulator.GetRenderer();
			interface_texture = emulator.GetInterfaceTexture();
			const int sprite_count = SpriteCount();
			sprite_info.resize(sprite_count);
			for (auto& texture : sprite_svg_textures)
				texture.Reset();
			sprite_svg_textures.clear();
			sprite_svg_textures.resize(sprite_count);
			sprite_available.assign(sprite_count, 0);
			for (int ix = 0; ix != sprite_count; ++ix) {
				const char* static_name = nullptr;
				std::string dynamic_name;
				if constexpr (IsEpsFamily(hardware_id)) {
					dynamic_name = ix == 0 ? "rsd_pixel" : emulator.ModelDefinition.status_indicators[static_cast<size_t>(ix - 1)].sprite_name;
				}
				else {
					static_name = sprite_bitmap[ix].name;
				}
				auto sprite = emulator.ModelDefinition.sprites.find(static_name ? static_name : dynamic_name);
				if (sprite == emulator.ModelDefinition.sprites.end())
					continue;
				sprite_info[ix] = sprite->second;
				sprite_available[ix] = 1;
			}

			ink_colour = emulator.ModelDefinition.ink_color;
			if constexpr (hardware_id == HW_TI) {
				screen_buffer = new uint8_t[192 * 9];
				// TODO: remove this
				memset(screen_buffer, 0, 192 * 9);
				// fillRandomData(screen_buffer, 192*9);
			}
			else {
				screen_buffer = new uint8_t[(N_ROW + 1) * ROW_SIZE];
				fillRandomData(screen_buffer, (N_ROW + 1) * ROW_SIZE);
			}
			if constexpr (hardware_id == HW_CLASSWIZ || hardware_id == HW_CLASSWIZ_II) {
				region_power.Setup(
					0xF03D, 1, "Screen/Power", this,
					[](MMURegion* region, size_t offset) {
						return ((Screen*)region->userdata)->screen_power;
					},
					[](MMURegion* region, size_t offset, uint8_t data) {
						bool a = (((Screen*)region->userdata)->screen_power & 1) ^ (data & 1);
						((Screen*)region->userdata)->screen_power = data & 0xf;
						if (a && ((data & 1) == 0)) { // 关闭屏幕
							((Screen*)region->userdata)->Uninitialise();
						}
						else {
							((Screen*)region->userdata)->Initialise();
						}
					},
					emulator);
			}
			if constexpr (hardware_id == HW_CLASSWIZ_II) {
				screen_buffer1 = new uint8_t[(N_ROW + 1) * ROW_SIZE];
				fillRandomData(screen_buffer1, (N_ROW + 1) * ROW_SIZE);
			}
			inited = true;
		}
		if constexpr (IsEpsFamily(hardware_id)) {
			// CPU-visible LCD registers and RAM are owned by EPS6800Core. This
			// peripheral is only the CasioEmuMsvc presentation/resource layer.
			return;
		}
		if constexpr (hardware_id == HW_TI) {
			auto pp = emulator.chipset.QueryInterface<IPortProvider>();
			pp->SetPortOutputCallback(7, [&](uint8_t data) {
				ti_port7 = data;
				});
			pp->SetPortOutputCallback(5, [&](uint8_t data) {
				// ti_port5 = data;
				if (ti_a0 && !(data & 0x40)) {
					if ((data & 0x10)) {
						auto bit_off = ti_col;
						auto off = bit_off + ti_page * 192;
						if (off > 192 * 9) {
							return;
						}
						if (off > 192 * 8) {
							std::cout << std::dec << off - 192 * 8 << " <- 0x" << std::hex << ti_port7 << "\n";
						}
						screen_buffer[off] = ti_port7;
						ti_col++;
						if (ti_col >= 192) {
							ti_col = 0;
							ti_page++;
						}
					}
					else {
						auto data = ti_port7;
						switch (ti_port_status) {
						case 0: {
							auto dh = data >> 4;
							if (dh == 0) {
								ti_col = (ti_col & 0xf0) | (data & 0xf);
							}
							else if (dh == 1) {
								ti_col = (ti_col & 0xf) | ((data & 0xf) << 4);
							}
							else if ((dh & 0b1100) == 0b0100) {
								// std::cout << "Set Scroll line " << (data & 0x3f) << "\n";
							}
							else if (dh == 0b1011) {
								// std::cout << "Set page  " << (data & 0xf) << "\n";
								ti_page = (data & 0xf);
							}
							else if ((data >> 3) == 17) {
								// std::cout << "Set addressing mode\n";
							}
							else if ((data >> 2) == 58) {
								// std::cout << "Set bias\n";
							}
							else if ((data >> 2) == 40) {
								// std::cout << "Set frame rate\n";
							}
							else if ((data >> 1) == 82) {
								// std::cout << "Clear all display segments\n";
							}
							else if ((data >> 1) == 83) {
								// std::cout << "Set inverse display\n";
							}
							else if ((data & 0xf9) == 0xc0) {
								// std::cout << "Set Com Seg Scan Direction\n";
							}
							else if (data == 0xe3) {
								// std::cout << "Nop\n";
							}
							else if (data == 0xe2) {
								// std::cout << "Software reset\n";
							}
							else if (data == 0xaf) {
								// std::cout << "Enabled screen!\n";
								ti_enabled = 1;
							}
							else if (data == 0x81) {
								ti_port_status = 1;
							}
							else if (data == 0xae) {
								// std::cout << "Disabled screen!\n";
								ti_enabled = 0;
							}
							else {
								std::cout << "[Screen][Warn] Unknown ST7525 command: 0x" << std::hex << (int)data << "\n";
							}
							break;
						}
						case 1:
							// std::cout << "Set contrast!\n";
							ti_contrast = data;
							ti_port_status = 0;
							break;
						}
					}
				}
				ti_a0 = (data & 0x40);
				});
			return;
		}
		if (!(hardware_id == HW_CLASSWIZ || hardware_id == HW_CLASSWIZ_II) || (!enabled_2 && (screen_power & 1))) {
			if constexpr (hardware_id != HW_CLASSWIZ_II) {
				region_buffer.Setup(
					0xF800, (N_ROW + 1) * ROW_SIZE, "Screen/Buffer", this, [](MMURegion* region, size_t offset) {
						offset -= region->base;
						if (offset % ROW_SIZE >= ROW_SIZE_DISP)
							return (uint8_t)0;
						return ((Screen*)region->userdata)->screen_buffer[offset]; },
					[](MMURegion* region, size_t offset, uint8_t data) {
						offset -= region->base;
						if (offset % ROW_SIZE >= ROW_SIZE_DISP)
							return;

						auto this_obj = (Screen*)region->userdata;
						this_obj->screen_buffer[offset] = data; },
					emulator);
			}
			else {
				region_buffer.Setup(
					0xF800, (N_ROW + 1) * ROW_SIZE, "Screen/Buffer", this,
					[](MMURegion* region, size_t offset) {
						offset -= region->base;
						if (offset % ROW_SIZE >= ROW_SIZE_DISP)
							return (uint8_t)0;
						if (((Screen*)region->userdata)->screen_select & 0x04) {
							return ((Screen*)region->userdata)->screen_buffer1[offset];
						}
						else {
							return ((Screen*)region->userdata)->screen_buffer[offset];
						}
					},
					[](MMURegion* region, size_t offset, uint8_t data) {
						offset -= region->base;
						if (offset % ROW_SIZE >= ROW_SIZE_DISP)
							return;

						auto this_obj = (Screen*)region->userdata;
						if (!(this_obj->screen_mode & 0x40)) {
							this_obj->screen_buffer1[offset] = this_obj->screen_buffer[offset] = data;
							return;
						}
						if (this_obj->screen_select & 0x04) {
							this_obj->screen_buffer1[offset] = data;
						}
						else {
							this_obj->screen_buffer[offset] = data;
						}
					},
					emulator);
				if (!emulator.ModelDefinition.real_hardware) {
					// region_buffer.Setup(
					//	0xF800, (N_ROW + 1) * ROW_SIZE, "Screen/Buffer", this,
					//	[](MMURegion* region, size_t offset) {
					//		offset -= region->base;
					//		if (offset % ROW_SIZE >= ROW_SIZE_DISP)
					//			return (uint8_t)0;
					//		return ((Screen*)region->userdata)->screen_buffer[offset];
					//	},
					//	[](MMURegion* region, size_t offset, uint8_t data) {
					//		offset -= region->base;
					//		if (offset % ROW_SIZE >= ROW_SIZE_DISP)
					//			return;

					//                auto this_obj = (Screen*)region->userdata;
					//                this_obj->screen_buffer[offset] = data;
					//        },
					//        emulator);
					region_buffer1.Setup(
						0x89000, (N_ROW + 1) * ROW_SIZE, "Screen/Buffer1", this,
						[](MMURegion* region, size_t offset) {
							offset -= region->base;
							if (offset % ROW_SIZE >= ROW_SIZE_DISP)
								return (uint8_t)0;
							return ((Screen*)region->userdata)->screen_buffer1[offset];
						},
						[](MMURegion* region, size_t offset, uint8_t data) {
							offset -= region->base;
							if (offset % ROW_SIZE >= ROW_SIZE_DISP)
								return;

							auto this_obj = (Screen*)region->userdata;
							this_obj->screen_buffer1[offset] = data;
						},
						emulator);
				}
			}
			if constexpr (hardware_id == HW_CLASSWIZ || hardware_id == HW_CLASSWIZ_II) {
				region_range.Setup(0xF030, 1, "Screen/Range", &screen_range, MMURegion::DefaultRead<uint8_t, 0x2F>,
					MMURegion::DefaultWrite<uint8_t, 0x2F>, emulator);
			}
			else {
				region_range.Setup(0xF030, 1, "Screen/Range", &screen_range, MMURegion::DefaultRead<uint8_t, 0x07>,
					MMURegion::DefaultWrite<uint8_t, 0x07>, emulator);
			}

			if constexpr (hardware_id == HW_CLASSWIZ_II) {
				region_mode.Setup(
					0xF031, 1, "Screen/Mode", this,
					[](MMURegion* region, size_t offset) {
						auto screen = ((Screen*)region->userdata);
						return screen->screen_mode;
					},
					[](MMURegion* region, size_t offset, uint8_t data) {
						auto screen = ((Screen*)region->userdata);
						auto old = screen->screen_mode & 0b1000;
						auto new_ = data & 0b1000;
						if (old ^ new_) {
							auto sb = screen->screen_buffer;
							for (int iy = 0; iy != (N_ROW + 1); ++iy) {
								for (int ix = 0; ix != ROW_SIZE_DISP; ++ix) {
									sb[ix + iy * ROW_SIZE] = bit_lookup_table[sb[(ix)+iy * ROW_SIZE]];
								}
							}
							for (int iy = 0; iy != (N_ROW + 1); ++iy) {
								for (int ix = 0; ix != (ROW_SIZE_DISP / 2); ++ix) {
									std::swap(sb[ix + iy * ROW_SIZE], sb[(ROW_SIZE_DISP - 1 - ix) + iy * ROW_SIZE]);
								}
							}
							if constexpr (hardware_id == HW_CLASSWIZ_II) {
								sb = screen->screen_buffer1;
								for (int iy = 0; iy != (N_ROW + 1); ++iy) {
									for (int ix = 0; ix != ROW_SIZE_DISP; ++ix) {
										sb[ix + iy * ROW_SIZE] = bit_lookup_table[sb[(ix)+iy * ROW_SIZE]];
									}
								}
								for (int iy = 0; iy != (N_ROW + 1); ++iy) {
									for (int ix = 0; ix != (ROW_SIZE_DISP / 2); ++ix) {
										std::swap(sb[ix + iy * ROW_SIZE], sb[(ROW_SIZE_DISP - 1 - ix) + iy * ROW_SIZE]);
									}
								}
							}
						}
						screen->screen_mode = data & 127;
					},
					emulator);
			}
			else if constexpr (hardware_id == HW_CLASSWIZ) {
				region_mode.Setup(0xF031, 1, "Screen/Mode", &screen_mode, MMURegion::DefaultRead<uint8_t, 63>,
					MMURegion::DefaultWrite<uint8_t, 63>, emulator);
			}
			else {
				region_mode.Setup(0xF031, 1, "Screen/Mode", &screen_mode, MMURegion::DefaultRead<uint8_t, 0x07>,
					MMURegion::DefaultWrite<uint8_t, 0x07>, emulator);
			}
			if constexpr (hardware_id == HW_CLASSWIZ || hardware_id == HW_CLASSWIZ_II) {
				region_contrast.Setup(0xF032, 1, "Screen/Contrast", &screen_contrast, MMURegion::DefaultRead<uint8_t, 0x3F>,
					MMURegion::DefaultWrite<uint8_t, 0x3F>, emulator);
				region_unk1.Setup(
					0xF03E, 1, "Screen/Unk1", this,
					[](MMURegion* region, size_t offset) {
						return (uint8_t)0;
					},
					[](MMURegion* region, size_t offset, uint8_t data) {
						((Screen*)region->userdata)->emulator.chipset.mmu.WriteData(0xF817, data);
					},
					emulator);
				region_unk2.Setup(
					0xF03F, 1, "Screen/Unk2", this,
					[](MMURegion* region, size_t offset) {
						return (uint8_t)0;
					},
					[](MMURegion* region, size_t offset, uint8_t data) {
						((Screen*)region->userdata)->emulator.chipset.mmu.WriteData(0xF817, data);
					},
					emulator);
			}
			else {
				region_contrast.Setup(0xF032, 1, "Screen/Contrast", &screen_contrast, MMURegion::DefaultRead<uint8_t, 0x1f>,
					MMURegion::DefaultWrite<uint8_t, 0x1f>, emulator);
			}

			if constexpr (hardware_id == HW_CLASSWIZ || hardware_id == HW_CLASSWIZ_II) {
				region_select.Setup(0xF037, 1, "Screen/Select", &screen_select, MMURegion::DefaultRead < uint8_t, 0x04 | 1 >,
					MMURegion::DefaultWrite < uint8_t, 0x04 | 1 >, emulator);

				region_brightness.Setup(0xF033, 1, "Screen/Brightness", &screen_brightness, MMURegion::DefaultRead<uint8_t, 0x07>,
					MMURegion::DefaultWrite<uint8_t, 0x07>, emulator);

				/*
cwx中F03B的值应该是由屏幕扫描和F035/F036决定的
1.每行扫描的时间大概是( [0xF034] * 25 ) us
2.F03B的mask是3，屏幕每扫描( [0xF036] == 0 ? 64 : [0xF035] )行后F03B的基础值会在0和3之间切换，如果F036是0的话这个循环的半周期和屏幕扫描应该是对齐的，也就是F03B的基础值切换后对应屏幕的第0行扫描（注：F035.0始终为1）
3.扫描屏幕的第0行 (对应bit0?) 及第32行 (对应bit1?) 时，F03B对应的bit会反转

n为行扫描计数，[0xF03B] = ( ( n / ( [0xF036] == 0 ? 64 : [0xF035] ) ) % 2 ? 3 : 0 ) ^ ( n % 64 == 0 ? 1 : ( n % 64 == 32 ? 2 : 0)  )
				*/

				region_scan_report_op1.Setup(0xF035, 1, "Screen/ScanReportOption1", &screen_scan_report_op1, MMURegion::DefaultRead<uint8_t, 0x1E>,
					MMURegion::DefaultWrite<uint8_t, 0x1E>, emulator);

				region_scan_report_en.Setup(0xF036, 1, "Screen/ScanReportOptionEnable", &screen_scan_report_en, MMURegion::DefaultRead<uint8_t, 0b1001>,
					MMURegion::DefaultWrite<uint8_t, 0b1001>, emulator);

				region_scan_report.Setup(0xF03B, 1, "Screen/ScanReport", &screen_scan_report, MMURegion::DefaultRead<uint8_t, 0x3>,
					MMURegion::IgnoreWrite, emulator);
			}
			else {
				screen_scan_report_op1 = 0x17;
				screen_scan_report_en = 1;
			}

			if constexpr (hardware_id == HardwareId::HW_FX_5800P || hardware_id == HardwareId::HW_ES_PLUS) {
				region_refresh_rate.Setup(0xF034, 1, "Screen/Unknown_F034", &unk_f034, MMURegion::DefaultRead<uint8_t, 0b11>,
					MMURegion::DefaultWrite<uint8_t, 0b11>, emulator);
			}
			else {
				region_offset.Setup(0xF039, 1, "Screen/DSPOFST", &screen_offset, MMURegion::DefaultRead<uint8_t, 0x3F>,
					MMURegion::DefaultWrite<uint8_t, 0x3F>, emulator);

				// 25us
				region_refresh_rate.Setup(0xF034, 1, "Screen/RefreshRate", &screen_refresh_rate, MMURegion::DefaultRead<uint8_t, 0x7F>,
					MMURegion::DefaultWrite<uint8_t, 0x7F>, emulator);
			}
			enabled_2 = true;
		}
	}

	template <HardwareId hardware_id>
	void Screen<hardware_id>::Uninitialise() {
		if (!enabled_2)
			return;
		fillRandomData(screen_buffer, (N_ROW + 1) * ROW_SIZE);
		if constexpr (hardware_id == HW_CLASSWIZ_II) {
			fillRandomData(screen_buffer1, (N_ROW + 1) * ROW_SIZE);
		}
		if constexpr (hardware_id != HW_CLASSWIZ_II) {
			region_buffer.Kill();
		}
		else {
			if (!emulator.ModelDefinition.real_hardware) {
				region_buffer.Kill();
				region_buffer1.Kill();
			}
			else {
				region_buffer.Kill();
			}
		}
		screen_range = 0;
		region_range.Kill();
		screen_mode = 0;
		region_mode.Kill();
		screen_contrast = 0;
		region_contrast.Kill();
		if constexpr (hardware_id == HW_CLASSWIZ || hardware_id == HW_CLASSWIZ_II) {
			screen_select = 0;
			region_select.Kill();
			screen_scan_report_op1 = 0;
			region_scan_report_op1.Kill();
			screen_scan_report_en = 0;
			region_scan_report_en.Kill();
			screen_scan_report = 0;
			region_scan_report.Kill();
			region_unk1.Kill();
			region_unk2.Kill();
			screen_brightness = 0;
			region_brightness.Kill();
		}
		screen_refresh_rate = 0;
		region_refresh_rate.Kill();
		if constexpr (hardware_id != HardwareId::HW_FX_5800P && hardware_id != HardwareId::HW_ES_PLUS) {
			screen_offset = 0;
			region_offset.Kill();
		}
		enabled_2 = false;
	}

#if !defined(__EMSCRIPTEN__) && !defined(CASIOEMU_CORE_WEB)
	bool GetCaptureRect(const std::vector<SDL_Rect>& spriteRects, const std::vector<SDL_Rect>& pixelRects, SDL_Rect& captureRect) {
		if (spriteRects.empty() && pixelRects.empty()) {
			return false;
		}

		int minX = INT_MAX, minY = INT_MAX, maxX = INT_MIN, maxY = INT_MIN;
		auto extendBounds = [&](const SDL_Rect& rect) {
			minX = std::min(minX, rect.x);
			minY = std::min(minY, rect.y);
			maxX = std::max(maxX, rect.x + rect.w);
			maxY = std::max(maxY, rect.y + rect.h);
		};

		for (const auto& rect : spriteRects) {
			extendBounds(rect);
		}
		for (const auto& rect : pixelRects) {
			extendBounds(rect);
		}

		if (maxX <= minX || maxY <= minY) {
			return false;
		}

		captureRect = { minX, minY, maxX - minX, maxY - minY };
		return true;
	}

	std::string MakeTimestampedName(const char* prefix, const char* suffix) {
		std::time_t t = std::time(nullptr);
		std::tm tm = *std::localtime(&t);
		std::ostringstream filename;
		filename << prefix
			<< std::put_time(&tm, "%Y-%m-%d-%H-%M-%S-")
			<< util::Random::uniform_uint32(0, 999)
			<< suffix;
		return filename.str();
	}

	std::filesystem::path GetRecordingOutputPath(const std::string& name) {
#ifdef __ANDROID__
		const char* externalPath = SDL_AndroidGetExternalStoragePath();
		if (externalPath && *externalPath) {
			return std::filesystem::path(externalPath) / "recordings" / name;
		}
#endif
		return std::filesystem::path(name);
	}

	void SaveScreenshotSurface(SDL_Surface* screenSurface, const std::string& filename) {
		if (!screenSurface)
			return;
#ifdef __ANDROID__
		bool success = saveImageToMediaStore(screenSurface->pixels, screenSurface->w, screenSurface->h, screenSurface->pitch, filename.c_str());
		if (!success) {
			SDL_Log("Error saving screenshot using MediaStore API");
		}
		else {
			SDL_Log("Screenshot saved successfully with MediaStore API");
		}

		JNIEnv* env = (JNIEnv*)SDL_AndroidGetJNIEnv();
		jobject activity = (jobject)SDL_AndroidGetActivity();

		if (env && activity) {
			jobject byteBuffer = env->NewDirectByteBuffer(screenSurface->pixels,
				screenSurface->h * screenSurface->pitch);

			jclass activityClass = env->GetObjectClass(activity);
			jmethodID copyToClipboardMethod = env->GetMethodID(activityClass, "copyImageToClipboard",
				"(Ljava/nio/ByteBuffer;III)Z");

			if (copyToClipboardMethod != NULL) {
				jboolean result = env->CallBooleanMethod(activity, copyToClipboardMethod,
					byteBuffer, screenSurface->w,
					screenSurface->h, screenSurface->pitch);
				if (result) {
					SDL_Log("Screenshot copied to clipboard");
				}
				else {
					SDL_Log("Failed to copy screenshot to clipboard");
				}
			}
			else {
				SDL_Log("copyImageToClipboard method not found. Add it to your Java activity.");
			}

			env->DeleteLocalRef(byteBuffer);
			env->DeleteLocalRef(activityClass);
			env->DeleteLocalRef(activity);
		}
#else
		if (IMG_SavePNG(screenSurface, filename.c_str()) != 0) {
			SDL_Log("Error saving screenshot: %s", IMG_GetError());
		}
		else {
			SDL_Log("Screenshot saved to %s", filename.c_str());
		}

#ifdef _WIN32
		HDC hdcScreen = GetDC(NULL);
		HDC hdcMem = CreateCompatibleDC(hdcScreen);

		BITMAPINFOHEADER bi;
		ZeroMemory(&bi, sizeof(BITMAPINFOHEADER));
		bi.biSize = sizeof(BITMAPINFOHEADER);
		bi.biWidth = screenSurface->w;
		bi.biHeight = -screenSurface->h;
		bi.biPlanes = 1;
		bi.biBitCount = 32;
		bi.biCompression = BI_RGB;

		void* bits = NULL;
		HBITMAP hBitmap = CreateDIBSection(hdcMem, (BITMAPINFO*)&bi, DIB_RGB_COLORS, &bits, NULL, 0);

		if (hBitmap) {
			HGDIOBJ oldBitmap = SelectObject(hdcMem, hBitmap);

			uint8_t* dst = (uint8_t*)bits;

			for (int y = 0; y < screenSurface->h; y++) {
				uint8_t* src = (uint8_t*)screenSurface->pixels + y * screenSurface->pitch;
				for (int x = 0; x < screenSurface->w; x++) {
					dst[0] = src[2];
					dst[1] = src[1];
					dst[2] = src[0];
					dst[3] = src[3];

					src += 4;
					dst += 4;
				}
			}
			if (oldBitmap)
				SelectObject(hdcMem, oldBitmap);

			bool clipboardOwnsBitmap = false;
			if (OpenClipboard(NULL)) {
				EmptyClipboard();
				if (SetClipboardData(CF_BITMAP, hBitmap)) {
					clipboardOwnsBitmap = true;
				}
				else {
					SDL_Log("Failed to set clipboard bitmap");
				}
				CloseClipboard();
				if (clipboardOwnsBitmap)
					SDL_Log("Screenshot copied to clipboard");
			}
			else {
				SDL_Log("Failed to open clipboard");
			}
			if (!clipboardOwnsBitmap) {
				DeleteObject(hBitmap);
			}

			DeleteDC(hdcMem);
		}
		else {
			SDL_Log("Failed to create DIB section for clipboard");
		}

		ReleaseDC(NULL, hdcScreen);
#else
		SDL_Log("Clipboard copy not implemented for this platform");
#endif
#endif
	}

	bool EnsureParentDirectory(const std::filesystem::path& path) {
		const auto parent = path.parent_path();
		if (parent.empty()) {
			return true;
		}

		std::error_code ec;
		std::filesystem::create_directories(parent, ec);
		if (ec) {
			SDL_Log("Could not create recording directory %s: %s",
				parent.string().c_str(), ec.message().c_str());
			return false;
		}
		return true;
	}

#ifdef __ANDROID__
	inline uint8_t ClampByte(int value) {
		return static_cast<uint8_t>(std::clamp(value, 0, 255));
	}

	class AndroidVideoEncoder {
	public:
		~AndroidVideoEncoder() {
			Stop();
		}

		bool Start(const std::filesystem::path& path, int videoWidth, int videoHeight, int videoFps) {
			Stop();
			if (android_get_device_api_level() < 21) {
				SDL_Log("Android recording requires API level 21 or newer.");
				return false;
			}
			if (!EnsureParentDirectory(path)) {
				return false;
			}

			width = videoWidth;
			height = videoHeight;
			fps = std::max(1, videoFps);
			frameIndex = 0;

			const std::string pathString = path.string();
			fd = open(pathString.c_str(), O_CREAT | O_TRUNC | O_WRONLY, 0666);
			if (fd < 0) {
				SDL_Log("Could not open recording output %s", pathString.c_str());
				return false;
			}

			muxer = AMediaMuxer_new(fd, AMEDIAMUXER_OUTPUT_FORMAT_MPEG_4);
			codec = AMediaCodec_createEncoderByType("video/avc");
			if (!muxer || !codec) {
				SDL_Log("Could not create Android media encoder.");
				Stop();
				return false;
			}

			AMediaFormat* format = AMediaFormat_new();
			AMediaFormat_setString(format, AMEDIAFORMAT_KEY_MIME, "video/avc");
			AMediaFormat_setInt32(format, AMEDIAFORMAT_KEY_WIDTH, width);
			AMediaFormat_setInt32(format, AMEDIAFORMAT_KEY_HEIGHT, height);
			AMediaFormat_setInt32(format, AMEDIAFORMAT_KEY_COLOR_FORMAT, kColorFormatYuv420SemiPlanar);
			AMediaFormat_setInt32(format, AMEDIAFORMAT_KEY_BIT_RATE, std::max(256000, width * height * fps / 2));
			AMediaFormat_setInt32(format, AMEDIAFORMAT_KEY_FRAME_RATE, fps);
			AMediaFormat_setInt32(format, AMEDIAFORMAT_KEY_I_FRAME_INTERVAL, 2);

			media_status_t status = AMediaCodec_configure(codec, format, nullptr, nullptr, AMEDIACODEC_CONFIGURE_FLAG_ENCODE);
			AMediaFormat_delete(format);
			if (status != AMEDIA_OK) {
				SDL_Log("Could not configure Android media encoder: %d", status);
				Stop();
				return false;
			}

			status = AMediaCodec_start(codec);
			if (status != AMEDIA_OK) {
				SDL_Log("Could not start Android media encoder: %d", status);
				Stop();
				return false;
			}

			started = true;
			return true;
		}

		bool WriteRgbaFrame(const uint8_t* rgba, int pitch) {
			if (!started) {
				return false;
			}

			if (!Drain(false)) {
				return false;
			}

			ssize_t inputIndex = AMediaCodec_dequeueInputBuffer(codec, 10000);
			if (inputIndex < 0) {
				SDL_Log("Android media encoder input buffer was not available.");
				return false;
			}

			size_t inputSize = 0;
			uint8_t* input = AMediaCodec_getInputBuffer(codec, inputIndex, &inputSize);
			const size_t needed = static_cast<size_t>(width) * height * 3 / 2;
			if (!input || inputSize < needed) {
				SDL_Log("Android media encoder input buffer is too small.");
				return false;
			}

			ConvertRgbaToNv12(rgba, pitch, input);
			const int64_t ptsUs = static_cast<int64_t>(frameIndex) * 1000000 / fps;
			media_status_t status = AMediaCodec_queueInputBuffer(codec, inputIndex, 0, needed, ptsUs, 0);
			if (status != AMEDIA_OK) {
				SDL_Log("Could not queue Android media encoder input: %d", status);
				return false;
			}

			++frameIndex;
			return Drain(false);
		}

		void Stop() {
			if (started && codec) {
				ssize_t inputIndex = AMediaCodec_dequeueInputBuffer(codec, 10000);
				if (inputIndex >= 0) {
					const int64_t ptsUs = static_cast<int64_t>(frameIndex) * 1000000 / std::max(1, fps);
					AMediaCodec_queueInputBuffer(codec, inputIndex, 0, 0, ptsUs, AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM);
					Drain(true);
				}
				AMediaCodec_stop(codec);
			}
			if (codec) {
				AMediaCodec_delete(codec);
				codec = nullptr;
			}
			if (muxer) {
				if (muxerStarted) {
					AMediaMuxer_stop(muxer);
				}
				AMediaMuxer_delete(muxer);
				muxer = nullptr;
			}
			if (fd >= 0) {
				close(fd);
				fd = -1;
			}

			started = false;
			muxerStarted = false;
			trackIndex = -1;
			frameIndex = 0;
		}

		bool IsOpen() const {
			return started;
		}

	private:
		void ConvertRgbaToNv12(const uint8_t* rgba, int pitch, uint8_t* yuv) const {
			uint8_t* yPlane = yuv;
			uint8_t* uvPlane = yuv + static_cast<size_t>(width) * height;

			for (int y = 0; y < height; ++y) {
				const uint8_t* row = rgba + static_cast<size_t>(y) * pitch;
				for (int x = 0; x < width; ++x) {
					const uint8_t* px = row + x * 4;
					const int r = px[0];
					const int g = px[1];
					const int b = px[2];
					yPlane[y * width + x] = ClampByte(((66 * r + 129 * g + 25 * b + 128) >> 8) + 16);
				}
			}

			for (int y = 0; y < height; y += 2) {
				for (int x = 0; x < width; x += 2) {
					int uSum = 0;
					int vSum = 0;
					for (int yy = 0; yy < 2; ++yy) {
						const uint8_t* row = rgba + static_cast<size_t>(y + yy) * pitch;
						for (int xx = 0; xx < 2; ++xx) {
							const uint8_t* px = row + (x + xx) * 4;
							const int r = px[0];
							const int g = px[1];
							const int b = px[2];
							uSum += ((-38 * r - 74 * g + 112 * b + 128) >> 8) + 128;
							vSum += ((112 * r - 94 * g - 18 * b + 128) >> 8) + 128;
						}
					}

					const size_t uvIndex = static_cast<size_t>(y / 2) * width + x;
					uvPlane[uvIndex] = ClampByte(uSum / 4);
					uvPlane[uvIndex + 1] = ClampByte(vSum / 4);
				}
			}
		}

		bool Drain(bool endOfStream) {
			while (true) {
				AMediaCodecBufferInfo info{};
				ssize_t outputIndex = AMediaCodec_dequeueOutputBuffer(codec, &info, endOfStream ? 10000 : 0);
				if (outputIndex >= 0) {
					if ((info.flags & AMEDIACODEC_BUFFER_FLAG_CODEC_CONFIG) != 0) {
						info.size = 0;
					}

					if (info.size > 0) {
						if (!muxerStarted) {
							SDL_Log("Android media encoder produced data before muxer was ready.");
							return false;
						}

						size_t outputSize = 0;
						uint8_t* output = AMediaCodec_getOutputBuffer(codec, outputIndex, &outputSize);
						if (!output || static_cast<size_t>(info.offset + info.size) > outputSize) {
							SDL_Log("Android media encoder output buffer is invalid.");
							AMediaCodec_releaseOutputBuffer(codec, outputIndex, false);
							return false;
						}
						AMediaCodecBufferInfo sampleInfo = info;
						sampleInfo.offset = 0;
						media_status_t status = AMediaMuxer_writeSampleData(muxer, trackIndex, output + info.offset, &sampleInfo);
						if (status != AMEDIA_OK) {
							SDL_Log("Could not write Android media sample: %d", status);
							AMediaCodec_releaseOutputBuffer(codec, outputIndex, false);
							return false;
						}
					}

					AMediaCodec_releaseOutputBuffer(codec, outputIndex, false);
					if ((info.flags & AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM) != 0) {
						return true;
					}
				}
				else if (outputIndex == AMEDIACODEC_INFO_OUTPUT_FORMAT_CHANGED) {
					AMediaFormat* outputFormat = AMediaCodec_getOutputFormat(codec);
					trackIndex = AMediaMuxer_addTrack(muxer, outputFormat);
					AMediaFormat_delete(outputFormat);
					if (trackIndex < 0 || AMediaMuxer_start(muxer) != AMEDIA_OK) {
						SDL_Log("Could not start Android media muxer.");
						return false;
					}
					muxerStarted = true;
				}
				else if (outputIndex == AMEDIACODEC_INFO_TRY_AGAIN_LATER) {
					if (endOfStream) {
						continue;
					}
					return true;
				}
				else {
					return true;
				}
			}
		}

		static constexpr int32_t kColorFormatYuv420SemiPlanar = 21;

		AMediaCodec* codec = nullptr;
		AMediaMuxer* muxer = nullptr;
		int fd = -1;
		int trackIndex = -1;
		bool started = false;
		bool muxerStarted = false;
		int width = 0;
		int height = 0;
		int fps = 30;
		int64_t frameIndex = 0;
	};
#endif

#ifndef __ANDROID__
	class RawVideoPipe {
	public:
		~RawVideoPipe() {
			Stop();
		}

		bool Start(const std::string& command) {
			Stop();
#ifdef _WIN32
			SECURITY_ATTRIBUTES securityAttrs{};
			securityAttrs.nLength = sizeof(securityAttrs);
			securityAttrs.bInheritHandle = TRUE;

			HANDLE stdinRead = nullptr;
			if (!CreatePipe(&stdinRead, &stdinWrite, &securityAttrs, 0)) {
				return false;
			}
			if (!SetHandleInformation(stdinWrite, HANDLE_FLAG_INHERIT, 0)) {
				CloseHandle(stdinRead);
				CloseHandle(stdinWrite);
				stdinWrite = nullptr;
				return false;
			}

			HANDLE nullOutput = CreateFileA("NUL", GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
				&securityAttrs, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);

			STARTUPINFOA startupInfo{};
			startupInfo.cb = sizeof(startupInfo);
			startupInfo.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
			startupInfo.wShowWindow = SW_HIDE;
			startupInfo.hStdInput = stdinRead;
			startupInfo.hStdOutput = nullOutput != INVALID_HANDLE_VALUE ? nullOutput : GetStdHandle(STD_OUTPUT_HANDLE);
			startupInfo.hStdError = nullOutput != INVALID_HANDLE_VALUE ? nullOutput : GetStdHandle(STD_ERROR_HANDLE);

			PROCESS_INFORMATION processInfo{};
			std::string mutableCommand = command;
			BOOL created = CreateProcessA(nullptr, mutableCommand.data(), nullptr, nullptr, TRUE,
				CREATE_NO_WINDOW, nullptr, nullptr, &startupInfo, &processInfo);

			CloseHandle(stdinRead);
			if (nullOutput != INVALID_HANDLE_VALUE) {
				CloseHandle(nullOutput);
			}

			if (!created) {
				CloseHandle(stdinWrite);
				stdinWrite = nullptr;
				return false;
			}

			processHandle = processInfo.hProcess;
			CloseHandle(processInfo.hThread);
			return true;
#else
			pipe = ::popen(command.c_str(), "w");
			return pipe != nullptr;
#endif
		}

		bool Write(const uint8_t* data, size_t size) {
#ifdef _WIN32
			if (!stdinWrite) {
				return false;
			}

			size_t offset = 0;
			while (offset < size) {
				DWORD chunk = static_cast<DWORD>(std::min<size_t>(size - offset, 1 << 20));
				DWORD written = 0;
				if (!WriteFile(stdinWrite, data + offset, chunk, &written, nullptr) || written == 0) {
					return false;
				}
				offset += written;
			}
			return true;
#else
			if (!pipe) {
				return false;
			}
			return std::fwrite(data, 1, size, pipe) == size;
#endif
		}

		void Stop() {
#ifdef _WIN32
			if (stdinWrite) {
				CloseHandle(stdinWrite);
				stdinWrite = nullptr;
			}
			if (processHandle) {
				DWORD waitResult = WaitForSingleObject(processHandle, 5000);
				if (waitResult == WAIT_TIMEOUT) {
					SDL_Log("Timed out while finalizing recording; terminating ffmpeg.");
					TerminateProcess(processHandle, 1);
				}
				CloseHandle(processHandle);
				processHandle = nullptr;
			}
#else
			if (pipe) {
				::pclose(pipe);
				pipe = nullptr;
			}
#endif
		}

		bool IsOpen() const {
#ifdef _WIN32
			return stdinWrite != nullptr;
#else
			return pipe != nullptr;
#endif
		}

	private:
#ifdef _WIN32
		HANDLE stdinWrite = nullptr;
		HANDLE processHandle = nullptr;
#else
		FILE* pipe = nullptr;
#endif
	};
#endif

	SDL_Color CaptureBackgroundColour(uint32_t rgb) {
		return {
			static_cast<Uint8>((rgb >> 16) & 0xff),
			static_cast<Uint8>((rgb >> 8) & 0xff),
			static_cast<Uint8>(rgb & 0xff),
			255};
	}

	Uint32 MapScreenshotPixel(SDL_PixelFormat* format, const ColourInfo& ink_colour, const SDL_Color& background, float alpha_value) {
		if (alpha_value > 255.0f) {
			const SDL_Color colour = ScreenPixelColour(ink_colour, alpha_value);
			return SDL_MapRGBA(format, colour.r, colour.g, colour.b, colour.a);
		}

		const int alpha = std::clamp(static_cast<int>(std::lround(alpha_value)), 0, 255);
		const auto blend = [alpha](int foreground, int background_channel) {
			return static_cast<uint8_t>((foreground * alpha + background_channel * (255 - alpha) + 127) / 255);
		};
		return SDL_MapRGBA(format,
			blend(ink_colour.r, background.r),
			blend(ink_colour.g, background.g),
			blend(ink_colour.b, background.b),
			255);
	}

	void FillScaledPixel(SDL_Surface* surface, int x, int y, int scale, Uint32 colour) {
		for (int dy = 0; dy < scale; ++dy) {
			const int py = y + dy;
			if (py < 0 || py >= surface->h)
				continue;
			auto* row = reinterpret_cast<Uint32*>(static_cast<uint8_t*>(surface->pixels) + py * surface->pitch);
			for (int dx = 0; dx < scale; ++dx) {
				const int px = x + dx;
				if (px >= 0 && px < surface->w)
					row[px] = colour;
			}
		}
	}

	SDL_Rect ScaleScreenshotRect(const SDL_Rect& rect, const SDL_Rect& capture_rect, double sx, double sy) {
		const int x0 = static_cast<int>(std::floor((rect.x - capture_rect.x) * sx));
		const int y0 = static_cast<int>(std::floor((rect.y - capture_rect.y) * sy));
		const int x1 = static_cast<int>(std::ceil((rect.x + rect.w - capture_rect.x) * sx));
		const int y1 = static_cast<int>(std::ceil((rect.y + rect.h - capture_rect.y) * sy));
		return {x0, y0, std::max(1, x1 - x0), std::max(1, y1 - y0)};
	}

	Rect ToModelRect(const SDL_Rect& rect) {
		return {rect.x, rect.y, rect.w, rect.h};
	}

	struct ScreenCaptureSource {
		SDL_Texture* interface_texture = nullptr;
		SDL_Surface* interface_surface = nullptr;
		const std::vector<SpriteInfo>* sprite_info = nullptr;
		const std::vector<uint8_t>* sprite_available = nullptr;
		const ColourInfo* ink_colour = nullptr;
		const float* screen_ink_alpha = nullptr;
		int logical_width = 0;
		int logical_height = 0;
		SDL_Rect lcd_dest{};
		bool render_pixel_layer = true;
	};

	struct ScreenCaptureLayout {
		SDL_Rect capture_rect{};
		SDL_Rect scaled_lcd{};
		int scale = 3;
		int content_width = 0;
		int content_height = 0;
		int output_width = 0;
		int output_height = 0;
		double sx = 1.0;
		double sy = 1.0;
	};

	bool BuildScreenCaptureLayout(const ScreenCaptureSource& source, int requested_scale, bool even_output, ScreenCaptureLayout& layout, const char* purpose) {
		if (!source.sprite_info || !source.sprite_available ||
			!source.ink_colour || !source.screen_ink_alpha ||
			source.logical_width <= 0 || source.logical_height <= 0 || source.lcd_dest.w <= 0 || source.lcd_dest.h <= 0) {
			SDL_Log("%s failed: invalid capture source: texture=%p sprites=%p available=%p ink=%p alpha=%p logical=%dx%d lcd=%d,%d %dx%d.",
				purpose,
				static_cast<void*>(source.interface_texture),
				static_cast<const void*>(source.sprite_info),
				static_cast<const void*>(source.sprite_available),
				static_cast<const void*>(source.ink_colour),
				static_cast<const void*>(source.screen_ink_alpha),
				source.logical_width,
				source.logical_height,
				source.lcd_dest.x,
				source.lcd_dest.y,
				source.lcd_dest.w,
				source.lcd_dest.h);
			return false;
		}

		std::vector<SDL_Rect> spriteRects;
		for (size_t ix = 1; ix < source.sprite_info->size() && ix < source.sprite_available->size(); ++ix) {
			if (!(*source.sprite_available)[ix])
				continue;
			spriteRects.push_back((*source.sprite_info)[ix].dest);
		}

		layout = {};
		if (!GetCaptureRect(spriteRects, std::vector<SDL_Rect>{source.lcd_dest}, layout.capture_rect)) {
			SDL_Log("%s failed: invalid capture region.", purpose);
			return false;
		}

		layout.scale = std::max(1, requested_scale);
		layout.sx = static_cast<double>(source.logical_width * layout.scale) / static_cast<double>(source.lcd_dest.w);
		layout.sy = static_cast<double>(source.logical_height * layout.scale) / static_cast<double>(source.lcd_dest.h);
		layout.content_width = std::max(1, static_cast<int>(std::ceil(layout.capture_rect.w * layout.sx)));
		layout.content_height = std::max(1, static_cast<int>(std::ceil(layout.capture_rect.h * layout.sy)));
		layout.output_width = even_output ? ((layout.content_width + 1) & ~1) : layout.content_width;
		layout.output_height = even_output ? ((layout.content_height + 1) & ~1) : layout.content_height;
		layout.scaled_lcd = ScaleScreenshotRect(source.lcd_dest, layout.capture_rect, layout.sx, layout.sy);
		return true;
	}

	class ScreenCaptureComposer {
	public:
		~ScreenCaptureComposer() {
			Reset();
		}

		void Reset() {
			if (target) {
				SDL_DestroyTexture(target);
				target = nullptr;
			}
			target_width = 0;
			target_height = 0;
		}

		bool Render(SDL_Renderer* renderer, const ScreenCaptureSource& source, const ScreenCaptureLayout& layout, SDL_Surface* surface, const SDL_Color& background, const char* purpose) {
			if (!renderer || !surface || surface->w != layout.output_width || surface->h != layout.output_height) {
				SDL_Log("%s failed: invalid capture surface.", purpose);
				return false;
			}
			if (!EnsureTarget(renderer, layout.output_width, layout.output_height, purpose)) {
				return false;
			}

			SDL_Texture* old_target = SDL_GetRenderTarget(renderer);
			SDL_Rect old_viewport{};
			SDL_Rect old_clip{};
			float old_scale_x = 1.0f;
			float old_scale_y = 1.0f;
			SDL_BlendMode old_blend_mode{};
			SDL_RenderGetViewport(renderer, &old_viewport);
			SDL_RenderGetClipRect(renderer, &old_clip);
			const SDL_bool old_clip_enabled = SDL_RenderIsClipEnabled(renderer);
			SDL_RenderGetScale(renderer, &old_scale_x, &old_scale_y);
			SDL_GetRenderDrawBlendMode(renderer, &old_blend_mode);
			bool render_target_active = false;
			auto restore = [&]() {
				if (!render_target_active)
					return;
				SDL_SetRenderTarget(renderer, old_target);
				SDL_RenderSetViewport(renderer, &old_viewport);
				SDL_RenderSetClipRect(renderer, old_clip_enabled ? &old_clip : nullptr);
				SDL_RenderSetScale(renderer, old_scale_x, old_scale_y);
				SDL_SetRenderDrawBlendMode(renderer, old_blend_mode);
				render_target_active = false;
			};

			if (SDL_SetRenderTarget(renderer, target) != 0) {
				SDL_Log("%s failed: cannot bind render target: %s", purpose, SDL_GetError());
				return false;
			}
			render_target_active = true;
			SDL_RenderSetViewport(renderer, nullptr);
			SDL_RenderSetClipRect(renderer, nullptr);
			SDL_RenderSetScale(renderer, 1.0f, 1.0f);
			SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
			SDL_SetRenderDrawColor(renderer, background.r, background.g, background.b, background.a);
			SDL_RenderClear(renderer);

			if (svg_texture_cache.size() < source.sprite_info->size())
				svg_texture_cache.resize(source.sprite_info->size());
			for (size_t ix = 1; ix < source.sprite_info->size() && ix < source.sprite_available->size(); ++ix) {
				if (!(*source.sprite_available)[ix])
					continue;
				const int alpha_index = static_cast<int>(ix - 1);
				SpriteInfo sprite = (*source.sprite_info)[ix];
				sprite.dest = ToModelRect(ScaleScreenshotRect(sprite.dest, layout.capture_rect, layout.sx, layout.sy));
				const uint8_t alpha = Uint8(std::clamp(static_cast<int>(source.screen_ink_alpha[alpha_index]), 0, 255));
				RenderModelSprite(renderer, source.interface_texture, &svg_texture_cache[ix], sprite, *source.ink_colour, alpha);
			}

			if (SDL_MUSTLOCK(surface) && SDL_LockSurface(surface) != 0) {
				SDL_Log("%s failed: cannot lock capture surface: %s", purpose, SDL_GetError());
				restore();
				return false;
			}

			if (SDL_RenderReadPixels(renderer, nullptr, SDL_PIXELFORMAT_RGBA32, surface->pixels, surface->pitch) != 0) {
				SDL_Log("%s failed: cannot read capture target pixels: %s", purpose, SDL_GetError());
				if (SDL_MUSTLOCK(surface))
					SDL_UnlockSurface(surface);
				restore();
				return false;
			}
			restore();

			if (source.render_pixel_layer) {
				for (int y = 0; y < source.logical_height; ++y) {
					const int source_y = y + 1;
					for (int x = 0; x < source.logical_width; ++x) {
						const float alpha_value = source.screen_ink_alpha[x + source_y * 192];
						if (alpha_value <= 0.0f)
							continue;
						FillScaledPixel(surface, layout.scaled_lcd.x + x * layout.scale, layout.scaled_lcd.y + y * layout.scale, layout.scale, MapScreenshotPixel(surface->format, *source.ink_colour, background, alpha_value));
					}
				}
			}
			if (SDL_MUSTLOCK(surface))
				SDL_UnlockSurface(surface);
			return true;
		}

	private:
		bool EnsureTarget(SDL_Renderer* renderer, int width, int height, const char* purpose) {
			if (target && target_width == width && target_height == height)
				return true;
			Reset();
			target = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_TARGET, width, height);
			if (!target) {
				SDL_Log("%s failed: cannot create render target: %s", purpose, SDL_GetError());
				return false;
			}
			SDL_SetTextureBlendMode(target, SDL_BLENDMODE_NONE);
			target_width = width;
			target_height = height;
			return true;
		}

		SDL_Texture* target = nullptr;
		int target_width = 0;
		int target_height = 0;
		std::vector<SvgSpriteTextureCache> svg_texture_cache;
	};

	class ScreenRecorder {
	public:
		~ScreenRecorder() {
			Stop();
		}

		bool Start(const ScreenCaptureSource& source, int capture_scale, int requestedFps = 30) {
			Stop();
			if (!BuildScreenCaptureLayout(source, capture_scale, true, layout, "Recording")) {
				return false;
			}

			fps = std::max(1, requestedFps);
			outputWidth = layout.output_width;
			outputHeight = layout.output_height;
			frameCount = 0;
			nextCaptureTick = 0;
			if (!EnsureFrameSurface()) {
				return false;
			}

			const std::string stem = MakeTimestampedName("recording-", "");
			outputPath = GetRecordingOutputPath(stem + ".mp4");
#ifdef __ANDROID__
			if (encoder.Start(outputPath, outputWidth, outputHeight, fps)) {
				frameSequence = false;
				recording = true;
				SDL_Log("Recording started: %s", outputPath.string().c_str());
				return true;
			}
#else
			const std::string command = BuildFfmpegCommand(outputPath);
			if (encoder.Start(command)) {
				frameSequence = false;
				recording = true;
				SDL_Log("Recording started: %s", outputPath.string().c_str());
				return true;
			}
#endif

			frameSequence = true;
			frameDirectory = GetRecordingOutputPath(stem + "-frames");
			std::error_code ec;
			std::filesystem::create_directories(frameDirectory, ec);
			if (ec) {
				SDL_Log("Recording failed: cannot create frame directory %s (%s)",
					frameDirectory.string().c_str(), ec.message().c_str());
				ResetFrameSurface();
				return false;
			}

			recording = true;
#ifdef __ANDROID__
			SDL_Log("Android video encoder was not available; recording PNG frames to %s", frameDirectory.string().c_str());
#else
			SDL_Log("ffmpeg was not available; recording PNG frames to %s", frameDirectory.string().c_str());
#endif
			return true;
		}

		void Stop() {
			if (!recording && !encoder.IsOpen()) {
				ResetFrameSurface();
				composer.Reset();
				return;
			}
			encoder.Stop();
			if (recording) {
				if (frameSequence) {
					SDL_Log("Recording stopped: %u frames saved to %s",
						frameCount, frameDirectory.string().c_str());
				}
				else {
					SDL_Log("Recording stopped: %u frames saved to %s",
						frameCount, outputPath.string().c_str());
				}
			}
			recording = false;
			ResetFrameSurface();
			composer.Reset();
		}

		bool CaptureFrame(SDL_Renderer* renderer, const ScreenCaptureSource& source, const SDL_Color& background) {
			if (!recording) {
				return false;
			}

			Uint64 now = SDL_GetTicks64();
			if (nextCaptureTick != 0 && now < nextCaptureTick) {
				return true;
			}
			nextCaptureTick = now + static_cast<Uint64>(1000 / fps);

			if (!frameSurface || !composer.Render(renderer, source, layout, frameSurface, background, "Recording")) {
				Stop();
				return false;
			}

			bool success = frameSequence
				? SaveFrameAsPng()
#ifdef __ANDROID__
				: encoder.WriteRgbaFrame(framePixels.data(), frameSurface->pitch);
#else
				: encoder.Write(framePixels.data(), framePixels.size());
#endif
			if (!success) {
				SDL_Log("Recording stopped because frame writing failed.");
				Stop();
				return false;
			}

			++frameCount;
			return true;
		}

		bool IsRecording() const {
			return recording;
		}

		unsigned int FrameCount() const {
			return frameCount;
		}

	private:
		std::string BuildFfmpegCommand(const std::filesystem::path& path) const {
			std::ostringstream command;
			command << "ffmpeg -y -hide_banner -loglevel error"
				<< " -f rawvideo -vcodec rawvideo"
				<< " -pixel_format rgba"
				<< " -video_size " << outputWidth << "x" << outputHeight
				<< " -framerate " << fps
				<< " -i - -an -c:v mpeg4 -q:v 3 -pix_fmt yuv420p "
				<< "\"" << path.string() << "\"";
			return command.str();
		}

		bool EnsureFrameSurface() {
			ResetFrameSurface();
			const int pitch = outputWidth * 4;
			framePixels.assign(static_cast<size_t>(pitch) * outputHeight, 255);
			frameSurface = SDL_CreateRGBSurfaceWithFormatFrom(
				framePixels.data(),
				outputWidth,
				outputHeight,
				32,
				pitch,
				SDL_PIXELFORMAT_RGBA32);
			if (!frameSurface) {
				SDL_Log("Recording failed: cannot create frame surface: %s", SDL_GetError());
				framePixels.clear();
				return false;
			}
			return true;
		}

		void ResetFrameSurface() {
			if (frameSurface) {
				SDL_FreeSurface(frameSurface);
				frameSurface = nullptr;
			}
			framePixels.clear();
		}

		bool SaveFrameAsPng() const {
			std::ostringstream filename;
			filename << "frame-" << std::setw(6) << std::setfill('0') << frameCount << ".png";
			std::filesystem::path framePath = frameDirectory / filename.str();

			const std::string pathString = framePath.string();
			int result = IMG_SavePNG(frameSurface, pathString.c_str());
			if (result != 0) {
				SDL_Log("Error saving recording frame: %s", IMG_GetError());
				return false;
			}
			return true;
		}

#ifdef __ANDROID__
		AndroidVideoEncoder encoder;
#else
		RawVideoPipe encoder;
#endif
		ScreenCaptureLayout layout{};
		ScreenCaptureComposer composer;
		std::vector<uint8_t> framePixels;
		SDL_Surface* frameSurface = nullptr;
		int fps = 30;
		int outputWidth = 0;
		int outputHeight = 0;
		Uint64 nextCaptureTick = 0;
		unsigned int frameCount = 0;
		bool recording = false;
		bool frameSequence = false;
		std::filesystem::path outputPath;
		std::filesystem::path frameDirectory;
	};

	class ScreenMirrorComposer {
	public:
		~ScreenMirrorComposer() {
			Reset();
		}

		void Reset() {
			if (pixel_texture) {
				SDL_DestroyTexture(pixel_texture);
				pixel_texture = nullptr;
			}
			if (interface_texture) {
				SDL_DestroyTexture(interface_texture);
				interface_texture = nullptr;
			}
			pixel_width = 0;
			pixel_height = 0;
			interface_surface = nullptr;
			pixel_pixels.clear();
			svg_texture_cache.clear();
		}

		bool Render(ScreenMirror& mirror, const ScreenCaptureSource& source, const SDL_Color& background) {
			SDL_Renderer* mirror_renderer = mirror.renderer();
			if (!mirror_renderer)
				return false;

			SDL_Rect capture_rect{};
			if (!BuildCaptureRect(source, capture_rect)) {
				SDL_Log("Mirror update failed: invalid capture region.");
				return false;
			}

			mirror.clear(background);
			const SDL_Rect content_rect = mirror.contentRect();
			if (content_rect.w <= 0 || content_rect.h <= 0)
				return false;

			const double sx = static_cast<double>(content_rect.w) / static_cast<double>(capture_rect.w);
			const double sy = static_cast<double>(content_rect.h) / static_cast<double>(capture_rect.h);
			SDL_Texture* fallback_texture = EnsureInterfaceTexture(mirror_renderer, source);

			if (svg_texture_cache.size() < source.sprite_info->size())
				svg_texture_cache.resize(source.sprite_info->size());
			for (size_t ix = 1; ix < source.sprite_info->size() && ix < source.sprite_available->size(); ++ix) {
				if (!(*source.sprite_available)[ix])
					continue;
				const int alpha_index = static_cast<int>(ix - 1);
				SpriteInfo sprite = (*source.sprite_info)[ix];
				SDL_Rect dest = ScaleScreenshotRect(sprite.dest, capture_rect, sx, sy);
				dest.x += content_rect.x;
				dest.y += content_rect.y;
				sprite.dest = ToModelRect(dest);
				const uint8_t alpha = Uint8(std::clamp(static_cast<int>(source.screen_ink_alpha[alpha_index]), 0, 255));
				RenderModelSprite(mirror_renderer, fallback_texture, &svg_texture_cache[ix], sprite, *source.ink_colour, alpha);
			}

			if (source.render_pixel_layer) {
				SDL_Rect lcd_dest = ScaleScreenshotRect(source.lcd_dest, capture_rect, sx, sy);
				lcd_dest.x += content_rect.x;
				lcd_dest.y += content_rect.y;
				if (!RenderPixelTexture(mirror_renderer, source, lcd_dest))
					return false;
			}
			mirror.present();
			return true;
		}

	private:
		bool BuildCaptureRect(const ScreenCaptureSource& source, SDL_Rect& capture_rect) const {
			if (!source.sprite_info || !source.sprite_available || !source.ink_colour || !source.screen_ink_alpha ||
				source.logical_width <= 0 || source.logical_height <= 0 || source.lcd_dest.w <= 0 || source.lcd_dest.h <= 0)
				return false;

			std::vector<SDL_Rect> sprite_rects;
			for (size_t ix = 1; ix < source.sprite_info->size() && ix < source.sprite_available->size(); ++ix) {
				if ((*source.sprite_available)[ix])
					sprite_rects.push_back((*source.sprite_info)[ix].dest);
			}
			return GetCaptureRect(sprite_rects, std::vector<SDL_Rect>{source.lcd_dest}, capture_rect);
		}

		SDL_Texture* EnsureInterfaceTexture(SDL_Renderer* renderer, const ScreenCaptureSource& source) {
			if (!renderer || !source.interface_surface)
				return nullptr;
			if (interface_texture && interface_surface == source.interface_surface)
				return interface_texture;
			if (interface_texture) {
				SDL_DestroyTexture(interface_texture);
				interface_texture = nullptr;
			}
			interface_surface = source.interface_surface;
			interface_texture = SDL_CreateTextureFromSurface(renderer, source.interface_surface);
			if (!interface_texture) {
				SDL_Log("Mirror update failed: cannot create interface texture: %s", SDL_GetError());
				interface_surface = nullptr;
				return nullptr;
			}
			SDL_SetTextureBlendMode(interface_texture, SDL_BLENDMODE_BLEND);
			return interface_texture;
		}

		bool EnsurePixelTexture(SDL_Renderer* renderer, int width, int height) {
			if (!renderer || width <= 0 || height <= 0)
				return false;
			if (pixel_texture && pixel_width == width && pixel_height == height)
				return true;
			if (pixel_texture) {
				SDL_DestroyTexture(pixel_texture);
				pixel_texture = nullptr;
			}
			pixel_texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STREAMING, width, height);
			if (!pixel_texture) {
				SDL_Log("Mirror update failed: cannot create pixel texture: %s", SDL_GetError());
				pixel_width = 0;
				pixel_height = 0;
				pixel_pixels.clear();
				return false;
			}
			SDL_SetTextureBlendMode(pixel_texture, SDL_BLENDMODE_BLEND);
#if SDL_VERSION_ATLEAST(2, 0, 12)
			SDL_SetTextureScaleMode(pixel_texture, SDL_ScaleModeNearest);
#endif
			pixel_width = width;
			pixel_height = height;
			pixel_pixels.resize(static_cast<size_t>(width) * static_cast<size_t>(height) * 4);
			return true;
		}

		bool RenderPixelTexture(SDL_Renderer* renderer, const ScreenCaptureSource& source, const SDL_Rect& dest) {
			if (!EnsurePixelTexture(renderer, source.logical_width, source.logical_height))
				return false;
			for (int y = 0; y < source.logical_height; ++y) {
				const int source_y = y + 1;
				for (int x = 0; x < source.logical_width; ++x) {
					const SDL_Color colour = ScreenPixelColour(*source.ink_colour, source.screen_ink_alpha[x + source_y * 192]);
					const size_t pixel_offset = (static_cast<size_t>(y) * static_cast<size_t>(source.logical_width) + static_cast<size_t>(x)) * 4;
					pixel_pixels[pixel_offset + 0] = colour.r;
					pixel_pixels[pixel_offset + 1] = colour.g;
					pixel_pixels[pixel_offset + 2] = colour.b;
					pixel_pixels[pixel_offset + 3] = colour.a;
				}
			}
			if (SDL_UpdateTexture(pixel_texture, nullptr, pixel_pixels.data(), source.logical_width * 4) != 0) {
				SDL_Log("Mirror update failed: cannot update pixel texture: %s", SDL_GetError());
				return false;
			}
			if (SDL_RenderCopy(renderer, pixel_texture, nullptr, &dest) != 0) {
				SDL_Log("Mirror update failed: cannot render pixel texture: %s", SDL_GetError());
				return false;
			}
			return true;
		}

		SDL_Texture* pixel_texture = nullptr;
		int pixel_width = 0;
		int pixel_height = 0;
		std::vector<uint8_t> pixel_pixels;
		SDL_Surface* interface_surface = nullptr;
		SDL_Texture* interface_texture = nullptr;
		std::vector<SvgSpriteTextureCache> svg_texture_cache;
	};

	bool CapturePixelPerfectScreenshot(
		SDL_Renderer* renderer,
		SDL_Texture* interface_texture,
		SDL_Surface* interface_surface,
		const std::vector<SpriteInfo>& sprite_info,
		const std::vector<uint8_t>& sprite_available,
		const ColourInfo& ink_colour,
		const float* screen_ink_alpha,
		int logical_width,
		int logical_height,
		const SDL_Rect& lcd_dest,
		int capture_scale,
		const SDL_Color& background,
		bool render_pixel_layer = true) {
		ScreenCaptureSource source{
			interface_texture,
			interface_surface,
			&sprite_info,
			&sprite_available,
			&ink_colour,
			screen_ink_alpha,
			logical_width,
			logical_height,
			lcd_dest,
			render_pixel_layer};

		ScreenCaptureLayout layout{};
		if (!BuildScreenCaptureLayout(source, capture_scale, false, layout, "Screenshot"))
			return false;

		SDL_Surface* screenSurface = SDL_CreateRGBSurfaceWithFormat(0, layout.output_width, layout.output_height, 32, SDL_PIXELFORMAT_RGBA32);
		if (!screenSurface) {
			SDL_Log("Error creating screenshot surface: %s", SDL_GetError());
			return false;
		}

		ScreenCaptureComposer composer;
		if (!composer.Render(renderer, source, layout, screenSurface, background, "Screenshot")) {
			SDL_FreeSurface(screenSurface);
			return false;
		}
		SaveScreenshotSurface(screenSurface, MakeTimestampedName("screenshot-", ".png"));
		SDL_FreeSurface(screenSurface);
		return true;
	}

	std::pair<int, int> GetSize(const ScreenCaptureSource& source) {
		SDL_Rect captureRect{};
		if (!source.sprite_info || !source.sprite_available)
			return {0, 0};
		std::vector<SDL_Rect> spriteRects;
		for (size_t ix = 1; ix < source.sprite_info->size() && ix < source.sprite_available->size(); ++ix) {
			if ((*source.sprite_available)[ix])
				spriteRects.push_back((*source.sprite_info)[ix].dest);
		}
		if (!GetCaptureRect(spriteRects, std::vector<SDL_Rect>{source.lcd_dest}, captureRect)) {
			return { 0, 0 };
		}
		return { captureRect.w, captureRect.h };
	}
#endif
	// Function to collect all sprite and pixel rectangles
	template <HardwareId hardware_id>
	void Screen<hardware_id>::Frame() {
#ifdef __EMSCRIPTEN__
		tick();
#elif defined(TEST_BUILD)
		if constexpr (IsEpsFamily(hardware_id))
			tick();
#endif
		if constexpr (IsEpsFamily(hardware_id)) {
			std::lock_guard<std::mutex> lock(eps_screen_alpha_mutex);
			std::copy(eps_screen_ink_alpha.begin(), eps_screen_ink_alpha.end(), screen_ink_alpha);
		}
		int screenWidth = 0, screenHeight = 0;

		// Get the renderer output size if not already available
		SDL_GetRendererOutputSize(renderer, &screenWidth, &screenHeight);

		if (!emulator.ModelDefinition.enable_new_screen) {
			SDL_SetTextureColorMod(interface_texture, ink_colour.r, ink_colour.g, ink_colour.b);
		}

		for (int ix = 1; ix != SpriteCount(); ++ix) {
			if (ix >= static_cast<int>(sprite_available.size()) || !sprite_available[ix])
				continue;
			const int alpha_index = ix - 1;
			const uint8_t alpha = Uint8(std::clamp((int)screen_ink_alpha[alpha_index], 0, 255));
			if (alpha == 0)
				continue;
			RenderModelSprite(renderer, interface_texture,
				ix < static_cast<int>(sprite_svg_textures.size()) ? &sprite_svg_textures[ix] : nullptr,
				sprite_info[ix], ink_colour, alpha);
		}

		static constexpr auto SPR_PIXEL = 0;
		SDL_Rect dest = Screen<hardware_id>::sprite_info[SPR_PIXEL].dest;
		const bool board_screen_slot = !emulator.ModelDefinition.board_path.empty();
		const bool segment_lcd = IsEpsSegmentLcd(hardware_id);
		const int logical_width = segment_lcd ? std::max(1, emulator.ModelDefinition.screen_width) : ROW_SIZE_DISP * 8;
		const int logical_height = segment_lcd ? std::max(1, emulator.ModelDefinition.screen_height) : N_ROW;
		SDL_Rect lcd_dest = dest;
		if (!board_screen_slot) {
			lcd_dest.w = std::max(1, (logical_width - 1) * sprite_info[SPR_PIXEL].src.w + sprite_info[SPR_PIXEL].dest.w);
			lcd_dest.h = std::max(1, (logical_height - 1) * sprite_info[SPR_PIXEL].src.h + sprite_info[SPR_PIXEL].dest.h);
		}

#ifndef CASIOEMU_CORE_WEB
		if (!segment_lcd)
			RenderPixelScreenTexture(lcd_dest, logical_width, logical_height);
#endif

#if !defined(__EMSCRIPTEN__) && !defined(CASIOEMU_CORE_WEB)
		const SDL_Color capture_background = CaptureBackgroundColour(emulator.capture_background_rgb.load());
		ScreenCaptureSource captureSource{
			interface_texture,
			emulator.interface_surface,
			&sprite_info,
			&sprite_available,
			&ink_colour,
			screen_ink_alpha,
			logical_width,
			logical_height,
			lcd_dest,
			!segment_lcd};

		// If screenshot is requested, capture only the rendered screen region
		if (emulator.screenshot_requested.load()) {
			CapturePixelPerfectScreenshot(
				renderer,
				interface_texture,
				emulator.interface_surface,
				sprite_info,
				sprite_available,
				ink_colour,
				screen_ink_alpha,
				logical_width,
				logical_height,
				lcd_dest,
				emulator.capture_scale.load(),
				capture_background,
				!segment_lcd);
			emulator.screenshot_requested.store(false);
		}
		static ScreenRecorder recorder;
		if (emulator.recording_requested.exchange(false) && !recorder.IsRecording()) {
			if (recorder.Start(captureSource, emulator.capture_scale.load(), 30)) {
				emulator.recording_frame_count.store(0);
				emulator.recording_active.store(true);
			}
			else {
				emulator.recording_active.store(false);
			}
		}
		if (emulator.recording_stop_requested.exchange(false)) {
			recorder.Stop();
			emulator.recording_active.store(false);
		}
		if (recorder.IsRecording()) {
			if (recorder.CaptureFrame(renderer, captureSource, capture_background)) {
				emulator.recording_active.store(true);
				emulator.recording_frame_count.store(recorder.FrameCount());
			}
			else {
				emulator.recording_active.store(false);
			}
		}
		else {
			emulator.recording_active.store(false);
		}
		static ScreenMirror* mirror = nullptr;
		static ScreenMirrorComposer mirrorComposer;
		if (emulator.mirroring_requested.load()) {
			auto p = GetSize(captureSource);
			if (mirror) {
				delete mirror;
				mirror = nullptr;
				mirrorComposer.Reset();
			}
			if (p.first > 0 && p.second > 0) {
				auto sm = new ScreenMirror(p.first, p.second);
				if (sm->create()) {
					mirror = sm;
				}
				else {
					delete sm;
				}
			}
			emulator.mirroring_requested.store(false);
		}
		if (mirror) {
			if (mirror->handleEvents()) {
				mirrorComposer.Render(*mirror, captureSource, capture_background);
			}
			else {
				delete mirror;
				mirror = nullptr;
				mirrorComposer.Reset();
			}
		}
#endif
	}

	template <HardwareId hardware_id>
	void Screen<hardware_id>::Reset() {
	}

	Peripheral* CreateScreen(Emulator& emulator) {
		switch (emulator.hardware_id) {
		case HW_FX_5800P:
			return new Screen<HW_FX_5800P>(emulator);
		case HW_ES_PLUS:
			return new Screen<HW_ES_PLUS>(emulator);

		case HW_CLASSWIZ:
			return new Screen<HW_CLASSWIZ>(emulator);

		case HW_CLASSWIZ_II:
			return new Screen<HW_CLASSWIZ_II>(emulator);

		case HW_SOLARII:
			return new SolarIIScreen(emulator);

		case HW_TI:
			return new Screen<HW_TI>(emulator);
		case HW_EPS6800:
			return new Screen<HW_EPS6800>(emulator);
		case HW_EPS6800_W192:
			return new Screen<HW_EPS6800_W192>(emulator);
		case HW_EPS6009:
			return new Screen<HW_EPS6009>(emulator);
		case HW_EPS9500:
			return new Screen<HW_EPS9500>(emulator);
		default:
			PANIC("Unknown hardware id\n");
		}
		std::abort();
	}
} // namespace casioemu
