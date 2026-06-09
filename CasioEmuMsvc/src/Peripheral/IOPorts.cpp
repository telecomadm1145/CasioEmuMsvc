#include "IOPorts.hpp"

#include "Chipset/Chipset.hpp"
#include "Chipset/MMU.hpp"
#include "Chipset/CPU.hpp"
#include "Emulator.hpp"
#include "Logger.hpp"
#include "Gui/Ui.hpp"

namespace casioemu {
	void IOPorts::Initialise() {
		memset(emulator.chipset.UserInput_level_Port0, 0, 3 * sizeof(bool));
		memset(emulator.chipset.UserInput_state_Port0, 0, 3 * sizeof(bool));
		memset(emulator.chipset.UserInput_level_Port1, 0, 7 * sizeof(bool));
		memset(emulator.chipset.UserInput_state_Port1, 0, 7 * sizeof(bool));

		auto p0_base = emulator.hardware_id == HW_FX_5800P ? 0xf046 : 0xF048;
		region_port0_data.Setup(
			p0_base, 1, "Port0/Data", this,
			[](MMURegion* region, size_t) {
				IOPorts* ioports = (IOPorts*)region->userdata;
				uint8_t data = 0;
				for (int i = 0; i < 3; i++) {
					if (ioports->port0_direction & (1 << i))
						data |= ioports->emulator.chipset.Port0Inputlevel[i] << i;
					else
						data |= ioports->port0_output & (1 << i);
				}
				return data;
			},
			[](MMURegion* region, size_t, uint8_t data) {
				IOPorts* ioports = (IOPorts*)region->userdata;
				data &= 0x07;
				ioports->port0_output = data;
			},
			emulator);

		region_port0_mode.Setup(p0_base + 1, 1, "Port0/Mode", &port0_mode, MMURegion::DefaultRead<uint8_t, 0x01>, MMURegion::DefaultWrite<uint8_t, 0x01>, emulator);

		region_port0_control_0.Setup(p0_base + 2, 1, "Port0/Control0", this, IOPorts::DefaultRead<3, &IOPorts::port0_control_0>, IOPorts::UpdateInputLevelWrite<3, 0, &IOPorts::port0_control_0>, emulator);
		region_port0_control_1.Setup(p0_base + 3, 1, "Port0/Control1", this, IOPorts::DefaultRead<3, &IOPorts::port0_control_1>, IOPorts::UpdateInputLevelWrite<3, 0, &IOPorts::port0_control_1>, emulator);

		region_port0_direction.Setup(
			p0_base + 4, 1, "Port0/Direction", this,
			[](MMURegion* region, size_t) {
				IOPorts* ioports = (IOPorts*)region->userdata;
				return (uint8_t)(ioports->port0_direction & 0x07);
			},
			[](MMURegion* region, size_t, uint8_t data) {
				IOPorts* ioports = (IOPorts*)region->userdata;
				data &= 0x07;
				uint8_t changed_data = data ^ ioports->port0_direction;
				ioports->port0_direction = data;
				for (int i = 0; i < 3; i++) {
					if (!(changed_data & (1 << i)))
						continue;
					// When the pin is set to output mode, the signal inputted to the chip is recognized as H level.
					// When a pin is switched from output mode to input mode, it will trigger a falling-edge interrupt first if there's no H level input to this pin,
					// even if it's set to input mode with a pull-up resistor.
					if (data & (1 << i)) {
						bool pin_level_released = ioports->emulator.chipset.UserInput_state_Port0[i] && ioports->emulator.chipset.UserInput_level_Port0[i];
						ioports->emulator.chipset.EXIhandle->UpdateInputLevel(i + 1, pin_level_released);
						ioports->emulator.chipset.Port0Inputlevel[i] = pin_level_released;
						ioports->AcceptInput(0, i);
					}
					else {
						ioports->emulator.chipset.EXIhandle->UpdateInputLevel(i + 1, true);
						ioports->emulator.chipset.Port0Inputlevel[i] = true;
						ioports->emulator.chipset.Port0Outputlevel[i] = bool(ioports->port0_output & (1 << i));
					}
				}
			},
			emulator);

		if (emulator.hardware_id == HW_CLASSWIZ_II)
			region_port0_unk.Setup(p0_base + 6, 1, "Port0/Unk", &port0_unk, MMURegion::DefaultRead<uint8_t, 0x11>, MMURegion::DefaultWrite<uint8_t, 0x11>, emulator);

		region_port1_data.Setup(
			0xF220, 1, "Port1/Data", this, [](MMURegion* region, size_t) {
            IOPorts* ioports = (IOPorts*)region->userdata;
            uint8_t data = 0;
            for(int i = 0; i < 7; i++) {
                if(ioports->port1_direction & (1 << i))
                    data |= ioports->emulator.chipset.Port1Inputlevel[i] << i;
                else
                    data |= ioports->port1_output & (1 << i);
            }
            return data; }, [](MMURegion* region, size_t, uint8_t data) {
            IOPorts* ioports = (IOPorts*)region->userdata;
            data &= 0x7F;
            ioports->port1_output = data; }, emulator);

		region_port1_direction.Setup(
			0xF221, 1, "Port1/Direction", this, [](MMURegion* region, size_t) {
            IOPorts* ioports = (IOPorts*)region->userdata;
            return (uint8_t)(ioports->port1_direction & 0x7F); }, [](MMURegion* region, size_t, uint8_t data) {
            IOPorts* ioports = (IOPorts*)region->userdata;
            data &= 0x7F;
            uint8_t changed_data = data ^ ioports->port1_direction;
            ioports->port1_direction = data;
            for(int i = 0; i < 7; i++) {
                if(!(changed_data & (1 << i)))
                    continue;
                if(data & (1 << i)) {
                    ioports->emulator.chipset.Port1Inputlevel[i] = ioports->emulator.chipset.UserInput_state_Port1[i] && ioports->emulator.chipset.UserInput_level_Port1[i];
                    ioports->AcceptInput(1, i);
                } else {
                    ioports->emulator.chipset.Port1Inputlevel[i] = true;
                    ioports->emulator.chipset.Port1Outputlevel[i] = bool(ioports->port1_output & (1 << i));
                }
            } }, emulator);

		region_port1_control_0.Setup(0xF222, 1, "Port1/Control0", this, IOPorts::DefaultRead<7, &IOPorts::port1_control_0>, IOPorts::UpdateInputLevelWrite<7, 1, &IOPorts::port1_control_0>, emulator);
		region_port1_control_1.Setup(0xF223, 1, "Port1/Control1", this, IOPorts::DefaultRead<7, &IOPorts::port1_control_1>, IOPorts::UpdateInputLevelWrite<7, 1, &IOPorts::port1_control_1>, emulator);

		region_port1_mode_0.Setup(0xF224, 1, "Port1/Mode0", &port1_mode_0, MMURegion::DefaultRead<uint8_t, 0x7F>, MMURegion::DefaultWrite<uint8_t, 0x7F>, emulator);

		if (emulator.hardware_id == HW_CLASSWIZ_II)
			region_port1_mode_1.Setup(0xF225, 1, "Port1/Mode1", &port1_mode_1, MMURegion::DefaultRead<uint8_t, 0x18>, MMURegion::DefaultWrite<uint8_t, 0x18>, emulator);
	}

