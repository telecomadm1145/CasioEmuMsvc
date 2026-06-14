#pragma once
#include <cstdint>

namespace casioemu {
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
