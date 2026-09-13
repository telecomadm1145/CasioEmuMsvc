#pragma once

#include "ModelInfo.h"

#include <cmath>
#include <limits>

namespace casioemu::lcd_response {

// These are deliberately centralized source-level tuning points. They are
// uncalibrated starting values, not a claim that the old appearance has been
// matched. A half-life is the time for the remaining target error to halve;
// five half-lives leave about 3.125% of that error. Rise and fall may be tuned
// independently without changing the LCD target calculation.
struct Config {
	double rise_half_life_ms;
	double fall_half_life_ms;
};

inline constexpr Config kEsPlusAnd5800P{50.0, 50.0};
inline constexpr Config kClassWiz{50.0, 50.0};
inline constexpr Config kClassWizII{50.0, 50.0};
inline constexpr Config kEpsDotMatrix{50.0, 50.0};
inline constexpr Config kEpsSegment{50.0, 50.0};

inline double GainForElapsed(double elapsed_ms, double half_life_ms) {
	// A zero half-life means an immediate transition to the target.
	if (half_life_ms == 0.0)
		return 1.0;

	constexpr double kLn2 = 0.6931471805599453094;
	// gain = 1 - 2^(-elapsed_ms / half_life_ms): close half the remaining
	// gap to the target each half-life. expm1 preserves precision for small steps.
	return -std::expm1(-kLn2 * elapsed_ms / half_life_ms);
}

inline double BlendWithGains(double alpha, double target, double rise_gain, double fall_gain) {
	const double gain = target >= alpha ? rise_gain : fall_gain;
	return alpha + (target - alpha) * gain;
}

constexpr bool IsValidHalfLife(double value) {
	return value >= 0.0 && value == value &&
		value != std::numeric_limits<double>::infinity() &&
		value != -std::numeric_limits<double>::infinity();
}

static_assert(IsValidHalfLife(kEsPlusAnd5800P.rise_half_life_ms));
static_assert(IsValidHalfLife(kEsPlusAnd5800P.fall_half_life_ms));
static_assert(IsValidHalfLife(kClassWiz.rise_half_life_ms));
static_assert(IsValidHalfLife(kClassWiz.fall_half_life_ms));
static_assert(IsValidHalfLife(kClassWizII.rise_half_life_ms));
static_assert(IsValidHalfLife(kClassWizII.fall_half_life_ms));
static_assert(IsValidHalfLife(kEpsDotMatrix.rise_half_life_ms));
static_assert(IsValidHalfLife(kEpsDotMatrix.fall_half_life_ms));
static_assert(IsValidHalfLife(kEpsSegment.rise_half_life_ms));
static_assert(IsValidHalfLife(kEpsSegment.fall_half_life_ms));

constexpr Config ForHardware(HardwareId hardware_id) {
	switch (hardware_id) {
	case HW_FX_5800P:
	case HW_ES_PLUS:
		return kEsPlusAnd5800P;
	case HW_CLASSWIZ:
		return kClassWiz;
	case HW_CLASSWIZ_II:
		return kClassWizII;
	case HW_EPS6800:
	case HW_EPS6800_W192:
	case HW_EPS9500:
		return kEpsDotMatrix;
	case HW_EPS6009:
		return kEpsSegment;
	default:
		return {0.0, 0.0};
	}
}

} // namespace casioemu::lcd_response
