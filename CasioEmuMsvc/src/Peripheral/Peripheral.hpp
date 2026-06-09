#pragma once
#include "Config.hpp"

#include <SDL.h>
#include <any>
#include <iosfwd>

namespace casioemu {
	class Emulator;

	enum ClockType {
		CLOCK_UNDEFINED = 0,
		CLOCK_LSCLK,
		CLOCK_HSCLK,
		CLOCK_SYSCLK,
		CLOCK_EMUCLK,
		CLOCK_STOPPED
	};

	class Peripheral {
	protected:
		Emulator& emulator;

		// Set this value for peripherals controlled by BLKCON
		bool enabled = false;

	public:
		int clock_type = CLOCK_SYSCLK;
		int block_bit = -1;
		Peripheral(Emulator& emulator) : emulator(emulator) {}
		virtual void Initialise() {}
		virtual void Uninitialise() {}
		virtual void Tick() {}
		virtual void TickAfterInterrupts() {}
		virtual void Frame() {}
		virtual void UIEvent(SDL_Event& event) {}
		virtual void Reset() {}
		virtual void ResetLSCLK() {}
		virtual void* QueryInterface(const char*) { return 0; }
		virtual void SaveState(std::ostream&) {}
		virtual void LoadState(std::istream&) {}
		virtual ~Peripheral() {}
	};
} // namespace casioemu
