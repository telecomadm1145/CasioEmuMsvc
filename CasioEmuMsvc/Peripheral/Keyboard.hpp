#pragma once
#include <cstdint>
#include <SDL_touch.h> // Required for SDL_FingerID

namespace casioemu {
	class Peripheral* CreateKeyboard(class Emulator& emu);

	// Forward declaration if Button struct is not fully defined here
	// struct Button;
	class IKeyboardAutomation {
	public:
		/// Press or release the button matching the given KI/KO bit indices (0-based).
		virtual void Key(int ki, int ko, bool pressed) = 0;
		/// Release all non-stuck buttons programmatically.
		virtual void ReleaseAll() = 0;
		/// Press/release a button by raw kiko code byte (same encoding as ModelInfo::buttons[].kiko).
		virtual void PressCode(uint8_t code, bool pressed) = 0;
	};
} // namespace casioemu