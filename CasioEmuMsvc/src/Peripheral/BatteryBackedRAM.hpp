#pragma once
namespace casioemu {
	class Peripheral* CreateBatteryBackedRAM(class Emulator& emu);
}
class IRam {
public:
	virtual void* GetRam() = 0;
	virtual void* GetPRam() = 0;
};