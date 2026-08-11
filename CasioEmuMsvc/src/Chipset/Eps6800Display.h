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
	constexpr uint8_t ESP_CONTRAST_MAX = 0x1f;
	constexpr float ESP_MIN_SCREEN_BRIGHTNESS = 3.0f;

	// EPS6800 exposes a four-bit adjustment while the ES Plus controller uses
	// five bits. Interpolate the EPS value over the complete ES Plus register
	// domain, then apply the same alpha equations used by Screen<HW_ES_PLUS>.
	inline float Eps6800AsEspContrast(uint8_t contrast) {
		const float level = static_cast<float>(contrast & EPS6800_CONTRAST_MAX);
		return level * static_cast<float>(ESP_CONTRAST_MAX) /
			static_cast<float>(EPS6800_CONTRAST_MAX);
	}

	inline float Eps6800ActiveAlpha(uint8_t contrast) {
		const float esp_contrast = Eps6800AsEspContrast(contrast);
		return std::max(0.0f, -240.0f + esp_contrast * 28.0f - ESP_MIN_SCREEN_BRIGHTNESS * 8.0f);
	}

	inline float Eps6800InactiveAlpha(uint8_t contrast) {
		const float esp_contrast = Eps6800AsEspContrast(contrast);
		return std::max(0.0f, -240.0f + 20.0f + esp_contrast * 17.0f - ESP_MIN_SCREEN_BRIGHTNESS * 13.0f);
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
