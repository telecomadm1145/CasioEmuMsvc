#pragma once
#include <array>
#include <cstdint>
namespace casioemu {
	// Status fields are individual fragments in the official MultiView painter.
	// The SVG owns their shapes; the architecture owns the LCD bit assignment.
	inline constexpr std::array<uint8_t, 39> TI_MV_STATUS_BITS = {
		2, 8, 9, 10, 11, 15, 16, 17, 21, 22, 23, 27, 31, 35, 41, 42, 43, 44, 45, 46,
		47, 48, 53, 54, 55, 56, 57, 58, 68, 76, 77, 78, 79, 80, 81, 87, 89, 91, 93};
	class Peripheral* CreateTiMultiViewScreen(class Emulator& emulator);
} // namespace casioemu
