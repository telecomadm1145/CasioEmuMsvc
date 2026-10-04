#pragma once
#include <algorithm>

namespace casioemu::ti_lcd {

	struct TargetLevels {
		float on;
		float off;
	};

	// The existing MathPrint renderer's visual calibration. These are alpha
	// levels, not physical LCD voltages; both TI displays use this same curve.
	inline TargetLevels CalculateTargetLevels(int contrast, bool residual_enabled, float residual_alpha_scale) {
		const float drive = (contrast - 100) * 20.0f;
		return {
			residual_enabled ? std::clamp(drive, 0.0f, 255.0f) : 255.0f,
			residual_enabled ? std::clamp(drive * 0.1f, 0.0f, 255.0f) * residual_alpha_scale : 0.0f};
	}

} // namespace casioemu::ti_lcd
