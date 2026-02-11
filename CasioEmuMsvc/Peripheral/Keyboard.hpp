#pragma once
#include <SDL_touch.h> // Required for SDL_FingerID
#include <vector>
#include <string>

namespace casioemu {
	struct KeyLogEntry {
		std::string key_name;
		uint64_t timestamp;
	};

	class IKeyLogger {
	public:
		virtual const std::vector<KeyLogEntry>& GetKeyLog() = 0;
		virtual void ClearKeyLog() = 0;
		virtual ~IKeyLogger() = default;
	};

	class Peripheral* CreateKeyboard(class Emulator& emu);

	// Forward declaration if Button struct is not fully defined here
	// struct Button;
}
