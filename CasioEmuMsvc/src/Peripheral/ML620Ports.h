#pragma once
#include <Peripheral.hpp>
#include <functional>
namespace casioemu {
	Peripheral* CreateML620Ports(Emulator& emu);
}
class IPortProvider {
public:
	virtual void SetPortOutputCallback(int port, std::function<void(uint8_t new_output)>) = 0;
	virtual void SetPortInput(int port, uint8_t inputd, uint8_t outputd) = 0;
};