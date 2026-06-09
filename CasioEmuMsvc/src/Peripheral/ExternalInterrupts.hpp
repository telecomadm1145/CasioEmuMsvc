#pragma once
#include "Config.hpp"

#include "Peripheral.hpp"
#include "Chipset/MMURegion.hpp"

namespace casioemu
{
	class ExternalInterrupts : public Peripheral
	{
		MMURegion region_EXICON;

		//EXI1INT to EXI3INT; EXI0INT is handled by keyboard.
        size_t EXIINTS[3] = {1, 2, 3};

	public:
		using Peripheral::Peripheral;

		void UpdateInputLevel(int pin, bool value);

		void Initialise();
		void Reset();
		void Tick();
	};
}
