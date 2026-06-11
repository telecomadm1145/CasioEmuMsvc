#pragma once
#include <cstdint>

namespace casioemu {
	class IScreenAutomation {
	public:
		virtual int Width() const = 0;
		virtual int Height() const = 0;
		virtual int StatusCount() const = 0;
		virtual int CopyAlpha(uint8_t* out, int len) = 0;
		virtual int CopyStatusAlpha(uint8_t* out, int len) = 0;
		virtual ~IScreenAutomation() = default;
	};
	class Peripheral* CreateScreen(class Emulator& emulator);
}
