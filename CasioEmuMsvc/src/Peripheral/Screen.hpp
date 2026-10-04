#pragma once
#include <array>
#include <cstdint>

namespace casioemu {
	// TI SmartView display callbacks. Physical ROMs also issue SWI 4 before
	// writing the segmented LCD row; observing it does not consume the SWI.
	class ITiSvDisplay {
	public:
		static constexpr int FrameBytes = 192 * 64 / 8;
		virtual void SetTiSvStatus(uint32_t status) = 0;
		virtual void SetTiSvFrame(const std::array<uint8_t, FrameBytes>& frame) = 0;
		virtual ~ITiSvDisplay() = default;
	};
	class IScreenFrameProvider {
	public:
		virtual void UpdateFrameAlpha() = 0;
		virtual int GetFrameWidth() const = 0;
		virtual int GetFrameHeight() const = 0;
		virtual void WriteFrameRgba(uint8_t* out, int r, int g, int b) const = 0;
		virtual int GetStatusAlphaCount() const = 0;
		virtual void WriteStatusAlpha(uint8_t* out, int max_len) const = 0;
		virtual ~IScreenFrameProvider() = default;
	};

	class Peripheral* CreateScreen(class Emulator& emulator);
}
