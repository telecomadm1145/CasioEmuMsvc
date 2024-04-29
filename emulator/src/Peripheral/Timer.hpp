#pragma once
#include "../Config.hpp"

#include "Peripheral.hpp"
#include "../Chipset/MMURegion.hpp"

namespace casioemu
{
	class Timer : public Peripheral
	{
		MMURegion region_counter, region_interval, region_F024, region_control;
		uint16_t data_counter, data_interval;
		uint8_t data_F024, data_control;

		size_t IntIndex = 4;

		bool raise_required;
		bool real_hardware;
		bool EmuStopped;
		uint64_t ext_to_int_counter, ext_to_int_next, ext_to_int_int_done;
		unsigned int cycles_per_second;
		static const uint64_t ext_to_int_frequency = 10000;

	public:
		using Peripheral::Peripheral;

		void Initialise();
		void Reset();
		void Tick();
		void TickAfterInterrupts();
		void DivideTicks();
	};
}

