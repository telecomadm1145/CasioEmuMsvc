#pragma once

#include "OrdinaryLcdTarget.hpp"

#include <cstddef>
#include <span>

namespace casioemu::ordinary_lcd {

struct SpriteSpec {
	const char* name = nullptr;
	uint8_t mask = 0;
	uint8_t offset = 0;
};

struct FrameControls {
	int n_row;
	int row_size;
	int row_size_display;
	uint8_t mode;
	uint8_t range;
	uint8_t offset;
	bool enabled;
	bool allow_vertical_flip;
	TargetLevels levels;
};

// One operation from the original Screen target loop. A layout consumer can
// retain this description and reevaluate only the outputs touched by a byte.
struct PixelSource {
	size_t source_offset;
	uint8_t mask;
	int scan_row;
	int ink_alpha_on;
	int ink_alpha_off;
	bool read_source;
	bool clear;
};

template <bool classwiz_ii>
float EvaluatePixel(const PixelSource& source, const uint8_t* primary,
	const uint8_t* secondary, const float* scan_alpha, bool apply_scan) {
	int ink_alpha = source.ink_alpha_off;
	if constexpr (classwiz_ii) {
		if (source.read_source && (primary[source.source_offset] & source.mask))
			ink_alpha += (source.ink_alpha_on - source.ink_alpha_off) * (1.0f / 3.0f);
		if (source.read_source && (secondary[source.source_offset] & source.mask))
			ink_alpha += (source.ink_alpha_on - source.ink_alpha_off) * (2.0f / 3.0f);
	}
	else {
		if (source.read_source && (primary[source.source_offset] & source.mask))
			ink_alpha = source.ink_alpha_on;
	}
	if (apply_scan)
		ink_alpha *= scan_alpha[source.scan_row];
	if (source.clear)
		ink_alpha = 0;
	return static_cast<float>(ink_alpha);
}

// Mechanically follows Screen's mode/status/dot order. Decay remains a distinct
// operation: the legacy response multiplies alpha by ratio on those ranges.
// Buffer validation belongs to a snapshot consumer; the live consumer retains
// the original buffer access rules, including alternate debug buffers.
template <bool classwiz_ii, typename Pixel, typename Decay, typename Status>
void VisitFrame(const FrameControls& controls, std::span<const SpriteSpec> sprites,
	Pixel&& pixel, Decay&& decay, Status&& status) {
	bool enable_status;
	bool clear_dots;
	bool mode_6 = false;
	if (!controls.enabled) {
		decay(0, 64 * 192);
		return;
	}
	switch (controls.mode & 7) {
	case 4:
		clear_dots = true;
		enable_status = false;
		break;
	case 5:
		clear_dots = false;
		enable_status = true;
		break;
	case 6:
		clear_dots = true;
		enable_status = true;
		mode_6 = true;
		break;
	default:
		decay(0, 64 * 192);
		return;
	}
	if (controls.range & 0b100000) {
		decay(0, 64 * 192);
		return;
	}
	const bool flip_screen_h = controls.mode & 0b1000;
	const bool flip_screen_v = controls.allow_vertical_flip && !(controls.mode & 0b10000);
	int ink_alpha_on = controls.levels.ink_alpha_on;
	int ink_alpha_off = controls.levels.ink_alpha_off;
	const int rng1 = 4 - (controls.range & 0x3);
	ink_alpha_off *= (4 / rng1);
	ink_alpha_on *= (4 / rng1);
	const int rng = rng1 * 8;
	if (enable_status) {
		status(TargetLevels{ink_alpha_on, ink_alpha_off});
		for (size_t ix = 0; ix != sprites.size(); ++ix) {
			const auto off = (sprites[ix].offset + controls.offset * controls.row_size) %
				((controls.n_row + 1) * controls.row_size);
			pixel(ix, PixelSource{static_cast<size_t>(off), sprites[ix].mask,
				0, ink_alpha_on, ink_alpha_off, true, false});
		}
	}
	else {
		decay(0, 192);
	}
	if (mode_6)
		ink_alpha_on = ink_alpha_off /= 2.55;
	for (int iy2 = 1; iy2 != controls.n_row + 1; ++iy2) {
		int iy = (iy2 + controls.offset) % (controls.n_row + 1);
		bool clear = false;
		if (iy2 >= rng && iy2 < 32)
			clear = true;
		if (iy2 >= 32) {
			if (iy2 <= 32 + rng)
				iy = (iy2 - 32 + rng + controls.offset) % (controls.n_row + 1);
			else
				clear = true;
		}
		int x = 0;
		for (int ix = 0; ix != controls.row_size_display; ++ix) {
			const auto index = (flip_screen_v
				? (classwiz_ii ? controls.n_row - iy : controls.n_row + 1 - iy)
				: iy) * controls.row_size + ix;
			for (uint8_t mask = 0x80; mask; mask >>= 1) {
				const size_t alpha_index = (flip_screen_h ? 191 - x : x) + iy2 * 192;
				pixel(alpha_index, PixelSource{static_cast<size_t>(index), mask,
					iy, ink_alpha_on, ink_alpha_off, !classwiz_ii || !clear_dots, clear});
				++x;
			}
		}
	}
}

} // namespace casioemu::ordinary_lcd
