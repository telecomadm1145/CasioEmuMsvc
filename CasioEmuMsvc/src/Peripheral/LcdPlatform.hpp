#pragma once

namespace casioemu::lcd_platform {

#if !defined(CASIOEMU_CORE_WEB) && !defined(__EMSCRIPTEN__) && !defined(__ANDROID__)
inline constexpr bool kNativeTemporalSupport = true;
#else
inline constexpr bool kNativeTemporalSupport = false;
#endif

// CASIO's fallback when the time-based response is unavailable.
constexpr float LegacyBlendRatio(float normal_ratio, bool low_performance = false) {
#if defined(CASIOEMU_CORE_WEB) || defined(__EMSCRIPTEN__)
	return 0.0f;
#elif defined(__ANDROID__)
	return 0.80f;
#else
	return low_performance ? 0.80f : normal_ratio;
#endif
}

} // namespace casioemu::lcd_platform
