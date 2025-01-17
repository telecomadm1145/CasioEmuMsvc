#pragma once
#include "Peripheral.hpp"
#include <functional>

class ISpiProvider {
public:
	virtual void SetRecvHandler(std::function<void(uint8_t)>) = 0;
	virtual void Send(uint8_t) = 0;
};
namespace casioemu {
	Peripheral* CreateSpi(Emulator& emu);
}