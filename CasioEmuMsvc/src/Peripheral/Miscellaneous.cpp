#include "Miscellaneous.hpp"

#include "Chipset/CPU.hpp"
#include "Chipset/Chipset.hpp"
#include "Emulator.hpp"
#include "Logger.hpp"

#include <iomanip>
#include <sstream>

namespace casioemu {
	class Miscellaneous : public Peripheral {
		MMURegion region_dsr, region_F004, region_F046;

	public:
		using Peripheral::Peripheral;

		void Initialise();
		void Tick();
		void Reset();
	};

	void Miscellaneous::Initialise() {
		region_dsr.Setup(
			0xF000, 1, "Miscellaneous/DSR", this, [](MMURegion* region, size_t) { return (uint8_t)((Miscellaneous*)region->userdata)->emulator.chipset.cpu.impl_last_dsr; }, [](MMURegion* region, size_t, uint8_t data) {
			Miscellaneous* self = (Miscellaneous *)region->userdata;
			self->emulator.chipset.cpu.impl_last_dsr = data & self->emulator.chipset.cpu.dsr_mask; }, emulator);

		if (emulator.hardware_id == HW_CLASSWIZ) {
			// Only tested on fx-991cnx
			region_F004.Setup(
				0xF004, 1, "Miscellaneous/DataSegAccess", this, [](MMURegion* region, size_t) { return (uint8_t)((Miscellaneous*)region->userdata)->emulator.chipset.SegmentAccess; }, [](MMURegion* region, size_t, uint8_t data) {
				Miscellaneous* self = (Miscellaneous *)region->userdata;
				self->emulator.chipset.SegmentAccess = data & 1; }, emulator);
		}
	}

	void Miscellaneous::Tick() {
	}

	void Miscellaneous::Reset() {
		if (emulator.hardware_id == HW_FX_5800P) {
			emulator.chipset.InputToPort(0, 3, true);
		}
	}
	Peripheral* CreateMiscellaneous(Emulator& emu) {
		return new Miscellaneous(emu);
	}
} // namespace casioemu
