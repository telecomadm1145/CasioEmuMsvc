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
	constexpr size_t EPS9500_LCD_DEVICE_COUNT = 98;
	constexpr size_t EPS9500_LCD_VISIBLE_DEVICE_FIRST = 1;
	constexpr size_t EPS9500_LCD_WIDTH = 96;
	constexpr size_t EPS9500_LCD_HEIGHT = 32;
	constexpr size_t EPS9500_LCD_PAGE_COUNT = 4;
	constexpr size_t EPS9500_LCD_RAW_SIZE = EPS9500_LCD_DEVICE_COUNT * EPS9500_LCD_PAGE_COUNT;
	constexpr size_t EPS9500_STATUS_SIZE = EPS9500_LCD_PAGE_COUNT;
	constexpr size_t EPS6800_W192_LCD_WIDTH = 192;
	constexpr size_t EPS6800_W192_LCD_HEIGHT = 63;
	constexpr size_t EPS6800_W192_LCD_PAGE_COUNT = 8;
	constexpr size_t EPS6800_W192_LCD_RAW_SIZE =
		EPS6800_W192_LCD_WIDTH * EPS6800_W192_LCD_PAGE_COUNT;
	constexpr size_t EPS6800_W192_STATUS_SIZE = EPS6800_W192_LCD_WIDTH / 8;

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

	struct Eps9500DisplayFrame {
		std::array<uint8_t, EPS9500_STATUS_SIZE> status{};
		std::array<uint8_t, EPS9500_LCD_WIDTH * EPS9500_LCD_HEIGHT> pixels{};
	};

	inline Eps9500DisplayFrame DecodeEps9500Display(const uint8_t* lcd, size_t size) {
		Eps9500DisplayFrame frame{};
		if (!lcd || size < EPS9500_LCD_RAW_SIZE)
			return frame;

		/* EL_W506T.SegLcd: SegDevCnt=97. Device 0 carries the 32
		 * annunciator bits (one byte per page); devices 1..96 carry the
		 * dot-matrix columns. Bit groups are bottom-to-top. */
		for (size_t page = 0; page < EPS9500_LCD_PAGE_COUNT; ++page) {
			frame.status[page] = lcd[page * EPS9500_LCD_DEVICE_COUNT];
			for (size_t x = 0; x < EPS9500_LCD_WIDTH; ++x) {
				const uint8_t value = lcd[page * EPS9500_LCD_DEVICE_COUNT +
					EPS9500_LCD_VISIBLE_DEVICE_FIRST + x];
				for (size_t bit = 0; bit < 8; ++bit) {
					if (value & (1u << bit)) {
						const size_t y = EPS9500_LCD_HEIGHT - 1 - (page * 8 + bit);
						frame.pixels[y * EPS9500_LCD_WIDTH + x] = 1;
					}
				}
			}
		}
		return frame;
	}

	struct Eps6800W192DisplayFrame {
		std::array<uint8_t, EPS6800_W192_STATUS_SIZE> status{};
		std::array<uint8_t, EPS6800_W192_LCD_WIDTH * EPS6800_W192_LCD_HEIGHT> pixels{};
	};

	inline Eps6800W192DisplayFrame DecodeEps6800W192Display(const uint8_t* lcd, size_t size) {
		Eps6800W192DisplayFrame frame{};
		if (!lcd || size < EPS6800_W192_LCD_RAW_SIZE)
			return frame;
		for (size_t page = 0; page < EPS6800_W192_LCD_PAGE_COUNT; ++page) {
			for (size_t x = 0; x < EPS6800_W192_LCD_WIDTH; ++x) {
				const uint8_t value = lcd[page * EPS6800_W192_LCD_WIDTH + x];
				for (size_t bit = 0; bit < 8; ++bit) {
					if (!(value & (1u << bit)))
						continue;
					/* IQV9.exe sub_416840 stores page 0 bit 0 in its
					 * dedicated status row.  Its pixel-table indices run in the
					 * opposite direction to the Y coordinates initialized by
					 * sub_416580, so serial bit 1 is the top visible row. */
					const size_t serial_y = page * 8 + bit;
					if (serial_y == 0)
						frame.status[x >> 3] |= static_cast<uint8_t>(1u << (x & 7));
					else
						frame.pixels[(serial_y - 1) * EPS6800_W192_LCD_WIDTH + x] = 1;
				}
			}
		}
		return frame;
	}
} // namespace casioemu
