#include "ExternalInterrupts.hpp"

#include "Logger.hpp"
#include "Chipset/MMU.hpp"
#include "Emulator.hpp"
#include "Chipset/Chipset.hpp"

namespace casioemu
{
    void ExternalInterrupts::Initialise() {
        clock_type = CLOCK_UNDEFINED;

        emulator.chipset.data_EXICON = 0;

        if (emulator.hardware_id != HW_TI) {
			region_EXICON.Setup(0xF018, 1, "ExternalInterrupts/EXICON", &emulator.chipset.data_EXICON, MMURegion::DefaultRead<uint8_t>, MMURegion::DefaultWrite<uint8_t>, emulator);
		}
        else {

        }
    }

    void ExternalInterrupts::UpdateInputLevel(int pin, bool value) {
        //Interrupt mode 0 or 1 wont use sampling.
        if(pin < 1 || pin > 3)
            PANIC("Trying to input to invalid pin %d of Port0!", pin);
        if(emulator.chipset.Port0Inputlevel[pin - 1] == value)
            return;
        switch((emulator.chipset.data_EXICON >> (2 * pin)) & 0x03)
        {
        case 0:
            if(!value)
                emulator.chipset.MaskableInterrupts[EXIINTS[pin - 1]].TryRaise();
            break;
        case 1:
            if(value)
                emulator.chipset.MaskableInterrupts[EXIINTS[pin - 1]].TryRaise();
            break;
        default:
            break;
        }
    }

    void ExternalInterrupts::Tick() {
        for(int index = 0; index < 3; index++) {
            switch ((emulator.chipset.data_EXICON >> (2 * index + 2)) & 0x03)
            {
            case 2:
                if(emulator.chipset.Port0Inputlevel[index])
                    emulator.chipset.MaskableInterrupts[EXIINTS[index]].TryRaise();
                break;
            case 3:
                if(!emulator.chipset.Port0Inputlevel[index])
                    emulator.chipset.MaskableInterrupts[EXIINTS[index]].TryRaise();
                break;
            default:
                break;
            }
        }
    }

    void ExternalInterrupts::Reset() {
        emulator.chipset.data_EXICON = 0;
    }
}