#pragma once
#include <algorithm>

namespace casioemu::ti_lcd {

	struct TargetLevels {
		float on;
		float off;
	};

	// The existing MathPrint renderer's visual calibration. These are alpha
	// levels, not physical LCD voltages.
	inline TargetLevels CalculateTargetLevels(int contrast, bool residual_enabled, float residual_alpha_scale) {
		const float drive = (contrast - 100) * 20.0f;
		return {
			residual_enabled ? std::clamp(drive, 0.0f, 255.0f) : 255.0f,
			residual_enabled ? std::clamp(drive * 0.1f, 0.0f, 255.0f) * residual_alpha_scale : 0.0f};
	}

	inline TargetLevels CalculateMultiViewTargetLevels(int contrast, bool residual_enabled, float residual_alpha_scale) {
		// R37 has sixteen levels. Preserve the startup appearance, but spread
		// the inactive-pixel alpha across its full range instead of MathPrint's
		// two-alpha-unit steps. Like the Casio curves, maximum drive can darken
		// the entire pixel matrix. This is visual calibration, not LCD voltage.
		return {
			residual_enabled ? std::clamp(240.0f + contrast * 20.0f, 0.0f, 255.0f) : 255.0f,
			residual_enabled ? std::clamp(24.0f + contrast * (231.0f / 15.0f), 0.0f, 255.0f) * residual_alpha_scale : 0.0f};
	}

} // namespace casioemu::ti_lcd
