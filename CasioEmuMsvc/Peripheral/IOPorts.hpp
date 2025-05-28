#pragma once
#include "Config.hpp"

#include "Chipset/MMURegion.hpp"
#include "Peripheral.hpp"

namespace casioemu {
	class IOPorts : public Peripheral {
		// There are 2 mode registers for Port1 in casioemu.
		MMURegion region_port0_data, region_port0_mode, region_port0_control_0, region_port0_control_1, region_port0_direction, region_port0_unk,
			region_port1_data, region_port1_mode_0, region_port1_mode_1, region_port1_control_0, region_port1_control_1, region_port1_direction;

		uint8_t port0_mode, port0_control_0, port0_control_1, port0_direction, port0_unk,
			port1_mode_0, port1_mode_1, port1_control_0, port1_control_1, port1_direction;

		// these values are stored on writing to port data registers, even if the port is at input mode.
		uint8_t port0_output, port1_output;

		template <int pin_count, uint8_t IOPorts::*member_ptr>
		static uint8_t DefaultRead(MMURegion* region, size_t) {
			IOPorts* ioports = (IOPorts*)(region->userdata);
			return (uint8_t)(ioports->*member_ptr & ((1 << pin_count) - 1));
		}

		template <int pin_count, int port_index, uint8_t IOPorts::*member_ptr>
		static void UpdateInputLevelWrite(MMURegion* region, size_t, uint8_t data) {
			IOPorts* ioports = (IOPorts*)region->userdata;
			ioports->*member_ptr = data & ((1 << pin_count) - 1);
			for (int i = 0; i < pin_count; i++)
				ioports->AcceptInput(port_index, i);
		}

	public:
		using Peripheral::Peripheral;

		// This is used to update the input pin level.
		void AcceptInput(int, int);

		void Initialise();
		void Reset();
	};
} // namespace casioemu