	void IOPorts::AcceptInput(int port, int pin) {
		if (port == 0) {
			if (!(port0_direction & (1 << pin)))
				return;
			bool value_to_input = false;
			if (!emulator.chipset.UserInput_state_Port0[pin]) {
				if (!(port0_control_0 & (1 << pin)) && (port0_control_1 & (1 << pin))) {
					// input mode with a pull-up resistor
					value_to_input = true;
				}
				else if ((port0_control_0 & (1 << pin)) && !(port0_control_1 & (1 << pin))) {
					// input mode with a pull-down resistor
					value_to_input = false;
				}
				else {
					value_to_input = emulator.chipset.UserInput_level_Port0[pin];
				}
			}
			else {
				value_to_input = emulator.chipset.UserInput_level_Port0[pin];
			}
			emulator.chipset.EXIhandle->UpdateInputLevel(pin + 1, value_to_input);
			emulator.chipset.Port0Inputlevel[pin] = value_to_input;
			// If sth try to read from the pins when its at input mode, we'll return the inputted pin level.
			emulator.chipset.Port0Outputlevel[pin] = value_to_input;
		}
		else if (port == 1) {
			if (!(port1_direction & (1 << pin)))
				return;
			bool value_to_input = false;
			if (!emulator.chipset.UserInput_state_Port1[pin]) {
				if (!(port1_control_0 & (1 << pin)) && (port1_control_1 & (1 << pin)))
					value_to_input = true;
				else if ((port1_control_0 & (1 << pin)) && !(port1_control_1 & (1 << pin)))
					value_to_input = false;
				else
					value_to_input = emulator.chipset.UserInput_level_Port1[pin];
			}
			else {
				value_to_input = emulator.chipset.UserInput_level_Port1[pin];
			}
			emulator.chipset.Port1Inputlevel[pin] = value_to_input;
			emulator.chipset.Port1Outputlevel[pin] = value_to_input;
		}
	}

	void IOPorts::Reset() {
		port0_mode = port0_control_0 = port0_control_1 = port0_direction = 0;
		port1_direction = port1_control_0 = port1_control_1 = port1_mode_0 = port1_mode_1 = 0;
		port0_output = port1_output = 0;
		for (int i = 0; i < 3; i++) {
			emulator.chipset.Port0Inputlevel[i] = true;
			emulator.chipset.Port0Outputlevel[i] = false;
		}
		for (int i = 0; i < 7; i++) {
			emulator.chipset.Port1Inputlevel[i] = true;
			emulator.chipset.Port1Outputlevel[i] = false;
		}
	}
} // namespace casioemu