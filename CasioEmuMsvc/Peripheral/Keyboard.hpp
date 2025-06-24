#pragma once
#include <SDL_touch.h> // Required for SDL_FingerID

namespace casioemu {
	class Peripheral* CreateKeyboard(class Emulator& emu);

	// Forward declaration if Button struct is not fully defined here
	// struct Button;
}