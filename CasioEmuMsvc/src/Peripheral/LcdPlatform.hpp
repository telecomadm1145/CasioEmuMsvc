#pragma once

namespace casioemu::lcd_platform {

#if !defined(CASIOEMU_CORE_WEB) && !defined(__EMSCRIPTEN__) && !defined(__ANDROID__)
inline constexpr bool kNativeTemporalSupport = true;
#else
inline constexpr bool kNativeTemporalSupport = false;
#endif

} // namespace casioemu::lcd_platform
