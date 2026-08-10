#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>

namespace casioemu {
	constexpr size_t EPS6800_LCD_WIDTH = 96;
	constexpr size_t EPS6800_LCD_PIXEL_HEIGHT = 31;
	constexpr size_t EPS6800_LCD_PAGE_COUNT = 4;
	constexpr size_t EPS6800_LCD_RAW_SIZE = EPS6800_LCD_WIDTH * EPS6800_LCD_PAGE_COUNT;
	constexpr size_t EPS6800_STATUS_SIZE = EPS6800_LCD_WIDTH / 8;
	constexpr uint8_t EPS6800_CONTRAST_MAX = 0x0f;
	constexpr uint8_t EPS6800_EFFECTIVE_CONTRAST_MAX = 0x1f;
	constexpr float ESP_MIN_SCREEN_BRIGHTNESS = 3.0f;
	constexpr uint8_t EPS6800_FIRST_VISIBLE_CONTRAST = 10;
	constexpr uint8_t EPS6800_LAST_UNCLIPPED_CONTRAST = 17;
	constexpr float LCD_ALPHA_MAX = 255.0f;

	// HP firmware keeps the five-bit requested contrast in RAM 15h and writes
	// only bits 4:1 to LCDARH.ADJ. The diagnostic screen therefore has adjacent
	// pairs (12h/13h, 14h/15h, ...) with the same LCDARH value. Accept the RAM
	// value only when its upper four bits agree with the hardware register;
	// otherwise fall back to the centre of the matching two-step interval.
	inline uint8_t Eps6800EffectiveContrast(uint8_t contrast, uint8_t requested_contrast) {
		const uint8_t adj = contrast & EPS6800_CONTRAST_MAX;
		const uint8_t requested = requested_contrast & EPS6800_EFFECTIVE_CONTRAST_MAX;
		if (requested_contrast <= EPS6800_EFFECTIVE_CONTRAST_MAX && (requested >> 1) == adj)
			return requested;
		return static_cast<uint8_t>(std::min<unsigned>(
			EPS6800_EFFECTIVE_CONTRAST_MAX, static_cast<unsigned>(adj) * 2u + 1u));
	}

	inline float Eps6800ActiveAlpha(uint8_t effective_contrast) {
		const uint8_t level = effective_contrast & EPS6800_EFFECTIVE_CONTRAST_MAX;
		const auto es_plus_alpha = [](uint8_t value) {
			return -240.0f + static_cast<float>(value) * 28.0f -
				ESP_MIN_SCREEN_BRIGHTNESS * 8.0f;
		};

		// Apply the ES Plus equation directly to the five-bit value. It has two
		// plateaus in an RGBA renderer: levels 0..9 are below zero and levels
		// 18..31 eventually exceed an alpha of 255. In particular, an EPS model
		// with black ink renders every value above 255 as exactly the same black.
		// Preserve the ES Plus curve over its useful interval (10..17), and
		// distribute each clipped interval over the remaining alpha range. The
		// diagnostic contrast steps, including 14h and 15h, then remain distinct.
		const float first_visible_alpha = es_plus_alpha(EPS6800_FIRST_VISIBLE_CONTRAST);
		if (level < EPS6800_FIRST_VISIBLE_CONTRAST) {
			return first_visible_alpha * static_cast<float>(level) /
				static_cast<float>(EPS6800_FIRST_VISIBLE_CONTRAST);
		}
		if (level <= EPS6800_LAST_UNCLIPPED_CONTRAST)
			return es_plus_alpha(level);

		const float last_unclipped_alpha = es_plus_alpha(EPS6800_LAST_UNCLIPPED_CONTRAST);
		return last_unclipped_alpha + (LCD_ALPHA_MAX - last_unclipped_alpha) *
			static_cast<float>(level - EPS6800_LAST_UNCLIPPED_CONTRAST) /
			static_cast<float>(EPS6800_EFFECTIVE_CONTRAST_MAX - EPS6800_LAST_UNCLIPPED_CONTRAST);
	}

	inline float Eps6800InactiveAlpha(uint8_t effective_contrast) {
		const float level = static_cast<float>(
			effective_contrast & EPS6800_EFFECTIVE_CONTRAST_MAX);
		return std::clamp(-240.0f + 20.0f + level * 17.0f -
			ESP_MIN_SCREEN_BRIGHTNESS * 13.0f, 0.0f, LCD_ALPHA_MAX);
	}

	struct Eps6800DisplayFrame {
		std::array<uint8_t, EPS6800_STATUS_SIZE> status{};
		std::array<uint8_t, EPS6800_LCD_WIDTH * EPS6800_LCD_PIXEL_HEIGHT> pixels{};
	};

	inline Eps6800DisplayFrame DecodeEps6800Display(const uint8_t* lcd, size_t size) {
		Eps6800DisplayFrame frame{};
		if (!lcd || size < EPS6800_LCD_RAW_SIZE)
			return frame;

		for (size_t page = 0; page < EPS6800_LCD_PAGE_COUNT; ++page) {
			for (size_t x = 0; x < EPS6800_LCD_WIDTH; ++x) {
				const uint8_t value = lcd[page * EPS6800_LCD_WIDTH + x];
				for (size_t bit = 0; bit < 8; ++bit) {
					if (!(value & (1u << bit)))
						continue;
					const size_t logical_y = 31 - (page * 8 + bit);
					if (logical_y == 0) {
						frame.status[x >> 3] |= static_cast<uint8_t>(1u << (x & 7));
					}
					else {
						frame.pixels[(logical_y - 1) * EPS6800_LCD_WIDTH + x] = 1;
					}
				}
			}
		}
		return frame;
	}
} // namespace casioemu
