#pragma once

#include <cstdint>

namespace casioemu::ordinary_lcd {

struct TargetLevels {
	int ink_alpha_on;
	int ink_alpha_off;
};

template <bool classwiz_ii>
TargetLevels CalculateTargetLevels(
	std::uint8_t brightness,
	int contrast,
	bool residual_enabled,
	float residual_alpha_scale) {
	auto sb = brightness;
	if (sb < 3) {
		sb = 3;
	}
	auto contrast_value = contrast;
	if (contrast_value < 0) {
		contrast_value = 0;
	}
	auto coeff = 16;
	auto off = 0;
	if constexpr (!classwiz_ii) {
		coeff = 28;
		off = -240;
	}
	int ink_alpha_on = off + contrast_value * coeff - sb * 8;
	int ink_alpha_off = off + 20 + (contrast_value) * (coeff - 11) - sb * 13;
	ink_alpha_off = residual_enabled ? static_cast<int>(ink_alpha_off * residual_alpha_scale) : 0;
	if (ink_alpha_on < 0)
		ink_alpha_on = 0;
	if (ink_alpha_off < 0)
		ink_alpha_off = 0;
	if (!residual_enabled) {
		ink_alpha_on = 255;
		ink_alpha_off = 0;
	}
	return {ink_alpha_on, ink_alpha_off};
}

} // namespace casioemu::ordinary_lcd
