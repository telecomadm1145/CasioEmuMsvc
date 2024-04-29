#include "InterruptSource.hpp"

#include "../Emulator.hpp"
#include "Chipset.hpp"

namespace casioemu
{
	InterruptSource::InterruptSource()
	{
		setup_done = false;
	}

	void InterruptSource::Setup(size_t _interrupt_index, Emulator &_emulator)
	{
		if (setup_done)
			PANIC("Setup invoked twice\n");

		interrupt_index = _interrupt_index;
		emulator = &_emulator;

		enabled = false;
		setup_done = true;
	}

	void InterruptSource::TryRaise()
	{
		if (!setup_done)
			PANIC("Setup not invoked\n");

		emulator->chipset.SetInterruptPendingSFR(interrupt_index, true);

		if(enabled)
			emulator->chipset.RaiseMaskable(interrupt_index);
	}

	void InterruptSource::SetEnabled(bool val) {
		if (!setup_done)
			PANIC("Setup not invoked\n");

		enabled = val;

		if(!enabled)
			ResetInt();

		if(enabled && emulator->chipset.GetInterruptPendingSFR(interrupt_index))
			emulator->chipset.RaiseMaskable(interrupt_index);
	}

	void InterruptSource::ResetInt() {
		if (!setup_done)
			PANIC("Setup not invoked\n");
		
		emulator->chipset.ResetMaskable(interrupt_index);
	}
}

