#pragma once
#include "Config.hpp"

namespace casioemu
{
	class Emulator;

	class InterruptSource
	{
		friend class Chipset;
		Emulator *emulator;
		bool enabled, setup_done;
		size_t interrupt_index;

	public:
		InterruptSource();
		void Setup(size_t interrupt_index, Emulator &_emulator);
		void TryRaise();
		void ResetInt();
		void SetEnabled(bool val);
	};
}

